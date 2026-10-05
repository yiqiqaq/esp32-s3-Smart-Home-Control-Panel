#include "time_service.h"
#include "app_state.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <sdkconfig.h>

static const char *TAG = "panel_time";

static const char *const kGreetings[4] = {
    "夜深了，注意休息",
    "早上好，开启美好一天",
    "下午好，家中一切如常",
    "晚上好，欢迎回家",
};
static const char *const kWeekdays[7] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};

static bool s_synced;

void time_service_tick(void);

static void sync_notifier(struct timeval *) {
    s_synced = true;
    ESP_LOGI(TAG, "time synchronized");
}

static void on_ip_event(void *, esp_event_base_t, int32_t event_id, void *) {
    if (event_id == IP_EVENT_STA_GOT_IP) {
        if (!esp_sntp_enabled()) {
            esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, CONFIG_PANEL_NTP_SERVER);
            esp_sntp_setservername(1, "pool.ntp.org");
            esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
            esp_sntp_init();
        }
    } else if (event_id == IP_EVENT_STA_LOST_IP) {
        if (esp_sntp_enabled()) esp_sntp_stop();
        /* epoch keeps running from the last sync; s_synced stays true on purpose */
    }
}

/* Greeting bands mirror the HTML prototype: <6 night, <11 morning, <18 afternoon, else evening. */
static uint8_t greeting_index(uint8_t hour) {
    if (hour < 6) return 0;
    if (hour < 11) return 1;
    if (hour < 18) return 2;
    return 3;
}

static void format_date(const struct tm *lt, char *out, size_t len) {
    snprintf(out, len, "%d月%d日 %s", lt->tm_mon + 1, lt->tm_mday, kWeekdays[lt->tm_wday]);
}

static bool epoch_plausible(const struct tm *lt) { return lt->tm_year >= 120; } /* pre-2020 = never synced */

static void compose(time_info_t *info) {
    time_t now = time(nullptr);
    struct tm lt;
    localtime_r(&now, &lt);
    memset(info, 0, sizeof(*info));
    if (!epoch_plausible(&lt)) {
        strlcpy(info->clock, "--:--", sizeof(info->clock));
        strlcpy(info->date_label, "--", sizeof(info->date_label));
        strlcpy(info->greeting, kGreetings[3], sizeof(info->greeting));
        info->is_day = true;
        return;
    }
    info->synced = s_synced;
    info->is_day = lt.tm_hour >= 6 && lt.tm_hour < 18;
    snprintf(info->clock, sizeof(info->clock), "%02d:%02d", lt.tm_hour, lt.tm_min);
    format_date(&lt, info->date_label, sizeof(info->date_label));
    strlcpy(info->greeting, kGreetings[greeting_index(lt.tm_hour)], sizeof(info->greeting));
}

static void tick_task(void *) {
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        time_service_tick();
    }
}

esp_err_t time_service_init(void) {
    setenv("TZ", CONFIG_PANEL_TIMEZONE, 1);
    tzset();
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, on_ip_event, nullptr));
    sntp_set_time_sync_notification_cb(sync_notifier);

    time_info_t info;
    compose(&info);
    app_state_set_time_info(&info);
    if (xTaskCreatePinnedToCore(tick_task, "time_tick", 2048, nullptr, 3, nullptr, 1) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "timezone %s, ntp %s", CONFIG_PANEL_TIMEZONE, CONFIG_PANEL_NTP_SERVER);
    return ESP_OK;
}

void time_service_tick(void) {
    time_info_t info;
    compose(&info);
    app_state_set_time_info(&info); /* app_state drops the commit when nothing changed */
}

bool time_service_is_synced(void) { return s_synced; }

bool time_service_is_day(void) {
    time_t now = time(nullptr);
    struct tm lt;
    localtime_r(&now, &lt);
    if (!epoch_plausible(&lt)) return true;
    return lt.tm_hour >= 6 && lt.tm_hour < 18;
}

uint8_t time_service_hour(void) {
    time_t now = time(nullptr);
    struct tm lt;
    localtime_r(&now, &lt);
    return (uint8_t)lt.tm_hour;
}
