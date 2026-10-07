#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Minimal HTTP service for phone-side configuration (SERVICE_API.md):
 *   GET  /api/v1/voice/config  -> {"api_url": "...", "api_key_set": true}
 *   POST /api/v1/voice/config  -> body {"api_url": "...", "api_key": "..."}
 * The key is write-only (never returned). CORS headers are open so a local
 * web app can drive it. */

esp_err_t service_http_start(void);

#ifdef __cplusplus
}
#endif
