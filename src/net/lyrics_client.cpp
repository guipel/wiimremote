#include "net/lyrics_client.h"
#include "net/net_utils.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>
#include "model.h"

extern QueueHandle_t xQueueUiState;

static int fetch_lrclib_url(const String& url, String& outPayload) {
    outPayload = "";
    HTTPClient http;
    http.setConnectTimeout(3500);
    http.setTimeout(4000);
    http.setUserAgent("WiiMRemote/1.0 (ESP32-S3)");

    int code = -1;
    for (uint8_t attempt = 0; attempt < 2; ++attempt) {
        log_i("[LRCLIB] GET %s", url.c_str());
        esp_task_wdt_reset();

        if (http.begin(url)) {
            code = http.GET();
            esp_task_wdt_reset();
            if (code == HTTP_CODE_OK) {
                outPayload = http.getString();
            }
            http.end();
        }

        // LRCLIB returns HTTP 503 with 'retry-after: 1' when momentarily busy
        if (code == HTTP_CODE_SERVICE_UNAVAILABLE && attempt == 0) {
            vTaskDelay(pdMS_TO_TICKS(1200));
            esp_task_wdt_reset();
            continue;
        }
        break;
    }
    return code;
}

static String extract_lyrics_from_obj(JsonObjectConst obj) {
    const char* plain = obj["plainLyrics"];
    if (plain && strlen(plain) > 0) {
        return String(plain);
    }
    const char* synced = obj["syncedLyrics"];
    if (synced && strlen(synced) > 0) {
        return net_strip_lrc_timestamps(synced);
    }
    return "";
}

static bool artist_matches(const char* queryArtist, const char* itemArtist) {
    if (!queryArtist || strlen(queryArtist) == 0) return true;
    if (!itemArtist || strlen(itemArtist) == 0) return false;
    String q = String(queryArtist);
    String it = String(itemArtist);
    q.toLowerCase();
    it.toLowerCase();
    return (it.indexOf(q) >= 0 || q.indexOf(it) >= 0);
}

void lyrics_client_fetch(const char* title, const char* artist) {
    if (!title || strlen(title) == 0) {
        return;
    }

    String url = "https://lrclib.net/api/get?track_name=" + net_url_encode(title);
    if (artist && strlen(artist) > 0) {
        url += "&artist_name=" + net_url_encode(artist);
    }

    String payload;
    int code = fetch_lrclib_url(url, payload);
    String lyricsText;

    if (code == HTTP_CODE_OK && payload.length() > 0) {
        JsonDocument doc;
        if (!deserializeJson(doc, payload)) {
            lyricsText = extract_lyrics_from_obj(doc.as<JsonObjectConst>());
        }
    }

    // Fallback: If exact /api/get returned 404 or empty lyrics, query /api/search?q=<title> <artist>
    if ((code == HTTP_CODE_NOT_FOUND || (code == HTTP_CODE_OK && lyricsText.length() == 0))) {
        String query = String(title);
        if (artist && strlen(artist) > 0) {
            query += " " + String(artist);
        }
        String searchUrl = "https://lrclib.net/api/search?q=" + net_url_encode(query.c_str());
        int searchCode = fetch_lrclib_url(searchUrl, payload);
        if (searchCode == HTTP_CODE_OK && payload.length() > 0) {
            code = HTTP_CODE_OK;
            JsonDocument filter;
            filter[0]["artistName"] = true;
            filter[0]["plainLyrics"] = true;
            filter[0]["syncedLyrics"] = true;

            JsonDocument doc;
            if (!deserializeJson(doc, payload, DeserializationOption::Filter(filter))) {
                JsonArrayConst arr = doc.as<JsonArrayConst>();
                for (JsonObjectConst item : arr) {
                    const char* itemArtist = item["artistName"] | "";
                    if (!artist_matches(artist, itemArtist)) continue;
                    String candidate = extract_lyrics_from_obj(item);
                    if (candidate.length() > 0) {
                        lyricsText = candidate;
                        break;
                    }
                }
            }
        } else if (searchCode > 0) {
            code = searchCode;
        }
    }

    log_i("[LRCLIB] Final code=%d, lyricsLen=%u", code, (unsigned int)lyricsText.length());

    LyricsInfo* pResult = (LyricsInfo*)malloc(sizeof(LyricsInfo));
    if (pResult) {
        memset(pResult, 0, sizeof(LyricsInfo));
        strncpy(pResult->title, title, sizeof(pResult->title) - 1);
        strncpy(pResult->artist, (artist ? artist : ""), sizeof(pResult->artist) - 1);
        pResult->is_loading = false;
        if (code == HTTP_CODE_OK && lyricsText.length() > 0) {
            pResult->text = strdup(lyricsText.c_str());
        } else if (code == HTTP_CODE_NOT_FOUND || code == HTTP_CODE_OK) {
            pResult->text = strdup("Lyrics not found\nfor this track.");
        } else if (code >= 500) {
            pResult->text = strdup("LRCLIB server is busy.\nPlease try again shortly.");
        } else {
            pResult->text = strdup("Connection error.\nPlease check your Wi-Fi.");
        }
        UiEvent evt;
        evt.type = UI_EVT_LYRICS_UPDATED;
        evt.data.lyrics = pResult;
        if (xQueueSend(xQueueUiState, &evt, 0) != pdTRUE) {
            if (pResult->text) free(pResult->text);
            free(pResult);
        }
    }
}
