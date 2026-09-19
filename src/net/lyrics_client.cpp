#include "net/lyrics_client.h"
#include "net/net_utils.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "model.h"

extern QueueHandle_t xQueueUiState;

void lyrics_client_fetch(const char* title, const char* artist) {
    if (!title || strlen(title) == 0) {
        return;
    }

    String cleanTitle = title;
    String cleanArtist = (artist ? artist : "");

    int lastHttpCode = 0;

    auto queryLrclib = [&lastHttpCode](const String& t, const String& a, String& outLyrics) -> bool {
        WiFiClientSecure secureClient;
        secureClient.setInsecure();
        secureClient.setTimeout(4000);

        HTTPClient http;
        String url = "https://lrclib.net/api/get?track_name=" + net_url_encode(t.c_str());
        if (a.length() > 0) {
            url += "&artist_name=" + net_url_encode(a.c_str());
        }

        http.begin(secureClient, url);
        http.setUserAgent("WiiMRemote/1.0 (ESP32-S3)");
        int code = http.GET();
        lastHttpCode = code;

        if (code == 200) {
            String payload = http.getString();
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, payload);
            if (!err) {
                const char* plain = doc["plainLyrics"];
                if (plain && strlen(plain) > 0) {
                    outLyrics = plain;
                    http.end();
                    return true;
                }
                const char* synced = doc["syncedLyrics"];
                if (synced && strlen(synced) > 0) {
                    outLyrics = net_strip_lrc_timestamps(synced);
                    http.end();
                    return (outLyrics.length() > 0);
                }
            }
        }
        http.end();
        return false;
    };

    String lyricsText;
    bool found = queryLrclib(cleanTitle, cleanArtist, lyricsText);

    // If not found and title has parenthesized/bracketed additions, e.g. "Song (Remastered 2011)", try stripped title
    if (!found && (cleanTitle.indexOf('(') >= 0 || cleanTitle.indexOf('-') >= 0)) {
        String strippedTitle = cleanTitle;
        int p = strippedTitle.indexOf('(');
        if (p > 0) strippedTitle = strippedTitle.substring(0, p);
        p = strippedTitle.indexOf('-');
        if (p > 0) strippedTitle = strippedTitle.substring(0, p);
        strippedTitle.trim();
        if (strippedTitle.length() > 0 && strippedTitle != cleanTitle) {
            found = queryLrclib(strippedTitle, cleanArtist, lyricsText);
        }
    }

    // Post result to UI
    LyricsInfo* pResult = (LyricsInfo*)malloc(sizeof(LyricsInfo));
    if (pResult) {
        memset(pResult, 0, sizeof(LyricsInfo));
        strncpy(pResult->title, title, sizeof(pResult->title) - 1);
        strncpy(pResult->artist, (artist ? artist : ""), sizeof(pResult->artist) - 1);
        pResult->is_loading = false;
        if (found && lyricsText.length() > 0) {
            pResult->text = strdup(lyricsText.c_str());
        } else if (lastHttpCode < 0 || lastHttpCode >= 500) {
            pResult->text = strdup("Failed to retrieve lyrics.\nPlease check your connection.");
        } else {
            pResult->text = strdup("No lyrics found for this track.");
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
