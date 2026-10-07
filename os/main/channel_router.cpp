#include "channel_router.h"
#include "matter_nodes.h"
#include "scene_engine.h"
#include "app_state.h"
#include "panel_services.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <sdkconfig.h>
#include <string.h>

static const char *TAG = "router";

static int8_t s_relay_gpio[PANEL_CHANNEL_COUNT];

/* Relay outputs are held OFF at boot regardless of Matter's last state:
 * with a fault during commissioning the attached driver board must not
 * energize. Test mode (panel_services) additionally suppresses every
 * energize while the firmware runs against fixtures. */
static void relay_write(uint8_t index, bool on) {
    if (index >= PANEL_CHANNEL_COUNT || s_relay_gpio[index] < 0) return;
    if (panel_services_test_mode()) return; /* safe preview: logic runs, outputs stay off */
    gpio_set_level((gpio_num_t)s_relay_gpio[index], on ? 1 : 0);
}

/* Every key bound to this light follows its state. With the default binding
 * (key1 -> light1, key2 -> light2) this is 1:1; several keys may share one
 * light and then all their relays switch together. */
static void relay_follow_light(uint8_t light_index, bool on) {
    app_snapshot_t snap;
    app_state_get_snapshot(&snap);
    for (uint8_t i = 0; i < PANEL_CHANNEL_COUNT; ++i) {
        if (snap.config.channel_binding[i] == CH_BIND_LIGHT &&
            snap.config.channel_dev_kind[i] == DEV_LIGHT &&
            snap.config.channel_dev[i] == (int8_t)light_index) {
            relay_write(i, on);
        }
    }
}

esp_err_t channel_router_init(const uint8_t switch_gpio[3], const int8_t relay_gpio[3]) {
    memcpy(s_relay_gpio, relay_gpio, sizeof(s_relay_gpio));
    for (int i = 0; i < PANEL_CHANNEL_COUNT; ++i) {
        if (relay_gpio[i] < 0) continue;
        gpio_num_t pin = (gpio_num_t)relay_gpio[i];
        gpio_reset_pin(pin);
        gpio_set_direction(pin, GPIO_MODE_OUTPUT);
        gpio_set_level(pin, 0); /* power-on default: all outputs off */
    }
    app_state_bind_gpio(switch_gpio, relay_gpio);
    ESP_LOGI(TAG, "outputs ready (relay %d/%d/%d)", relay_gpio[0], relay_gpio[1], relay_gpio[2]);
    return ESP_OK;
}

void channel_router_set_light(uint8_t light_index, bool on, uint8_t source) {
    if (light_index >= PANEL_LIGHT_COUNT) return;

    if (source != CH_SRC_MATTER) {
        /* Notify the Matter fabric so clients converge on the same value. */
        esp_err_t err = matter_nodes_set_light_onoff(light_index, on);
        if (err != ESP_OK) ESP_LOGE(TAG, "Matter OnOff update light%u: %s", light_index + 1, esp_err_to_name(err));
    }
    relay_follow_light(light_index, on);
    app_state_set_light(light_index, on);
    ESP_LOGI(TAG, "light%u -> %s (src %u)", light_index + 1, on ? "ON" : "OFF", source);
}

void channel_router_on_matter_write(uint8_t light_index, bool on) {
    if (light_index >= PANEL_LIGHT_COUNT) return;
    /* PRE_UPDATE runs in the CHIP task; GPIO writes are cheap and idempotent,
     * the state model is mutex-protected, so no extra marshalling is needed. */
    relay_follow_light(light_index, on);
    app_state_set_light(light_index, on);
    ESP_LOGI(TAG, "light%u <- Matter %s", light_index + 1, on ? "ON" : "OFF");
}

void channel_router_on_switch_event(uint8_t index, const switch_event_t *event) {
    if (index >= PANEL_CHANNEL_COUNT || !event) return;
    app_snapshot_t snap;
    app_state_get_snapshot(&snap);

    if (snap.config.channel_binding[index] == CH_BIND_LIGHT &&
        snap.config.channel_dev[index] >= 0) {
        /* Wall-switch mode: the press is consumed by the bound device, no
         * Generic Switch events (the hub sees the state change instead). */
        const int8_t dev = snap.config.channel_dev[index];
        switch (event->event) {
        case SW_PRESSED: /* momentary key: press toggles the device */
            if (snap.config.channel_dev_kind[index] == DEV_AC) {
                const uint8_t ac = (uint8_t)dev;
                app_state_set_ac(ac, !snap.acs[ac].on, snap.acs[ac].mode,
                                 snap.acs[ac].fan, snap.acs[ac].temp_set);
                ESP_LOGI(TAG, "ch%u -> ac%u %s", index + 1, ac + 1, snap.acs[ac].on ? "OFF" : "ON");
            } else {
                channel_router_set_light((uint8_t)dev, !snap.lights[dev].on, CH_SRC_LOCAL);
            }
            break;
        case SW_LEVEL: /* bistable rocker: sync the light to the contact */
            if (snap.config.channel_dev_kind[index] == DEV_LIGHT) {
                channel_router_set_light((uint8_t)dev, event->level, CH_SRC_LOCAL);
            }
            break;
        case SW_LONG_PRESS:
            ESP_LOGI(TAG, "ch%u bound long press ignored (device hold goes to UI layer)", index + 1);
            break;
        default:
            break;
        }
        return;
    }

    /* Manual custom control: with no action configured the key reports
     * Generic Switch events (press/release/latched) plus the local action
     * hook; with a scene action it works as a scene key instead. */
    const uint8_t action = snap.config.channel_action[index];
    switch (event->event) {
    case SW_PRESSED:
        if (action != PANEL_ACTION_EVENTS) {
            app_state_set_switch(index, true);
            scene_engine_run(action);
            ESP_LOGI(TAG, "ch%u scene key -> scene %u", index + 1, action);
            break;
        }
        matter_nodes_report_initial_press(index);
        app_state_set_switch(index, true);
        ESP_LOGI(TAG, "ch%u manual pressed -> initial_press", index + 1);
        break;
    case SW_RELEASED:
        app_state_set_switch(index, false);
        if (action != PANEL_ACTION_EVENTS) break; /* scene key: no event pairing */
        matter_nodes_report_short_release(index);
        ESP_LOGI(TAG, "ch%u manual released -> short_release", index + 1);
        break;
    case SW_LONG_PRESS:
        /* Hook for the per-channel custom action defined in channel settings. */
        ESP_LOGI(TAG, "ch%u manual long press (hold %lu ms)", index + 1, (unsigned long)event->hold_ms);
        break;
    case SW_LEVEL:
        if (action != PANEL_ACTION_EVENTS) {
            app_state_set_switch(index, event->level);
            if (event->level) scene_engine_run(action);
            break;
        }
        matter_nodes_report_latched(index, event->level ? 1 : 0);
        app_state_set_switch(index, event->level);
        ESP_LOGI(TAG, "ch%u manual level %d -> switch_latched", index + 1, event->level);
        break;
    default:
        break;
    }
}

/* relay_ready kept for the future system-status page (设置 → 系统状态). */
bool channel_router_relay_enabled(uint8_t index) {
    return index < PANEL_CHANNEL_COUNT && s_relay_gpio[index] >= 0;
}
