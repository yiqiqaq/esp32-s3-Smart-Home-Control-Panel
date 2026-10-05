#include "app_nvs.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "panel_nvs";
static const char *NAMESPACE = "panel";

#define WEATHER_CACHE_MAGIC 0x57455448 /* "WETH" */
#define WEATHER_CACHE_VERSION 1

esp_err_t app_nvs_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
    return err;
}

esp_err_t app_nvs_load(const char *key, void *out, size_t size, uint32_t magic, uint16_t version) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    size_t stored = 0;
    err = nvs_get_blob(handle, key, NULL, &stored);
    if (err == ESP_OK) {
        if (stored != sizeof(app_nvs_blob_header_t) + size) {
            err = ESP_ERR_NVS_INVALID_LENGTH;
        } else {
            uint8_t *raw = (uint8_t *)malloc(stored);
            if (!raw) {
                err = ESP_ERR_NO_MEM;
            } else {
                err = nvs_get_blob(handle, key, raw, &stored);
                if (err == ESP_OK) {
                    app_nvs_blob_header_t header;
                    memcpy(&header, raw, sizeof(header));
                    if (header.magic != magic || header.version != version) {
                        err = ESP_ERR_INVALID_VERSION;
                    } else {
                        memcpy(out, raw + sizeof(header), size);
                    }
                }
                free(raw);
            }
        }
    }
    nvs_close(handle);
    return err;
}

esp_err_t app_nvs_save(const char *key, const void *data, size_t size, uint32_t magic, uint16_t version) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    const size_t total = sizeof(app_nvs_blob_header_t) + size;
    uint8_t *raw = (uint8_t *)malloc(total);
    if (!raw) {
        nvs_close(handle);
        return ESP_ERR_NO_MEM;
    }
    app_nvs_blob_header_t header;
    header.magic = magic;
    header.version = version;
    memcpy(raw, &header, sizeof(header));
    memcpy(raw + sizeof(header), data, size);
    err = nvs_set_blob(handle, key, raw, total);
    if (err == ESP_OK) err = nvs_commit(handle);
    free(raw);
    nvs_close(handle);
    return err;
}

esp_err_t app_nvs_save_weather_cache(const void *data, size_t size) {
    return app_nvs_save("wcache", data, size, WEATHER_CACHE_MAGIC, WEATHER_CACHE_VERSION);
}

esp_err_t app_nvs_load_weather_cache(void *out, size_t size) {
    return app_nvs_load("wcache", out, size, WEATHER_CACHE_MAGIC, WEATHER_CACHE_VERSION);
}
