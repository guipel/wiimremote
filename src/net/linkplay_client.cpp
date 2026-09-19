#include "net/linkplay_client.h"
#include "net/net_utils.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"

extern QueueHandle_t xQueueUiState;

static WiFiClientSecure s_persistent_client;
static HTTPClient s_persistent_http;
static String s_persistent_ip = "";
static bool s_persistent_http_configured = false;

static String s_last_known_track_title = "";
static String s_last_known_artist = "";
static bool s_meta_resolved = false;
static uint32_t s_cached_track_duration_ms = 0;
static uint8_t s_active_mode = 0;
static uint16_t s_active_plicurr = 0;
static uint16_t s_active_plicount = 0;
static PlayerState s_last_player_state;

void linkplay_client_init() {
    memset(&s_last_player_state, 0, sizeof(s_last_player_state));
    s_last_known_track_title = "";
    s_last_known_artist = "";
    s_meta_resolved = false;
    s_cached_track_duration_ms = 0;
    s_active_mode = 0;
    s_active_plicurr = 0;
    s_active_plicount = 0;
}

void linkplay_client_reset_session() {
    s_persistent_http.end();
    s_persistent_client.stop();
    s_persistent_ip = "";
    s_persistent_http_configured = false;
}

void linkplay_client_reset_track_cache() {
    s_last_known_track_title = "";
    s_last_known_artist = "";
    s_cached_track_duration_ms = 0;
    s_meta_resolved = false;
}

static int execute_api_get(const char* targetIp, const String& cmd, String& outPayload) {
    if (!targetIp || strlen(targetIp) == 0) return -1;

    char path[128];
    snprintf(path, sizeof(path), "/httpapi.asp?command=%s", cmd.c_str());
    char fullUrl[160];
    snprintf(fullUrl, sizeof(fullUrl), "https://%s%s", targetIp, path);

    // Reset session if target IP changed or client not configured yet
    if (strcmp(s_persistent_ip.c_str(), targetIp) != 0 || !s_persistent_http_configured) {
        linkplay_client_reset_session();
        s_persistent_client.setInsecure();
        s_persistent_http.setReuse(true);
        s_persistent_http.setTimeout(HTTP_REQUEST_TIMEOUT_MS);
        s_persistent_ip = String(targetIp);
        s_persistent_http_configured = true;
    }

    if (!s_persistent_http.connected()) {
        if (!s_persistent_http.begin(s_persistent_client, String(fullUrl))) {
            log_e("HTTPS begin failed for: %s", fullUrl);
            s_persistent_client.stop();
            return -1;
        }
    } else {
        s_persistent_http.setURL(String(path));
    }

    int code = s_persistent_http.GET();

    // If socket dropped by server (e.g. keep-alive timeout), reconnect once
    if (code <= 0) {
        log_w("Keep-alive socket dropped (%d), reconnecting to %s...", code, targetIp);
        linkplay_client_reset_session();
        s_persistent_client.setInsecure();
        s_persistent_http.setReuse(true);
        s_persistent_http.setTimeout(HTTP_REQUEST_TIMEOUT_MS);
        s_persistent_ip = String(targetIp);
        s_persistent_http_configured = true;

        if (s_persistent_http.begin(s_persistent_client, String(fullUrl))) {
            code = s_persistent_http.GET();
        }
    }

    if (code == HTTP_CODE_OK) {
        outPayload = s_persistent_http.getString();
    } else {
        log_w("HTTP GET command '%s' returned %d", cmd.c_str(), code);
    }

    return code;
}

bool linkplay_client_send_cmd(const char* targetIp, const String& cmd) {
    if (!targetIp || strlen(targetIp) == 0) return false;
    String payload;
    int httpCode = execute_api_get(targetIp, cmd, payload);
    return (httpCode == HTTP_CODE_OK);
}

static void fetch_upnp_track_duration(const char* targetIp) {
    if (!targetIp || strlen(targetIp) == 0) return;

    HTTPClient http;
    String url = "http://" + String(targetIp) + ":49152/upnp/control/rendertransport1";
    http.setTimeout(1500);

    if (!http.begin(url)) {
        return;
    }

    http.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
    http.addHeader("SOAPAction", "\"urn:schemas-upnp-org:service:AVTransport:1#GetPositionInfo\"");

    const char* soapBody = "<?xml version=\"1.0\"?>"
                           "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\">"
                           "<s:Body><u:GetPositionInfo xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
                           "<InstanceID>0</InstanceID>"
                           "</u:GetPositionInfo></s:Body></s:Envelope>";

    int httpCode = http.POST(soapBody);
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        int start = payload.indexOf("<TrackDuration>");
        if (start >= 0) {
            start += 15;
            int end = payload.indexOf("</TrackDuration>", start);
            if (end > start) {
                String durStr = payload.substring(start, end);
                int h = 0, m = 0, s = 0;
                if (sscanf(durStr.c_str(), "%d:%d:%d", &h, &m, &s) == 3) {
                    s_cached_track_duration_ms = (uint32_t)(h * 3600 + m * 60 + s) * 1000;
                    log_i("UPnP TrackDuration: %s (%u ms)", durStr.c_str(), s_cached_track_duration_ms);
                }
            }
        }
    }
    http.end();
}

bool linkplay_client_fetch_meta(const char* targetIp) {
    if (!targetIp || strlen(targetIp) == 0) return false;

    String payload;
    int httpCode = execute_api_get(targetIp, "getMetaInfo", payload);
    if (httpCode != HTTP_CODE_OK || payload.length() == 0) {
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) return false;

    TrackMeta* pMeta = (TrackMeta*)malloc(sizeof(TrackMeta));
    if (!pMeta) return false;
    memset(pMeta, 0, sizeof(TrackMeta));

    const char* rawTitle = doc["metaData"]["title"] | doc["Title"] | doc["title"] | "";
    const char* rawArtist = doc["metaData"]["artist"] | doc["Artist"] | doc["artist"] | "";
    const char* rawAlbum = doc["metaData"]["album"] | doc["Album"] | doc["album"] | "";
    String decTitle = net_decode_hex_string(rawTitle);
    String decArtist = net_decode_hex_string(rawArtist);
    String decAlbum = net_decode_hex_string(rawAlbum);

    strncpy(pMeta->title, decTitle.c_str(), sizeof(pMeta->title) - 1);
    strncpy(pMeta->artist, decArtist.c_str(), sizeof(pMeta->artist) - 1);
    strncpy(pMeta->album, decAlbum.c_str(), sizeof(pMeta->album) - 1);

    // Robust parsing of sampleRate and bitDepth (handles both integer and string variants)
    uint32_t sampleRate = 0;
    JsonVariant vRate = doc["metaData"]["sampleRate"];
    if (vRate.isNull()) vRate = doc["sampleRate"];
    if (vRate.is<uint32_t>()) {
        sampleRate = vRate.as<uint32_t>();
    } else if (vRate.is<const char*>()) {
        sampleRate = strtoul(vRate.as<const char*>(), nullptr, 10);
    }

    uint8_t bitDepth = 0;
    JsonVariant vDepth = doc["metaData"]["bitDepth"];
    if (vDepth.isNull()) vDepth = doc["bitDepth"];
    if (vDepth.is<uint8_t>()) {
        bitDepth = vDepth.as<uint8_t>();
    } else if (vDepth.is<const char*>()) {
        bitDepth = (uint8_t)atoi(vDepth.as<const char*>());
    }

    pMeta->sample_rate = sampleRate;
    pMeta->bit_depth = bitDepth;

    UiEvent evt;
    evt.type = UI_EVT_META_UPDATED;
    evt.data.meta = pMeta;

    if (xQueueSend(xQueueUiState, &evt, 0) != pdTRUE) {
        free(pMeta);
    }

    return (sampleRate > 0);
}

void linkplay_client_poll_active(const char* targetIp, bool is_fixed_vol, PlayerState& outState) {
    if (!targetIp || strlen(targetIp) == 0) return;

    String payload;
    int httpCode = execute_api_get(targetIp, "getPlayerStatus", payload);
    if (httpCode != HTTP_CODE_OK || payload.length() == 0) {
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) return;

    UiEvent evt;
    evt.type = UI_EVT_PLAYER_STATE;

    // Parse status
    const char* statusStr = doc["status"] | "";
    const char* rawTitle = doc["Title"] | doc["title"] | "";
    const char* rawArtist = doc["Artist"] | doc["artist"] | "";
    bool hasTitle = (strlen(rawTitle) > 0 || s_last_known_track_title.length() > 0);

    if (strcmp(statusStr, "play") == 0) {
        evt.data.player.state = PLAY_STATE_PLAYING;
    } else if (strcmp(statusStr, "pause") == 0) {
        evt.data.player.state = PLAY_STATE_PAUSED;
    } else if (strcmp(statusStr, "stop") == 0) {
        evt.data.player.state = PLAY_STATE_STOPPED;
    } else if (strcmp(statusStr, "none") == 0) {
        evt.data.player.state = hasTitle ? PLAY_STATE_BUFFERING : PLAY_STATE_NONE;
    } else if (strcmp(statusStr, "load") == 0 || strcmp(statusStr, "loading") == 0) {
        evt.data.player.state = PLAY_STATE_BUFFERING;
    } else {
        evt.data.player.state = PLAY_STATE_UNKNOWN;
    }

    // Volume & Mute
    const char* volStr = doc["vol"] | "0";
    evt.data.player.volume = (uint8_t)atoi(volStr);
    const char* muteStr = doc["mute"] | "0";
    evt.data.player.mute = (strcmp(muteStr, "1") == 0 || atoi(muteStr) == 1);
    evt.data.player.is_fixed_volume = is_fixed_vol;

    // Track Identity & Change Detection (evaluated BEFORE duration/position math)
    bool isNewTrack = false;
    if (evt.data.player.state == PLAY_STATE_NONE) {
        s_last_known_track_title = "";
        s_last_known_artist = "";
        s_cached_track_duration_ms = 0;
        s_meta_resolved = false;
    } else {
        if (strlen(rawTitle) > 0) {
            String decTitle = net_decode_hex_string(rawTitle);
            String decArtist = net_decode_hex_string(rawArtist);
            if (s_last_known_track_title != decTitle || s_last_known_artist != decArtist) {
                s_last_known_track_title = decTitle;
                s_last_known_artist = decArtist;
                s_cached_track_duration_ms = 0;
                s_meta_resolved = false;
                isNewTrack = true;
                if (linkplay_client_fetch_meta(targetIp)) {
                    s_meta_resolved = true;
                }
            } else if (!s_meta_resolved && evt.data.player.state == PLAY_STATE_PLAYING) {
                if (linkplay_client_fetch_meta(targetIp)) {
                    s_meta_resolved = true;
                }
            }
        } else if (s_last_known_track_title.length() > 0) {
            s_last_known_track_title = "";
            s_last_known_artist = "";
            s_cached_track_duration_ms = 0;
            s_meta_resolved = false;

            TrackMeta* pMeta = (TrackMeta*)malloc(sizeof(TrackMeta));
            if (pMeta) {
                memset(pMeta, 0, sizeof(TrackMeta));
                UiEvent evtMeta;
                evtMeta.type = UI_EVT_META_UPDATED;
                evtMeta.data.meta = pMeta;
                if (xQueueSend(xQueueUiState, &evtMeta, 0) != pdTRUE) {
                    free(pMeta);
                }
            }
        }
    }

    // Track duration and position
    const char* totlenStr = doc["totlen"] | "0";
    const char* curposStr = doc["curpos"] | "0";
    uint32_t totlen = strtoul(totlenStr, nullptr, 10);
    int32_t rawCurpos = strtol(curposStr, nullptr, 10);
    uint32_t curpos = (rawCurpos > 0) ? (uint32_t)rawCurpos : 0;

    // If streamer reports a negative pre-buffer countdown, audio has not started playing yet
    if (rawCurpos < 0 && evt.data.player.state == PLAY_STATE_PLAYING) {
        evt.data.player.state = PLAY_STATE_BUFFERING;
    }

    // Discard any residual transition position or stale buffering offsets
    if (isNewTrack || evt.data.player.state == PLAY_STATE_BUFFERING || evt.data.player.state == PLAY_STATE_LOADING || evt.data.player.state == PLAY_STATE_NONE || evt.data.player.state == PLAY_STATE_UNKNOWN) {
        curpos = 0;
    }

    // If totlen is given in seconds (< 10000 for normal song durations), convert to ms
    if (totlen > 0 && totlen < 10000) {
        totlen *= 1000;
    }

    // Fallback: If LinkPlay reports totlen == 0 (e.g. Amazon Music / Prime streams),
    // use the cached duration obtained from UPnP AVTransport for any active or stopped media
    if (totlen == 0 && evt.data.player.state != PLAY_STATE_NONE) {
        if (s_cached_track_duration_ms == 0) {
            fetch_upnp_track_duration(targetIp);
        }
        if (s_cached_track_duration_ms > 0) {
            totlen = s_cached_track_duration_ms;
        }
    }

    // Atomic validation: Curpos cannot legitimately exceed totlen in normal playback.
    // Cap curpos at totlen to accurately reflect when a song has reached the end.
    if (totlen > 0 && curpos > totlen) {
        curpos = totlen;
    }

    evt.data.player.totlen_ms = totlen;
    evt.data.player.curpos_ms = curpos;

    // Vendor / Service Provider & Stream Format
    if (evt.data.player.state == PLAY_STATE_NONE) {
        evt.data.player.vendor[0] = '\0';
        evt.data.player.stream.format[0] = '\0';
    } else {
        const char* rawVendor = doc["vendor"] | "";
        static const struct { const char* raw; const char* clean; } vendor_map[] = {
            {"Prime", "Amazon Music"},
            {"Spotify", "Spotify"},
            {"Tidal", "Tidal"},
            {"Qobuz", "Qobuz"},
            {"TuneIn", "TuneIn"},
            {"Deezer", "Deezer"}
        };
        const char* cleanVendor = nullptr;
        for (size_t i = 0; i < sizeof(vendor_map) / sizeof(vendor_map[0]); ++i) {
            if (strcasecmp(rawVendor, vendor_map[i].raw) == 0) {
                cleanVendor = vendor_map[i].clean;
                break;
            }
        }
        if (!cleanVendor) {
            cleanVendor = (strlen(rawVendor) > 0) ? rawVendor : "WiiM";
        }
        strncpy(evt.data.player.vendor, cleanVendor, sizeof(evt.data.player.vendor) - 1);

        const char* format = doc["format"] | doc["Type"] | doc["type"] | doc["vendor"] | "FLAC";
        strncpy(evt.data.player.stream.format, format, sizeof(evt.data.player.stream.format) - 1);
    }

    // Operating Mode
    const char* modeStr = doc["mode"] | "0";
    s_active_mode = (uint8_t)atoi(modeStr);

    // Queue / Playlist Track Count
    const char* plicurrStr = doc["plicurr"] | "0";
    const char* plicountStr = doc["plicount"] | "0";
    s_active_plicurr = (uint16_t)atoi(plicurrStr);
    s_active_plicount = (uint16_t)atoi(plicountStr);
    evt.data.player.track_num = s_active_plicurr;
    evt.data.player.track_total = s_active_plicount;

    // Stream / Audio Format
    const char* rate = doc["rate"] | doc["SampleRate"] | doc["samplerate"] | "";
    const char* bitDepth = doc["bitDepth"] | doc["bitdepth"] | "";
    evt.data.player.stream.sample_rate = atoi(rate);
    evt.data.player.stream.bit_depth = (uint8_t)atoi(bitDepth);

    s_last_player_state = evt.data.player;
    outState = evt.data.player;
    xQueueSend(xQueueUiState, &evt, 0);
}

void linkplay_client_fetch_presets(const char* targetIp) {
    if (!targetIp || strlen(targetIp) == 0) return;

    String payload;
    int httpCode = execute_api_get(targetIp, "getPresetInfo", payload);
    if (httpCode != HTTP_CODE_OK || payload.length() == 0) {
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);

    PresetList* pPresets = (PresetList*)malloc(sizeof(PresetList));
    if (!pPresets) return;
    memset(pPresets, 0, sizeof(PresetList));
    pPresets->count = MAX_PRESETS;

    // Initialize defaults 1..12
    for (uint8_t i = 0; i < MAX_PRESETS; ++i) {
        pPresets->items[i].index = i + 1;
        snprintf(pPresets->items[i].name, sizeof(pPresets->items[i].name), "Preset %d", i + 1);
    }

    if (!err) {
        JsonArray array = doc["preset_list"].as<JsonArray>();
        if (!array.isNull()) {
            uint8_t idx = 0;
            for (JsonObject item : array) {
                if (idx >= MAX_PRESETS) break;
                int pNum = item["number"] | item["preset_key"] | (idx + 1);
                const char* pName = item["name"] | item["title"] | "";
                if (pNum >= 1 && pNum <= MAX_PRESETS) {
                    pPresets->items[pNum - 1].index = pNum;
                    if (strlen(pName) > 0) {
                        String dec = net_decode_hex_string(pName);
                        strncpy(pPresets->items[pNum - 1].name, dec.c_str(), sizeof(pPresets->items[pNum - 1].name) - 1);
                    }
                }
                idx++;
            }
        }
    }

    UiEvent evt;
    evt.type = UI_EVT_PRESETS_UPDATED;
    evt.data.presets = pPresets;

    if (xQueueSend(xQueueUiState, &evt, 0) != pdTRUE) {
        free(pPresets);
    }
}

bool linkplay_client_fetch_config(const char* targetIp, bool& outIsFixedVol) {
    if (!targetIp || strlen(targetIp) == 0) return false;

    String payload;
    int httpCode = execute_api_get(targetIp, "getStatusEx", payload);
    if (httpCode != HTTP_CODE_OK || payload.length() == 0) {
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) return false;

    const char* vc = doc["volume_control"] | "0";
    outIsFixedVol = (strcmp(vc, "1") == 0);
    s_last_player_state.is_fixed_volume = outIsFixedVol;
    return true;
}

void linkplay_client_seek(const char* targetIp, uint32_t seek_ms) {
    if (!targetIp || strlen(targetIp) == 0) return;

    uint32_t totalSec = seek_ms / 1000;
    uint8_t h = totalSec / 3600;
    uint8_t m = (totalSec % 3600) / 60;
    uint8_t s = totalSec % 60;

    char targetStr[16];
    snprintf(targetStr, sizeof(targetStr), "%02u:%02u:%02u", h, m, s);

    HTTPClient http;
    String url = "http://" + String(targetIp) + ":49152/upnp/control/rendertransport1";
    http.setTimeout(1500);

    if (!http.begin(url)) {
        return;
    }

    http.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
    http.addHeader("SOAPAction", "\"urn:schemas-upnp-org:service:AVTransport:1#Seek\"");

    String soapBody = "<?xml version=\"1.0\"?>"
                      "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\">"
                      "<s:Body><u:Seek xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
                      "<InstanceID>0</InstanceID>"
                      "<Unit>REL_TIME</Unit>"
                      "<Target>" + String(targetStr) + "</Target>"
                      "</u:Seek></s:Body></s:Envelope>";

    int httpCode = http.POST(soapBody);
    log_i("UPnP Seek to %s returned HTTP %d", targetStr, httpCode);
    http.end();
}

const String& linkplay_client_get_last_title() {
    return s_last_known_track_title;
}

const String& linkplay_client_get_last_artist() {
    return s_last_known_artist;
}

const PlayerState& linkplay_client_get_last_player_state() {
    return s_last_player_state;
}
