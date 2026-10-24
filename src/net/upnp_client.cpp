#include "net/upnp_client.h"
#include "net/net_utils.h"
#include <Preferences.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include "config.h"

extern QueueHandle_t xQueueUiState;
extern QueueHandle_t xQueueUiCmd;

struct UpnpTrackFull {
    char id[64];
    char title[64];
    char artist[48];
    char album[48];
    char url[256];
    char album_art[160];
    bool meta_resolved;
};

static Preferences s_upnp_prefs;
static UpnpServerList s_server_list;

static UpnpTrackFull* s_folder_tracks = nullptr;
static uint8_t s_folder_track_count = 0;
static char s_active_control_url[128] = "";

static void load_saved_servers() {
    uint8_t count = s_upnp_prefs.getUChar("srv_count", 0);
    if (count > MAX_UPNP_SERVERS) count = MAX_UPNP_SERVERS;

    s_server_list.count = 0;
    for (uint8_t i = 0; i < count; ++i) {
        char kName[16], kBase[16], kCtrl[16], kUdn[16];
        snprintf(kName, sizeof(kName), "srv_name_%u", i);
        snprintf(kBase, sizeof(kBase), "srv_base_%u", i);
        snprintf(kCtrl, sizeof(kCtrl), "srv_ctrl_%u", i);
        snprintf(kUdn, sizeof(kUdn), "srv_udn_%u", i);

        String name = s_upnp_prefs.getString(kName, "");
        String base = s_upnp_prefs.getString(kBase, "");
        String ctrl = s_upnp_prefs.getString(kCtrl, "");
        String udn  = s_upnp_prefs.getString(kUdn, "");

        if (name.length() > 0 && base.length() > 0 && ctrl.length() > 0) {
            UpnpServerInfo& srv = s_server_list.servers[s_server_list.count++];
            memset(&srv, 0, sizeof(srv));
            strncpy(srv.name, name.c_str(), sizeof(srv.name) - 1);
            strncpy(srv.base_url, base.c_str(), sizeof(srv.base_url) - 1);
            strncpy(srv.control_url, ctrl.c_str(), sizeof(srv.control_url) - 1);
            strncpy(srv.udn, udn.c_str(), sizeof(srv.udn) - 1);
        }
    }
}

static void save_saved_servers() {
    uint8_t count = s_server_list.count;
    if (count > MAX_UPNP_SERVERS) count = MAX_UPNP_SERVERS;
    s_upnp_prefs.putUChar("srv_count", count);
    for (uint8_t i = 0; i < count; ++i) {
        char kName[16], kBase[16], kCtrl[16], kUdn[16];
        snprintf(kName, sizeof(kName), "srv_name_%u", i);
        snprintf(kBase, sizeof(kBase), "srv_base_%u", i);
        snprintf(kCtrl, sizeof(kCtrl), "srv_ctrl_%u", i);
        snprintf(kUdn, sizeof(kUdn), "srv_udn_%u", i);

        s_upnp_prefs.putString(kName, s_server_list.servers[i].name);
        s_upnp_prefs.putString(kBase, s_server_list.servers[i].base_url);
        s_upnp_prefs.putString(kCtrl, s_server_list.servers[i].control_url);
        s_upnp_prefs.putString(kUdn,  s_server_list.servers[i].udn);
    }
}

static String xml_unescape(const String& s) {
    String out;
    out.reserve(s.length());
    size_t i = 0;
    size_t n = s.length();
    while (i < n) {
        if (s[i] == '&') {
            if (s.startsWith("&lt;", i)) { out += '<'; i += 4; continue; }
            if (s.startsWith("&gt;", i)) { out += '>'; i += 4; continue; }
            if (s.startsWith("&quot;", i)) { out += '"'; i += 6; continue; }
            if (s.startsWith("&apos;", i)) { out += '\''; i += 6; continue; }
            if (s.startsWith("&#39;", i)) { out += '\''; i += 5; continue; }
            if (s.startsWith("&amp;", i)) { out += '&'; i += 5; continue; }
        }
        out += s[i++];
    }
    return out;
}

static void append_xml_escaped(String& out, const char* s) {
    if (!s) return;
    while (*s) {
        char c = *s++;
        switch (c) {
            case '&':  out += "&amp;"; break;
            case '<':  out += "&lt;"; break;
            case '>':  out += "&gt;"; break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out += c; break;
        }
    }
}

static String xml_escape(const char* s) {
    if (!s) return "";
    String out;
    out.reserve(strlen(s) + 16);
    append_xml_escaped(out, s);
    return out;
}

static String extract_attr(const String& tagHeader, const char* attrName) {
    String needle = String(attrName) + "=\"";
    int start = tagHeader.indexOf(needle);
    if (start < 0) return "";
    start += needle.length();
    int end = tagHeader.indexOf('"', start);
    if (end < 0) return "";
    return xml_unescape(tagHeader.substring(start, end));
}

// Extracts content of <tag ...>content</tag>
static String extract_tag_content(const String& xml, const char* tagName) {
    String openExact = String("<") + tagName + ">";
    String closeTag = String("</") + tagName + ">";
    int start = xml.indexOf(openExact);
    if (start >= 0) {
        start += openExact.length();
    } else {
        String openAttr = String("<") + tagName + " ";
        start = xml.indexOf(openAttr);
        if (start < 0) return "";
        start = xml.indexOf('>', start);
        if (start < 0) return "";
        start += 1;
    }
    int end = xml.indexOf(closeTag, start);
    if (end < 0) return "";
    return xml_unescape(xml.substring(start, end));
}

static UpnpBrowseList* alloc_browse_list() {
    UpnpBrowseList* p = (UpnpBrowseList*)heap_caps_malloc(
        sizeof(UpnpBrowseList),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    if (!p) {
        p = (UpnpBrowseList*)malloc(sizeof(UpnpBrowseList));
    }
    if (p) {
        memset(p, 0, sizeof(UpnpBrowseList));
    }
    return p;
}

void upnp_client_init() {
    s_upnp_prefs.begin("wiim_upnp", false);
    memset(&s_server_list, 0, sizeof(s_server_list));
    if (!s_folder_tracks) {
        s_folder_tracks = (UpnpTrackFull*)heap_caps_malloc(
            sizeof(UpnpTrackFull) * MAX_UPNP_ITEMS,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );
        if (!s_folder_tracks) {
            s_folder_tracks = (UpnpTrackFull*)malloc(sizeof(UpnpTrackFull) * MAX_UPNP_ITEMS);
        }
        if (s_folder_tracks) {
            memset(s_folder_tracks, 0, sizeof(UpnpTrackFull) * MAX_UPNP_ITEMS);
        }
    }
    s_folder_track_count = 0;

    load_saved_servers();
    if (s_server_list.count > 0) {
        upnp_client_broadcast_servers();
    }
}

void upnp_client_broadcast_servers() {
    UpnpServerList* pList = (UpnpServerList*)malloc(sizeof(UpnpServerList));
    if (pList) {
        *pList = s_server_list;
        UiEvent evt;
        evt.type = UI_EVT_UPNP_SERVERS_UPDATED;
        evt.data.upnp_servers = pList;
        if (xQueueSend(xQueueUiState, &evt, 0) != pdTRUE) {
            free(pList);
        }
    }
}

void upnp_client_on_ssdp_location(const char* locationUrl) {
    if (!locationUrl || strncmp(locationUrl, "http://", 7) != 0) return;

    // Extract base URL: http://ip:port
    const char* hostStart = locationUrl + 7;
    const char* pathSlash = strchr(hostStart, '/');
    size_t baseLen = pathSlash ? (size_t)(pathSlash - locationUrl) : strlen(locationUrl);
    if (baseLen == 0 || baseLen >= sizeof(s_server_list.servers[0].base_url)) return;

    char baseUrl[64];
    memcpy(baseUrl, locationUrl, baseLen);
    baseUrl[baseLen] = '\0';

    // Check if already discovered
    for (uint8_t i = 0; i < s_server_list.count; ++i) {
        if (strcmp(s_server_list.servers[i].base_url, baseUrl) == 0) {
            return;
        }
    }

    esp_task_wdt_reset();

    HTTPClient http;
    http.setConnectTimeout(1500);
    http.setTimeout(2500);
    if (!http.begin(String(locationUrl))) {
        return;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        return;
    }

    String payload = http.getString();
    http.end();

    int cdIdx = payload.indexOf("urn:schemas-upnp-org:service:ContentDirectory:");
    if (cdIdx < 0) {
        return;
    }

    // Extract <controlURL> inside the ContentDirectory <service> block
    int serviceEnd = payload.indexOf("</service>", cdIdx);
    String serviceSlice = (serviceEnd > cdIdx) ? payload.substring(cdIdx, serviceEnd) : payload.substring(cdIdx);
    String controlPath = net_extract_xml_tag(serviceSlice, "controlURL");
    if (controlPath.length() == 0) {
        return;
    }

    String fullControlUrl;
    if (controlPath.startsWith("http://") || controlPath.startsWith("https://")) {
        fullControlUrl = controlPath;
    } else if (controlPath.startsWith("/")) {
        fullControlUrl = String(baseUrl) + controlPath;
    } else {
        fullControlUrl = String(baseUrl) + "/" + controlPath;
    }

    String friendlyName = net_to_valid_utf8(xml_unescape(net_extract_xml_tag(payload, "friendlyName")));
    if (friendlyName.length() == 0) friendlyName = "UPnP Media Server";
    String udn = net_extract_xml_tag(payload, "UDN");

    // Update existing server if UDN or friendlyName matches (e.g. if server IP/port changed)
    for (uint8_t i = 0; i < s_server_list.count; ++i) {
        bool udnMatch = (udn.length() > 0 && strcmp(s_server_list.servers[i].udn, udn.c_str()) == 0);
        bool nameMatch = (strcmp(s_server_list.servers[i].name, friendlyName.c_str()) == 0);
        if (udnMatch || nameMatch) {
            UpnpServerInfo& srv = s_server_list.servers[i];
            memset(&srv, 0, sizeof(srv));
            strncpy(srv.name, friendlyName.c_str(), sizeof(srv.name) - 1);
            strncpy(srv.base_url, baseUrl, sizeof(srv.base_url) - 1);
            strncpy(srv.control_url, fullControlUrl.c_str(), sizeof(srv.control_url) - 1);
            strncpy(srv.udn, udn.c_str(), sizeof(srv.udn) - 1);
            save_saved_servers();
            upnp_client_broadcast_servers();
            return;
        }
    }

    // If table is full, evict oldest server entry so newly discovered servers are never blocked
    if (s_server_list.count >= MAX_UPNP_SERVERS) {
        memmove(&s_server_list.servers[0],
                &s_server_list.servers[1],
                sizeof(UpnpServerInfo) * (MAX_UPNP_SERVERS - 1));
        s_server_list.count = MAX_UPNP_SERVERS - 1;
    }

    UpnpServerInfo& srv = s_server_list.servers[s_server_list.count++];
    memset(&srv, 0, sizeof(srv));
    strncpy(srv.name, friendlyName.c_str(), sizeof(srv.name) - 1);
    strncpy(srv.base_url, baseUrl, sizeof(srv.base_url) - 1);
    strncpy(srv.control_url, fullControlUrl.c_str(), sizeof(srv.control_url) - 1);
    strncpy(srv.udn, udn.c_str(), sizeof(srv.udn) - 1);

    log_i("[UPnP] Discovered MediaServer: '%s' at %s", srv.name, srv.control_url);
    save_saved_servers();
    upnp_client_broadcast_servers();
}

void upnp_client_browse(uint8_t serverIdx, const char* objectId, const char* folderTitle, uint16_t startIndex) {
    UpnpBrowseList* pBrowse = alloc_browse_list();
    if (!pBrowse) return;
    strncpy(pBrowse->title, (folderTitle && strlen(folderTitle) > 0) ? folderTitle : "Folder", sizeof(pBrowse->title) - 1);
    pBrowse->start_index = startIndex;

    if (serverIdx >= s_server_list.count) {
        pBrowse->is_error = true;
        UiEvent evt;
        evt.type = UI_EVT_UPNP_BROWSE_RESULT;
        evt.data.upnp_browse = pBrowse;
        if (xQueueSend(xQueueUiState, &evt, 0) != pdTRUE) free(pBrowse);
        return;
    }

    const UpnpServerInfo& srv = s_server_list.servers[serverIdx];
    String objIdStr = (objectId && strlen(objectId) > 0) ? xml_escape(objectId) : "0";

    String soapBody =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body><u:Browse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">"
        "<ObjectID>" + objIdStr + "</ObjectID>"
        "<BrowseFlag>BrowseDirectChildren</BrowseFlag>"
        "<Filter>*</Filter>"
        "<StartingIndex>" + String(startIndex) + "</StartingIndex>"
        "<RequestedCount>" + String(MAX_UPNP_ITEMS) + "</RequestedCount>"
        "<SortCriteria></SortCriteria>"
        "</u:Browse></s:Body></s:Envelope>";

    esp_task_wdt_reset();

    HTTPClient http;
    http.setConnectTimeout(2000);
    http.setTimeout(4000);
    if (!http.begin(String(srv.control_url))) {
        pBrowse->is_error = true;
        UiEvent evt;
        evt.type = UI_EVT_UPNP_BROWSE_RESULT;
        evt.data.upnp_browse = pBrowse;
        if (xQueueSend(xQueueUiState, &evt, 0) != pdTRUE) free(pBrowse);
        return;
    }

    http.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
    http.addHeader("SOAPAction", "\"urn:schemas-upnp-org:service:ContentDirectory:1#Browse\"");

    int code = http.POST(soapBody);
    esp_task_wdt_reset();

    if (code != HTTP_CODE_OK) {
        log_w("[UPnP] Browse failed (HTTP %d) for ObjectID='%s'", code, objIdStr.c_str());
        http.end();
        pBrowse->is_error = true;
        UiEvent evt;
        evt.type = UI_EVT_UPNP_BROWSE_RESULT;
        evt.data.upnp_browse = pBrowse;
        if (xQueueSend(xQueueUiState, &evt, 0) != pdTRUE) free(pBrowse);
        return;
    }

    String soapResp = http.getString();
    http.end();

    int32_t parsedTotalMatches = net_extract_xml_tag(soapResp, "TotalMatches").toInt();
    uint32_t totalMatches = (parsedTotalMatches > 0) ? (uint32_t)parsedTotalMatches : 0;
    String didl = xml_unescape(net_extract_xml_tag(soapResp, "Result"));
    soapResp = String(); // Free SOAP envelope immediately

    // 1. Parse <container ...>...</container> entries
    int pos = 0;
    while (pBrowse->count < MAX_UPNP_ITEMS) {
        int cStart = didl.indexOf("<container", pos);
        if (cStart < 0) break;
        int cHeaderEnd = didl.indexOf('>', cStart);
        int cEnd = didl.indexOf("</container>", cHeaderEnd);
        if (cHeaderEnd < 0 || cEnd < 0) break;

        String header = didl.substring(cStart, cHeaderEnd + 1);
        String body = didl.substring(cHeaderEnd + 1, cEnd);

        String id = extract_attr(header, "id");
        String title = net_to_valid_utf8(extract_tag_content(body, "dc:title"));

        if (id.length() > 0 && title.length() > 0) {
            UpnpItem& item = pBrowse->items[pBrowse->count++];
            strncpy(item.id, id.c_str(), sizeof(item.id) - 1);
            strncpy(item.title, title.c_str(), sizeof(item.title) - 1);
            item.is_folder = true;
            item.track_index = 0;
        }
        pos = cEnd + 12;
    }

    // 2. Parse <item ...>...</item> audio tracks
    strncpy(s_active_control_url, srv.control_url, sizeof(s_active_control_url) - 1);
    s_active_control_url[sizeof(s_active_control_url) - 1] = '\0';
    s_folder_track_count = 0;
    pos = 0;
    while (pBrowse->count < MAX_UPNP_ITEMS && s_folder_track_count < MAX_UPNP_ITEMS) {
        int iStart = didl.indexOf("<item", pos);
        if (iStart < 0) break;
        int iHeaderEnd = didl.indexOf('>', iStart);
        int iEnd = didl.indexOf("</item>", iHeaderEnd);
        if (iHeaderEnd < 0 || iEnd < 0) break;

        String header = didl.substring(iStart, iHeaderEnd + 1);
        String body = didl.substring(iHeaderEnd + 1, iEnd);

        // Extract <res ...>url</res>
        int resStart = body.indexOf("<res");
        int resHeaderEnd = (resStart >= 0) ? body.indexOf('>', resStart) : -1;
        int resEnd = (resHeaderEnd >= 0) ? body.indexOf("</res>", resHeaderEnd) : -1;

        if (resHeaderEnd >= 0 && resEnd > resHeaderEnd) {
            String streamUrl = xml_unescape(body.substring(resHeaderEnd + 1, resEnd));
            streamUrl.trim();

            if (streamUrl.startsWith("http://") || streamUrl.startsWith("https://")) {
                String id = extract_attr(header, "id");
                String title = net_to_valid_utf8(extract_tag_content(body, "dc:title"));
                String artist = net_to_valid_utf8(xml_unescape(net_extract_xml_tag(body, "upnp:artist")));
                if (artist.length() == 0) {
                    artist = net_to_valid_utf8(extract_tag_content(body, "dc:creator"));
                }
                if (artist.length() == 0) {
                    artist = net_to_valid_utf8(extract_tag_content(body, "upnp:artist"));
                }
                String album = net_to_valid_utf8(extract_tag_content(body, "upnp:album"));
                String albumArt = extract_tag_content(body, "upnp:albumArtURI");

                uint8_t trkIdx = s_folder_track_count;
                if (s_folder_tracks) {
                    UpnpTrackFull& trk = s_folder_tracks[s_folder_track_count++];
                    memset(&trk, 0, sizeof(trk));
                    strncpy(trk.id, id.c_str(), sizeof(trk.id) - 1);
                    strncpy(trk.title, title.c_str(), sizeof(trk.title) - 1);
                    strncpy(trk.artist, artist.c_str(), sizeof(trk.artist) - 1);
                    strncpy(trk.album, album.c_str(), sizeof(trk.album) - 1);
                    strncpy(trk.url, streamUrl.c_str(), sizeof(trk.url) - 1);
                    strncpy(trk.album_art, albumArt.c_str(), sizeof(trk.album_art) - 1);
                    trk.meta_resolved = false;
                }

                UpnpItem& item = pBrowse->items[pBrowse->count++];
                strncpy(item.id, id.c_str(), sizeof(item.id) - 1);
                strncpy(item.title, title.c_str(), sizeof(item.title) - 1);
                strncpy(item.artist, artist.c_str(), sizeof(item.artist) - 1);
                item.is_folder = false;
                item.track_index = trkIdx;
            }
        }
        pos = iEnd + 7;
    }

    pBrowse->total_matches = (uint16_t)((totalMatches > 65535U) ? 65535U : totalMatches);
    if (totalMatches > (uint32_t)startIndex + (uint32_t)pBrowse->count) {
        pBrowse->has_more = true;
    } else if (totalMatches == 0 && pBrowse->count >= MAX_UPNP_ITEMS) {
        pBrowse->has_more = true;
    }

    log_i("[UPnP] Browse '%s' -> %u items (%u tracks)",
          pBrowse->title, pBrowse->count, s_folder_track_count);

    UiEvent evt;
    evt.type = UI_EVT_UPNP_BROWSE_RESULT;
    evt.data.upnp_browse = pBrowse;
    if (xQueueSend(xQueueUiState, &evt, 0) != pdTRUE) {
        free(pBrowse);
    }
}

bool upnp_client_play_track(const char* wiimIp, uint8_t trackIdx, bool isFixedVolume) {
    if (!wiimIp || strlen(wiimIp) == 0 || !s_folder_tracks || trackIdx >= s_folder_track_count) {
        return false;
    }

    uint8_t totalTracks = s_folder_track_count;
    uint8_t startIdx1Based = trackIdx + 1;

    // Resolve true embedded track metadata via UPnP BrowseMetadata so Folder & Filename Browsing
    // populates the playback queue with embedded <dc:title> tags rather than disk filenames.
    if (strlen(s_active_control_url) > 0) {
        WiFiClient metaClient;
        HTTPClient metaHttp;
        metaHttp.setReuse(true);
        metaHttp.setConnectTimeout(800);
        metaHttp.setTimeout(1200);
        bool firstChecked = false;
        unsigned long metaStartMs = millis();

        for (uint8_t offset = 0; offset < totalTracks; ++offset) {
            uint8_t i = (uint8_t)((trackIdx + offset) % totalTracks);
            if (offset > 0 && ((millis() - metaStartMs >= 1500UL) || (xQueueUiCmd && uxQueueMessagesWaiting(xQueueUiCmd) > 0))) {
                break;
            }
            UpnpTrackFull& t = s_folder_tracks[i];
            if (t.meta_resolved || strlen(t.id) == 0) continue;

            esp_task_wdt_reset();
            String soapBody =
                "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
                "<s:Body><u:Browse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">"
                "<ObjectID>" + xml_escape(t.id) + "</ObjectID>"
                "<BrowseFlag>BrowseMetadata</BrowseFlag>"
                "<Filter>*</Filter>"
                "<StartingIndex>0</StartingIndex>"
                "<RequestedCount>1</RequestedCount>"
                "<SortCriteria></SortCriteria>"
                "</u:Browse></s:Body></s:Envelope>";

            if (!metaHttp.begin(metaClient, String(s_active_control_url))) {
                break;
            }
            metaHttp.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
            metaHttp.addHeader("SOAPAction", "\"urn:schemas-upnp-org:service:ContentDirectory:1#Browse\"");
            int code = metaHttp.POST(soapBody);
            if (code != HTTP_CODE_OK) {
                metaHttp.end();
                break; // Abort remaining BrowseMetadata calls immediately if UPnP server is unreachable/slow
            }

            String resp = metaHttp.getString();
            metaHttp.end();

            String itemDidl = xml_unescape(net_extract_xml_tag(resp, "Result"));
            String realTitle = net_to_valid_utf8(extract_tag_content(itemDidl, "dc:title"));
            bool titleMatches = (realTitle.length() > 0 && strcmp(realTitle.c_str(), t.title) == 0);
            if (realTitle.length() > 0) {
                memset(t.title, 0, sizeof(t.title));
                strncpy(t.title, realTitle.c_str(), sizeof(t.title) - 1);
            }
            t.meta_resolved = true;

            // If the first track's BrowseDirectChildren title already equals BrowseMetadata,
            // this container is already serving embedded titles—skip remaining HTTP round-trips.
            if (!firstChecked) {
                firstChecked = true;
                if (titleMatches) {
                    for (uint8_t j = 0; j < totalTracks; ++j) {
                        s_folder_tracks[j].meta_resolved = true;
                    }
                    break;
                }
            }
        }
    }

    // Build LinkPlay QueueVersion 2.0 PlayList XML (matches WiiM Home app UPnPQueue schema)
    String playListXml;
    playListXml.reserve((size_t)totalTracks * 480 + 256);
    playListXml += "<?xml version=\"1.0\"?>\n<PlayList>\n";
    playListXml += "<ListName>UPnPQueue</ListName>\n";
    playListXml += "<ListInfo>\n";
    playListXml += "<QueueVersion>2.0</QueueVersion>\n";
    playListXml += "<SourceName>UPnPServer</SourceName>\n";
    playListXml += "<PicUrl></PicUrl>\n";
    playListXml += "<TotalNumber>" + String(totalTracks) + "</TotalNumber>\n";
    playListXml += "<TrackNumber>" + String(totalTracks) + "</TrackNumber>\n";
    playListXml += "<LastPlayIndex>" + String(startIdx1Based) + "</LastPlayIndex>\n";
    playListXml += "</ListInfo>\n<Tracks>\n";

    for (uint8_t i = 0; i < totalTracks; ++i) {
        const UpnpTrackFull& t = s_folder_tracks[i];
        String trackUrl = String(t.url);
        if (!isFixedVolume && trackUrl.endsWith(".forced.wavdop")) {
            // Strip "dop" suffix so Asset UPnP decodes DSD on-the-fly to 24-bit/176.4kHz Linear PCM WAV
            // for WiiM streamers using their internal DAC (Variable Volume mode)
            trackUrl.remove(trackUrl.length() - 3);
        }
        String escUrl = xml_escape(trackUrl.c_str());
        String escTitle = xml_escape(t.title);
        String escArtist = xml_escape(t.artist);
        String escAlbum = xml_escape(t.album);
        String escArt = xml_escape(t.album_art);

        // Use WiiM Home's fixed <res protocolInfo="http-get:*:audio/mpeg:DLNA.ORG_PN=MP3;DLNA.ORG_OP=01;" duration=""></res>
        // so non-standard DLNA profile strings (e.g. Asset UPnP's DLNA.ORG_PN=WAV for .forced.wavdop DSD streams)
        // do not trigger UPnPError 714.
        String didl =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<DIDL-Lite xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
            "xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\" "
            "xmlns:song=\"www.wiimu.com/song/\" "
            "xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\">\n"
            "<upnp:class>object.item.audioItem.musicTrack</upnp:class>\n"
            "<item id=\"\">\n"
            "<song:singerid></song:singerid>\n"
            "<song:albumid></song:albumid>\n"
            "<res protocolInfo=\"http-get:*:audio/mpeg:DLNA.ORG_PN=MP3;DLNA.ORG_OP=01;\" duration=\"\"></res>\n"
            "<dc:title>" + escTitle + "</dc:title>\n"
            "<upnp:artist>" + escArtist + "</upnp:artist>\n"
            "<upnp:album>" + escAlbum + "</upnp:album>\n";
        if (escArt.length() > 0) {
            didl += "<upnp:albumArtURI>" + escArt + "</upnp:albumArtURI>\n";
        }
        didl += "</item>\n</DIDL-Lite>\n";

        String trackTag = "Track" + String(i + 1);
        playListXml += "<" + trackTag + ">\n";
        playListXml += "<Id>" + String(i + 1) + "</Id>\n";
        playListXml += "<URL>" + escUrl + "</URL>\n";
        playListXml += "<Metadata>";
        append_xml_escaped(playListXml, didl.c_str());
        playListXml += "</Metadata>\n";
        playListXml += "<Source>UPnPServer</Source>\n";
        playListXml += "</" + trackTag + ">\n";
    }
    playListXml += "</Tracks>\n</PlayList>";

    String createSoap;
    createSoap.reserve((playListXml.length() * 3) / 2 + 512);
    createSoap +=
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body><u:CreateQueue xmlns:u=\"urn:schemas-wiimu-com:service:PlayQueue:1\">"
        "<QueueContext>";
    append_xml_escaped(createSoap, playListXml.c_str());
    playListXml = String(); // Free unescaped playlist before HTTP POST
    createSoap += "</QueueContext></u:CreateQueue></s:Body></s:Envelope>";

    String queueUrl = "http://" + String(wiimIp) + ":" + String(WIIM_UPNP_PORT) + "/upnp/control/PlayQueue1";

    esp_task_wdt_reset();
    HTTPClient http;
    http.setConnectTimeout(1500);
    http.setTimeout(5000);

    int createCode = -1;
    if (http.begin(queueUrl)) {
        http.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
        http.addHeader("SOAPAction", "\"urn:schemas-wiimu-com:service:PlayQueue:1#CreateQueue\"");
        createCode = http.POST(createSoap);
        http.end();
    }
    createSoap = String();
    esp_task_wdt_reset();

    int playCode = -1;
    if (createCode == HTTP_CODE_OK) {
        String playSoap =
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
            "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
            "<s:Body><u:PlayQueueWithIndex xmlns:u=\"urn:schemas-wiimu-com:service:PlayQueue:1\">"
            "<QueueName>UPnPQueue</QueueName>"
            "<Index>" + String(startIdx1Based) + "</Index>"
            "</u:PlayQueueWithIndex></s:Body></s:Envelope>";

        if (http.begin(queueUrl)) {
            http.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
            http.addHeader("SOAPAction", "\"urn:schemas-wiimu-com:service:PlayQueue:1#PlayQueueWithIndex\"");
            playCode = http.POST(playSoap);
            http.end();
        }
    }

    log_i("[UPnP] PlayQueue Create=%d, PlayWithIndex(%u/%u)=%d",
          createCode, startIdx1Based, totalTracks, playCode);

    return (playCode == HTTP_CODE_OK);
}
