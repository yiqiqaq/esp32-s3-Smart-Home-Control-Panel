#include "ui_settings.h"
#include "ui_app.h"
#include "ui_theme.h"
#include "ui_fonts.h"
#include "theme_service.h"
#include "panel_services.h"
#include "app_state.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ui_settings";

static lv_obj_t *s_theme_chip[THEME_COUNT];
static lv_obj_t *s_name_lbl, *s_city_lbl, *s_matter_lbl, *s_test_sw, *s_weather_sw, *s_time_sw;

static void set_text(lv_obj_t *lbl, const char *txt) {
    if (strcmp(lv_label_get_text(lbl), txt) != 0) {
        lv_label_set_text(lbl, txt);
    }
}

/* ---------------- interactions ---------------- */

static void config_mutator(app_config_t *config, void *ctx) {
    /* ctx = (field << 8) | value */
    uintptr_t packed = (uintptr_t)ctx;
    switch (packed >> 8) {
    case 0: config->theme = (uint8_t)(packed & 0xFF); break;
    case 1: config->weather_auto_bg = packed & 1; break;
    case 2: config->time_auto_theme = packed & 1; break;
    default: break;
    }
}

static void theme_chip_cb(lv_event_t *e) {
    uintptr_t theme = (uintptr_t)lv_event_get_user_data(e);
    esp_err_t err = app_state_update_config(config_mutator, (void *)theme);
    if (err != ESP_OK) ESP_LOGW(TAG, "theme update: %s", esp_err_to_name(err));
}

static void config_toggle_cb(lv_event_t *e) {
    uintptr_t packed = (uintptr_t)lv_event_get_user_data(e); /* field << 8 */
    bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    esp_err_t err = app_state_update_config(config_mutator, (void *)(packed | (on ? 1u : 0u)));
    if (err != ESP_OK) ESP_LOGW(TAG, "config update: %s", esp_err_to_name(err));
}

static void test_mode_cb(lv_event_t *e) {
    bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    esp_err_t err = panel_services_set_test_mode(on);
    if (err != ESP_OK) ESP_LOGW(TAG, "test mode: %s", esp_err_to_name(err));
}

static void back_cb(lv_event_t *e) {
    (void)e;
    ui_app_open_home();
}

/* ---------------- build ---------------- */

static lv_obj_t *section_caption(lv_obj_t *parent, const char *text) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_add_style(lbl, ui_theme_style_muted(), 0);
    lv_obj_set_style_text_font(lbl, ui_theme_font_small(), 0);
    lv_label_set_text(lbl, text);
    return lbl;
}

static lv_obj_t *setting_row(lv_obj_t *parent) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_add_style(row, ui_theme_style_card(), 0);
    lv_obj_set_size(row, lv_pct(100), 58);
    lv_obj_set_style_radius(row, 16, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

static lv_obj_t *row_caption(lv_obj_t *row, const char *text) {
    lv_obj_t *lbl = lv_label_create(row);
    lv_obj_add_style(lbl, ui_theme_style_muted(), 0);
    lv_obj_set_style_text_font(lbl, ui_theme_font_small(), 0);
    lv_label_set_text(lbl, text);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
    return lbl;
}

static lv_obj_t *row_value_label(lv_obj_t *row) {
    lv_obj_t *lbl = lv_label_create(row);
    lv_obj_add_style(lbl, ui_theme_style_ink(), 0);
    lv_obj_set_style_text_font(lbl, ui_theme_font_body(), 0);
    lv_obj_align(lbl, LV_ALIGN_RIGHT_MID, 0, 0);
    return lbl;
}

void ui_settings_build(lv_obj_t *parent) {
    lv_obj_add_style(parent, ui_theme_style_bg(), 0);
    lv_obj_set_style_pad_all(parent, 16, 0);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 8, 0);
    /* taller than the screen by design: settings scrolls vertically */

    /* header */
    lv_obj_t *head = lv_obj_create(parent);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, lv_pct(100), 44);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, 12, 0);

    lv_obj_t *back = lv_button_create(head);
    lv_obj_remove_style_all(back);
    lv_obj_set_size(back, LV_SIZE_CONTENT, 40);
    lv_obj_set_style_pad_hor(back, 14, 0);
    lv_obj_set_style_radius(back, 14, 0);
    lv_obj_add_style(back, ui_theme_style_card2(), 0);
    lv_obj_add_style(back, ui_theme_style_press(), LV_STATE_PRESSED);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back);
    lv_label_set_text_fmt(back_lbl, LV_SYMBOL_LEFT " 返回");
    lv_obj_center(back_lbl);

    lv_obj_t *title = lv_label_create(head);
    lv_obj_add_style(title, ui_theme_style_ink(), 0);
    lv_obj_set_style_text_font(title, ui_theme_font_body(), 0);
    lv_label_set_text(title, "设置");

    /* theme library */
    section_caption(parent, "主题库");
    lv_obj_t *theme_row = lv_obj_create(parent);
    lv_obj_remove_style_all(theme_row);
    lv_obj_set_size(theme_row, lv_pct(100), 56);
    lv_obj_set_flex_flow(theme_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(theme_row, 10, 0);

    for (uint8_t i = 0; i < THEME_COUNT; ++i) {
        lv_obj_t *chip = lv_obj_create(theme_row);
        lv_obj_add_style(chip, ui_theme_style_card2(), 0);
        lv_obj_set_size(chip, LV_SIZE_CONTENT, 52);
        lv_obj_set_style_radius(chip, 16, 0);
        lv_obj_set_style_pad_hor(chip, 16, 0);
        lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_style(chip, ui_theme_style_press(), LV_STATE_PRESSED);
        lv_obj_add_style(chip, ui_theme_style_on(), LV_STATE_CHECKED);
        lv_obj_add_event_cb(chip, theme_chip_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        s_theme_chip[i] = chip;

        lv_obj_t *lbl = lv_label_create(chip);
        lv_obj_add_style(lbl, ui_theme_style_ink(), 0);
        lv_obj_set_style_text_font(lbl, ui_theme_font_body(), 0);
        lv_label_set_text(lbl, theme_service_name(i));
        lv_obj_center(lbl);
    }

    /* panel */
    section_caption(parent, "面板");

    lv_obj_t *name_row = setting_row(parent);
    row_caption(name_row, "面板名称");
    s_name_lbl = row_value_label(name_row);

    lv_obj_t *city_row = setting_row(parent);
    row_caption(city_row, "天气城市");
    s_city_lbl = row_value_label(city_row);

    lv_obj_t *w_row = setting_row(parent);
    row_caption(w_row, "天气联动背景");
    s_weather_sw = lv_switch_create(w_row);
    lv_obj_set_size(s_weather_sw, 52, 28);
    lv_obj_add_event_cb(s_weather_sw, config_toggle_cb, LV_EVENT_VALUE_CHANGED,
                        (void *)(uintptr_t)(1u << 8));
    lv_obj_align(s_weather_sw, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t *t_row = setting_row(parent);
    row_caption(t_row, "昼夜自动切换");
    s_time_sw = lv_switch_create(t_row);
    lv_obj_set_size(s_time_sw, 52, 28);
    lv_obj_add_event_cb(s_time_sw, config_toggle_cb, LV_EVENT_VALUE_CHANGED,
                        (void *)(uintptr_t)(2u << 8));
    lv_obj_align(s_time_sw, LV_ALIGN_RIGHT_MID, 0, 0);

    /* system */
    section_caption(parent, "系统");

    lv_obj_t *m_row = setting_row(parent);
    row_caption(m_row, "Matter");
    s_matter_lbl = row_value_label(m_row);

    lv_obj_t *sys_row = setting_row(parent);
    row_caption(sys_row, "测试模式（输出保持断开）");
    s_test_sw = lv_switch_create(sys_row);
    lv_obj_set_size(s_test_sw, 52, 28);
    lv_obj_add_event_cb(s_test_sw, test_mode_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_align(s_test_sw, LV_ALIGN_RIGHT_MID, 0, 0);
}

/* ---------------- apply ---------------- */

void ui_settings_apply(const app_snapshot_t *snap, bool matter_paired) {
    for (uint8_t i = 0; i < THEME_COUNT; ++i) {
        if (snap->config.theme == i) {
            lv_obj_add_state(s_theme_chip[i], LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(s_theme_chip[i], LV_STATE_CHECKED);
        }
    }

    set_text(s_name_lbl, snap->config.panel_name);
    set_text(s_city_lbl, snap->weather.city);
    set_text(s_matter_lbl, matter_paired ? "已配网" : "未配网");

    if (snap->config.weather_auto_bg) {
        lv_obj_add_state(s_weather_sw, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(s_weather_sw, LV_STATE_CHECKED);
    }
    if (snap->config.time_auto_theme) {
        lv_obj_add_state(s_time_sw, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(s_time_sw, LV_STATE_CHECKED);
    }
    if (panel_services_test_mode()) {
        lv_obj_add_state(s_test_sw, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(s_test_sw, LV_STATE_CHECKED);
    }
}
