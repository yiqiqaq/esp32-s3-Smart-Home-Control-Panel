#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Typed NVS access: versioned blobs for user config and the weather cache.
 * Keys live under the "panel" namespace so factory reset via nvs erase clears them all. */

typedef struct {
    uint32_t magic;
    uint16_t version;
} app_nvs_blob_header_t;

#define APP_NVS_CFG_MAGIC 0x504E4C31 /* "PNL1" */
/* v2: channel bindings. v3: manual action (scene key). v4: device-kind
 * bindings (light/AC) + persisted AC state. Older blobs fall back to defaults. */
#define APP_NVS_CFG_VERSION 4

esp_err_t app_nvs_init(void);
esp_err_t app_nvs_load(const char *key, void *out, size_t size, uint32_t magic, uint16_t version);
esp_err_t app_nvs_save(const char *key, const void *data, size_t size, uint32_t magic, uint16_t version);
esp_err_t app_nvs_save_weather_cache(const void *data, size_t size);
esp_err_t app_nvs_load_weather_cache(void *out, size_t size);

#ifdef __cplusplus
}
#endif
