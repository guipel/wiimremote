#include "ui/ui_lyrics.h"
#include "ui/ui_theme.h"
#include "config.h"

// External Queue for sending UI Commands to Network Task
extern QueueHandle_t xQueueUiCmd;

// UI Widgets - Lyrics Tab
static lv_obj_t* lbl_lyrics_title = nullptr;
static lv_obj_t* lbl_lyrics_artist = nullptr;
static lv_obj_t* cont_lyrics_scroll = nullptr;
static lv_obj_t* lbl_lyrics_body = nullptr;

static TrackMeta s_current_track_meta;

static void fetch_lyrics_if_needed() {
    if (strlen(s_current_track_meta.title) == 0) {
        if (lbl_lyrics_title) lv_label_set_text(lbl_lyrics_title, "Lyrics");
        if (lbl_lyrics_artist) lv_label_set_text(lbl_lyrics_artist, "");
        if (lbl_lyrics_body) lv_label_set_text(lbl_lyrics_body, "Play a track to view lyrics.");
        return;
    }

    const char* lyr_title = lbl_lyrics_title ? lv_label_get_text(lbl_lyrics_title) : "";
    const char* lyr_artist = lbl_lyrics_artist ? lv_label_get_text(lbl_lyrics_artist) : "";

    if (strcmp(s_current_track_meta.title, lyr_title) != 0 || strcmp(s_current_track_meta.artist, lyr_artist) != 0) {
        if (lbl_lyrics_title) lv_label_set_text(lbl_lyrics_title, s_current_track_meta.title);
        if (lbl_lyrics_artist) lv_label_set_text(lbl_lyrics_artist, s_current_track_meta.artist);
        if (lbl_lyrics_body) lv_label_set_text(lbl_lyrics_body, "Fetching lyrics from LRCLIB...");
        if (cont_lyrics_scroll) lv_obj_scroll_to_y(cont_lyrics_scroll, 0, LV_ANIM_OFF);

        UiCommand cmd;
        cmd.type = CMD_FETCH_LYRICS;
        xQueueSend(xQueueUiCmd, &cmd, 0);
    }
}

void ui_lyrics_init(lv_obj_t* parent) {
    lv_obj_set_style_bg_color(parent, COLOR_BG, 0);
    lv_obj_set_style_pad_all(parent, 8, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    // Track Title Header
    lbl_lyrics_title = lv_label_create(parent);
    lv_label_set_text(lbl_lyrics_title, "Lyrics");
    lv_obj_set_style_text_font(lbl_lyrics_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_lyrics_title, COLOR_ACCENT, 0);
    lv_obj_set_style_text_align(lbl_lyrics_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lbl_lyrics_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(lbl_lyrics_title, 224);
    lv_obj_align(lbl_lyrics_title, LV_ALIGN_TOP_MID, 0, 4);

    // Track Artist Header
    lbl_lyrics_artist = lv_label_create(parent);
    lv_label_set_text(lbl_lyrics_artist, "");
    lv_obj_set_style_text_font(lbl_lyrics_artist, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_lyrics_artist, COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_align(lbl_lyrics_artist, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lbl_lyrics_artist, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(lbl_lyrics_artist, 224);
    lv_obj_align(lbl_lyrics_artist, LV_ALIGN_TOP_MID, 0, 26);

    // Separator line
    lv_obj_t* sep = lv_obj_create(parent);
    lv_obj_set_size(sep, 216, 1);
    lv_obj_align(sep, LV_ALIGN_TOP_MID, 0, 46);
    lv_obj_set_style_bg_color(sep, COLOR_SURFACE_LIGHT, 0);
    lv_obj_set_style_border_opa(sep, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(sep, 0, 0);

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
    lv_obj_set_style_text_color(lbl_lyrics_body, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_line_space(lbl_lyrics_body, 6, 0);
    lv_obj_set_style_text_align(lbl_lyrics_body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(lbl_lyrics_body, 214);
    lv_obj_align(lbl_lyrics_body, LV_ALIGN_TOP_MID, 0, 4);
}

void ui_lyrics_set_content(const LyricsInfo& info) {
    // If lyrics arrived for a track that is no longer playing, discard them
    if (strlen(s_current_track_meta.title) > 0 && strcmp(info.title, s_current_track_meta.title) != 0) {
        log_w("Discarding stale lyrics for '%s' (currently playing: '%s')", info.title, s_current_track_meta.title);
        return;
    }

    if (lbl_lyrics_title) {
        if (strlen(info.title) > 0) {
            lv_label_set_text(lbl_lyrics_title, info.title);
        } else {
            lv_label_set_text(lbl_lyrics_title, "Lyrics");
        }
    }
    if (lbl_lyrics_artist) {
        lv_label_set_text(lbl_lyrics_artist, info.artist);
    }
    if (lbl_lyrics_body) {
        if (info.text && strlen(info.text) > 0) {
            lv_label_set_text(lbl_lyrics_body, info.text);
        } else if (info.is_loading) {
            lv_label_set_text(lbl_lyrics_body, "Fetching lyrics from LRCLIB...");
        } else {
            lv_label_set_text(lbl_lyrics_body, "No lyrics found for this track.");
        }
    }
    if (cont_lyrics_scroll) {
        lv_obj_scroll_to_y(cont_lyrics_scroll, 0, LV_ANIM_OFF);
    }
}

void ui_lyrics_on_track_changed(const TrackMeta& meta, bool is_lyrics_tab_active) {
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
    if (lbl_lyrics_title) lv_label_set_text(lbl_lyrics_title, "Lyrics");
    if (lbl_lyrics_artist) lv_label_set_text(lbl_lyrics_artist, "");
    if (lbl_lyrics_body) lv_label_set_text(lbl_lyrics_body, "Play a track to view lyrics.");
    if (cont_lyrics_scroll) lv_obj_scroll_to_y(cont_lyrics_scroll, 0, LV_ANIM_OFF);
}
