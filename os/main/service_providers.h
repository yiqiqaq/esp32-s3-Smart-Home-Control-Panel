#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bridges the concrete services (app_state / weather_service / scene_engine /
 * channel_router) into the panel_services provider vtable, so the HTTP API
 * layer and a future UI always talk to one stable C interface.
 *
 * Test mode keeps working as designed by panel_services: it swaps lights and
 * scenes for fixtures. Weather stays real in both modes because fetching is
 * observe-only and never touches hardware. */

esp_err_t service_providers_register(void);

#ifdef __cplusplus
}
#endif
