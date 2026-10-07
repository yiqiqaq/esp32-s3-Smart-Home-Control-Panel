#include "app_state.h"
#include "app_nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include <string.h>
#include <sdkconfig.h>

static const char *TAG = "app_state";

#define CFG_KEY "cfg"
#define NOTIFY_QUEUE_LEN 4
#define MAX_LISTENERS 4

static SemaphoreHandle_t s_mutex;
static QueueHandle_t s_notify_queue;
static app_snapshot_t s_state;
static app_state_listener_t s_listeners[MAX_LISTENERS];
static void *s_listener_ctx[MAX_LISTENERS];
static bool s_dirty; /* a notify is pending because the snapshot changed */

const char *const kPanelRooms[PANEL_ROOM_COUNT] = {"客厅", "餐厅", "卧室", "走廊", "阳台"};

static const light_t kDefaultLights[PANEL_LIGHT_COUNT] = {
    {"客厅主灯", false, 0, -1},
    {"餐厅吊灯", false, 1, -1},
};

static const ac_t kDefaultAcs[PANEL_AC_COUNT] = {
    {"客厅空调", false, AC_MODE_COOL, AC_FAN_AUTO, 26, 26, 0},
    {"卧室空调", false, AC_MODE_COOL, AC_FAN_AUTO, 26, 26, 3},
};

static const channel_t kDefaultChannels[PANEL_CHANNEL_COUNT] = {
    {"客厅开关", false, 0, -1},
    {"餐厅开关", false, 0, -1},
    {"自定义联动", false, 0, -1},
};

static app_config_t default_config(void) {
    app_config_t cfg = {};
    strlcpy(cfg.panel_name, "栖居", sizeof(cfg.panel_name));
    cfg.theme = THEME_COOL;
    cfg.city_index = 0;
    cfg.weather_auto_bg = true;
    cfg.time_auto_theme = true;
    for (int i = 0; i < PINNED_SCENE_MAX; ++i) cfg.pinned[i] = 0xFF;
    cfg.pinned[0] = SC_HOME;
    cfg.pinned[1] = SC_REST;
    cfg.pinned[2] = SC_AWAY;
    /* Out of the box the first two keys act as their light's wall switch. */
    for (int i = 0; i < PANEL_CHANNEL_COUNT; ++i) {
        cfg.channel_binding[i] = CH_BIND_MANUAL;
        cfg.channel_dev_kind[i] = DEV_LIGHT;
        cfg.channel_dev[i] = -1;
        cfg.channel_action[i] = PANEL_ACTION_EVENTS;
    }
    cfg.channel_binding[0] = CH_BIND_LIGHT;
    cfg.channel_dev_kind[0] = DEV_LIGHT;
    cfg.channel_dev[0] = 0;
    cfg.channel_binding[1] = CH_BIND_LIGHT;
    cfg.channel_dev_kind[1] = DEV_LIGHT;
    cfg.channel_dev[1] = 1;
    return cfg;
}

static void recount_lights(void) {
    s_state.lights_on = 0;
    s_state.lights_total = PANEL_LIGHT_COUNT;
    for (int i = 0; i < PANEL_LIGHT_COUNT; ++i) {
        if (s_state.lights[i].on) ++s_state.lights_on;
    }
}

static void request_notify_locked(void) { s_dirty = true; }

static void notify_task(void *) {
    for (;;) {
        uint32_t tick;
        if (xQueueReceive(s_notify_queue, &tick, portMAX_DELAY) != pdTRUE) continue;
        for (;;) {
            app_snapshot_t snap;
            if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) break;
            if (s_dirty) {
                s_dirty = false;
                snap = s_state;
                xSemaphoreGive(s_mutex);
                for (int i = 0; i < MAX_LISTENERS; ++i) {
                    if (s_listeners[i]) s_listeners[i](&snap, s_listener_ctx[i]);
                }
                continue;
            }
            xSemaphoreGive(s_mutex);
            break;
        }
    }
}

static void kick_notify(void) {
    uint32_t token = 1;
    xQueueSend(s_notify_queue, &token, 0); /* drop if full; the drain loop re-checks s_dirty */
}

esp_err_t app_state_init(void) {
    s_mutex = xSemaphoreCreateMutex();
    s_notify_queue = xQueueCreate(NOTIFY_QUEUE_LEN, sizeof(uint32_t));
    if (!s_mutex || !s_notify_queue) return ESP_ERR_NO_MEM;

    memset(&s_state, 0, sizeof(s_state));
    memcpy(s_state.lights, kDefaultLights, sizeof(kDefaultLights));
    memcpy(s_state.acs, kDefaultAcs, sizeof(kDefaultAcs));
    memcpy(s_state.channels, kDefaultChannels, sizeof(kDefaultChannels));
    s_state.active_scene = 0xFF;

    app_config_t cfg = default_config();
    if (app_nvs_load(CFG_KEY, &cfg, sizeof(cfg), APP_NVS_CFG_MAGIC, APP_NVS_CFG_VERSION) != ESP_OK) {
        app_nvs_save(CFG_KEY, &cfg, sizeof(cfg), APP_NVS_CFG_MAGIC, APP_NVS_CFG_VERSION);
    }
    s_state.config = cfg;
    /* runtime room temperature starts at the persisted setpoint */
    for (int i = 0; i < PANEL_AC_COUNT; ++i) {
        s_state.acs[i].on = cfg.ac_on[i];
        s_state.acs[i].mode = cfg.ac_mode[i];
        s_state.acs[i].fan = cfg.ac_fan[i];
        s_state.acs[i].temp_set = cfg.ac_temp[i];
        s_state.acs[i].temp_now = cfg.ac_temp[i];
    }

    /* GPIO numbers are provided by the caller modules after Kconfig is resolved. */
    s_dirty = false;
    if (xTaskCreatePinnedToCore(notify_task, "state_notify", 3072, nullptr, 4, nullptr, 1) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "state model ready, panel '%s'", s_state.config.panel_name);
    return ESP_OK;
}

void app_state_bind_gpio(const uint8_t switch_gpio[PANEL_CHANNEL_COUNT],
                         const int8_t relay_gpio[PANEL_CHANNEL_COUNT]) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < PANEL_CHANNEL_COUNT; ++i) {
        s_state.channels[i].switch_gpio = switch_gpio[i];
        s_state.channels[i].relay_gpio = relay_gpio[i];
    }
    xSemaphoreGive(s_mutex);
}

esp_err_t app_state_set_light(uint8_t light_index, bool on) {
    if (light_index >= PANEL_LIGHT_COUNT) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.lights[light_index].on = on;
    recount_lights();
    request_notify_locked();
    xSemaphoreGive(s_mutex);
    kick_notify();
    return ESP_OK;
}

esp_err_t app_state_set_ac(uint8_t ac_index, bool on, uint8_t mode, uint8_t fan, int8_t temp_set) {
    if (ac_index >= PANEL_AC_COUNT) return ESP_ERR_INVALID_ARG;
    if (temp_set < 16) temp_set = 16;
    if (temp_set > 30) temp_set = 30;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.acs[ac_index].on = on;
    s_state.acs[ac_index].mode = mode;
    s_state.acs[ac_index].fan = fan;
    s_state.acs[ac_index].temp_set = temp_set;
    request_notify_locked();
    app_config_t cfg = s_state.config;
    xSemaphoreGive(s_mutex);
    cfg.ac_on[ac_index] = on;
    cfg.ac_mode[ac_index] = mode;
    cfg.ac_fan[ac_index] = fan;
    cfg.ac_temp[ac_index] = temp_set;
    return app_nvs_save(CFG_KEY, &cfg, sizeof(cfg), APP_NVS_CFG_MAGIC, APP_NVS_CFG_VERSION);
}

esp_err_t app_state_set_ac_room_temp(uint8_t ac_index, int8_t temp_now) {
    if (ac_index >= PANEL_AC_COUNT) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.acs[ac_index].temp_now = temp_now;
    request_notify_locked();
    xSemaphoreGive(s_mutex);
    kick_notify();
    return ESP_OK;
}

esp_err_t app_state_set_switch(uint8_t index, bool triggered) {
    if (index >= PANEL_CHANNEL_COUNT) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.channels[index].triggered = triggered;
    request_notify_locked();
    xSemaphoreGive(s_mutex);
    kick_notify();
    return ESP_OK;
}

esp_err_t app_state_set_config(const app_config_t *config) {
    if (!config) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.config = *config;
    request_notify_locked();
    xSemaphoreGive(s_mutex);
    kick_notify();
    return app_nvs_save(CFG_KEY, config, sizeof(*config), APP_NVS_CFG_MAGIC, APP_NVS_CFG_VERSION);
}

esp_err_t app_state_update_config(app_config_mutator_t mutator, void *ctx) {
    if (!mutator) return ESP_ERR_INVALID_ARG;
    app_config_t cfg;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    cfg = s_state.config;
    mutator(&cfg, ctx);
    s_state.config = cfg;
    request_notify_locked();
    xSemaphoreGive(s_mutex);
    kick_notify();
    return app_nvs_save(CFG_KEY, &cfg, sizeof(cfg), APP_NVS_CFG_MAGIC, APP_NVS_CFG_VERSION);
}

esp_err_t app_state_set_weather(const weather_t *weather) {
    if (!weather) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.weather = *weather;
    request_notify_locked();
    xSemaphoreGive(s_mutex);
    kick_notify();
    return ESP_OK;
}

esp_err_t app_state_set_time_info(const time_info_t *info) {
    if (!info) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool changed = memcmp(&s_state.time, info, sizeof(*info)) != 0;
    if (changed) {
        s_state.time = *info;
        request_notify_locked();
    }
    xSemaphoreGive(s_mutex);
    if (changed) kick_notify();
    return ESP_OK;
}

esp_err_t app_state_set_active_scene(uint8_t scene_id) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.active_scene = scene_id;
    request_notify_locked();
    xSemaphoreGive(s_mutex);
    kick_notify();
    return ESP_OK;
}

void app_state_get_snapshot(app_snapshot_t *out) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_state;
    xSemaphoreGive(s_mutex);
}

esp_err_t app_state_subscribe(app_state_listener_t listener, void *ctx) {
    for (int i = 0; i < MAX_LISTENERS; ++i) {
        if (!s_listeners[i]) {
            s_listeners[i] = listener;
            s_listener_ctx[i] = ctx;
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}
