#include "ui/ui_lyrics.h"
#include "ui/ui_theme.h"
#include "config.h"

// External Queue for sending UI Commands to Network Task
extern QueueHandle_t xQueueUiCmd;

// UI Widgets - Lyrics Tab
static lv_obj_t* s_parent = nullptr;
static lv_obj_t* lbl_lyrics_title = nullptr;
static lv_obj_t* lbl_lyrics_artist = nullptr;
static lv_obj_t* sep_lyrics = nullptr;
static lv_obj_t* cont_lyrics_scroll = nullptr;
static lv_obj_t* lbl_lyrics_body = nullptr;

static TrackMeta s_current_track_meta;
static bool s_lyrics_resolved = false;
static bool s_lyrics_loading = false;

static void fetch_lyrics_if_needed() {
    if (strlen(s_current_track_meta.title) == 0) {
        if (lbl_lyrics_title) lv_label_set_text(lbl_lyrics_title, "Lyrics");
        if (lbl_lyrics_artist) lv_label_set_text(lbl_lyrics_artist, "");
        if (lbl_lyrics_body) lv_label_set_text(lbl_lyrics_body, "Play a track to view lyrics.");
        s_lyrics_resolved = false;
        s_lyrics_loading = false;
        return;
    }

    const char* lyr_title = lbl_lyrics_title ? lv_label_get_text(lbl_lyrics_title) : "";
    const char* lyr_artist = lbl_lyrics_artist ? lv_label_get_text(lbl_lyrics_artist) : "";
    bool trackChanged = (strcmp(s_current_track_meta.title, lyr_title) != 0 ||
                         strcmp(s_current_track_meta.artist, lyr_artist) != 0);

    if (trackChanged || (!s_lyrics_resolved && !s_lyrics_loading)) {
        if (lbl_lyrics_title) lv_label_set_text(lbl_lyrics_title, s_current_track_meta.title);
        if (lbl_lyrics_artist) lv_label_set_text(lbl_lyrics_artist, s_current_track_meta.artist);
        if (lbl_lyrics_body) lv_label_set_text(lbl_lyrics_body, "Fetching lyrics from LRCLIB...");
        if (cont_lyrics_scroll) lv_obj_scroll_to_y(cont_lyrics_scroll, 0, LV_ANIM_OFF);

        UiCommand cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = CMD_FETCH_LYRICS;
        strncpy(cmd.data.lyrics_req.title, s_current_track_meta.title, sizeof(cmd.data.lyrics_req.title) - 1);
        strncpy(cmd.data.lyrics_req.artist, s_current_track_meta.artist, sizeof(cmd.data.lyrics_req.artist) - 1);
        if (xQueueSend(xQueueUiCmd, &cmd, 0) == pdTRUE) {
            s_lyrics_loading = true;
            s_lyrics_resolved = false;
        }
    }
}

void ui_lyrics_init(lv_obj_t* parent) {
    s_parent = parent;
    lv_obj_set_style_pad_all(parent, 8, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    // Track Title Header
    lbl_lyrics_title = lv_label_create(parent);
    lv_label_set_text(lbl_lyrics_title, "Lyrics");
    lv_obj_set_style_text_font(lbl_lyrics_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(lbl_lyrics_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lbl_lyrics_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(lbl_lyrics_title, 224);
    lv_obj_align(lbl_lyrics_title, LV_ALIGN_TOP_MID, 0, 4);

    // Track Artist Header
    lbl_lyrics_artist = lv_label_create(parent);
    lv_label_set_text(lbl_lyrics_artist, "");
    lv_obj_set_style_text_font(lbl_lyrics_artist, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(lbl_lyrics_artist, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lbl_lyrics_artist, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(lbl_lyrics_artist, 224);
    lv_obj_align(lbl_lyrics_artist, LV_ALIGN_TOP_MID, 0, 26);

    // Separator line
    sep_lyrics = lv_obj_create(parent);
    lv_obj_set_size(sep_lyrics, 216, 1);
    lv_obj_align(sep_lyrics, LV_ALIGN_TOP_MID, 0, 46);
    lv_obj_set_style_border_opa(sep_lyrics, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(sep_lyrics, 0, 0);

    // Scrollable container for lyrics body
    cont_lyrics_scroll = lv_obj_create(parent);
    lv_obj_set_size(cont_lyrics_scroll, 224, 206);
    lv_obj_align(cont_lyrics_scroll, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_style_bg_opa(cont_lyrics_scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(cont_lyrics_scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(cont_lyrics_scroll, 4, 0);
    lv_obj_add_flag(cont_lyrics_scroll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(cont_lyrics_scroll, LV_DIR_VER);

    lbl_lyrics_body = lv_label_create(cont_lyrics_scroll);
    lv_label_set_text(lbl_lyrics_body, "Play a track to view lyrics.");
    lv_obj_set_style_text_font(lbl_lyrics_body, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_line_space(lbl_lyrics_body, 6, 0);
    lv_obj_set_style_text_align(lbl_lyrics_body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(lbl_lyrics_body, 214);
    lv_obj_align(lbl_lyrics_body, LV_ALIGN_TOP_MID, 0, 4);
}

void ui_lyrics_apply_theme() {
    if (s_parent) lv_obj_set_style_bg_color(s_parent, COLOR_BG, 0);
    if (lbl_lyrics_title) lv_obj_set_style_text_color(lbl_lyrics_title, COLOR_ACCENT, 0);
    if (lbl_lyrics_artist) lv_obj_set_style_text_color(lbl_lyrics_artist, COLOR_TEXT_MUTED, 0);
    if (sep_lyrics) lv_obj_set_style_bg_color(sep_lyrics, COLOR_SURFACE_LIGHT, 0);
    if (lbl_lyrics_body) lv_obj_set_style_text_color(lbl_lyrics_body, COLOR_TEXT_PRIMARY, 0);
}

void ui_lyrics_set_content(const LyricsInfo& info) {
    // If lyrics arrived for a track that is no longer playing, discard them
    if (strlen(s_current_track_meta.title) > 0 && strlen(info.title) > 0 &&
        strcmp(info.title, s_current_track_meta.title) != 0) {
        log_w("Discarding stale lyrics for '%s' (currently playing: '%s')", info.title, s_current_track_meta.title);
        return;
    }

    s_lyrics_loading = info.is_loading;

    if (lbl_lyrics_title) {
        if (strlen(info.title) > 0) {
            lv_label_set_text(lbl_lyrics_title, info.title);
        } else if (strlen(s_current_track_meta.title) > 0) {
            lv_label_set_text(lbl_lyrics_title, s_current_track_meta.title);
        } else {
            lv_label_set_text(lbl_lyrics_title, "Lyrics");
        }
    }
    if (lbl_lyrics_artist) {
        lv_label_set_text(lbl_lyrics_artist, strlen(info.artist) > 0 ? info.artist : s_current_track_meta.artist);
    }
    if (lbl_lyrics_body) {
        if (info.text && strlen(info.text) > 0) {
            lv_label_set_text(lbl_lyrics_body, info.text);
            // Allow retry on re-entering tab if the fetch failed due to network error
            s_lyrics_resolved = (strncmp(info.text, "Connection error", 16) != 0);
        } else if (info.is_loading) {
            lv_label_set_text(lbl_lyrics_body, "Fetching lyrics from LRCLIB...");
            s_lyrics_resolved = false;
        } else {
            lv_label_set_text(lbl_lyrics_body, "Lyrics not found\nfor this track.");
            s_lyrics_resolved = true;
        }
    }
    if (cont_lyrics_scroll) {
        lv_obj_scroll_to_y(cont_lyrics_scroll, 0, LV_ANIM_OFF);
    }
}

void ui_lyrics_on_track_changed(const TrackMeta& meta, bool is_lyrics_tab_active) {
    if (strcmp(s_current_track_meta.title, meta.title) != 0 ||
        strcmp(s_current_track_meta.artist, meta.artist) != 0) {
        s_lyrics_resolved = false;
        s_lyrics_loading = false;
    }
    s_current_track_meta = meta;
    if (is_lyrics_tab_active) {
        fetch_lyrics_if_needed();
    }
}

void ui_lyrics_on_tab_activated() {
    fetch_lyrics_if_needed();
}

void ui_lyrics_clear() {
    memset(&s_current_track_meta, 0, sizeof(s_current_track_meta));
    s_lyrics_resolved = false;
    s_lyrics_loading = false;
    if (lbl_lyrics_title) lv_label_set_text(lbl_lyrics_title, "Lyrics");
    if (lbl_lyrics_artist) lv_label_set_text(lbl_lyrics_artist, "");
    if (lbl_lyrics_body) lv_label_set_text(lbl_lyrics_body, "Play a track to view lyrics.");
    if (cont_lyrics_scroll) lv_obj_scroll_to_y(cont_lyrics_scroll, 0, LV_ANIM_OFF);
}
