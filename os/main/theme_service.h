#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "app_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pure resolution algorithm behind the prototype's data-weather/data-time/
 * data-theme CSS cascade. Input: user theme + day/night + weather bucket +
 * the two auto toggles. Output: resolved palette for a future display driver.
 * No rendering happens here. */

typedef struct {
    uint32_t bg, card, card2, ink, muted, accent, edge, glow;
} theme_palette_t;

void theme_service_resolve(uint8_t theme, bool is_day, uint8_t weather,
                           bool weather_auto, bool time_auto,
                           theme_palette_t *out);
const char *theme_service_name(uint8_t theme);
const char *theme_service_glyph(uint8_t weather); /* ☀ ☁ ☂ ❄ */

#ifdef __cplusplus
}
#endif
