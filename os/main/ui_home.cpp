#include "ui_home.h"
#include "ui_app.h"
#include "ui_theme.h"
#include "ui_fonts.h"
#include "channel_router.h"
#include "scene_engine.h"
#include "theme_service.h"
#include "panel_services.h"
#include "app_state.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ui_home";

/* Widget references needed by the apply pass */
static lv_obj_t *s_matter_dot, *s_matter_lbl, *s_test_chip;
static lv_obj_t *s_clock_lbl, *s_title_lbl, *s_date_lbl, *s_greet_lbl;
static lv_obj_t *s_temp_lbl, *s_wicon_lbl, *s_wdesc_lbl, *s_whum_lbl, *s_wcity_lbl, *s_wupd_lbl;
static lv_obj_t *s_lights_lbl;
static lv_obj_t *s_hourly_time[HOURLY_POINTS], *s_hourly_temp[HOURLY_POINTS],
    *s_hourly_icon[HOURLY_POINTS];
static lv_obj_t *s_chan_card[PANEL_CHANNEL_COUNT], *s_chan_name[PANEL_CHANNEL_COUNT],
    *s_chan_kind[PANEL_CHANNEL_COUNT], *s_chan_state[PANEL_CHANNEL_COUNT];
static lv_obj_t *s_scene_row, *s_overlay;

static uint8_t s_last_pinned[PINNED_SCENE_MAX] = {0xFF, 0xFF, 0xFF};

/* ---------------- helpers ---------------- */

static lv_obj_t *make_label(lv_obj_t *parent, lv_style_t *color_style, const lv_font_t *font) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_add_style(lbl, color_style, 0);
    lv_obj_set_style_text_font(lbl, font, 0);
    return lbl;
}

static void set_text(lv_obj_t *lbl, const char *txt) {
    if (strcmp(lv_label_get_text(lbl), txt) != 0) {
        lv_label_set_text(lbl, txt);
    }
}

static void set_state_color(lv_obj_t *lbl, bool on) {
    lv_obj_remove_style(lbl, ui_theme_style_accent(), LV_PART_MAIN);
    lv_obj_remove_style(lbl, ui_theme_style_muted(), LV_PART_MAIN);
    lv_obj_add_style(lbl, on ? ui_theme_style_accent() : ui_theme_style_muted(), 0);
}

/* Small rounded status chip: colored dot (child 0) + caption (child 1) */
static lv_obj_t *make_chip(lv_obj_t *parent, const char *caption) {
    lv_obj_t *chip = lv_obj_create(parent);
    lv_obj_add_style(chip, ui_theme_style_card2(), 0);
    lv_obj_set_style_radius(chip, 14, 0);
    lv_obj_set_style_pad_hor(chip, 12, 0);
    lv_obj_set_style_pad_ver(chip, 7, 0);
    lv_obj_set_style_border_width(chip, 0, 0);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(chip, 6, 0);

    lv_obj_t *dot = lv_obj_create(chip);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0x979BA5), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);

    lv_obj_t *lbl = make_label(chip, ui_theme_style_muted(), ui_theme_font_small());
    lv_label_set_text(lbl, caption);
    return chip;
}

/* ---------------- interactions ---------------- */

static void overlay_hide(void) {
    if (s_overlay) {
        lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

static void overlay_backdrop_cb(lv_event_t *e) {
    (void)e;
    overlay_hide();
}

static void overlay_scene_cb(lv_event_t *e) {
    uint8_t id = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    switch (lv_event_get_code(e)) {
    case LV_EVENT_LONG_PRESSED: {
        /* Long press toggles the "pinned on home" slot; the quick row refresh
         * comes through the state notification once app_state commits. */
        bool pinned = false;
        app_snapshot_t s;
        app_state_get_snapshot(&s);
        for (int i = 0; i < PINNED_SCENE_MAX; ++i) {
            if (s.config.pinned[i] == id) pinned = true;
        }
        esp_err_t err = scene_engine_set_pinned(id, !pinned);
        if (err != ESP_OK) ESP_LOGW(TAG, "pin scene %u: %s", id, esp_err_to_name(err));
        break;
    }
    case LV_EVENT_CLICKED: {
        esp_err_t err = scene_engine_run(id);
        if (err != ESP_OK) ESP_LOGW(TAG, "run scene %u: %s", id, esp_err_to_name(err));
        overlay_hide();
        break;
    }
    default:
        break;
    }
}

static void more_btn_cb(lv_event_t *e) {
    (void)e;
    if (s_overlay) {
        lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

static void scene_chip_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        scene_engine_run((uint8_t)(uintptr_t)lv_event_get_user_data(e));
    }
}

static void channel_cb(lv_event_t *e) {
    uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
    case LV_EVENT_RELEASED: {
        /* Custom channels mirror a momentary wall switch: press/release pairs
         * become Generic Switch events through the same router the GPIO
         * inputs used to feed. */
        if (app_state_channel_kind(idx) == CH_KIND_CUSTOM) {
            switch_event_t ev = {
                .event = (lv_event_get_code(e) == LV_EVENT_PRESSED) ? SW_PRESSED : SW_RELEASED,
                .level = lv_event_get_code(e) == LV_EVENT_PRESSED,
                .hold_ms = 0,
            };
            channel_router_on_switch_event(idx, &ev);
        }
        break;
    }
    case LV_EVENT_CLICKED: {
        if (app_state_channel_kind(idx) == CH_KIND_LIGHT) {
            app_snapshot_t s;
            app_state_get_snapshot(&s);
            channel_router_set_channel(idx, !s.channels[idx].on, CH_SRC_LOCAL);
        }
        break;
    }
    default:
        break;
    }
}

/* ---------------- scene overlay ---------------- */

static void build_overlay(lv_obj_t *parent) {
    s_overlay = lv_obj_create(parent);
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_size(s_overlay, 800, 480);
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_60, 0);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_overlay, overlay_backdrop_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *panel = lv_obj_create(s_overlay);
    lv_obj_add_style(panel, ui_theme_style_card(), 0);
    lv_obj_set_style_radius(panel, 24, 0);
    lv_obj_set_size(panel, 470, 404);
    lv_obj_center(panel);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel, 10, 0);

    lv_obj_t *title = make_label(panel, ui_theme_style_ink(), ui_theme_font_body());
    lv_label_set_text(title, "快捷场景");
    lv_obj_t *hint = make_label(panel, ui_theme_style_muted(), ui_theme_font_small());
    lv_label_set_text(hint, "轻点执行 · 长按固定到首页");
    lv_obj_align(hint, LV_ALIGN_TOP_RIGHT, -4, 4);

    uint8_t count = 0;
    const scene_def_t *scenes = scene_engine_all(&count);
    for (uint8_t i = 0; i < count; ++i) {
        lv_obj_t *row = lv_obj_create(panel);
        lv_obj_add_style(row, ui_theme_style_card2(), 0);
        lv_obj_set_size(row, lv_pct(100), 58);
        lv_obj_set_style_radius(row, 16, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_style(row, ui_theme_style_press(), LV_STATE_PRESSED);
        lv_obj_add_event_cb(row, overlay_scene_cb, LV_EVENT_ALL, (void *)(uintptr_t)scenes[i].id);

        lv_obj_t *icon = make_label(row, ui_theme_style_accent(), ui_theme_font_body());
        lv_label_set_text(icon, scenes[i].icon);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 4, 0);

        lv_obj_t *name = make_label(row, ui_theme_style_ink(), ui_theme_font_body());
        lv_label_set_text(name, scenes[i].name);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 40, 0);

        lv_obj_t *desc = make_label(row, ui_theme_style_muted(), ui_theme_font_small());
        lv_label_set_text(desc, scenes[i].desc);
        lv_obj_align(desc, LV_ALIGN_RIGHT_MID, -4, 0);
    }
}

/* ---------------- build ---------------- */

static void build_topbar(lv_obj_t *parent) {
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, lv_pct(100), 36);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 10, 0);

    lv_obj_t *matter_chip = make_chip(bar, "Matter");
    s_matter_dot = lv_obj_get_child(matter_chip, 0);
    s_matter_lbl = lv_obj_get_child(matter_chip, 1);

    s_test_chip = make_chip(bar, "测试模式");

    lv_obj_t *spacer = lv_obj_create(bar);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_flex_grow(spacer, 1);

    lv_obj_t *gear = lv_button_create(bar);
    lv_obj_remove_style_all(gear);
    lv_obj_set_size(gear, 36, 36);
    lv_obj_set_style_radius(gear, 12, 0);
    lv_obj_add_style(gear, ui_theme_style_card2(), 0);
    lv_obj_add_style(gear, ui_theme_style_press(), LV_STATE_PRESSED);
    lv_obj_add_event_cb(gear, [](lv_event_t *e) { (void)e; ui_app_open_settings(); },
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *gear_lbl = lv_label_create(gear);
    lv_label_set_text(gear_lbl, LV_SYMBOL_SETTINGS);
    lv_obj_center(gear_lbl);

    s_clock_lbl = make_label(bar, ui_theme_style_ink(), ui_theme_font_large());
    lv_label_set_text(s_clock_lbl, "--:--");
}

static void build_header(lv_obj_t *parent) {
    lv_obj_t *head = lv_obj_create(parent);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, lv_pct(100), 52);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *col = lv_obj_create(head);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 4, 0);

    s_title_lbl = make_label(col, ui_theme_style_ink(), ui_theme_font_body());
    s_date_lbl = make_label(col, ui_theme_style_muted(), ui_theme_font_small());

    lv_obj_t *spacer = lv_obj_create(head);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_flex_grow(spacer, 1);

    s_greet_lbl = make_label(head, ui_theme_style_muted(), ui_theme_font_small());
}

static void build_weather_card(lv_obj_t *parent) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_add_style(card, ui_theme_style_card(), 0);
    lv_obj_set_size(card, lv_pct(100), 128);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    s_temp_lbl = make_label(card, ui_theme_style_ink(), ui_theme_font_large());
    lv_obj_align(s_temp_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    s_wicon_lbl = make_label(card, ui_theme_style_accent(), ui_theme_font_large());
    lv_obj_align(s_wicon_lbl, LV_ALIGN_TOP_RIGHT, -4, 0);

    s_wdesc_lbl = make_label(card, ui_theme_style_muted(), ui_theme_font_small());
    lv_obj_align(s_wdesc_lbl, LV_ALIGN_TOP_LEFT, 0, 42);

    s_whum_lbl = make_label(card, ui_theme_style_muted(), ui_theme_font_small());
    lv_obj_align(s_whum_lbl, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    s_wupd_lbl = make_label(card, ui_theme_style_muted(), ui_theme_font_small());
    lv_obj_align(s_wupd_lbl, LV_ALIGN_BOTTOM_RIGHT, 0, -20);

    s_wcity_lbl = make_label(card, ui_theme_style_muted(), ui_theme_font_small());
    lv_obj_align(s_wcity_lbl, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

static void build_lights_card(lv_obj_t *parent) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_add_style(card, ui_theme_style_card(), 0);
    lv_obj_set_size(card, lv_pct(100), 50);
    lv_obj_set_style_pad_ver(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cap = make_label(card, ui_theme_style_muted(), ui_theme_font_small());
    lv_label_set_text(cap, "全屋灯具");
    lv_obj_align(cap, LV_ALIGN_LEFT_MID, 0, 0);

    s_lights_lbl = make_label(card, ui_theme_style_ink(), ui_theme_font_body());
    lv_obj_align(s_lights_lbl, LV_ALIGN_RIGHT_MID, 0, 0);
}

static void build_hourly_card(lv_obj_t *parent) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_add_style(card, ui_theme_style_card(), 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_size(card, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_grow(card, 1);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    static const lv_coord_t dx[HOURLY_POINTS] = {-108, -36, 36, 108};
    for (int i = 0; i < HOURLY_POINTS; ++i) {
        lv_obj_t *col = lv_obj_create(card);
        lv_obj_remove_style_all(col);
        lv_obj_set_size(col, 64, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(col, 4, 0);
        lv_obj_align(col, LV_ALIGN_TOP_MID, dx[i], 6);

        s_hourly_time[i] = make_label(col, ui_theme_style_muted(), ui_theme_font_small());
        s_hourly_icon[i] = make_label(col, ui_theme_style_accent(), ui_theme_font_body());
        s_hourly_temp[i] = make_label(col, ui_theme_style_ink(), ui_theme_font_small());
    }
}

static void build_channel_card(lv_obj_t *parent, uint8_t idx) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_add_style(card, ui_theme_style_card(), 0);
    lv_obj_set_size(card, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_style(card, ui_theme_style_press(), LV_STATE_PRESSED);
    lv_obj_add_style(card, ui_theme_style_on(), LV_STATE_CHECKED);
    lv_obj_add_event_cb(card, channel_cb, LV_EVENT_ALL, (void *)(uintptr_t)idx);

    s_chan_card[idx] = card;
    s_chan_name[idx] = make_label(card, ui_theme_style_ink(), ui_theme_font_body());
    lv_obj_align(s_chan_name[idx], LV_ALIGN_TOP_LEFT, 0, 0);

    s_chan_kind[idx] = make_label(card, ui_theme_style_muted(), ui_theme_font_small());
    lv_obj_align(s_chan_kind[idx], LV_ALIGN_TOP_LEFT, 0, 32);

    s_chan_state[idx] = make_label(card, ui_theme_style_muted(), ui_theme_font_body());
    lv_obj_align(s_chan_state[idx], LV_ALIGN_RIGHT_MID, 0, 0);
}

static void build_scene_chip(lv_obj_t *parent, const scene_def_t *scene) {
    lv_obj_t *chip = lv_obj_create(parent);
    lv_obj_add_style(chip, ui_theme_style_card2(), 0);
    lv_obj_set_size(chip, LV_SIZE_CONTENT, 44);
    lv_obj_set_style_radius(chip, 14, 0);
    lv_obj_set_style_pad_hor(chip, 14, 0);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_style(chip, ui_theme_style_press(), LV_STATE_PRESSED);
    lv_obj_add_event_cb(chip, scene_chip_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)scene->id);

    lv_obj_t *icon = make_label(chip, ui_theme_style_accent(), ui_theme_font_body());
    lv_label_set_text(icon, scene->icon);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *name = make_label(chip, ui_theme_style_ink(), ui_theme_font_body());
    lv_label_set_text(name, scene->name);
    lv_obj_align_to(name, icon, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
}

static void rebuild_scene_row(const app_snapshot_t *snap) {
    if (memcmp(s_last_pinned, snap->config.pinned, sizeof(s_last_pinned)) == 0) {
        return;
    }
    memcpy(s_last_pinned, snap->config.pinned, sizeof(s_last_pinned));

    lv_obj_clean(s_scene_row);
    uint8_t count = 0;
    const scene_def_t *scenes = scene_engine_all(&count);
    for (int i = 0; i < PINNED_SCENE_MAX; ++i) {
        uint8_t id = snap->config.pinned[i];
        if (id < count) {
            build_scene_chip(s_scene_row, &scenes[id]);
        }
    }

    lv_obj_t *more = lv_obj_create(s_scene_row);
    lv_obj_add_style(more, ui_theme_style_card2(), 0);
    lv_obj_set_size(more, LV_SIZE_CONTENT, 44);
    lv_obj_set_style_radius(more, 14, 0);
    lv_obj_set_style_pad_hor(more, 14, 0);
    lv_obj_clear_flag(more, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(more, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_style(more, ui_theme_style_press(), LV_STATE_PRESSED);
    lv_obj_add_event_cb(more, more_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *more_lbl = make_label(more, ui_theme_style_muted(), ui_theme_font_body());
    lv_label_set_text(more_lbl, "更多场景");
    lv_obj_center(more_lbl);
}

/* Vertical budget on the 800x480 panel: content 448 = topbar 36 + header 52 +
 * main 288 + scene bar 48 + 3 flex gaps of 8 (screen pad 16, row gap 8).
 * The left column splits into weather 128 + lights 50 + hourly (rest). */
void ui_home_build(lv_obj_t *parent) {
    lv_obj_add_style(parent, ui_theme_style_bg(), 0);
    lv_obj_set_style_pad_all(parent, 16, 0);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 8, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    build_topbar(parent);
    build_header(parent);

    lv_obj_t *main = lv_obj_create(parent);
    lv_obj_remove_style_all(main);
    lv_obj_set_size(main, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(main, 1);
    lv_obj_set_flex_flow(main, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(main, 12, 0);

    lv_obj_t *left = lv_obj_create(main);
    lv_obj_remove_style_all(left);
    lv_obj_set_size(left, 336, lv_pct(100));
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(left, 8, 0);

    build_weather_card(left);
    build_lights_card(left);
    build_hourly_card(left);

    lv_obj_t *right = lv_obj_create(main);
    lv_obj_remove_style_all(right);
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_size(right, LV_SIZE_CONTENT, lv_pct(100));
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right, 8, 0);

    for (uint8_t i = 0; i < PANEL_CHANNEL_COUNT; ++i) {
        build_channel_card(right, i);
    }

    s_scene_row = lv_obj_create(parent);
    lv_obj_remove_style_all(s_scene_row);
    lv_obj_set_size(s_scene_row, lv_pct(100), 44);
    lv_obj_set_flex_flow(s_scene_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(s_scene_row, 10, 0);

    build_overlay(parent);
}

/* ---------------- apply ---------------- */

void ui_home_apply(const app_snapshot_t *snap, bool matter_paired) {
    bool test = panel_services_test_mode();
    if (test) {
        lv_obj_remove_flag(s_test_chip, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_test_chip, LV_OBJ_FLAG_HIDDEN);
    }

    set_text(s_matter_lbl, matter_paired ? "已连接" : "未配网");
    lv_obj_set_style_bg_color(s_matter_dot,
                              lv_color_hex(matter_paired ? 0x6AD9B3 : 0x979BA5), 0);

    set_text(s_clock_lbl, snap->time.clock);
    set_text(s_title_lbl, snap->config.panel_name);
    set_text(s_date_lbl, snap->time.date_label);
    set_text(s_greet_lbl, snap->time.greeting);

    char buf[48];
    snprintf(buf, sizeof(buf), "%d°", snap->weather.temp_c);
    set_text(s_temp_lbl, buf);
    set_text(s_wicon_lbl, theme_service_glyph(snap->weather.icon));
    set_text(s_wdesc_lbl, snap->weather.text);
    snprintf(buf, sizeof(buf), "湿度 %u%%", snap->weather.humidity);
    set_text(s_whum_lbl, buf);
    set_text(s_wcity_lbl, snap->weather.city);
    set_text(s_wupd_lbl, snap->weather.updated);

    snprintf(buf, sizeof(buf), "%u / %u", snap->lights_on, snap->lights_total);
    set_text(s_lights_lbl, buf);

    for (int i = 0; i < HOURLY_POINTS; ++i) {
        const weather_hourly_t *h = &snap->weather.hourly[i];
        snprintf(buf, sizeof(buf), "%02u时", h->hour);
        set_text(s_hourly_time[i], buf);
        snprintf(buf, sizeof(buf), "%d°", h->temp_c);
        set_text(s_hourly_temp[i], buf);
        set_text(s_hourly_icon[i], theme_service_glyph(h->icon));
    }

    for (uint8_t i = 0; i < PANEL_CHANNEL_COUNT; ++i) {
        const channel_t *ch = &snap->channels[i];
        bool light = ch->kind == CH_KIND_LIGHT;
        set_text(s_chan_name[i], ch->name);
        set_text(s_chan_kind[i], light ? "灯具控制" : "自定义开关");
        if (light) {
            set_text(s_chan_state[i], ch->on ? "已开启" : "已关闭");
            set_state_color(s_chan_state[i], ch->on);
        } else {
            set_text(s_chan_state[i], ch->triggered ? "已触发" : "待触发");
            set_state_color(s_chan_state[i], ch->triggered);
        }
        if ((light && ch->on) || (!light && ch->triggered)) {
            lv_obj_add_state(s_chan_card[i], LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(s_chan_card[i], LV_STATE_CHECKED);
        }
    }

    rebuild_scene_row(snap);
}
