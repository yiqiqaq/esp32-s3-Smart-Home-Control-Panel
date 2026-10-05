#include "channel_router.h"
#include "matter_nodes.h"
#include "app_state.h"
#include "panel_services.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <sdkconfig.h>
#include <string.h>

static const char *TAG = "router";

static int8_t s_relay_gpio[3];

/* Relay outputs are held OFF at boot regardless of Matter's last state:
 * with a fault during commissioning the attached driver board must not
 * energize. Test mode (panel_services) additionally suppresses every
 * energize while the firmware runs against fixtures. */
static void relay_write(uint8_t index, bool on) {
    if (s_relay_gpio[index] < 0) return;
    if (panel_services_test_mode()) return; /* safe preview: logic runs, outputs stay off */
    gpio_set_level((gpio_num_t)s_relay_gpio[index], on ? 1 : 0);
}

static bool relay_ready(uint8_t index) {
    return s_relay_gpio[index] >= 0;
}

esp_err_t channel_router_init(const uint8_t switch_gpio[3], const int8_t relay_gpio[3]) {
    memcpy(s_relay_gpio, relay_gpio, sizeof(s_relay_gpio));
    for (int i = 0; i < 3; ++i) {
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

void channel_router_set_channel(uint8_t index, bool on, uint8_t source) {
    if (index >= 3) return;
    if (app_state_channel_kind(index) != CH_KIND_LIGHT) return;

    if (source != CH_SRC_MATTER) {
        /* Notify the Matter fabric so clients converge on the same value. */
        esp_err_t err = matter_nodes_set_onoff(index, on);
        if (err != ESP_OK) ESP_LOGE(TAG, "Matter OnOff update ch%u: %s", index + 1, esp_err_to_name(err));
    }
    relay_write(index, on);
    app_state_set_channel(index, on, false);
    ESP_LOGI(TAG, "ch%u light -> %s (src %u)", index + 1, on ? "ON" : "OFF", source);
}

void channel_router_on_matter_write(uint8_t index, bool on) {
    if (index >= 3) return;
    /* PRE_UPDATE runs in the CHIP task; GPIO writes are cheap and idempotent,
     * the state model is mutex-protected, so no extra marshalling is needed. */
    relay_write(index, on);
    app_state_set_channel(index, on, false);
    ESP_LOGI(TAG, "ch%u light <- Matter %s", index + 1, on ? "ON" : "OFF");
}

/* Custom channel: physical press becomes a Matter Generic Switch press/release
 * pair plus the local custom action hook. Not counted in the lights summary.
 * triggered mirrors the contact: held = 已触发, released = 待触发. */
static void custom_channel_event(uint8_t index, const switch_event_t *ev) {
    switch (ev->event) {
    case SW_PRESSED:
        matter_nodes_report_initial_press(index);
        app_state_set_channel(index, false, true);
        ESP_LOGI(TAG, "ch%u custom pressed -> initial_press", index + 1);
        break;
    case SW_RELEASED:
        matter_nodes_report_short_release(index);
        app_state_set_channel(index, false, false);
        ESP_LOGI(TAG, "ch%u custom released -> short_release", index + 1);
        break;
    case SW_LONG_PRESS:
        /* Hook for the per-channel custom action defined in channel settings. */
        ESP_LOGI(TAG, "ch%u custom long press (hold %lu ms)", index + 1, (unsigned long)ev->hold_ms);
        break;
    case SW_LEVEL:
        matter_nodes_report_latched(index, ev->level ? 1 : 0);
        app_state_set_channel(index, false, ev->level);
        ESP_LOGI(TAG, "ch%u custom level %d -> switch_latched", index + 1, ev->level);
        break;
    default:
        break;
    }
}

void channel_router_on_switch_event(uint8_t index, const switch_event_t *event) {
    if (index >= 3 || !event) return;
    if (app_state_channel_kind(index) == CH_KIND_CUSTOM) {
        custom_channel_event(index, event);
        return;
    }

    switch (event->event) {
    case SW_PRESSED: /* momentary: press toggles the light */
    case SW_LEVEL: { /* toggle mode: every stable change syncs the light */
        app_snapshot_t snap;
        app_state_get_snapshot(&snap);
        channel_router_set_channel(index, !snap.channels[index].on, CH_SRC_LOCAL);
        break;
    }
    case SW_LONG_PRESS:
        ESP_LOGI(TAG, "ch%u light long press ignored (scene hold goes to UI layer)", index + 1);
        break;
    default:
        break;
    }
}

/* relay_ready kept for the future system-status page (设置 → 系统状态). */
bool channel_router_relay_enabled(uint8_t index) {
    return index < 3 && relay_ready(index);
}
