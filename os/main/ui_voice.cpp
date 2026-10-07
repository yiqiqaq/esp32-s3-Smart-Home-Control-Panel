#include "ui_voice.h"
#include "ui_theme.h"
#include "ui_fonts.h"
#include "voice_assistant.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ui_voice";

static lv_obj_t *s_overlay, *s_orb_outer, *s_orb_inner, *s_status_lbl, *s_reply_lbl;
static bool s_open;
static voice_state_t s_applied_state = VOICE_ERROR; /* force first paint */
static char s_applied_text[128] = {0xFF};

static void orb_anim_cb(void *var, int32_t v) {
    lv_obj_set_size((lv_obj_t *)var, v, v);
    lv_obj_center((lv_obj_t *)var);
}

/* Breathing orb: outer ring scales 150->210 and fades, inner dot 70->96. */
static void orb_breathe_start(void) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_orb_outer);
    lv_anim_set_exec_cb(&a, orb_anim_cb);
    lv_anim_set_values(&a, 150, 210);
    lv_anim_set_duration(&a, 1400);
    lv_anim_set_playback_duration(&a, 1400);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);

    lv_anim_init(&a);
    lv_anim_set_var(&a, s_orb_inner);
    lv_anim_set_exec_cb(&a, orb_anim_cb);
    lv_anim_set_values(&a, 70, 96);
    lv_anim_set_duration(&a, 1400);
    lv_anim_set_playback_duration(&a, 1400);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

static void orb_anim_stop(void) {
    lv_anim_del(s_orb_outer, orb_anim_cb);
    lv_anim_del(s_orb_inner, orb_anim_cb);
    lv_obj_set_size(s_orb_outer, 150, 150);
    lv_obj_center(s_orb_outer);
    lv_obj_set_size(s_orb_inner, 70, 70);
    lv_obj_center(s_orb_inner);
}

static void apply_state(voice_state_t st, const char *text) {
    const char *status = "";
    switch (st) {
    case VOICE_RECORDING: status = "在听"; break;
    case VOICE_THINKING: status = "思考中"; break;
    case VOICE_SPEAKING: status = "正在回答"; break;
    case VOICE_ERROR: status = "出错了"; break;
    case VOICE_IDLE: status = "点击圆圈说话"; break;
    }
    lv_label_set_text(s_status_lbl, status);
    lv_label_set_text(s_reply_lbl, text ? text : "");

    if (st == VOICE_RECORDING || st == VOICE_THINKING || st == VOICE_SPEAKING) {
        orb_breathe_start();
    } else {
        orb_anim_stop();
    }
    s_applied_state = st;
    strlcpy(s_applied_text, text ? text : "", sizeof(s_applied_text));
}

static void sync_timer_cb(lv_timer_t *t) {
    (void)t;
    if (!s_open) return;
    voice_state_t st = voice_assistant_state();
    const char *text = voice_assistant_text();
    if (st == s_applied_state && strcmp(text, s_applied_text) == 0) return;
    apply_state(st, text);
}

static void orb_click_cb(lv_event_t *e) {
    (void)e;
    voice_state_t st = voice_assistant_state();
    if (st == VOICE_RECORDING) {
        voice_assistant_finish();
    } else if (st == VOICE_IDLE || st == VOICE_ERROR) {
        voice_assistant_start();
    }
}

static void close_cb(lv_event_t *e) {
    (void)e;
    voice_assistant_cancel();
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_anim_del(s_orb_outer, orb_anim_cb);
    lv_anim_del(s_orb_inner, orb_anim_cb);
    s_open = false;
}

static void mic_btn_cb(lv_event_t *e) {
    (void)e;
    if (s_open) return;
    lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    s_open = true;
    apply_state(VOICE_IDLE, NULL);
    voice_assistant_start();
}

void ui_voice_attach_button(lv_obj_t *parent) {
    lv_obj_t *mic = lv_button_create(parent);
    lv_obj_remove_style_all(mic);
    lv_obj_set_size(mic, 36, 36);
    lv_obj_set_style_radius(mic, 12, 0);
    lv_obj_add_style(mic, ui_theme_style_card2(), 0);
    lv_obj_add_style(mic, ui_theme_style_press(), LV_STATE_PRESSED);
    lv_obj_add_event_cb(mic, mic_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(mic);
    lv_label_set_text(lbl, LV_SYMBOL_AUDIO);
    lv_obj_center(lbl);
}

esp_err_t ui_voice_init(void) {
    /* full-screen layer above both screens */
    lv_obj_t *layer = lv_layer_top();

    s_overlay = lv_obj_create(layer);
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_size(s_overlay, 800, 480);
    /* XiaoAI-style: near-black backdrop */
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(0x0B0D12), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_90, 0);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_overlay, close_cb, LV_EVENT_CLICKED, NULL);

    /* breathing orb, bottom center */
    s_orb_outer = lv_obj_create(s_overlay);
    lv_obj_remove_style_all(s_orb_outer);
    lv_obj_set_size(s_orb_outer, 150, 150);
    lv_obj_set_style_radius(s_orb_outer, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_orb_outer, lv_color_hex(0x5D9DFF), 0);
    lv_obj_set_style_bg_opa(s_orb_outer, LV_OPA_30, 0);
    lv_obj_align(s_orb_outer, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_add_flag(s_orb_outer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_orb_outer, orb_click_cb, LV_EVENT_CLICKED, NULL);

    s_orb_inner = lv_obj_create(s_orb_outer);
    lv_obj_remove_style_all(s_orb_inner);
    lv_obj_set_size(s_orb_inner, 70, 70);
    lv_obj_set_style_radius(s_orb_inner, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_orb_inner, lv_color_hex(0x92C2FF), 0);
    lv_obj_set_style_bg_opa(s_orb_inner, LV_OPA_80, 0);
    lv_obj_center(s_orb_inner);
    lv_obj_add_flag(s_orb_inner, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_orb_inner, orb_click_cb, LV_EVENT_CLICKED, NULL);

    /* close chip, top right */
    lv_obj_t *close = lv_button_create(s_overlay);
    lv_obj_remove_style_all(close);
    lv_obj_set_size(close, 42, 42);
    lv_obj_set_style_radius(close, 14, 0);
    lv_obj_add_style(close, ui_theme_style_card2(), 0);
    lv_obj_add_style(close, ui_theme_style_press(), LV_STATE_PRESSED);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, -16, 16);
    lv_obj_add_event_cb(close, close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_lbl = lv_label_create(close);
    lv_label_set_text(close_lbl, LV_SYMBOL_CLOSE);
    lv_obj_center(close_lbl);

    /* status (top) and reply (center) */
    s_status_lbl = lv_label_create(s_overlay);
    lv_obj_add_style(s_status_lbl, ui_theme_style_muted(), 0);
    lv_obj_set_style_text_font(s_status_lbl, ui_theme_font_body(), 0);
    lv_obj_align(s_status_lbl, LV_ALIGN_TOP_MID, 0, 24);

    s_reply_lbl = lv_label_create(s_overlay);
    lv_obj_add_style(s_reply_lbl, ui_theme_style_ink(), 0);
    lv_obj_set_style_text_font(s_reply_lbl, &cn18, 0); /* full GB2312-L1 coverage */
    lv_obj_set_width(s_reply_lbl, 640);
    lv_label_set_long_mode(s_reply_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_reply_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_reply_lbl, LV_ALIGN_CENTER, 0, -60);

    lvgl_port_lock(0);
    lv_timer_create(sync_timer_cb, 200, NULL);
    lvgl_port_unlock();

    voice_assistant_on_state([](voice_state_t st, const char *text, void *ctx) {
        (void)st; (void)text; (void)ctx;
        /* UI polls in sync_timer_cb; nothing to do here */
    }, NULL);

    ESP_LOGI(TAG, "voice overlay ready");
    return ESP_OK;
}
