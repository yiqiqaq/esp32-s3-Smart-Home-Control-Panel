#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Owns the esp-matter data model:
 *   light endpoints  0..1 : On/Off Light  (the two bindable local lights)
 *   switch endpoints 0..2 : Generic Switch, one per channel (momentary +
 *                           action switch features)
 * Routes remote On/Off writes on lights into the channel router and exposes
 * switch-event reporting for channels in manual binding. */

esp_err_t matter_nodes_init(void);
uint16_t matter_nodes_light_endpoint_id(uint8_t light_index);  /* 0..1 */
uint16_t matter_nodes_switch_endpoint_id(uint8_t switch_index); /* 0..2 */

/* Commissioning status for the UI top bar. matter_nodes_stack_started() is
 * called once esp_matter::start() finished; the getter returns false before
 * that because the fabric table is not initialised yet. */
void matter_nodes_stack_started(void);
bool matter_nodes_fabric_paired(void);

/* Local-side light state pushed into the fabric (attribute::update). */
esp_err_t matter_nodes_set_light_onoff(uint8_t light_index, bool on);

/* Switch cluster events for channels in manual binding (index = channel). */
esp_err_t matter_nodes_report_initial_press(uint8_t switch_index);
esp_err_t matter_nodes_report_short_release(uint8_t switch_index);
esp_err_t matter_nodes_report_latched(uint8_t switch_index, uint8_t new_position);

#ifdef __cplusplus
}
#endif
