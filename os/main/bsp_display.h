#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bring up the Waveshare 4.3B display stack: shared I2C bus, CH422G reset of
 * the GT911, RGB panel (2 PSRAM frame buffers), esp_lvgl_port with the panel
 * frame buffers used as LVGL draw buffers (avoid-tearing mode) and the touch
 * input device. The backlight stays off; call bsp_display_backlight_on() once
 * the first frame has been drawn to avoid a garbage flash. */
esp_err_t bsp_display_init(void);

esp_err_t bsp_display_backlight_on(void);

#ifdef __cplusplus
}
#endif
