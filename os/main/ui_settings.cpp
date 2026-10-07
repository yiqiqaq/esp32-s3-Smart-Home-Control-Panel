#include "ui_settings.h"
#include "ui_app.h"
#include "ui_theme.h"
#include "ui_fonts.h"
#include "theme_service.h"
#include "scene_engine.h"
#include "panel_services.h"
#include "app_state.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ui_settings";

static lv_obj_t *s_theme_chip[THEME_COUNT];
static lv_obj_t *s_name_lbl, *s_city_lbl, *s_matter_lbl, *s_test_sw, *s_weather_sw, *s_time_sw;
static lv_obj_t *s_bind_cap[PANEL_CHANNEL_COUNT], *s_bind_btn[PANEL_CHANNEL_COUNT],
    *s_bind_lbl[PANEL_CHANNEL_COUNT];

/* Binding picker (second-level window): room list -> device list */
static lv_obj_t *s_pick_overlay, *s_pick_body, *s_pick_ch_lbl;
static uint8_t s_pick_channel;

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
    case 5: {
        /* field 5: manual action, value = channel << 4 | code;
         * code 0 = plain switch events, 1 + scene = scene key */
        uint8_t idx = (uint8_t)((packed >> 4) & 0xF);
        uint8_t code = (uint8_t)(packed & 0xF);
        if (idx >= PANEL_CHANNEL_COUNT) break;
        config->channel_binding[idx] = CH_BIND_MANUAL;
        config->channel_dev_kind[idx] = DEV_LIGHT;
        config->channel_dev[idx] = -1;
        if (code == 0) {
            config->channel_action[idx] = PANEL_ACTION_EVENTS;
        } else if ((code - 1) < SC_COUNT) {
            config->channel_action[idx] = (uint8_t)(code - 1);
        }
        break;
    }
    case 4: {
        /* field 4: picker result, value = channel << 4 | code;
         * code 0 = manual, 1..2 = light index + 1, 3..4 = AC index + 1 */
        uint8_t idx = (uint8_t)((packed >> 4) & 0xF);
        uint8_t code = (uint8_t)(packed & 0xF);
        if (idx >= PANEL_CHANNEL_COUNT) break;
        if (code == 0) {
            config->channel_binding[idx] = CH_BIND_MANUAL;
            config->channel_dev[idx] = -1;
        } else if (code >= 1 && code <= PANEL_LIGHT_COUNT) {
            config->channel_binding[idx] = CH_BIND_LIGHT;
            config->channel_dev_kind[idx] = DEV_LIGHT;
            config->channel_dev[idx] = (int8_t)(code - 1);
        } else if (code >= 3 && code < 3 + PANEL_AC_COUNT) {
            config->channel_binding[idx] = CH_BIND_LIGHT;
            config->channel_dev_kind[idx] = DEV_AC;
            config->channel_dev[idx] = (int8_t)(code - 3);
        }
        break;
    }
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

/* ---- binding picker: level 1 = rooms, level 2 = devices of a room ---- */

static void picker_show_rooms(void);
static void picker_show_devices(uint8_t room);
static void picker_show_manual(void);
static void picker_back_cb(lv_event_t *e);

static lv_obj_t *make_label(lv_obj_t *parent, lv_style_t *color_style, const lv_font_t *font) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_add_style(lbl, color_style, 0);
    lv_obj_set_style_text_font(lbl, font, 0);
    return lbl;
}

static lv_obj_t *picker_row(lv_obj_t *parent) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_add_style(row, ui_theme_style_card2(), 0);
    lv_obj_set_size(row, lv_pct(100), 48);
    lv_obj_set_style_radius(row, 14, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_style(row, ui_theme_style_press(), LV_STATE_PRESSED);
    return row;
}

static void picker_bind_light_cb(lv_event_t *e) {
    uintptr_t packed = (uintptr_t)lv_event_get_user_data(e); /* light+1 in low nibble */
    esp_err_t err = app_state_update_config(config_mutator,
                                            (void *)((4u << 8) | (s_pick_channel << 4) | packed));
    if (err != ESP_OK) ESP_LOGW(TAG, "binding update: %s", esp_err_to_name(err));
    lv_obj_add_flag(s_pick_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void picker_manual_cb(lv_event_t *e) {
    (void)e;
    picker_show_manual();
}

/* level 2 for manual binding: pick what the key does when pressed */
static void picker_action_cb(lv_event_t *e) {
    uintptr_t code = (uintptr_t)lv_event_get_user_data(e); /* 0 = events, 1+scene */
    esp_err_t err = app_state_update_config(config_mutator,
                                            (void *)((5u << 8) | (s_pick_channel << 4) | code));
    if (err != ESP_OK) ESP_LOGW(TAG, "binding update: %s", esp_err_to_name(err));
    lv_obj_add_flag(s_pick_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void picker_show_manual() {
    lv_obj_clean(s_pick_body);

    lv_obj_t *head = lv_obj_create(s_pick_body);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, lv_pct(100), 30);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, 10, 0);

    lv_obj_t *back = lv_button_create(head);
    lv_obj_remove_style_all(back);
    lv_obj_set_size(back, LV_SIZE_CONTENT, 28);
    lv_obj_set_style_pad_hor(back, 10, 0);
    lv_obj_set_style_radius(back, 10, 0);
    lv_obj_add_style(back, ui_theme_style_card2(), 0);
    lv_obj_add_style(back, ui_theme_style_press(), LV_STATE_PRESSED);
    lv_obj_add_event_cb(back, picker_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back);
    lv_label_set_text_fmt(back_lbl, LV_SYMBOL_LEFT " 房间");
    lv_obj_center(back_lbl);

    lv_obj_t *room_lbl = make_label(head, ui_theme_style_ink(), ui_theme_font_body());
    lv_label_set_text(room_lbl, "手动控制");

    lv_obj_t *plain = picker_row(s_pick_body);
    lv_obj_add_event_cb(plain, picker_action_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)0);
    lv_obj_t *p_lbl = make_label(plain, ui_theme_style_ink(), ui_theme_font_body());
    lv_label_set_text(p_lbl, "按键事件");
    lv_obj_align(p_lbl, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_t *p_sub = make_label(plain, ui_theme_style_muted(), ui_theme_font_small());
    lv_label_set_text(p_sub, "上报开关事件");
    lv_obj_align(p_sub, LV_ALIGN_RIGHT_MID, -4, 0);

    uint8_t count = 0;
    const scene_def_t *scenes = scene_engine_all(&count);
    for (uint8_t i = 0; i < count; ++i) {
        lv_obj_t *row = picker_row(s_pick_body);
        lv_obj_add_event_cb(row, picker_action_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)(i + 1));
        char nm[32];
        snprintf(nm, sizeof(nm), "%s %s", scenes[i].icon, scenes[i].name);
        lv_obj_t *n_lbl = make_label(row, ui_theme_style_ink(), ui_theme_font_body());
        lv_label_set_text(n_lbl, nm);
        lv_obj_align(n_lbl, LV_ALIGN_LEFT_MID, 4, 0);
        lv_obj_t *d_lbl = make_label(row, ui_theme_style_muted(), ui_theme_font_small());
        lv_label_set_text(d_lbl, scenes[i].desc);
        lv_obj_align(d_lbl, LV_ALIGN_RIGHT_MID, -4, 0);
    }
}

static void picker_back_cb(lv_event_t *e) {
    (void)e;
    picker_show_rooms();
}

static void picker_room_cb(lv_event_t *e) {
    picker_show_devices((uint8_t)(uintptr_t)lv_event_get_user_data(e));
}

static void picker_show_devices(uint8_t room) {
    lv_obj_clean(s_pick_body);

    lv_obj_t *head = lv_obj_create(s_pick_body);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, lv_pct(100), 30);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, 10, 0);

    lv_obj_t *back = lv_button_create(head);
    lv_obj_remove_style_all(back);
    lv_obj_set_size(back, LV_SIZE_CONTENT, 28);
    lv_obj_set_style_pad_hor(back, 10, 0);
    lv_obj_set_style_radius(back, 10, 0);
    lv_obj_add_style(back, ui_theme_style_card2(), 0);
    lv_obj_add_style(back, ui_theme_style_press(), LV_STATE_PRESSED);
    lv_obj_add_event_cb(back, picker_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back);
    lv_label_set_text_fmt(back_lbl, LV_SYMBOL_LEFT " 房间");
    lv_obj_center(back_lbl);

    lv_obj_t *room_lbl = make_label(head, ui_theme_style_ink(), ui_theme_font_body());
    lv_label_set_text(room_lbl, kPanelRooms[room]);

    uint8_t found = 0;
    app_snapshot_t snap;
    app_state_get_snapshot(&snap);
    for (uint8_t i = 0; i < PANEL_LIGHT_COUNT; ++i) {
        if (snap.lights[i].room != room) continue;
        ++found;
        lv_obj_t *row = picker_row(s_pick_body);
        lv_obj_add_event_cb(row, picker_bind_light_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)(i + 1));
        lv_obj_t *nm = make_label(row, ui_theme_style_ink(), ui_theme_font_body());
        lv_label_set_text(nm, snap.lights[i].name);
        lv_obj_align(nm, LV_ALIGN_LEFT_MID, 4, 0);
        lv_obj_t *st = make_label(row, ui_theme_style_muted(), ui_theme_font_small());
        lv_label_set_text(st, snap.lights[i].on ? "灯 · 已开启" : "灯 · 已关闭");
        lv_obj_align(st, LV_ALIGN_RIGHT_MID, -4, 0);
    }
    for (uint8_t i = 0; i < PANEL_AC_COUNT; ++i) {
        if (snap.acs[i].room != room) continue;
        ++found;
        lv_obj_t *row = picker_row(s_pick_body);
        lv_obj_add_event_cb(row, picker_bind_light_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)(3 + i));
        lv_obj_t *nm = make_label(row, ui_theme_style_ink(), ui_theme_font_body());
        lv_label_set_text(nm, snap.acs[i].name);
        lv_obj_align(nm, LV_ALIGN_LEFT_MID, 4, 0);
        lv_obj_t *st = make_label(row, ui_theme_style_muted(), ui_theme_font_small());
        char acst[32];
        snprintf(acst, sizeof(acst), "空调 · %d°", snap.acs[i].temp_now);
        lv_label_set_text(st, acst);
        lv_obj_align(st, LV_ALIGN_RIGHT_MID, -4, 0);
    }
    if (!found) {
        lv_obj_t *empty = make_label(s_pick_body, ui_theme_style_muted(), ui_theme_font_small());
        lv_label_set_text(empty, "暂无设备");
        lv_obj_set_style_pad_left(empty, 6, 0);
    }
}

static void picker_show_rooms() {
    lv_obj_clean(s_pick_body);

    lv_obj_t *hint = make_label(s_pick_body, ui_theme_style_muted(), ui_theme_font_small());
    lv_label_set_text(hint, "选择房间");
    lv_obj_set_style_pad_left(hint, 6, 0);

    lv_obj_t *manual = picker_row(s_pick_body);
    lv_obj_add_event_cb(manual, picker_manual_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *m_lbl = make_label(manual, ui_theme_style_ink(), ui_theme_font_body());
    lv_label_set_text(m_lbl, "手动控制");
    lv_obj_align(m_lbl, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_t *m_sub = make_label(manual, ui_theme_style_muted(), ui_theme_font_small());
    lv_label_set_text(m_sub, "自定义手动控制");
    lv_obj_align(m_sub, LV_ALIGN_RIGHT_MID, -4, 0);

    app_snapshot_t snap;
    app_state_get_snapshot(&snap);
    for (uint8_t r = 0; r < PANEL_ROOM_COUNT; ++r) {
        uint8_t count = 0;
        for (uint8_t i = 0; i < PANEL_LIGHT_COUNT; ++i) {
            if (snap.lights[i].room == r) ++count;
        }
        for (uint8_t i = 0; i < PANEL_AC_COUNT; ++i) {
            if (snap.acs[i].room == r) ++count;
        }
        lv_obj_t *row = picker_row(s_pick_body);
        lv_obj_add_event_cb(row, picker_room_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)r);
        lv_obj_t *nm = make_label(row, ui_theme_style_ink(), ui_theme_font_body());
        lv_label_set_text(nm, kPanelRooms[r]);
        lv_obj_align(nm, LV_ALIGN_LEFT_MID, 4, 0);
        char cnt[16];
        snprintf(cnt, sizeof(cnt), count ? "%u 台设备" : "暂无设备", count);
        lv_obj_t *c_lbl = make_label(row, ui_theme_style_muted(), ui_theme_font_small());
        lv_label_set_text(c_lbl, cnt);
        lv_obj_align(c_lbl, LV_ALIGN_RIGHT_MID, -4, 0);
    }
}

static void picker_close_cb(lv_event_t *e) {
    /* The overlay is clickable, but clicks inside the panel must not dismiss
     * it even if event bubbling is enabled by the display theme. */
    if (lv_event_get_target(e) == s_pick_overlay) {
        lv_obj_add_flag(s_pick_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

static void build_binding_picker(lv_obj_t *parent) {
    s_pick_overlay = lv_obj_create(parent);
    lv_obj_remove_style_all(s_pick_overlay);
    lv_obj_set_size(s_pick_overlay, 800, 480);
    /* Settings content scrolls in a column. Keep the picker pinned to the
     * screen instead of letting it become the last item in that column. */
    lv_obj_add_flag(s_pick_overlay, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(s_pick_overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_pick_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_pick_overlay, LV_OPA_60, 0);
    lv_obj_add_flag(s_pick_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_pick_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_pick_overlay, picker_close_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *panel = lv_obj_create(s_pick_overlay);
    lv_obj_add_style(panel, ui_theme_style_card(), 0);
    lv_obj_set_style_radius(panel, 24, 0);
    lv_obj_set_size(panel, 470, 430);
    lv_obj_center(panel);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel, 8, 0);

    lv_obj_t *head = lv_obj_create(panel);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, lv_pct(100), 30);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, 10, 0);
    lv_obj_t *title = make_label(head, ui_theme_style_ink(), ui_theme_font_body());
    lv_label_set_text(title, "通道绑定");
    s_pick_ch_lbl = make_label(head, ui_theme_style_muted(), ui_theme_font_small());
    lv_label_set_text(s_pick_ch_lbl, "");

    s_pick_body = lv_obj_create(panel);
    lv_obj_remove_style_all(s_pick_body);
    lv_obj_set_size(s_pick_body, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(s_pick_body, 1);
    lv_obj_set_flex_flow(s_pick_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_pick_body, 8, 0);
}

static void binding_open_cb(lv_event_t *e) {
    s_pick_channel = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    app_snapshot_t snap;
    app_state_get_snapshot(&snap);
    lv_label_set_text(s_pick_ch_lbl, snap.channels[s_pick_channel].name);
    picker_show_rooms();
    lv_obj_remove_flag(s_pick_overlay, LV_OBJ_FLAG_HIDDEN);
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

    /* channel bindings: tap the value chip to cycle manual -> light1 -> light2 */
    section_caption(parent, "通道绑定");
    for (uint8_t i = 0; i < PANEL_CHANNEL_COUNT; ++i) {
        lv_obj_t *row = setting_row(parent);
        s_bind_cap[i] = row_caption(row, "");
        s_bind_btn[i] = lv_button_create(row);
        lv_obj_remove_style_all(s_bind_btn[i]);
        lv_obj_set_size(s_bind_btn[i], LV_SIZE_CONTENT, 36);
        lv_obj_set_style_pad_hor(s_bind_btn[i], 12, 0);
        lv_obj_set_style_radius(s_bind_btn[i], 12, 0);
        lv_obj_add_style(s_bind_btn[i], ui_theme_style_card2(), 0);
        lv_obj_add_style(s_bind_btn[i], ui_theme_style_press(), LV_STATE_PRESSED);
        /* The picker callback expects the channel index itself. The old
         * packed value was decoded as a large channel index and could make
         * the binding button appear to do nothing. */
        lv_obj_add_event_cb(s_bind_btn[i], binding_open_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);
        s_bind_lbl[i] = lv_label_create(s_bind_btn[i]);
        lv_obj_add_style(s_bind_lbl[i], ui_theme_style_ink(), 0);
        lv_obj_set_style_text_font(s_bind_lbl[i], ui_theme_font_small(), 0);
        lv_obj_center(s_bind_lbl[i]);
        lv_obj_align(s_bind_btn[i], LV_ALIGN_RIGHT_MID, 0, 0);
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

    build_binding_picker(parent); /* created last so it stacks above the rows */
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

    for (uint8_t i = 0; i < PANEL_CHANNEL_COUNT; ++i) {
        set_text(s_bind_cap[i], snap->channels[i].name);
        if (snap->config.channel_binding[i] == CH_BIND_LIGHT &&
            snap->config.channel_dev[i] >= 0) {
            if (snap->config.channel_dev_kind[i] == DEV_AC) {
                set_text(s_bind_lbl[i], snap->acs[snap->config.channel_dev[i]].name);
            } else {
                set_text(s_bind_lbl[i], snap->lights[snap->config.channel_dev[i]].name);
            }
        } else if (snap->config.channel_action[i] < SC_COUNT) {
            char lbl[32];
            snprintf(lbl, sizeof(lbl), "手动 · %s",
                     scene_engine_all(nullptr)[snap->config.channel_action[i]].name);
            set_text(s_bind_lbl[i], lbl);
        } else {
            set_text(s_bind_lbl[i], "手动控制");
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
