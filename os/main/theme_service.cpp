#include "theme_service.h"

/* Palette values transcribed from ui/panel-preview.html CSS variables.
 * 0xRRGGBB. Alpha bits of edge/glow kept in the low byte as in the CSS #RRGGBBAA. */
typedef struct {
    uint32_t bg, card, card2, ink, muted, accent, edge, glow;
} palette_def_t;

static const palette_def_t kBase[THEME_COUNT] = {
    /* cool  */ {0x11151c, 0x202a37, 0x273444, 0xf4f6fa, 0xa9b4c2, 0x92c2ff, 0xffffff19, 0x78aef477},
    /* warm  */ {0x11151c, 0x332b27, 0x41352e, 0xf4f6fa, 0xa9b4c2, 0xffc18b, 0xffffff19, 0xf5aa646e},
    /* mint  */ {0x11151c, 0x1c302c, 0x254039, 0xf4f6fa, 0xa9b4c2, 0x9ee6c8, 0xffffff19, 0x69d6ad69},
    /* light */ {0xdce5ef, 0xf4f7fb, 0xe9eff6, 0x1b2533, 0x647387, 0x203b58, 0x1b253322, 0xffffffb8},
};

/* cool theme night variant (screen[data-time="night"]). */
static const palette_def_t kCoolNight = {
    0x0d1320, 0x192235, 0x202c41, 0xf4f6fa, 0xa9b4c2, 0xb3c9ff, 0xffffff19, 0x7476dc66,
};

/* Ambient glow tints per weather bucket (screen[data-weather=...]). */
static const uint32_t kWeatherGlow[W_ICON_COUNT] = {
    0x78aef477, /* sun    */
    0x98aabe66, /* cloud  */
    0x68849b88, /* rain   */
    0xd3e5ff77, /* snow   */
};

static const char *const kThemeNames[THEME_COUNT] = {"清透蓝", "暖阳", "薄荷", "浅色"};
static const char *const kWeatherGlyphs[W_ICON_COUNT] = {"☀", "☁", "☂", "❄"};

void theme_service_resolve(uint8_t theme, bool is_day, uint8_t weather,
                           bool weather_auto, bool time_auto,
                           theme_palette_t *out) {
    if (theme >= THEME_COUNT) theme = THEME_COOL;
    if (weather >= W_ICON_COUNT) weather = W_SUN;

    const palette_def_t *p = &kBase[theme];
    /* Day/night linkage only retones the dark cool theme; the light theme is
     * explicit and the warm/mint accents stay stable, matching the prototype. */
    if (time_auto && !is_day && theme == THEME_COOL) p = &kCoolNight;

    out->bg = p->bg;
    out->card = p->card;
    out->card2 = p->card2;
    out->ink = p->ink;
    out->muted = p->muted;
    out->accent = p->accent;
    out->edge = p->edge;
    out->glow = p->glow;
    if (weather_auto) out->glow = kWeatherGlow[weather];
}

const char *theme_service_name(uint8_t theme) {
    return theme < THEME_COUNT ? kThemeNames[theme] : kThemeNames[0];
}

const char *theme_service_glyph(uint8_t weather) {
    return weather < W_ICON_COUNT ? kWeatherGlyphs[weather] : kWeatherGlyphs[0];
}
