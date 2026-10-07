#pragma once
#include "lvgl.h"
#include "app_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Resolves theme_service's palette for the current snapshot and applies it to
 * the shared LVGL styles. Widgets only reference these styles (plus the fonts
 * below), so a palette change repaints the whole UI without rebuilding. */

esp_err_t ui_theme_init(void);
void ui_theme_apply(const app_snapshot_t *snap);

const lv_font_t *ui_theme_font_small(void); /* 14px, latin + CJK subset */
const lv_font_t *ui_theme_font_body(void);  /* 18px, latin + CJK subset */
const lv_font_t *ui_theme_font_large(void); /* 30px, latin only */

lv_style_t *ui_theme_style_bg(void);
lv_style_t *ui_theme_style_card(void);
lv_style_t *ui_theme_style_card2(void);
lv_style_t *ui_theme_style_ink(void);
lv_style_t *ui_theme_style_muted(void);
lv_style_t *ui_theme_style_accent(void);
lv_style_t *ui_theme_style_on(void);   /* checked-state border */
lv_style_t *ui_theme_style_press(void); /* pressed-state surface */

#ifdef __cplusplus
}
#endif
