#include "net/lyrics_client.h"
#include "net/net_utils.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>
#include "model.h"

extern QueueHandle_t xQueueUiState;

void lyrics_client_fetch(const char* title, const char* artist) {
    if (!title || strlen(title) == 0) {
        return;
    }

    String url = "https://lrclib.net/api/get?track_name=" + net_url_encode(title);
    if (artist && strlen(artist) > 0) {
        url += "&artist_name=" + net_url_encode(artist);
    }

    esp_task_wdt_reset();

    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(10000);
    http.setUserAgent("WiiMRemote/1.0 (ESP32-S3)");

    int code = -1;
    String lyricsText;

    for (uint8_t attempt = 0; attempt < 2; ++attempt) {
        Serial.printf("[LRCLIB] GET %s\n", url.c_str());

        if (http.begin(url)) {
            code = http.GET();
            esp_task_wdt_reset();

            if (code == HTTP_CODE_OK) {
                String payload = http.getString();
                JsonDocument doc;
                if (!deserializeJson(doc, payload)) {
                    const char* plain = doc["plainLyrics"];
                    if (plain && strlen(plain) > 0) {
                        lyricsText = plain;
                    } else {
                        const char* synced = doc["syncedLyrics"];
                        if (synced && strlen(synced) > 0) {
                            lyricsText = net_strip_lrc_timestamps(synced);
                        }
                    }
                }
            }
            http.end();
        }

        Serial.printf("[LRCLIB] Response code=%d, lyricsLen=%u\n", code, (unsigned int)lyricsText.length());

        // LRCLIB returns HTTP 503 with 'retry-after: 1' when momentarily busy
        if (code == HTTP_CODE_SERVICE_UNAVAILABLE && attempt == 0) {
            vTaskDelay(pdMS_TO_TICKS(1200));
            esp_task_wdt_reset();
            continue;
        }
        break;
    }

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
