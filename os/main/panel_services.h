#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PANEL_MAX_LIGHTS 12
#define PANEL_MAX_SCENES 12
#define PANEL_NAME_MAX 32

typedef struct {
    char id[PANEL_NAME_MAX];
    char name[PANEL_NAME_MAX];
    char room[PANEL_NAME_MAX];
    bool is_on;
    bool is_light; // Generic Switch is deliberately excluded from home-light totals.
} panel_light_t;

typedef struct {
    char id[PANEL_NAME_MAX];
    char name[PANEL_NAME_MAX];
    char icon[8];
    bool pinned;
} panel_scene_t;

typedef struct {
    int16_t temperature_centi_c;
    uint8_t humidity_percent;
    uint8_t condition; // 0 clear, 1 cloudy, 2 rain, 3 snow, 255 unknown
    uint32_t observed_at;
    bool stale;
} panel_weather_t;

// Production adapters provide weather, full-home Matter controller state, and scenes.
typedef struct {
    esp_err_t (*weather_get)(panel_weather_t *out);
    esp_err_t (*lights_get)(panel_light_t *out, uint8_t capacity, uint8_t *count);
    esp_err_t (*light_set)(const char *id, bool on);
    esp_err_t (*scenes_get)(panel_scene_t *out, uint8_t capacity, uint8_t *count);
    esp_err_t (*scene_activate)(const char *id);
} panel_services_provider_t;

esp_err_t panel_services_init(void);
esp_err_t panel_services_set_test_mode(bool enabled);
bool panel_services_test_mode(void);
esp_err_t panel_services_set_provider(const panel_services_provider_t *provider);
esp_err_t panel_services_weather_get(panel_weather_t *out);
esp_err_t panel_services_lights_get(panel_light_t *out, uint8_t capacity, uint8_t *count);
esp_err_t panel_services_light_set(const char *id, bool on);
esp_err_t panel_services_scenes_get(panel_scene_t *out, uint8_t capacity, uint8_t *count);
esp_err_t panel_services_scene_activate(const char *id);

#ifdef __cplusplus
}
#endif
