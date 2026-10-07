#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "switch_inputs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Where a light command came from. Sources converge on the same output
 * driver so the relay and the state model stay authoritative regardless of
 * who asked. */
enum {
    CH_SRC_LOCAL = 0, /* bound switch press or service call */
    CH_SRC_SCENE,     /* scene engine */
    CH_SRC_MATTER,    /* On/Off write from a Matter client */
};

/* Light state changes (scene execution, bound switch presses, service calls). */
esp_err_t channel_router_init(const uint8_t switch_gpio[3], const int8_t relay_gpio[3]);
void channel_router_set_light(uint8_t light_index, bool on, uint8_t source);

/* Called from the esp-matter PRE_UPDATE callback (CHIP task) for On/Off writes
 * on the light endpoints. */
void channel_router_on_matter_write(uint8_t light_index, bool on);

/* Switch events (physical GPIO when enabled, or synthesized by the touch UI).
 * Routing follows the channel binding: a light-bound key toggles its light
 * like a wall switch; a manual key reports Generic Switch events plus the
 * local action hook. */
void channel_router_on_switch_event(uint8_t index, const switch_event_t *event);

/* True when the channel has a relay GPIO configured (system-status page). */
bool channel_router_relay_enabled(uint8_t index);

#ifdef __cplusplus
}
#endif
