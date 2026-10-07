#pragma once
#include "lvgl.h"
#include "app_state.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Settings screen: theme library, weather/time auto toggles and the system
 * section (panel name, Matter status, safe test mode switch). */

void ui_settings_build(lv_obj_t *parent);
void ui_settings_apply(const app_snapshot_t *snap, bool matter_paired);

#ifdef __cplusplus
}
#endif
