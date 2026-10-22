#pragma once

#include <lvgl.h>
#include "model.h"

typedef void (*OnUpnpTrackPlayedCb)();

void ui_browser_init(lv_obj_t* parent);
void ui_browser_apply_theme();
void ui_browser_on_tab_activated();
void ui_browser_set_servers(const UpnpServerList& servers);
void ui_browser_set_browse_result(const UpnpBrowseList& result);
void ui_browser_set_on_track_played(OnUpnpTrackPlayedCb cb);
