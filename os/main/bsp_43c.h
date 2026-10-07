#pragma once
#include "hal/gpio_types.h"

/* Waveshare ESP32-S3-Touch-LCD-4.3C fixed board definition.
 * Every value below is taken from the vendor ESP-IDF demo
 * (waveshareteam/ESP32-S3-Touch-LCD-4.3C, examples/esp-idf 03_lcd /
 * 06_touch / 11_speaker_microphone); do not "fix" values without checking
 * the board schematic.
 *
 * Compared with the earlier 4.3B: same RGB pin map, different sync porches
 * and a different CH422G register protocol (single I2C address 0x24 with
 * two-byte register writes). This header is 4.3C only.
 *
 * The RGB data bus is 16-bit: DATA0..15 map to B3..B7 / G2..G7 / R3..R7,
 * so a plain RGB565 framebuffer feeds the panel directly. */

#define BSP_LCD_H_RES 800
#define BSP_LCD_V_RES 480
#define BSP_LCD_DATA_WIDTH 16
#define BSP_LCD_BITS_PER_PIXEL 16
#define BSP_LCD_PIXEL_CLOCK_HZ (16 * 1000 * 1000)

/* RGB timing (vendor demo, 800x480 panel) */
#define BSP_LCD_TIMING_HSYNC_PULSE 8
#define BSP_LCD_TIMING_HSYNC_BACK 8
#define BSP_LCD_TIMING_HSYNC_FRONT 4
#define BSP_LCD_TIMING_VSYNC_PULSE 8
#define BSP_LCD_TIMING_VSYNC_BACK 8
#define BSP_LCD_TIMING_VSYNC_FRONT 4

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

/* Shared I2C bus (touch + CH422G + RTC + audio codecs) */
#define BSP_I2C_SDA GPIO_NUM_8
#define BSP_I2C_SCL GPIO_NUM_9
#define BSP_I2C_SPEED_HZ 400000

/* CH422G IO expander: one slave address, two-byte register writes
 * [reg, value]. Vendor demo drives it through io_extension.c. */
#define BSP_CH422G_ADDR 0x24
#define BSP_CH422G_REG_MODE 0x02 /* value 0xFF = all pins push-pull output */
#define BSP_CH422G_REG_OUT 0x03  /* value = output level bitmap */
/* Output-register bits (vendor io_extension.h pin assignments) */
#define BSP_CH422G_BIT_TP_RST (1U << 1)      /* GT911 reset, active low */
#define BSP_CH422G_BIT_BACKLIGHT (1U << 2)   /* LCD backlight */
#define BSP_CH422G_BIT_PA (1U << 3)          /* speaker amplifier enable */
#define BSP_CH422G_BIT_SD_CS (1U << 4)       /* TF card CS, active low */

/* GT911 touch */
#define BSP_TOUCH_RST GPIO_NUM_NC /* reset is done through CH422G TP_RST */
#define BSP_TOUCH_INT GPIO_NUM_4  /* held low during reset -> I2C addr 0x5D */

/* Onboard audio: ES8311 speaker codec + ES7210 4-channel mic ADC (dual MEMS
 * mic array). The audio I2S reuses the CAN/RS485 pads; CH422G IO5 picks the
 * terminal function, so audio and those terminals are mutually exclusive. */
#define BSP_I2S_PORT 1
#define BSP_I2S_MCLK GPIO_NUM_6
#define BSP_I2S_BCLK GPIO_NUM_44
#define BSP_I2S_WS GPIO_NUM_16
#define BSP_I2S_DOUT GPIO_NUM_15
#define BSP_I2S_DIN GPIO_NUM_43
