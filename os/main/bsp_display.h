#pragma once
#include "esp_err.h"
#include "driver/i2c_master.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bring up the Waveshare 4.3C display stack: shared I2C bus, CH422G reset of
 * the GT911, RGB panel (2 PSRAM frame buffers), esp_lvgl_port with the panel
 * frame buffers used as LVGL draw buffers (avoid-tearing mode) and the touch
 * input device. The backlight stays off; call bsp_display_backlight_on() once
 * the first frame has been drawn to avoid a garbage flash. */
esp_err_t bsp_display_init(void);

esp_err_t bsp_display_backlight_on(void);

/* Shared I2C bus (touch, CH422G, RTC, audio codecs) for other BSP modules. */
i2c_master_bus_handle_t bsp_display_i2c_bus(void);

/* Set/clear one bit in the CH422G output register (see bsp_43c.h bit map). */
esp_err_t bsp_board_ch422g_set_bit(uint8_t bit_mask, bool on);

#ifdef __cplusplus
}
#endif
