#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "app_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Weather data path (mirrors UI_DESIGN.md):
 * HTTP(S) weather source -> this adapter (fetch, WMO code mapping, hourly
 * sampling, cache, offline fallback) -> weather_t -> app_state -> UI.
 * The adapter never talks to Matter; weather is not a Matter concept. */

esp_err_t weather_service_init(void);
esp_err_t weather_service_refresh(void); /* synchronous fetch; also runs in the periodic task */

#ifdef __cplusplus
}
#endif
