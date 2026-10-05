#include "switch_inputs.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <sdkconfig.h>
#include <string.h>

static const char *TAG = "switch_scan";

#define SAMPLE_PERIOD_MS 5

#if CONFIG_PANEL_SWITCH_MODE_MOMENTARY
#define MODE_MOMENTARY 1
#else
#define MODE_MOMENTARY 0
#endif

static switch_callback_t s_callback;
static gpio_num_t s_pins[3];

/* Integrative debounce state, one channel each:
 * raw -> candidate -> stable. candidate only promotes to stable after
 * DEBOUNCE_MS / 5 consecutive equal samples, so a single glitch can never
 * cross the stable level. */
typedef struct {
    bool stable;      /* debounced level (true = pressed/closed) */
    bool candidate;   /* level being accumulated */
    uint8_t run;      /* consecutive samples agreeing with candidate */
    int64_t press_ts; /* us timestamp of the stable press */
    bool long_fired;
} chan_state_t;

static chan_state_t s_ch[3];

static inline uint8_t required_run(void) {
    uint8_t need = (uint8_t)(CONFIG_PANEL_DEBOUNCE_MS / SAMPLE_PERIOD_MS);
    return need < 1 ? 1 : need;
}

static void emit(uint8_t idx, uint8_t event, bool level) {
    if (!s_callback) return;
    switch_event_t ev = {};
    ev.event = event;
    ev.level = level;
    ev.hold_ms = (uint32_t)((esp_timer_get_time() - s_ch[idx].press_ts) / 1000);
    s_callback(idx, &ev);
}

/* Promote candidate -> stable and dispatch the matching event. */
static void settle(uint8_t idx, int64_t now) {
    chan_state_t *c = &s_ch[idx];
    c->stable = c->candidate;
    c->run = 0;
    if (MODE_MOMENTARY) {
        if (c->stable) {
            c->press_ts = now;
            c->long_fired = false;
            emit(idx, SW_PRESSED, true);
        } else {
            emit(idx, SW_RELEASED, false);
        }
    } else {
        emit(idx, SW_LEVEL, c->stable); /* bistable rocker: sync to contact position */
    }
}

static void poll_task(void *) {
    const uint8_t need = required_run();
    const int64_t long_press_us = (int64_t)CONFIG_PANEL_LONGPRESS_MS * 1000;
    for (;;) {
        const int64_t now = esp_timer_get_time();
        for (uint8_t i = 0; i < 3; ++i) {
            chan_state_t *c = &s_ch[i];
            const bool raw = gpio_get_level(s_pins[i]) == 0; /* switch between GPIO and GND */

            if (raw != c->candidate) {
                c->candidate = raw;
                c->run = 1;
            } else if (c->candidate != c->stable) {
                if (++c->run >= need) settle(i, now);
            }

            if (MODE_MOMENTARY && c->stable && !c->long_fired &&
                now - c->press_ts >= long_press_us) {
                c->long_fired = true;
                emit(i, SW_LONG_PRESS, true);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    }
}

esp_err_t switch_inputs_init(switch_callback_t callback, const uint8_t gpios[3]) {
    s_callback = callback;
    memcpy(s_pins, gpios, sizeof(s_pins));
    memset(s_ch, 0, sizeof(s_ch));

    uint64_t mask = 0;
    for (int i = 0; i < 3; ++i) {
        if (s_pins[i] < 0 || s_pins[i] == GPIO_NUM_NC) return ESP_ERR_INVALID_ARG;
        mask |= 1ULL << s_pins[i];
    }
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = mask;
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE; /* open switch reads 1; closing to GND reads 0 */
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) return err;

    /* Adopt the contact state present at boot instead of emitting a spurious event. */
    for (int i = 0; i < 3; ++i) {
        s_ch[i].candidate = gpio_get_level(s_pins[i]) == 0;
        s_ch[i].stable = s_ch[i].candidate;
    }

    if (xTaskCreatePinnedToCore(poll_task, "switch_scan", 3072, nullptr, 5, nullptr, 1) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "scanning GPIO %d/%d/%d, %s mode, debounce %d ms",
             s_pins[0], s_pins[1], s_pins[2],
             MODE_MOMENTARY ? "momentary" : "toggle", CONFIG_PANEL_DEBOUNCE_MS);
    return ESP_OK;
}
