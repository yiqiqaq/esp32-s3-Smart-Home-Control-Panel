#pragma once
#include "lvgl.h"
#include "app_state.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Home screen: top bar (Matter status / test chip / clock), header (panel
 * title, date, greeting), weather + lights summary + hourly forecast column,
 * three channel cards and the quick scene bar with the scene library overlay. */

void ui_home_build(lv_obj_t *parent);
void ui_home_apply(const app_snapshot_t *snap, bool matter_paired);

/* Show/hide the now-playing bar and refresh its title/volume. */
void ui_home_music_update(bool active, const char *title, int volume, bool playing);

#ifdef __cplusplus
}
#endif
