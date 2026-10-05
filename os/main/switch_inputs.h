#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SW_PRESSED = 0,     /* momentary mode: stable falling edge (active low input) */
    SW_RELEASED,        /* momentary mode: stable rising edge */
    SW_LONG_PRESS,      /* held >= CONFIG_PANEL_LONGPRESS_MS, fired once per press */
    SW_LEVEL,           /* toggle mode: stable level change, level in arg */
};

typedef struct {
    uint8_t event;   /* SW_* */
    bool level;      /* current stable level: true = closed/active */
    uint32_t hold_ms;/* hold time at the moment of the event */
} switch_event_t;

typedef void (*switch_callback_t)(uint8_t index, const switch_event_t *event);

/* Samples every 5 ms; a level change becomes stable after
 * CONFIG_PANEL_DEBOUNCE_MS of continuous readings (integrative debounce).
 * momentary: emits SW_PRESSED/SW_RELEASED/SW_LONG_PRESS
 * toggle:    emits SW_LEVEL on every stable change (bistable rocker sync) */
esp_err_t switch_inputs_init(switch_callback_t callback, const uint8_t gpios[3]);

#ifdef __cplusplus
}
#endif
