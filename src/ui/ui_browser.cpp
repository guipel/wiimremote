#include "ui/ui_browser.h"
#include "ui/ui_theme.h"
#include "config.h"
#include <stdio.h>

extern QueueHandle_t xQueueUiCmd;

struct NavEntry {
    char object_id[64];
    char title[48];
    uint16_t start_index;
};

static constexpr uint8_t MAX_NAV_DEPTH = 10;
static NavEntry s_nav_stack[MAX_NAV_DEPTH];
static uint8_t s_nav_depth = 0; // 0 = Server List, >= 1 = Inside s_active_server_idx
static uint8_t s_active_server_idx = 0;

static UpnpServerList s_servers;
static UpnpBrowseList s_current_browse;
static OnUpnpTrackPlayedCb s_on_track_played = nullptr;

static lv_obj_t* s_parent = nullptr;
static lv_obj_t* s_cont_header = nullptr;
static lv_obj_t* lbl_nav_icon = nullptr;
static lv_obj_t* lbl_header_title = nullptr;
static lv_obj_t* list_browser = nullptr;

static void render_server_list();
static bool request_browse(uint8_t serverIdx, const char* objectId, const char* title, uint16_t startIndex = 0);

static void show_status_message(const char* msg) {
    if (!list_browser) return;
    lv_obj_clean(list_browser);
    lv_obj_scroll_to_y(list_browser, 0, LV_ANIM_OFF);

    lv_obj_t* lbl = lv_label_create(list_browser);
    lv_label_set_text(lbl, msg);
    lv_obj_set_width(lbl, 224);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl, COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(lbl, 40, 0);
    lv_obj_set_style_pad_left(lbl, 8, 0);
}

static void update_header(bool showPageRange = false) {
    if (!lbl_nav_icon || !lbl_header_title) return;
    if (s_nav_depth == 0) {
        lv_label_set_text(lbl_nav_icon, LV_SYMBOL_REFRESH);
        lv_label_set_text(lbl_header_title, "UPnP Servers");
    } else {
        lv_label_set_text(lbl_nav_icon, LV_SYMBOL_LEFT);
        const char* baseTitle = s_nav_stack[s_nav_depth - 1].title;
        if (showPageRange && s_current_browse.count > 0 && (s_current_browse.start_index > 0 || s_current_browse.has_more)) {
            char hdrBuf[96];
            uint32_t firstItem = (uint32_t)s_current_browse.start_index + 1;
            uint32_t lastItem = (uint32_t)s_current_browse.start_index + (uint32_t)s_current_browse.count;
            if (s_current_browse.total_matches >= lastItem) {
                snprintf(hdrBuf, sizeof(hdrBuf), "%s (%u-%u of %u)",
                         baseTitle, (unsigned int)firstItem, (unsigned int)lastItem, (unsigned int)s_current_browse.total_matches);
            } else {
                snprintf(hdrBuf, sizeof(hdrBuf), "%s (%u-%u)",
                         baseTitle, (unsigned int)firstItem, (unsigned int)lastItem);
            }
            lv_label_set_text(lbl_header_title, hdrBuf);
        } else {
            lv_label_set_text(lbl_header_title, baseTitle);
        }
    }
}

static bool request_browse(uint8_t serverIdx, const char* objectId, const char* title, uint16_t startIndex) {
    UiCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = CMD_UPNP_BROWSE;
    cmd.data.upnp_browse.server_index = serverIdx;
    cmd.data.upnp_browse.start_index = startIndex;
    strncpy(cmd.data.upnp_browse.object_id, objectId, sizeof(cmd.data.upnp_browse.object_id) - 1);
    strncpy(cmd.data.upnp_browse.title, title, sizeof(cmd.data.upnp_browse.title) - 1);
    if (xQueueSend(xQueueUiCmd, &cmd, pdMS_TO_TICKS(50)) != pdTRUE) {
        log_w("[UPnP] Command queue busy, browse request dropped");
        return false;
    }

    update_header(false);
    show_status_message("Loading folder...");
    return true;
}

static void event_btn_nav(lv_event_t* e) {
    if (s_nav_depth == 0) {
        UiCommand cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = CMD_UPNP_SCAN_SERVERS;
        if (xQueueSend(xQueueUiCmd, &cmd, pdMS_TO_TICKS(50)) == pdTRUE) {
            show_status_message("Scanning for UPnP servers...");
        }
    } else {
        s_nav_depth--;
        if (s_nav_depth == 0) {
            render_server_list();
        } else {
            const NavEntry& prev = s_nav_stack[s_nav_depth - 1];
            if (!request_browse(s_active_server_idx, prev.object_id, prev.title, prev.start_index)) {
                s_nav_depth++;
            }
        }
    }
}

static void event_btn_page(lv_event_t* e) {
    if (s_nav_depth == 0) return;
    uint16_t targetStart = (uint16_t)(uintptr_t)lv_event_get_user_data(e);
    NavEntry& cur = s_nav_stack[s_nav_depth - 1];
    uint16_t prevStart = cur.start_index;
    cur.start_index = targetStart;
    if (!request_browse(s_active_server_idx, cur.object_id, cur.title, targetStart)) {
        cur.start_index = prevStart;
    }
}

static void event_btn_server(lv_event_t* e) {
    uint8_t srvIdx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (srvIdx >= s_servers.count) return;

    s_active_server_idx = srvIdx;
    s_nav_depth = 1;
    memset(&s_nav_stack[0], 0, sizeof(s_nav_stack[0]));
    strncpy(s_nav_stack[0].object_id, "0", sizeof(s_nav_stack[0].object_id) - 1);
    strncpy(s_nav_stack[0].title, s_servers.servers[srvIdx].name, sizeof(s_nav_stack[0].title) - 1);
    s_nav_stack[0].start_index = 0;

    if (!request_browse(srvIdx, "0", s_servers.servers[srvIdx].name, 0)) {
        s_nav_depth = 0;
    }
}

static void event_btn_item(lv_event_t* e) {
    uint8_t itemIdx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (itemIdx >= s_current_browse.count) return;

    const UpnpItem& item = s_current_browse.items[itemIdx];
    if (item.is_folder) {
        uint8_t prevDepth = s_nav_depth;
        if (s_nav_depth < MAX_NAV_DEPTH) {
            NavEntry& next = s_nav_stack[s_nav_depth++];
            memset(&next, 0, sizeof(next));
            strncpy(next.object_id, item.id, sizeof(next.object_id) - 1);
            strncpy(next.title, item.title, sizeof(next.title) - 1);
            next.start_index = 0;
        } else {
            NavEntry& top = s_nav_stack[MAX_NAV_DEPTH - 1];
            strncpy(top.object_id, item.id, sizeof(top.object_id) - 1);
            strncpy(top.title, item.title, sizeof(top.title) - 1);
            top.start_index = 0;
        }
        if (!request_browse(s_active_server_idx, item.id, item.title, 0)) {
            s_nav_depth = prevDepth;
        }
    } else {
        UiCommand cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = CMD_UPNP_PLAY_TRACK;
        cmd.data.upnp_play.track_index = item.track_index;
        if (xQueueSend(xQueueUiCmd, &cmd, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (s_on_track_played) {
                s_on_track_played();
            }
        } else {
            log_w("[UPnP] Command queue full, play track request dropped");
        }
    }
}

static void render_server_list() {
    update_header(false);
    if (!list_browser) return;
    lv_obj_clean(list_browser);
    lv_obj_scroll_to_y(list_browser, 0, LV_ANIM_OFF);

    if (s_servers.count == 0) {
        show_status_message("No UPnP servers found.\nTap refresh to scan.");
        return;
    }

    for (uint8_t i = 0; i < s_servers.count; ++i) {
        lv_obj_t* btn = lv_list_add_btn(list_browser, LV_SYMBOL_DRIVE, s_servers.servers[i].name);
        ui_style_flat_list_btn(btn, COLOR_ACCENT, COLOR_TEXT_PRIMARY, 7);
        lv_obj_add_event_cb(btn, event_btn_server, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
    }
}

void ui_browser_init(lv_obj_t* parent) {
    s_parent = parent;
    memset(&s_servers, 0, sizeof(s_servers));
    memset(&s_current_browse, 0, sizeof(s_current_browse));
    s_nav_depth = 0;

    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    // Full-Width Clickable Header Bar (240 x 32 px)
    s_cont_header = lv_obj_create(parent);
    lv_obj_set_size(s_cont_header, 240, 32);
    lv_obj_align(s_cont_header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_border_side(s_cont_header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(s_cont_header, 1, 0);
    lv_obj_set_style_radius(s_cont_header, 0, 0);
    lv_obj_set_style_pad_all(s_cont_header, 0, 0);
    lv_obj_clear_flag(s_cont_header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cont_header, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_cont_header, event_btn_nav, LV_EVENT_CLICKED, nullptr);

    lbl_nav_icon = lv_label_create(s_cont_header);
    lv_label_set_text(lbl_nav_icon, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_font(lbl_nav_icon, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_nav_icon, LV_ALIGN_LEFT_MID, 10, 0);

    lbl_header_title = lv_label_create(s_cont_header);
    lv_label_set_text(lbl_header_title, "UPnP Servers");
    lv_label_set_long_mode(lbl_header_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(lbl_header_title, 200);
    lv_obj_set_style_text_font(lbl_header_title, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_header_title, LV_ALIGN_LEFT_MID, 32, 0);

    // Flat Edge-to-Edge List Container (240 x 228 px)
    list_browser = lv_list_create(parent);
    lv_obj_set_size(list_browser, 240, 228);
    lv_obj_align(list_browser, LV_ALIGN_TOP_MID, 0, 32);
    lv_obj_set_style_border_side(list_browser, LV_BORDER_SIDE_NONE, 0);
    lv_obj_set_style_radius(list_browser, 0, 0);
    lv_obj_set_style_pad_all(list_browser, 0, 0);
    lv_obj_set_style_pad_row(list_browser, 0, 0);
}

void ui_browser_apply_theme() {
    if (s_parent) lv_obj_set_style_bg_color(s_parent, COLOR_BG, 0);
    if (s_cont_header) {
        lv_obj_set_style_bg_color(s_cont_header, COLOR_SURFACE, 0);
        lv_obj_set_style_bg_color(s_cont_header, COLOR_SURFACE_LIGHT, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(s_cont_header, COLOR_SURFACE_LIGHT, 0);
    }
    if (lbl_nav_icon) lv_obj_set_style_text_color(lbl_nav_icon, COLOR_ACCENT, 0);
    if (lbl_header_title) lv_obj_set_style_text_color(lbl_header_title, COLOR_ACCENT, 0);
    if (list_browser) lv_obj_set_style_bg_color(list_browser, COLOR_BG, 0);

    if (s_nav_depth == 0) {
        render_server_list();
    } else {
        ui_browser_set_browse_result(s_current_browse);
    }
}

void ui_browser_on_tab_activated() {
    if (s_nav_depth == 0 && s_servers.count == 0) {
        UiCommand cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = CMD_UPNP_SCAN_SERVERS;
        if (xQueueSend(xQueueUiCmd, &cmd, pdMS_TO_TICKS(50)) == pdTRUE) {
            show_status_message("Scanning for UPnP servers...");
        }
    }
}

void ui_browser_set_servers(const UpnpServerList& servers) {
    s_servers = servers;
    if (s_nav_depth == 0) {
        render_server_list();
    }
}

void ui_browser_set_browse_result(const UpnpBrowseList& result) {
    s_current_browse = result;
    if (s_nav_depth == 0 || !list_browser) return;

    update_header(true);
    lv_obj_clean(list_browser);
    lv_obj_scroll_to_y(list_browser, 0, LV_ANIM_OFF);

    if (result.is_error) {
        show_status_message("Failed to load folder.\nCheck UPnP server.");
        return;
    }

    if (result.count == 0) {
        show_status_message("Folder is empty.");
        return;
    }

    if (result.start_index > 0) {
        uint16_t prevStart = (result.start_index >= MAX_UPNP_ITEMS) ? (result.start_index - MAX_UPNP_ITEMS) : 0;
        char prevLabel[48];
        snprintf(prevLabel, sizeof(prevLabel), "Previous Page (%u-%u)",
                 (unsigned int)(prevStart + 1), (unsigned int)result.start_index);
        lv_obj_t* btnPrev = lv_list_add_btn(list_browser, LV_SYMBOL_LEFT, prevLabel);
        ui_style_flat_list_btn(btnPrev, COLOR_ACCENT, COLOR_ACCENT, 7);
        lv_obj_add_event_cb(btnPrev, event_btn_page, LV_EVENT_CLICKED, (void*)(uintptr_t)prevStart);
    }

    for (uint8_t i = 0; i < result.count; ++i) {
        const UpnpItem& item = result.items[i];
        lv_obj_t* btn = lv_list_add_btn(
            list_browser,
            item.is_folder ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_AUDIO,
            item.title
        );
        ui_style_flat_list_btn(btn, item.is_folder ? COLOR_ACCENT : COLOR_TEXT_MUTED, COLOR_TEXT_PRIMARY, 7);
        lv_obj_add_event_cb(btn, event_btn_item, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
    }

    if (result.has_more) {
        uint32_t nextStart32 = (uint32_t)result.start_index + (uint32_t)result.count;
        if (nextStart32 <= 65000U) {
            uint16_t nextStart = (uint16_t)nextStart32;
            uint32_t nextEnd = nextStart32 + MAX_UPNP_ITEMS;
            if (result.total_matches > nextStart && result.total_matches < nextEnd) {
                nextEnd = result.total_matches;
            }
            char nextLabel[48];
            if (result.total_matches > nextStart) {
                snprintf(nextLabel, sizeof(nextLabel), "Next Page (%u-%u)",
                         (unsigned int)(nextStart + 1), (unsigned int)nextEnd);
            } else {
                snprintf(nextLabel, sizeof(nextLabel), "Next Page (%u+)",
                         (unsigned int)(nextStart + 1));
            }
            lv_obj_t* btnNext = lv_list_add_btn(list_browser, LV_SYMBOL_RIGHT, nextLabel);
            ui_style_flat_list_btn(btnNext, COLOR_ACCENT, COLOR_ACCENT, 7);
            lv_obj_add_event_cb(btnNext, event_btn_page, LV_EVENT_CLICKED, (void*)(uintptr_t)nextStart);
        }
    }
}

void ui_browser_set_on_track_played(OnUpnpTrackPlayedCb cb) {
    s_on_track_played = cb;
}
