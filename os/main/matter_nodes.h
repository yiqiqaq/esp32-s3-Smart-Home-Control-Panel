#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Owns the esp-matter data model: endpoint 1/2 = On/Off Light, endpoint 3 =
 * Generic Switch (momentary + action switch features). Routes remote On/Off
 * writes into the channel router and exposes switch-event reporting for the
 * custom channel. */

esp_err_t matter_nodes_init(void);
uint16_t matter_nodes_endpoint_id(uint8_t index); /* index 0..2 */

/* Local-side updates pushed into the fabric (attribute::update). */
esp_err_t matter_nodes_set_onoff(uint8_t index, bool on);

/* Switch cluster events for the custom channel endpoint. */
esp_err_t matter_nodes_report_initial_press(uint8_t index);
esp_err_t matter_nodes_report_short_release(uint8_t index);
esp_err_t matter_nodes_report_latched(uint8_t index, uint8_t new_position);

#ifdef __cplusplus
}
#endif
