#include "service_providers.h"
#include "panel_services.h"
#include "app_state.h"
#include "scene_engine.h"
#include "channel_router.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

static const char *const kSceneIds[SC_COUNT] = {"home", "rest", "away", "movie", "read"};

static uint8_t scene_index(const char *id) {
    for (uint8_t i = 0; i < SC_COUNT; ++i) {
        if (strcmp(kSceneIds[i], id) == 0) return i;
    }
    return 0xFF;
}

/* weather_get: real values from the weather adapter (test mode only affects
 * hardware-acting services inside panel_services). */
static esp_err_t provider_weather_get(panel_weather_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    app_snapshot_t snap;
    app_state_get_snapshot(&snap);
    out->temperature_centi_c = (int16_t)(snap.weather.temp_c * 100);
    out->humidity_percent = snap.weather.humidity;
    out->condition = snap.weather.icon; /* W_SUN..W_SNOW map 1:1 to 0..3 */
    out->observed_at = (uint32_t)time(nullptr);
    out->stale = snap.weather.offline || !snap.weather.valid;
    return ESP_OK;
}

static esp_err_t provider_lights_get(panel_light_t *out, uint8_t capacity, uint8_t *count) {
    if (!out || !count) return ESP_ERR_INVALID_ARG;
    if (capacity < PANEL_LIGHT_COUNT) return ESP_ERR_INVALID_SIZE;
    app_snapshot_t snap;
    app_state_get_snapshot(&snap);
    for (uint8_t i = 0; i < PANEL_LIGHT_COUNT; ++i) {
        panel_light_t *l = &out[i];
        memset(l, 0, sizeof(*l));
        snprintf(l->id, sizeof(l->id), "light%u", i + 1);
        strlcpy(l->name, snap.lights[i].name, sizeof(l->name));
        strlcpy(l->room, "客厅", sizeof(l->room));
        l->is_on = snap.lights[i].on;
        l->is_light = true;
    }
    *count = PANEL_LIGHT_COUNT;
    return ESP_OK;
}

static esp_err_t provider_light_set(const char *id, bool on) {
    if (!id || strlen(id) < 5 || strncmp(id, "light", 5) != 0) return ESP_ERR_NOT_FOUND;
    const int index = id[5] - '1';
    if (index < 0 || index >= PANEL_LIGHT_COUNT) return ESP_ERR_NOT_FOUND;
    channel_router_set_light((uint8_t)index, on, CH_SRC_LOCAL);
    return ESP_OK;
}

static esp_err_t provider_scenes_get(panel_scene_t *out, uint8_t capacity, uint8_t *count) {
    if (!out || !count) return ESP_ERR_INVALID_ARG;
    if (capacity < SC_COUNT) return ESP_ERR_INVALID_SIZE;
    app_snapshot_t snap;
    app_state_get_snapshot(&snap);
    uint8_t total;
    const scene_def_t *scenes = scene_engine_all(&total);
    for (uint8_t i = 0; i < total; ++i) {
        panel_scene_t *s = &out[i];
        memset(s, 0, sizeof(*s));
        strlcpy(s->id, kSceneIds[scenes[i].id], sizeof(s->id));
        strlcpy(s->name, scenes[i].name, sizeof(s->name));
        strlcpy(s->icon, scenes[i].icon, sizeof(s->icon));
        s->pinned = false; /* scene pinning was removed; the bar shows all six */
    }
    *count = total;
    return ESP_OK;
}

static esp_err_t provider_scene_activate(const char *id) {
    const uint8_t index = scene_index(id);
    if (index == 0xFF) return ESP_ERR_NOT_FOUND;
    return scene_engine_run(index);
}

esp_err_t service_providers_register(void) {
    panel_services_provider_t provider = {};
    provider.weather_get = provider_weather_get;
    provider.lights_get = provider_lights_get;
    provider.light_set = provider_light_set;
    provider.scenes_get = provider_scenes_get;
    provider.scene_activate = provider_scene_activate;
    return panel_services_set_provider(&provider);
}
