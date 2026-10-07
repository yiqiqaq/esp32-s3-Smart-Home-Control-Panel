#include "ui_app.h"
#include "ui_home.h"
#include "ui_settings.h"
#include "ui_theme.h"
#include "bsp_display.h"
#include "app_state.h"
#include "matter_nodes.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_lvgl_port.h"
#include <string.h>
#include <stdbool.h>

static const char *TAG = "ui_app";

static lv_obj_t *s_home_scr, *s_settings_scr;

/* Snapshot staging: the app_state listener runs in the state_notify task and
 * must not touch LVGL; it copies into s_stage and the LVGL timer applies it. */
static app_snapshot_t s_stage;
static SemaphoreHandle_t s_stage_lock;
static volatile bool s_stage_dirty;

/* Inputs of the palette resolution; when any of them changes the styles are
 * refreshed before the widget sync. */
typedef struct {
    uint8_t theme, weather_icon;
    bool is_day, weather_auto, time_auto;
} theme_key_t;

static theme_key_t s_applied_theme = {0xFF, 0xFF, false, false, false};

static bool theme_key_equal(const theme_key_t *a, const theme_key_t *b) {
    return a->theme == b->theme && a->weather_icon == b->weather_icon &&
           a->is_day == b->is_day && a->weather_auto == b->weather_auto &&
           a->time_auto == b->time_auto;
}

static void state_listener(const app_snapshot_t *snapshot, void *ctx) {
    (void)ctx;
    if (!s_stage_lock) return;
    if (xSemaphoreTake(s_stage_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_stage = *snapshot;
        s_stage_dirty = true;
        xSemaphoreGive(s_stage_lock);
    }
}

static void sync_timer_cb(lv_timer_t *timer) {
    (void)timer;
    app_snapshot_t snap;
    bool fresh = false;
    if (xSemaphoreTake(s_stage_lock, 0) == pdTRUE) {
        fresh = s_stage_dirty;
        if (fresh) {
            snap = s_stage;
            s_stage_dirty = false;
        }
        xSemaphoreGive(s_stage_lock);
    }
    if (!fresh) return;

    theme_key_t key = {
        .theme = snap.config.theme,
        .weather_icon = snap.weather.icon,
        .is_day = snap.time.is_day,
        .weather_auto = snap.config.weather_auto_bg,
        .time_auto = snap.config.time_auto_theme,
    };
    if (!theme_key_equal(&key, &s_applied_theme)) {
        ui_theme_apply(&snap);
        s_applied_theme = key;
    }

    bool matter_paired = matter_nodes_fabric_paired();
    ui_home_apply(&snap, matter_paired);
    ui_settings_apply(&snap, matter_paired);
}

void ui_app_open_settings(void) {
    lv_screen_load(s_settings_scr);
}

void ui_app_open_home(void) {
    lv_screen_load(s_home_scr);
}

esp_err_t ui_app_init(void) {
    ESP_RETURN_ON_ERROR(bsp_display_init(), TAG, "display init");

    s_stage_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_stage_lock != NULL, ESP_ERR_NO_MEM, TAG, "stage lock");

    app_state_get_snapshot(&s_stage);
    s_stage_dirty = true;

    lvgl_port_lock(0);
    ui_theme_init();
    ui_theme_apply(&s_stage);

    /* Dark default theme with our accent; widget styles do the real work. */
    lv_display_t *disp = lv_display_get_default();
    if (disp) {
        lv_theme_default_init(disp, lv_color_hex(0x5D9DFF), lv_color_hex(0x979BA5), true,
                              ui_theme_font_body());
    }

    s_home_scr = lv_obj_create(NULL);
    ui_home_build(s_home_scr);
    s_settings_scr = lv_obj_create(NULL);
    ui_settings_build(s_settings_scr);
    lv_screen_load(s_home_scr);

    lv_timer_create(sync_timer_cb, 250, NULL);
    lvgl_port_unlock();

    ESP_RETURN_ON_ERROR(app_state_subscribe(state_listener, NULL), TAG, "subscribe");
    ESP_RETURN_ON_ERROR(bsp_display_backlight_on(), TAG, "backlight");
    ESP_LOGI(TAG, "touch UI ready (800x480, LVGL %d.%d)", lv_version_major(), lv_version_minor());
    return ESP_OK;
}
