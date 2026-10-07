#include "ui_theme.h"
#include "ui_fonts.h"
#include "theme_service.h"

/* Shared styles: every widget references these, so re-theming is a property
 * update on the style objects and LVGL repaints automatically. */
static lv_style_t s_style_bg;
static lv_style_t s_style_card;
static lv_style_t s_style_card2;
static lv_style_t s_style_ink;
static lv_style_t s_style_muted;
static lv_style_t s_style_accent;
static lv_style_t s_style_on;    /* checked state on toggle cards */
static lv_style_t s_style_press; /* pressed feedback */

static const lv_font_t *const s_font_small = &panel_14;
static const lv_font_t *const s_font_body = &panel_18;
static const lv_font_t *const s_font_large = &panel_30;

static lv_color_t hex(uint32_t v) {
    return lv_color_hex(v & 0xFFFFFFU);
}

esp_err_t ui_theme_init(void) {
    lv_style_init(&s_style_bg);
    lv_style_set_bg_color(&s_style_bg, hex(0x101114));
    lv_style_set_pad_all(&s_style_bg, 0);

    lv_style_init(&s_style_card);
    lv_style_set_bg_color(&s_style_card, hex(0x1B1D22));
    lv_style_set_radius(&s_style_card, 22);
    lv_style_set_border_width(&s_style_card, 0);
    lv_style_set_pad_all(&s_style_card, 14);

    lv_style_init(&s_style_card2);
    lv_style_set_bg_color(&s_style_card2, hex(0x24272E));
    lv_style_set_radius(&s_style_card2, 16);
    lv_style_set_border_width(&s_style_card2, 0);
    lv_style_set_pad_all(&s_style_card2, 10);

    lv_style_init(&s_style_ink);
    lv_style_set_text_color(&s_style_ink, hex(0xF3F4F6));

    lv_style_init(&s_style_muted);
    lv_style_set_text_color(&s_style_muted, hex(0x979BA5));

    lv_style_init(&s_style_accent);
    lv_style_set_text_color(&s_style_accent, hex(0x5D9DFF));

    lv_style_init(&s_style_on);
    lv_style_set_border_color(&s_style_on, hex(0x5D9DFF));
    lv_style_set_border_width(&s_style_on, 2);

    lv_style_init(&s_style_press);
    lv_style_set_bg_color(&s_style_press, hex(0x2E3440));
    return ESP_OK;
}

void ui_theme_apply(const app_snapshot_t *snap) {
    theme_palette_t p;
    theme_service_resolve(snap->config.theme, snap->time.is_day, snap->weather.icon,
                          snap->config.weather_auto_bg, snap->config.time_auto_theme, &p);
    lv_style_set_bg_color(&s_style_bg, hex(p.bg));
    lv_style_set_bg_color(&s_style_card, hex(p.card));
    lv_style_set_bg_color(&s_style_card2, hex(p.card2));
    lv_style_set_text_color(&s_style_ink, hex(p.ink));
    lv_style_set_text_color(&s_style_muted, hex(p.muted));
    lv_style_set_text_color(&s_style_accent, hex(p.accent));
    lv_style_set_border_color(&s_style_on, hex(p.accent));
    lv_style_set_bg_color(&s_style_press, hex(p.glow));
}

const lv_font_t *ui_theme_font_small(void) { return s_font_small; }
const lv_font_t *ui_theme_font_body(void) { return s_font_body; }
const lv_font_t *ui_theme_font_large(void) { return s_font_large; }

lv_style_t *ui_theme_style_bg(void) { return &s_style_bg; }
lv_style_t *ui_theme_style_card(void) { return &s_style_card; }
lv_style_t *ui_theme_style_card2(void) { return &s_style_card2; }
lv_style_t *ui_theme_style_ink(void) { return &s_style_ink; }
lv_style_t *ui_theme_style_muted(void) { return &s_style_muted; }
lv_style_t *ui_theme_style_accent(void) { return &s_style_accent; }
lv_style_t *ui_theme_style_on(void) { return &s_style_on; }
lv_style_t *ui_theme_style_press(void) { return &s_style_press; }
