#include "panel_services.h"
#include <cstring>
#include "esp_timer.h"
#include "nvs.h"
#include "sdkconfig.h"

namespace {
bool s_test_mode = CONFIG_PANEL_TEST_MODE_DEFAULT;
panel_services_provider_t s_provider_storage{};
const panel_services_provider_t *s_provider = nullptr;

panel_light_t kTestLights[] = {
    {"living", "客厅主灯", "客厅", true, true},
    {"dining", "餐厅吊灯", "餐厅", false, true},
    {"bedroom", "卧室主灯", "卧室", false, true},
    {"hall", "走廊灯", "走廊", true, true},
    {"custom-3", "自定义联动", "客厅", false, false},
};
const panel_scene_t kTestScenes[] = {
    {"home", "回家", "⌂", true},
    {"rest", "休息", "☾", true},
    {"away", "离家", "↗", true},
    {"movie", "观影", "▣", false},
    {"read", "阅读", "▤", false},
};

esp_err_t copy_items(const panel_light_t *source, uint8_t source_count,
                     panel_light_t *out, uint8_t capacity, uint8_t *count) {
    if (!out || !count) return ESP_ERR_INVALID_ARG;
    if (capacity < source_count) return ESP_ERR_INVALID_SIZE;
    std::memcpy(out, source, sizeof(panel_light_t) * source_count);
    *count = source_count;
    return ESP_OK;
}
esp_err_t copy_items(const panel_scene_t *source, uint8_t source_count,
                     panel_scene_t *out, uint8_t capacity, uint8_t *count) {
    if (!out || !count) return ESP_ERR_INVALID_ARG;
    if (capacity < source_count) return ESP_ERR_INVALID_SIZE;
    std::memcpy(out, source, sizeof(panel_scene_t) * source_count);
    *count = source_count;
    return ESP_OK;
}
}

extern "C" esp_err_t panel_services_init(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("panel_cfg", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_test_mode = CONFIG_PANEL_TEST_MODE_DEFAULT;
        return ESP_OK;
    }
    if (err != ESP_OK) return err;
    uint8_t value = CONFIG_PANEL_TEST_MODE_DEFAULT;
    err = nvs_get_u8(handle, "test_mode", &value);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) s_test_mode = value != 0;
    return err;
}

extern "C" esp_err_t panel_services_set_test_mode(bool enabled) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("panel_cfg", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(handle, "test_mode", enabled ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err == ESP_OK) s_test_mode = enabled;
    return err;
}

extern "C" bool panel_services_test_mode(void) { return s_test_mode; }

extern "C" esp_err_t panel_services_set_provider(const panel_services_provider_t *provider) {
    if (!provider) return ESP_ERR_INVALID_ARG;
    s_provider_storage = *provider;
    s_provider = &s_provider_storage;
    return ESP_OK;
}

extern "C" esp_err_t panel_services_weather_get(panel_weather_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    if (!s_test_mode) return s_provider && s_provider->weather_get ? s_provider->weather_get(out) : ESP_ERR_NOT_SUPPORTED;
    out->temperature_centi_c = 2350;
    out->humidity_percent = 46;
    out->condition = 0;
    out->observed_at = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    out->stale = false;
    return ESP_OK;
}

extern "C" esp_err_t panel_services_lights_get(panel_light_t *out, uint8_t capacity, uint8_t *count) {
    if (!s_test_mode) return s_provider && s_provider->lights_get ? s_provider->lights_get(out, capacity, count) : ESP_ERR_NOT_SUPPORTED;
    return copy_items(kTestLights, sizeof(kTestLights) / sizeof(kTestLights[0]), out, capacity, count);
}

extern "C" esp_err_t panel_services_light_set(const char *id, bool on) {
    if (!id) return ESP_ERR_INVALID_ARG;
    if (s_test_mode) {
        for (auto &light : kTestLights) {
            if (std::strcmp(light.id, id) == 0) {
                if (!light.is_light) return ESP_ERR_NOT_SUPPORTED;
                light.is_on = on;
                return ESP_OK;
            }
        }
        return ESP_ERR_NOT_FOUND;
    } // Test actions never energize physical outputs.
    return s_provider && s_provider->light_set ? s_provider->light_set(id, on) : ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t panel_services_scenes_get(panel_scene_t *out, uint8_t capacity, uint8_t *count) {
    if (!s_test_mode) return s_provider && s_provider->scenes_get ? s_provider->scenes_get(out, capacity, count) : ESP_ERR_NOT_SUPPORTED;
    return copy_items(kTestScenes, sizeof(kTestScenes) / sizeof(kTestScenes[0]), out, capacity, count);
}

extern "C" esp_err_t panel_services_scene_activate(const char *id) {
    if (!id) return ESP_ERR_INVALID_ARG;
    if (s_test_mode) {
        bool found = false;
        for (const auto &scene : kTestScenes) found |= std::strcmp(scene.id, id) == 0;
        if (!found) return ESP_ERR_NOT_FOUND;
        const bool home = std::strcmp(id, "home") == 0;
        const bool rest = std::strcmp(id, "rest") == 0 || std::strcmp(id, "read") == 0;
        const bool movie = std::strcmp(id, "movie") == 0;
        const bool away = std::strcmp(id, "away") == 0;
        for (auto &light : kTestLights) {
            if (!light.is_light) continue;
            light.is_on = (home && (!std::strcmp(light.id, "living") || !std::strcmp(light.id, "dining") || !std::strcmp(light.id, "hall"))) ||
                          (rest && !std::strcmp(light.id, "bedroom")) ||
                          (movie && !std::strcmp(light.id, "living"));
            if (!away && !home && !rest && !movie) light.is_on = false;
        }
        return ESP_OK;
    }
    return s_provider && s_provider->scene_activate ? s_provider->scene_activate(id) : ESP_ERR_NOT_SUPPORTED;
}
