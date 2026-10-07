#include "ui_home.h"
#include "ui_app.h"
#include "ui_voice.h"
#include "voice_assistant.h"
#include <stdlib.h>
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
static lv_obj_t *s_clock_lbl, *s_title_lbl, *s_dg_lbl;
static lv_obj_t *s_temp_lbl, *s_wicon_lbl, *s_wdesc_lbl, *s_whum_lbl, *s_wcity_lbl;
static lv_obj_t *s_lights_lbl;
static lv_obj_t *s_chan_pill[PANEL_CHANNEL_COUNT];
static lv_obj_t *s_hourly_time[HOURLY_POINTS], *s_hourly_temp[HOURLY_POINTS],
    *s_hourly_icon[HOURLY_POINTS];
static lv_obj_t *s_chan_card[PANEL_CHANNEL_COUNT], *s_chan_name[PANEL_CHANNEL_COUNT],
    *s_chan_kind[PANEL_CHANNEL_COUNT], *s_chan_state[PANEL_CHANNEL_COUNT];
static lv_obj_t *s_scene_row;
static lv_obj_t *s_scene_chip[SC_COUNT];
static lv_obj_t *s_media_bar, *s_media_title, *s_media_play_lbl, *s_media_vol;
static bool s_media_playing;
static lv_obj_t *s_acp_overlay, *s_acp_title, *s_acp_pow, *s_acp_cool, *s_acp_heat,
    *s_acp_temp_lbl, *s_acp_fan_btn[4];
static uint8_t s_acp_idx;


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

/* Light on/off feedback: quick scale pulse on the card */
static bool s_card_prev_active[PANEL_CHANNEL_COUNT];
static bool s_card_seen[PANEL_CHANNEL_COUNT];

static void card_pulse(lv_obj_t *card) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, card);
    lv_anim_set_exec_cb(&a, [](void *var, int32_t v) {
        lv_obj_set_style_transform_scale((lv_obj_t *)var, (int32_t)v, 0);
    });
    lv_anim_set_values(&a, 250, 260);
    lv_anim_set_duration(&a, 140);
    lv_anim_set_playback_duration(&a, 180);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
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

static void scene_chip_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        scene_engine_run((uint8_t)(uintptr_t)lv_event_get_user_data(e));
    }
}

static void acp_open(uint8_t ac_index); /* defined with the AC panel below */

static void channel_cb(lv_event_t *e) {
    uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    app_snapshot_t s;
    app_state_get_snapshot(&s);
    const bool bound = s.config.channel_binding[idx] == CH_BIND_LIGHT &&
                       s.config.channel_dev[idx] >= 0 &&
                       ((s.config.channel_dev_kind[idx] == DEV_AC &&
                         s.config.channel_dev[idx] < PANEL_AC_COUNT) ||
                        (s.config.channel_dev_kind[idx] == DEV_LIGHT &&
                         s.config.channel_dev[idx] < PANEL_LIGHT_COUNT));

    if (code == LV_EVENT_CLICKED) {
        if (bound && s.config.channel_dev_kind[idx] == DEV_AC) {
            acp_open((uint8_t)s.config.channel_dev[idx]);
        } else if (bound) {
            const uint8_t light_idx = (uint8_t)s.config.channel_dev[idx];
            channel_router_set_light(light_idx, !s.lights[light_idx].on, CH_SRC_LOCAL);
        }
        return;
    }

    switch_event_t ev = {SW_PRESSED, false, 0};
    if (code == LV_EVENT_RELEASED) {
        ev.event = SW_RELEASED;
    } else if (code == LV_EVENT_LONG_PRESSED) {
        ev.event = SW_LONG_PRESS;
    } else if (code != LV_EVENT_PRESSED) {
        return;
    }

    /* Manual keys report the full press/release/latched event set; bound keys
     * only surface the long-press hook (a press is the light toggle above). */
    if (!bound || ev.event == SW_LONG_PRESS) {
        channel_router_on_switch_event(idx, &ev);
    }
}

/* ---------------- AC control panel (second-level UI) ---------------- */

static void acp_apply_to_state(bool on, uint8_t mode, uint8_t fan, int8_t temp) {
    app_snapshot_t snap;
    app_state_get_snapshot(&snap);
    app_state_set_ac(s_acp_idx, on, mode, fan, temp);
}

static void acp_refresh(void) {
    app_snapshot_t snap;
    app_state_get_snapshot(&snap);
    const ac_t *ac = &snap.acs[s_acp_idx];
    lv_label_set_text(s_acp_title, ac->name);
    if (ac->on) {
        lv_obj_add_state(s_acp_pow, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(s_acp_pow, LV_STATE_CHECKED);
    }
    lv_obj_add_state(s_acp_cool, ac->mode == AC_MODE_COOL ? LV_STATE_CHECKED : (lv_state_t)0);
    if (ac->mode == AC_MODE_COOL) lv_obj_add_state(s_acp_cool, LV_STATE_CHECKED);
    else lv_obj_remove_state(s_acp_cool, LV_STATE_CHECKED);
    if (ac->mode == AC_MODE_HEAT) lv_obj_add_state(s_acp_heat, LV_STATE_CHECKED);
    else lv_obj_remove_state(s_acp_heat, LV_STATE_CHECKED);

    char buf[16];
    snprintf(buf, sizeof(buf), "%d°", ac->temp_set);
    lv_label_set_text(s_acp_temp_lbl, buf);

    for (uint8_t f = 0; f < 4; ++f) {
        if (ac->fan == f) lv_obj_add_state(s_acp_fan_btn[f], LV_STATE_CHECKED);
        else lv_obj_remove_state(s_acp_fan_btn[f], LV_STATE_CHECKED);
    }
}

static void acp_close_cb(lv_event_t *e) {
    (void)e;
    lv_obj_add_flag(s_acp_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void acp_pow_cb(lv_event_t *e) {
    (void)e;
    app_snapshot_t snap;
    app_state_get_snapshot(&snap);
    const ac_t *ac = &snap.acs[s_acp_idx];
    acp_apply_to_state(!ac->on, ac->mode, ac->fan, ac->temp_set);
}

static void acp_cool_cb(lv_event_t *e) {
    (void)e;
    app_snapshot_t snap;
    app_state_get_snapshot(&snap);
    const ac_t *ac = &snap.acs[s_acp_idx];
    acp_apply_to_state(ac->on, AC_MODE_COOL, ac->fan, ac->temp_set);
}

static void acp_heat_cb(lv_event_t *e) {
    (void)e;
    app_snapshot_t snap;
    app_state_get_snapshot(&snap);
    const ac_t *ac = &snap.acs[s_acp_idx];
    acp_apply_to_state(ac->on, AC_MODE_HEAT, ac->fan, ac->temp_set);
}

static void acp_temp_down_cb(lv_event_t *e) {
    (void)e;
    app_snapshot_t snap;
    app_state_get_snapshot(&snap);
    const ac_t *ac = &snap.acs[s_acp_idx];
    acp_apply_to_state(ac->on, ac->mode, ac->fan, ac->temp_set - 1);
}

static void acp_temp_up_cb(lv_event_t *e) {
    (void)e;
    app_snapshot_t snap;
    app_state_get_snapshot(&snap);
    const ac_t *ac = &snap.acs[s_acp_idx];
    acp_apply_to_state(ac->on, ac->mode, ac->fan, ac->temp_set + 1);
}

static void acp_fan_cb(uint8_t fan) {
    app_snapshot_t snap;
    app_state_get_snapshot(&snap);
    const ac_t *ac = &snap.acs[s_acp_idx];
    acp_apply_to_state(ac->on, ac->mode, fan, ac->temp_set);
}

/* ---------------- media bar ---------------- */

static lv_obj_t *media_btn(lv_obj_t *parent, const char *symbol, lv_event_cb_t cb, void *user_data) {
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 36, 36);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_add_style(btn, ui_theme_style_card2(), 0);
    lv_obj_add_style(btn, ui_theme_style_press(), LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, symbol);
    lv_obj_center(lbl);
    return btn;
}

static void media_prev_cb(lv_event_t *e) { (void)e; voice_assistant_music_cmd("prev"); }
static void media_next_cb(lv_event_t *e) { (void)e; voice_assistant_music_cmd("next"); }
static void media_play_cb(lv_event_t *e) {
    (void)e;
    voice_assistant_music_cmd(s_media_playing ? "pause" : "play");
    s_media_playing = !s_media_playing;
    lv_label_set_text(s_media_play_lbl, s_media_playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
}
static void media_vol_cb(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target_obj(e);
    voice_assistant_music_set_volume(lv_slider_get_value(slider));
}

void ui_home_music_update(bool active, const char *title, int volume, bool playing) {
    if (!s_media_bar) return;
    s_media_playing = playing;
    lv_label_set_text(s_media_play_lbl, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    if (active) {
        lv_obj_remove_flag(s_media_bar, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_media_bar, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (title) set_text(s_media_title, title);
    if (lv_slider_get_value(s_media_vol) != volume) lv_slider_set_value(s_media_vol, volume, LV_ANIM_OFF);
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

    ui_voice_attach_button(bar);

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
    lv_obj_set_size(head, lv_pct(100), 46);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_title_lbl = make_label(head, ui_theme_style_ink(), ui_theme_font_body());

    lv_obj_t *spacer = lv_obj_create(head);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_flex_grow(spacer, 1);

    s_dg_lbl = make_label(head, ui_theme_style_muted(), ui_theme_font_small());
}

/* Flex-column weather card: survives height changes (media bar) gracefully */
static void build_weather_card(lv_obj_t *parent) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_add_style(card, ui_theme_style_card(), 0);
    lv_obj_set_size(card, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_grow(card, 1);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 4, 0);

    lv_obj_t *top = lv_obj_create(card);
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    s_temp_lbl = make_label(top, ui_theme_style_ink(), ui_theme_font_large());
    lv_obj_t *sp = lv_obj_create(top);
    lv_obj_remove_style_all(sp);
    lv_obj_set_flex_grow(sp, 1);
    s_wicon_lbl = make_label(top, ui_theme_style_accent(), ui_theme_font_large());

    s_wdesc_lbl = make_label(card, ui_theme_style_muted(), ui_theme_font_small());

    lv_obj_t *grow = lv_obj_create(card);
    lv_obj_remove_style_all(grow);
    lv_obj_set_flex_grow(grow, 1);

    lv_obj_t *bot = lv_obj_create(card);
    lv_obj_remove_style_all(bot);
    lv_obj_set_size(bot, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bot, LV_FLEX_FLOW_ROW);
    s_whum_lbl = make_label(bot, ui_theme_style_muted(), ui_theme_font_small());
    lv_obj_t *sp2 = lv_obj_create(bot);
    lv_obj_remove_style_all(sp2);
    lv_obj_set_flex_grow(sp2, 1);
    s_wcity_lbl = make_label(bot, ui_theme_style_muted(), ui_theme_font_small());
}

static void build_lights_row(lv_obj_t *parent) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 22);
    lv_obj_set_style_pad_hor(row, 6, 0);

    lv_obj_t *cap = make_label(row, ui_theme_style_muted(), ui_theme_font_small());
    lv_label_set_text(cap, "全屋灯具");
    lv_obj_align(cap, LV_ALIGN_LEFT_MID, 0, 0);

    s_lights_lbl = make_label(row, ui_theme_style_ink(), ui_theme_font_small());
    lv_obj_align(s_lights_lbl, LV_ALIGN_RIGHT_MID, 0, 0);
}

static void build_hourly_card(lv_obj_t *parent) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_add_style(card, ui_theme_style_card(), 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_size(card, lv_pct(100), 96);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    static const lv_coord_t dx[HOURLY_POINTS] = {-104, -35, 35, 104};
    for (int i = 0; i < HOURLY_POINTS; ++i) {
        lv_obj_t *col = lv_obj_create(card);
        lv_obj_remove_style_all(col);
        lv_obj_set_size(col, 64, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(col, 4, 0);
        lv_obj_align(col, LV_ALIGN_TOP_MID, dx[i], 8);

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

    /* state pill: quieter than bare text, reads as one unit */
    s_chan_pill[idx] = lv_obj_create(card);
    lv_obj_add_style(s_chan_pill[idx], ui_theme_style_card2(), 0);
    lv_obj_set_size(s_chan_pill[idx], LV_SIZE_CONTENT, 30);
    lv_obj_set_style_radius(s_chan_pill[idx], 15, 0);
    lv_obj_set_style_pad_hor(s_chan_pill[idx], 12, 0);
    lv_obj_clear_flag(s_chan_pill[idx], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_chan_pill[idx], LV_ALIGN_RIGHT_MID, 0, 0);
    s_chan_state[idx] = make_label(s_chan_pill[idx], ui_theme_style_muted(), ui_theme_font_small());
}

static void build_scene_chip(lv_obj_t *parent, const scene_def_t *scene) {
    lv_obj_t *chip = lv_obj_create(parent);
    lv_obj_add_style(chip, ui_theme_style_card2(), 0);
    lv_obj_set_flex_grow(chip, 1);
    lv_obj_set_size(chip, LV_SIZE_CONTENT, 44);
    lv_obj_set_style_radius(chip, 14, 0);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_style(chip, ui_theme_style_press(), LV_STATE_PRESSED);
    lv_obj_add_style(chip, ui_theme_style_on(), LV_STATE_CHECKED);
    lv_obj_add_event_cb(chip, scene_chip_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)scene->id);
    s_scene_chip[scene->id] = chip;

    lv_obj_t *icon = make_label(chip, ui_theme_style_accent(), ui_theme_font_body());
    lv_label_set_text(icon, scene->icon);
    lv_obj_align(icon, LV_ALIGN_RIGHT_MID, -32, 0);
    lv_obj_t *name = make_label(chip, ui_theme_style_ink(), ui_theme_font_body());
    lv_label_set_text(name, scene->name);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 34, 0);
}

static void build_media_bar(lv_obj_t *parent) {
    s_media_bar = lv_obj_create(parent);
    lv_obj_add_style(s_media_bar, ui_theme_style_card(), 0);
    lv_obj_set_size(s_media_bar, lv_pct(100), 52);
    lv_obj_set_style_pad_ver(s_media_bar, 8, 0);
    lv_obj_clear_flag(s_media_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_media_bar, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *title = make_label(s_media_bar, ui_theme_style_ink(), ui_theme_font_small());
    lv_label_set_text(title, "音乐");
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, 300);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 6, 0);
    s_media_title = title;

    lv_obj_t *row = lv_obj_create(s_media_bar);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, 36);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_align(row, LV_ALIGN_LEFT_MID, 340, 0);
    media_btn(row, LV_SYMBOL_PREV, media_prev_cb, NULL);
    lv_obj_t *play_btn = media_btn(row, LV_SYMBOL_PLAY, media_play_cb, NULL);
    s_media_play_lbl = lv_obj_get_child(play_btn, 0);
    media_btn(row, LV_SYMBOL_NEXT, media_next_cb, NULL);

    s_media_vol = lv_slider_create(s_media_bar);
    lv_slider_set_range(s_media_vol, 0, 100);
    lv_slider_set_value(s_media_vol, 70, LV_ANIM_OFF);
    lv_obj_set_size(s_media_vol, 90, 6);
    lv_obj_add_event_cb(s_media_vol, media_vol_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_align(s_media_vol, LV_ALIGN_RIGHT_MID, -6, 0);
}

static void acp_open(uint8_t ac_index) {
    s_acp_idx = ac_index < PANEL_AC_COUNT ? ac_index : 0;
    acp_refresh();
    lv_obj_remove_flag(s_acp_overlay, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *acp_chip(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *ud) {
    lv_obj_t *chip = lv_button_create(parent);
    lv_obj_remove_style_all(chip);
    lv_obj_set_size(chip, LV_SIZE_CONTENT, 40);
    lv_obj_set_style_pad_hor(chip, 18, 0);
    lv_obj_set_style_radius(chip, 14, 0);
    lv_obj_add_style(chip, ui_theme_style_card2(), 0);
    lv_obj_add_style(chip, ui_theme_style_press(), LV_STATE_PRESSED);
    lv_obj_add_style(chip, ui_theme_style_on(), LV_STATE_CHECKED);
    lv_obj_add_event_cb(chip, cb, LV_EVENT_CLICKED, ud);
    lv_obj_t *lbl = make_label(chip, ui_theme_style_ink(), ui_theme_font_body());
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    return chip;
}

static void build_acp(lv_obj_t *parent) {
    s_acp_overlay = lv_obj_create(parent);
    lv_obj_remove_style_all(s_acp_overlay);
    lv_obj_set_size(s_acp_overlay, 800, 480);
    lv_obj_set_style_bg_color(s_acp_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_acp_overlay, LV_OPA_60, 0);
    lv_obj_add_flag(s_acp_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_acp_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_acp_overlay, acp_close_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *panel = lv_obj_create(s_acp_overlay);
    lv_obj_add_style(panel, ui_theme_style_card(), 0);
    lv_obj_set_style_radius(panel, 24, 0);
    lv_obj_set_size(panel, 420, 336);
    lv_obj_center(panel);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);

    /* header: title + power */
    s_acp_title = make_label(panel, ui_theme_style_ink(), ui_theme_font_body());
    lv_obj_align(s_acp_title, LV_ALIGN_TOP_LEFT, 0, 0);
    s_acp_pow = lv_switch_create(panel);
    lv_obj_set_size(s_acp_pow, 52, 28);
    lv_obj_align(s_acp_pow, LV_ALIGN_TOP_RIGHT, 0, -4);
    lv_obj_add_event_cb(s_acp_pow, acp_pow_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* mode row */
    s_acp_cool = acp_chip(panel, "制冷", acp_cool_cb, NULL);
    lv_obj_align(s_acp_cool, LV_ALIGN_TOP_LEFT, 0, 46);
    s_acp_heat = acp_chip(panel, "制热", acp_heat_cb, NULL);
    lv_obj_align(s_acp_heat, LV_ALIGN_TOP_LEFT, 110, 46);

    /* big temperature with +/- */
    s_acp_temp_lbl = make_label(panel, ui_theme_style_ink(), ui_theme_font_large());
    lv_obj_align(s_acp_temp_lbl, LV_ALIGN_TOP_MID, 0, 96);
    lv_obj_t *down = acp_chip(panel, LV_SYMBOL_MINUS, acp_temp_down_cb, NULL);
    lv_obj_align(down, LV_ALIGN_TOP_LEFT, 0, 100);
    lv_obj_t *up = acp_chip(panel, LV_SYMBOL_PLUS, acp_temp_up_cb, NULL);
    lv_obj_align(up, LV_ALIGN_TOP_RIGHT, 0, 100);

    /* fan row */
    static const char *fans[4] = {"自动风", "低风", "中风", "高风"};
    for (uint8_t f = 0; f < 4; ++f) {
        lv_event_cb_t cb = nullptr;
        switch (f) {
        case 0: cb = [](lv_event_t *e) { (void)e; acp_fan_cb(AC_FAN_AUTO); }; break;
        case 1: cb = [](lv_event_t *e) { (void)e; acp_fan_cb(AC_FAN_LOW); }; break;
        case 2: cb = [](lv_event_t *e) { (void)e; acp_fan_cb(AC_FAN_MID); }; break;
        default: cb = [](lv_event_t *e) { (void)e; acp_fan_cb(AC_FAN_HIGH); }; break;
        }
        s_acp_fan_btn[f] = acp_chip(panel, fans[f], cb, NULL);
        lv_obj_align(s_acp_fan_btn[f], LV_ALIGN_TOP_LEFT, (lv_coord_t)(f * 100), 220);
    }
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
    build_hourly_card(left);
    build_lights_row(left);

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

    /* All six scenes live directly on the home bar; no scene library. */
    uint8_t count = 0;
    const scene_def_t *scenes = scene_engine_all(&count);
    for (uint8_t i = 0; i < count; ++i) {
        build_scene_chip(s_scene_row, &scenes[i]);
    }

    build_media_bar(parent);
    build_acp(parent); /* last: control panel stacks above everything */
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
    char dg[64];
    snprintf(dg, sizeof(dg), "%s · %s", snap->time.date_label, snap->time.greeting);
    set_text(s_dg_lbl, dg);

    char buf[48];
    snprintf(buf, sizeof(buf), "%d°", snap->weather.temp_c);
    set_text(s_temp_lbl, buf);
    set_text(s_wicon_lbl, theme_service_glyph(snap->weather.icon));
    set_text(s_wdesc_lbl, snap->weather.text);
    snprintf(buf, sizeof(buf), "湿度 %u%%", snap->weather.humidity);
    set_text(s_whum_lbl, buf);
    set_text(s_wcity_lbl, snap->weather.city);

    snprintf(buf, sizeof(buf), "%u / %u", snap->lights_on, snap->lights_total);
    set_text(s_lights_lbl, buf);
    (void)0;

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
        const bool bound = snap->config.channel_binding[i] == CH_BIND_LIGHT &&
                           snap->config.channel_dev[i] >= 0 &&
                           ((snap->config.channel_dev_kind[i] == DEV_AC &&
                             snap->config.channel_dev[i] < PANEL_AC_COUNT) ||
                            (snap->config.channel_dev_kind[i] == DEV_LIGHT &&
                             snap->config.channel_dev[i] < PANEL_LIGHT_COUNT));
        bool active;
        if (bound && snap->config.channel_dev_kind[i] == DEV_AC) {
            const uint8_t ai = (uint8_t)snap->config.channel_dev[i];
            const ac_t *ac = &snap->acs[ai];
            set_text(s_chan_name[i], ac->name);
            set_text(s_chan_kind[i], "空调控制");
            if (ac->on) {
                snprintf(buf2, sizeof(buf2), "%s %d°",
                         ac->mode == AC_MODE_COOL ? "制冷" : "制热", ac->temp_now);
                set_text(s_chan_state[i], buf2);
            } else {
                set_text(s_chan_state[i], "已关闭");
            }
            set_state_color(s_chan_state[i], ac->on);
            active = ac->on;
        } else if (bound) {
            const uint8_t li = (uint8_t)snap->config.channel_dev[i];
            set_text(s_chan_name[i], snap->lights[li].name);
            set_text(s_chan_kind[i], "灯光开关");
            set_text(s_chan_state[i], snap->lights[li].on ? "已开启" : "已关闭");
            set_state_color(s_chan_state[i], snap->lights[li].on);
            active = snap->lights[li].on;
        } else {
            set_text(s_chan_name[i], ch->name);
            const uint8_t act = snap->config.channel_action[i];
            if (act < SC_COUNT) {
                char buf2[40];
                snprintf(buf2, sizeof(buf2), "场景键 · %s", scene_engine_all(nullptr)[act].name);
                set_text(s_chan_kind[i], buf2);
            } else {
                set_text(s_chan_kind[i], "自定义开关");
            }
            set_text(s_chan_state[i], ch->triggered ? "已触发" : "待触发");
            set_state_color(s_chan_state[i], ch->triggered);
            active = ch->triggered;
        }
        if (active) {
            lv_obj_add_state(s_chan_card[i], LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(s_chan_card[i], LV_STATE_CHECKED);
        }
        /* on/off change animation (skip the first paint) */
        if (s_card_seen[i] && active != s_card_prev_active[i]) {
            card_pulse(s_chan_card[i]);
        }
        s_card_prev_active[i] = active;
        s_card_seen[i] = true;
    }

    for (uint8_t i = 0; i < SC_COUNT; ++i) {
        if (snap->active_scene == i) {
            lv_obj_add_state(s_scene_chip[i], LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(s_scene_chip[i], LV_STATE_CHECKED);
        }
    }
}
