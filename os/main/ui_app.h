#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Touch UI application: display bring-up, screen construction, app_state
 * subscription and the periodic snapshot -> widget sync. Per the project
 * layering the UI only reads snapshots/palettes and calls service/routing
 * interfaces; it never touches GPIO or Matter internals directly. */

esp_err_t ui_app_init(void);

void ui_app_open_settings(void);
void ui_app_open_home(void);

#ifdef __cplusplus
}
#endif
