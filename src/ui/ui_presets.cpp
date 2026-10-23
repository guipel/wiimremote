#include "ui/ui_presets.h"
#include "ui/ui_theme.h"
#include "config.h"

// External Queue for sending UI Commands to Network Task
extern QueueHandle_t xQueueUiCmd;

static lv_obj_t* s_parent = nullptr;
static lv_obj_t* list_presets = nullptr;
static lv_obj_t* preset_btns[MAX_PRESETS] = { nullptr };
static lv_obj_t* preset_lbls[MAX_PRESETS] = { nullptr };
static lv_obj_t* preset_idx_lbls[MAX_PRESETS] = { nullptr };
static OnPresetTriggeredCb s_on_preset_triggered = nullptr;

static void event_btn_preset(lv_event_t* e) {
    uintptr_t index = (uintptr_t)lv_event_get_user_data(e);
    UiCommand cmd;
    cmd.type = CMD_TRIGGER_PRESET;
    cmd.data.preset_index = (uint8_t)index;
    if (xQueueSend(xQueueUiCmd, &cmd, pdMS_TO_TICKS(20)) != pdTRUE) return;

    if (s_on_preset_triggered) {
        s_on_preset_triggered((uint8_t)index);
    }
}

void ui_presets_init(lv_obj_t* parent) {
    s_parent = parent;
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    list_presets = lv_list_create(parent);
    lv_obj_set_size(list_presets, 240, 260);
    lv_obj_align(list_presets, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_border_side(list_presets, LV_BORDER_SIDE_NONE, 0);
    lv_obj_set_style_radius(list_presets, 0, 0);
    lv_obj_set_style_pad_all(list_presets, 0, 0);
    lv_obj_set_style_pad_row(list_presets, 0, 0);

    for (uint8_t i = 0; i < MAX_PRESETS; ++i) {
        char default_name[32];
        snprintf(default_name, sizeof(default_name), "Preset %d", i + 1);

        lv_obj_t* btn = lv_list_add_btn(list_presets, nullptr, default_name);
        preset_btns[i] = btn;

        lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_style_pad_ver(btn, 7, 0);
        lv_obj_set_style_pad_hor(btn, 8, 0);
        lv_obj_set_style_pad_column(btn, 8, 0);
        lv_obj_set_style_transform_width(btn, 0, LV_STATE_PRESSED);
        lv_obj_set_style_flex_cross_place(btn, LV_FLEX_ALIGN_CENTER, 0);
        lv_obj_add_event_cb(btn, event_btn_preset, LV_EVENT_CLICKED, (void*)(uintptr_t)(i + 1));

        // The label created by lv_list_add_btn has LV_LABEL_LONG_SCROLL_CIRCULAR and flex_grow = 1
        preset_lbls[i] = lv_obj_get_child(btn, 0);
        if (preset_lbls[i]) {
            lv_obj_set_style_text_font(preset_lbls[i], &lv_font_montserrat_14, 0);
        }

        // Compact Left Index Label (prepended at index 0)
        lv_obj_t* lbl_idx = lv_label_create(btn);
        preset_idx_lbls[i] = lbl_idx;
        char idx_str[8];
        snprintf(idx_str, sizeof(idx_str), "%d", i + 1);
        lv_label_set_text(lbl_idx, idx_str);
        lv_obj_set_width(lbl_idx, 18);
        lv_obj_set_style_text_font(lbl_idx, &lv_font_montserrat_12, 0);
        lv_obj_move_to_index(lbl_idx, 0);
    }
}

void ui_presets_apply_theme() {
    if (s_parent) lv_obj_set_style_bg_color(s_parent, COLOR_BG, 0);
    if (list_presets) lv_obj_set_style_bg_color(list_presets, COLOR_BG, 0);
    for (uint8_t i = 0; i < MAX_PRESETS; ++i) {
        if (preset_btns[i]) {
            lv_obj_set_style_bg_color(preset_btns[i], COLOR_BG, 0);
            lv_obj_set_style_bg_color(preset_btns[i], COLOR_SURFACE_LIGHT, LV_STATE_PRESSED);
            lv_obj_set_style_border_color(preset_btns[i], COLOR_SURFACE_LIGHT, 0);
        }
        if (preset_lbls[i]) {
            lv_obj_set_style_text_color(preset_lbls[i], COLOR_TEXT_PRIMARY, 0);
        }
        if (preset_idx_lbls[i]) {
            lv_obj_set_style_text_color(preset_idx_lbls[i], COLOR_ACCENT, 0);
        }
    }
}

void ui_presets_set_list(const PresetList& presets) {
    for (uint8_t i = 0; i < presets.count && i < MAX_PRESETS; ++i) {
        if (preset_lbls[i]) {
            lv_label_set_text(preset_lbls[i], presets.items[i].name);
        }
        if (preset_btns[i]) {
            lv_obj_set_style_opa(preset_btns[i], presets.items[i].is_assigned ? LV_OPA_COVER : LV_OPA_50, 0);
        }
    }
}

void ui_presets_set_on_triggered(OnPresetTriggeredCb cb) {
    s_on_preset_triggered = cb;
}
