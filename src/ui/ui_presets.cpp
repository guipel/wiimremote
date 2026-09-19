#include "ui/ui_presets.h"
#include "ui/ui_theme.h"
#include "config.h"

// External Queue for sending UI Commands to Network Task
extern QueueHandle_t xQueueUiCmd;

static lv_obj_t* preset_btns[MAX_PRESETS] = { nullptr };
static lv_obj_t* preset_lbls[MAX_PRESETS] = { nullptr };
static OnPresetTriggeredCb s_on_preset_triggered = nullptr;

static void event_btn_preset(lv_event_t* e) {
    uintptr_t index = (uintptr_t)lv_event_get_user_data(e);
    UiCommand cmd;
    cmd.type = CMD_TRIGGER_PRESET;
    cmd.data.preset_index = (uint8_t)index;
    xQueueSend(xQueueUiCmd, &cmd, 0);

    if (s_on_preset_triggered) {
        s_on_preset_triggered((uint8_t)index);
    }
}

void ui_presets_init(lv_obj_t* parent) {
    lv_obj_set_style_bg_color(parent, COLOR_BG, 0);
    lv_obj_set_style_pad_all(parent, 6, 0);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 6, 0);

    for (uint8_t i = 0; i < MAX_PRESETS; ++i) {
        preset_btns[i] = lv_btn_create(parent);
        lv_obj_set_size(preset_btns[i], 224, 44);
        lv_obj_set_style_bg_color(preset_btns[i], COLOR_SURFACE, 0);
        lv_obj_set_style_border_color(preset_btns[i], COLOR_SURFACE_LIGHT, 0);
        lv_obj_set_style_border_width(preset_btns[i], 1, 0);
        lv_obj_set_style_radius(preset_btns[i], 8, 0);
        lv_obj_set_style_pad_all(preset_btns[i], 4, 0);
        lv_obj_set_style_bg_color(preset_btns[i], COLOR_SURFACE_LIGHT, LV_STATE_PRESSED);
        lv_obj_clear_flag(preset_btns[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(preset_btns[i], event_btn_preset, LV_EVENT_CLICKED, (void*)(uintptr_t)(i + 1));

        // Preset Index Badge (Circle Pill)
        lv_obj_t* badge = lv_obj_create(preset_btns[i]);
        lv_obj_set_size(badge, 26, 26);
        lv_obj_align(badge, LV_ALIGN_LEFT_MID, 2, 0);
        lv_obj_set_style_bg_color(badge, COLOR_SURFACE_LIGHT, 0);
        lv_obj_set_style_radius(badge, 13, 0);
        lv_obj_set_style_border_side(badge, LV_BORDER_SIDE_NONE, 0);
        lv_obj_set_style_pad_all(badge, 0, 0);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* lbl_idx = lv_label_create(badge);
        char idx_str[8];
        snprintf(idx_str, sizeof(idx_str), "%d", i + 1);
        lv_label_set_text(lbl_idx, idx_str);
        lv_obj_set_style_text_font(lbl_idx, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl_idx, COLOR_ACCENT, 0);
        lv_obj_center(lbl_idx);

        // Preset Title (Full readable name)
        preset_lbls[i] = lv_label_create(preset_btns[i]);
        char default_name[32];
        snprintf(default_name, sizeof(default_name), "Preset %d", i + 1);
        lv_label_set_text(preset_lbls[i], default_name);
        lv_label_set_long_mode(preset_lbls[i], LV_LABEL_LONG_DOT);
        lv_obj_set_width(preset_lbls[i], 148);
        lv_obj_set_style_text_font(preset_lbls[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(preset_lbls[i], COLOR_TEXT_PRIMARY, 0);
        lv_obj_align(preset_lbls[i], LV_ALIGN_LEFT_MID, 36, 0);

        // Right Play Arrow Indicator
        lv_obj_t* icon_play = lv_label_create(preset_btns[i]);
        lv_label_set_text(icon_play, LV_SYMBOL_PLAY);
        lv_obj_set_style_text_font(icon_play, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(icon_play, COLOR_TEXT_MUTED, 0);
        lv_obj_align(icon_play, LV_ALIGN_RIGHT_MID, -6, 0);
    }
}

void ui_presets_set_list(const PresetList& presets) {
    for (uint8_t i = 0; i < presets.count && i < MAX_PRESETS; ++i) {
        if (preset_lbls[i]) {
            lv_label_set_text(preset_lbls[i], presets.items[i].name);
        }
    }
}

void ui_presets_set_on_triggered(OnPresetTriggeredCb cb) {
    s_on_preset_triggered = cb;
}
