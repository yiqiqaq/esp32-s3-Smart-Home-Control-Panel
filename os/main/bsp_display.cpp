#include "bsp_display.h"
#include "bsp_43b.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lvgl_port.h"
#include "esp_lvgl_port_disp.h"
#include "esp_lvgl_port_touch.h"

static const char *TAG = "bsp_display";

static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_ch422g_mode; /* SET register @0x24 */
static i2c_master_dev_handle_t s_ch422g_out;  /* OUT register @0x38 */
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_touch_handle_t s_touch;

static esp_err_t i2c_bus_init(void) {
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = 0,
        .sda_io_num = (gpio_num_t)BSP_I2C_SDA,
        .scl_io_num = (gpio_num_t)BSP_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = { .enable_internal_pullup = true },
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c_bus), TAG, "i2c bus");

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BSP_CH422G_ADDR_MODE,
        .scl_speed_hz = BSP_I2C_SPEED_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_ch422g_mode), TAG, "ch422g mode");
    dev_cfg.device_address = BSP_CH422G_ADDR_OUT;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_ch422g_out), TAG, "ch422g out");
    return ESP_OK;
}

static esp_err_t ch422g_set_output(uint8_t value) {
    return i2c_master_transmit(s_ch422g_out, &value, 1, 50);
}

static esp_err_t ch422g_output_mode(void) {
    uint8_t mode = BSP_CH422G_MODE_OUTPUT;
    return i2c_master_transmit(s_ch422g_mode, &mode, 1, 50);
}

/* Vendor reset sequence: TP_RST low via CH422G EXIO1, ESP32 holds the GT911 INT
 * line low across the reset release, which latches the 0x5D I2C address. */
static esp_err_t touch_reset(void) {
    ESP_RETURN_ON_ERROR(ch422g_output_mode(), TAG, "ch422g mode");
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << BSP_TOUCH_INT,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);

    ESP_RETURN_ON_ERROR(ch422g_set_output(BSP_CH422G_OUT_TP_RESET_LOW), TAG, "tp rst low");
    esp_rom_delay_us(100 * 1000);
    gpio_set_level((gpio_num_t)BSP_TOUCH_INT, 0);
    esp_rom_delay_us(100 * 1000);
    ESP_RETURN_ON_ERROR(ch422g_set_output(BSP_CH422G_OUT_TP_RESET_HIGH), TAG, "tp rst high");
    esp_rom_delay_us(200 * 1000);

    /* Release the INT line so the GT911 can drive it again; touch is polled. */
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);
    return ESP_OK;
}

esp_err_t bsp_display_init(void) {
    ESP_RETURN_ON_ERROR(i2c_bus_init(), TAG, "i2c init");
    ESP_RETURN_ON_ERROR(touch_reset(), TAG, "touch reset");

    ESP_LOGI(TAG, "install RGB panel (800x480 @16MHz, 2 PSRAM frame buffers)");
    esp_lcd_rgb_panel_config_t panel_cfg = {
        .clk_src = LCD_CLK_SRC_PLL160M, /* 160 / 16 = 10, exact pclk divider */
        .timings = {
            .pclk_hz = BSP_LCD_PIXEL_CLOCK_HZ,
            .h_res = BSP_LCD_H_RES,
            .v_res = BSP_LCD_V_RES,
            .hsync_pulse_width = BSP_LCD_TIMING_HSYNC_PULSE,
            .hsync_back_porch = BSP_LCD_TIMING_HSYNC_BACK,
            .hsync_front_porch = BSP_LCD_TIMING_HSYNC_FRONT,
            .vsync_pulse_width = BSP_LCD_TIMING_VSYNC_PULSE,
            .vsync_back_porch = BSP_LCD_TIMING_VSYNC_BACK,
            .vsync_front_porch = BSP_LCD_TIMING_VSYNC_FRONT,
            .flags = { .pclk_active_neg = 1 },
        },
        .data_width = BSP_LCD_DATA_WIDTH,
        .bits_per_pixel = BSP_LCD_BITS_PER_PIXEL,
        .num_fbs = 2,
        .dma_burst_size = 64,
        .hsync_gpio_num = BSP_LCD_GPIO_HSYNC,
        .vsync_gpio_num = BSP_LCD_GPIO_VSYNC,
        .de_gpio_num = BSP_LCD_GPIO_DE,
        .pclk_gpio_num = BSP_LCD_GPIO_PCLK,
        .disp_gpio_num = BSP_LCD_GPIO_DISP,
        .data_gpio_nums = {
            BSP_LCD_GPIO_DATA0, BSP_LCD_GPIO_DATA1, BSP_LCD_GPIO_DATA2, BSP_LCD_GPIO_DATA3,
            BSP_LCD_GPIO_DATA4, BSP_LCD_GPIO_DATA5, BSP_LCD_GPIO_DATA6, BSP_LCD_GPIO_DATA7,
            BSP_LCD_GPIO_DATA8, BSP_LCD_GPIO_DATA9, BSP_LCD_GPIO_DATA10, BSP_LCD_GPIO_DATA11,
            BSP_LCD_GPIO_DATA12, BSP_LCD_GPIO_DATA13, BSP_LCD_GPIO_DATA14, BSP_LCD_GPIO_DATA15,
        },
        .flags = { .fb_in_psram = 1 },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&panel_cfg, &s_panel), TAG, "rgb panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "rgb init");

    /* GT911: reset already done through CH422G; poll via esp_lvgl_port.
     * Field order follows esp_lcd_panel_io_i2c_config_t (IDF >= 5.4); the
     * controller macro in esp_lcd_touch_gt911 targets an older layout. */
    esp_lcd_panel_io_handle_t tp_io = NULL;
    const esp_lcd_panel_io_i2c_config_t tp_io_cfg = {
        .dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,
        .control_phase_bytes = 1,
        .dc_bit_offset = 0,
        .lcd_cmd_bits = 16,
        .flags = { .disable_control_phase = 1 },
        .scl_speed_hz = BSP_I2C_SPEED_HZ,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c_bus, &tp_io_cfg, &tp_io), TAG, "tp panel io");
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = BSP_LCD_H_RES,
        .y_max = BSP_LCD_V_RES,
        .rst_gpio_num = (gpio_num_t)BSP_TOUCH_RST,
        .int_gpio_num = GPIO_NUM_NC,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &s_touch), TAG, "gt911");

    /* LVGL renders straight into the two frame buffers, synchronised on vsync. */
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,
        .task_stack = 8192,
        .task_affinity = 1, /* business core, next to the other app tasks */
        .task_max_sleep_ms = 500,
        .timer_period_ms = 5,
    };
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "lvgl port");

    const lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = s_panel,
        .buffer_size = BSP_LCD_H_RES * BSP_LCD_V_RES,
        .hres = BSP_LCD_H_RES,
        .vres = BSP_LCD_V_RES,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = { .swap_bytes = 0, .direct_mode = 1 },
    };
    const lvgl_port_display_rgb_cfg_t rgb_cfg = { .flags = { .avoid_tearing = 1 } };
    lv_display_t *disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    ESP_RETURN_ON_FALSE(disp != NULL, ESP_FAIL, TAG, "lvgl disp");

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = disp,
        .handle = s_touch,
    };
    lv_indev_t *indev = lvgl_port_add_touch(&touch_cfg);
    ESP_RETURN_ON_FALSE(indev != NULL, ESP_FAIL, TAG, "lvgl touch");

    ESP_LOGI(TAG, "display stack ready");
    return ESP_OK;
}

esp_err_t bsp_display_backlight_on(void) {
    ESP_RETURN_ON_ERROR(ch422g_output_mode(), TAG, "ch422g mode");
    return ch422g_set_output(BSP_CH422G_OUT_BACKLIGHT_ON);
}
