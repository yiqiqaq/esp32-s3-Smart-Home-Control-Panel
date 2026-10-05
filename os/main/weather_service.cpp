#include "weather_service.h"
#include "app_nvs.h"
#include "time_service.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <sdkconfig.h>

static const char *TAG = "weather";

#define HTTP_RX_MAX (8 * 1024)
#define CACHE_MAGIC 0x57455448 /* "WETH" */
#define CACHE_VERSION 1

static EventGroupHandle_t s_net_events;
#define NET_GOT_IP_BIT BIT0

/* ------------------------------------------------------------------ */
/* WMO 4677 weather code -> one of the four prototype condition buckets */
/* ------------------------------------------------------------------ */

static uint8_t wmo_to_icon(int code) {
    if (code == 0) return W_SUN;
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82) || (code >= 95 && code <= 99)) return W_RAIN;
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return W_SNOW;
    return W_CLOUD; /* 1-3 partly/cloudy, 45/48 fog, else unknown */
}

static const char *icon_text(uint8_t icon) {
    switch (icon) {
    case W_SUN:   return "晴 · 体感舒适";
    case W_RAIN:  return "有雨 · 记得带伞";
    case W_SNOW:  return "降雪 · 注意保暖";
    default:      return "多云 · 微风";
    }
}

static const char *icon_suggestion(uint8_t icon) {
    switch (icon) {
    case W_SUN:   return "适合通风";
    case W_RAIN:  return "建议关闭窗户";
    case W_SNOW:  return "建议开启暖灯";
    default:      return "空气舒适";
    }
}

static const char *icon_glyph(uint8_t icon) {
    switch (icon) {
    case W_SUN:   return "☀";
    case W_RAIN:  return "☂";
    case W_SNOW:  return "❄";
    default:      return "☁";
    }
}

/* Demo snapshot: matches the prototype's default screen until a real fetch succeeds. */
static weather_t demo_weather(void) {
    weather_t w = {};
    w.valid = false;
    w.offline = true;
    w.icon = W_SUN;
    w.temp_c = 24;
    w.humidity = 42;
    strlcpy(w.text, icon_text(W_SUN), sizeof(w.text));
    strlcpy(w.suggestion, icon_suggestion(W_SUN), sizeof(w.suggestion));
    strlcpy(w.city, CONFIG_PANEL_WEATHER_CITY, sizeof(w.city));
    strlcpy(w.updated, "--:--", sizeof(w.updated));
    static const int8_t temps[HOURLY_POINTS] = {24, 23, 22, 20};
    static const uint8_t icons[HOURLY_POINTS] = {W_SUN, W_SUN, W_CLOUD, W_CLOUD};
    for (int i = 0; i < HOURLY_POINTS; ++i) {
        w.hourly[i].temp_c = temps[i];
        w.hourly[i].icon = icons[i];
        w.hourly[i].hour = (uint8_t)((time_service_hour() + 2 * i) % 24);
    }
    return w;
}
/* ------------------------------------------------------------------ */
/* JSON -> weather_t                                                   */
/* ------------------------------------------------------------------ */

static int json_int(const cJSON *obj, const char *key, int fallback) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(v) ? v->valueint : fallback;
}

static double json_double(const cJSON *obj, const char *key, double fallback) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(v) ? v->valuedouble : fallback;
}

/* Sample the next 6 hours as [now, +2, +4, +6] like the prototype panel.
 * forecast_days=1 returns 24 local hourly entries, so index == local hour. */
static void sample_hourly(const cJSON *hourly, weather_t *w) {
    const cJSON *temps = cJSON_GetObjectItemCaseSensitive(hourly, "temperature_2m");
    const cJSON *codes = cJSON_GetObjectItemCaseSensitive(hourly, "weather_code");
    const uint8_t base = time_service_hour();
    for (int i = 0; i < HOURLY_POINTS; ++i) {
        const uint8_t hour = (uint8_t)((base + 2 * i) % 24);
        const cJSON *t = cJSON_GetArrayItem(temps, hour);
        const cJSON *c = cJSON_GetArrayItem(codes, hour);
        w->hourly[i].hour = hour;
        w->hourly[i].temp_c = (int8_t)lround(cJSON_IsNumber(t) ? t->valuedouble : w->temp_c);
        w->hourly[i].icon = cJSON_IsNumber(c) ? wmo_to_icon(c->valueint) : w->icon;
    }
}

static bool parse_weather(const char *body, weather_t *out) {
    cJSON *root = cJSON_ParseWithLength(body, strlen(body));
    if (!root) {
        ESP_LOGW(TAG, "JSON parse failed");
        return false;
    }
    const cJSON *current = cJSON_GetObjectItemCaseSensitive(root, "current");
    const cJSON *hourly = cJSON_GetObjectItemCaseSensitive(root, "hourly");
    bool ok = cJSON_IsObject(current);
    if (ok) {
        memset(out, 0, sizeof(*out));
        strlcpy(out->city, CONFIG_PANEL_WEATHER_CITY, sizeof(out->city));
        const double temp = json_double(current, "temperature_2m", NAN);
        if (!isnan(temp)) out->temp_c = (int8_t)lround(temp);
        out->humidity = (uint8_t)json_int(current, "relative_humidity_2m", 0);
        const int code = json_int(current, "weather_code", 0);
        out->icon = wmo_to_icon(code);
        strlcpy(out->text, icon_text(out->icon), sizeof(out->text));
        strlcpy(out->suggestion, icon_suggestion(out->icon), sizeof(out->suggestion));
        out->valid = true;
        out->offline = false;
        if (cJSON_IsObject(hourly)) sample_hourly(hourly, out);
        time_t now = time(nullptr);
        struct tm lt;
        localtime_r(&now, &lt);
        if (lt.tm_year >= 120) snprintf(out->updated, sizeof(out->updated), "%02d:%02d", lt.tm_hour, lt.tm_min);
    }
    cJSON_Delete(root);
    return ok;
}

/* ------------------------------------------------------------------ */
/* HTTP fetch + cache                                                  */
/* ------------------------------------------------------------------ */

/* Streaming GET: open -> fetch_headers -> read -> close. perform() would drop the body. */
static esp_err_t http_fetch(char *buf, size_t buf_len, int *status) {
    esp_http_client_config_t cfg = {};
    cfg.url = CONFIG_PANEL_WEATHER_URL;
    cfg.method = HTTP_METHOD_GET;
    cfg.timeout_ms = 10000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.buffer_size = 2048;

    *status = 0;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;

    esp_err_t err = esp_http_client_open(client, 0); /* write_len = 0 for GET */
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "http open: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }
    esp_http_client_fetch_headers(client);
    *status = esp_http_client_get_status_code(client);

    int total = 0, read_len;
    while ((read_len = esp_http_client_read(client, buf + total, (int)(buf_len - 1 - total))) > 0) {
        total += read_len;
        if ((size_t)total >= buf_len - 1) break;
    }
    buf[total] = '\0';
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (*status != 200) return ESP_FAIL;
    if (total <= 0) return ESP_ERR_INVALID_RESPONSE;
    if ((size_t)total >= buf_len - 1) return ESP_ERR_NO_MEM; /* truncated body */
    return ESP_OK;
}

/* Synchronous fetch + publish + cache. Single-caller: the weather task (and
 * any future settings entry point), never concurrently. */
esp_err_t weather_service_refresh(void) {
    static char rx[HTTP_RX_MAX];
    int status = 0;
    esp_err_t err = http_fetch(rx, sizeof(rx), &status);
    if (err != ESP_OK) return err; /* offline: app_state keeps the last cached/demo values */

    weather_t w;
    if (!parse_weather(rx, &w)) return ESP_ERR_INVALID_RESPONSE;

    app_state_set_weather(&w);
    app_nvs_save_weather_cache(&w, sizeof(w)); /* reboot keeps the last good values */
    ESP_LOGI(TAG, "weather %d°C, humidity %u%%, bucket %u", w.temp_c, w.humidity, w.icon);
    return ESP_OK;
}

static void on_ip_event(void *, esp_event_base_t, int32_t event_id, void *) {
    if (event_id == IP_EVENT_STA_GOT_IP) xEventGroupSetBits(s_net_events, NET_GOT_IP_BIT);
    else if (event_id == IP_EVENT_STA_LOST_IP) xEventGroupClearBits(s_net_events, NET_GOT_IP_BIT);
}

static void weather_task(void *) {
    const TickType_t period = pdMS_TO_TICKS((TickType_t)CONFIG_PANEL_WEATHER_PERIOD_MIN * 60 * 1000);
    for (;;) {
        xEventGroupWaitBits(s_net_events, NET_GOT_IP_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
        esp_err_t err = weather_service_refresh();
        if (err != ESP_OK) {
            /* stay offline: app_state keeps the last cached/demo values */
            vTaskDelay(pdMS_TO_TICKS(60 * 1000)); /* retry sooner than the full period */
            continue;
        }
        vTaskDelay(period);
    }
}

esp_err_t weather_service_init(void) {
    s_net_events = xEventGroupCreate();
    if (!s_net_events) return ESP_ERR_NO_MEM;
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, on_ip_event, nullptr));

    /* Boot value: last good cache, else the demo snapshot. */
    weather_t w;
    if (app_nvs_load_weather_cache(&w, sizeof(w)) != ESP_OK) w = demo_weather();
    app_state_set_weather(&w);

    if (xTaskCreatePinnedToCore(weather_task, "weather", 6144, nullptr, 4, nullptr, 1) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
