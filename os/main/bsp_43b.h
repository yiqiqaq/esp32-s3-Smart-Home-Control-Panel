#pragma once
#include "hal/gpio_types.h"

/* Waveshare ESP32-S3-Touch-LCD-4.3B (SKU 27848) fixed board definition.
 * Every value below is taken from the vendor ESP-IDF demo
 * (waveshareteam/ESP32-S3-Touch-LCD-4.3B, examples/ESP-IDF) and the wiki
 * pin table; do not "fix" pins without checking the board schematic.
 *
 * The RGB data bus is 16-bit: DATA0..15 map to B3..B7 / G2..G7 / R3..R7,
 * so a plain RGB565 framebuffer feeds the panel directly. */

#define BSP_LCD_H_RES 800
#define BSP_LCD_V_RES 480
#define BSP_LCD_DATA_WIDTH 16
#define BSP_LCD_BITS_PER_PIXEL 16
#define BSP_LCD_PIXEL_CLOCK_HZ (16 * 1000 * 1000)

/* RGB timing (vendor demo, 800x480 panel) */
#define BSP_LCD_TIMING_HSYNC_PULSE 4
#define BSP_LCD_TIMING_HSYNC_BACK 8
#define BSP_LCD_TIMING_HSYNC_FRONT 8
#define BSP_LCD_TIMING_VSYNC_PULSE 4
#define BSP_LCD_TIMING_VSYNC_BACK 8
#define BSP_LCD_TIMING_VSYNC_FRONT 8

/* Control pins */
#define BSP_LCD_GPIO_VSYNC GPIO_NUM_3
#define BSP_LCD_GPIO_HSYNC GPIO_NUM_46
#define BSP_LCD_GPIO_DE GPIO_NUM_5
#define BSP_LCD_GPIO_PCLK GPIO_NUM_7
#define BSP_LCD_GPIO_DISP GPIO_NUM_NC

/* Data pins DATA0..DATA15 (bit0..bit15 of the RGB565 bus) */
#define BSP_LCD_GPIO_DATA0 GPIO_NUM_14
#define BSP_LCD_GPIO_DATA1 GPIO_NUM_38
#define BSP_LCD_GPIO_DATA2 GPIO_NUM_18
#define BSP_LCD_GPIO_DATA3 GPIO_NUM_17
#define BSP_LCD_GPIO_DATA4 GPIO_NUM_10
#define BSP_LCD_GPIO_DATA5 GPIO_NUM_39
#define BSP_LCD_GPIO_DATA6 GPIO_NUM_0
#define BSP_LCD_GPIO_DATA7 GPIO_NUM_45
#define BSP_LCD_GPIO_DATA8 GPIO_NUM_48
#define BSP_LCD_GPIO_DATA9 GPIO_NUM_47
#define BSP_LCD_GPIO_DATA10 GPIO_NUM_21
#define BSP_LCD_GPIO_DATA11 GPIO_NUM_1
#define BSP_LCD_GPIO_DATA12 GPIO_NUM_2
#define BSP_LCD_GPIO_DATA13 GPIO_NUM_42
#define BSP_LCD_GPIO_DATA14 GPIO_NUM_41
#define BSP_LCD_GPIO_DATA15 GPIO_NUM_40

/* Shared I2C bus (touch + CH422G expander + RTC) */
#define BSP_I2C_SDA GPIO_NUM_8
#define BSP_I2C_SCL GPIO_NUM_9
#define BSP_I2C_SPEED_HZ 400000

/* CH422G IO expander: 0x24 = SET (mode) register, 0x38 = OUT register */
#define BSP_CH422G_ADDR_MODE 0x24
#define BSP_CH422G_ADDR_OUT 0x38
#define BSP_CH422G_MODE_OUTPUT 0x01
/* EXIO1 = TP_RST, EXIO2 = backlight (DISP), EXIO4 = SD_CS (active low) */
#define BSP_CH422G_OUT_BACKLIGHT_ON 0x1E
#define BSP_CH422G_OUT_TP_RESET_LOW 0x2C
#define BSP_CH422G_OUT_TP_RESET_HIGH 0x2E

/* GT911 touch */
#define BSP_TOUCH_RST GPIO_NUM_NC /* reset is done through CH422G EXIO1 */
#define BSP_TOUCH_INT GPIO_NUM_4  /* held low during reset -> I2C addr 0x5D */
