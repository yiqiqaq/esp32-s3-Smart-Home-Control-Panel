#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PANEL_CHANNEL_COUNT 3
#define PANEL_DISPLAY_NAME_MAX 13
#define PANEL_CHANNEL_NAME_MAX 17
#define PINNED_SCENE_MAX 3
#define HOURLY_POINTS 4

enum {
    W_SUN = 0,
    W_CLOUD,
    W_RAIN,
    W_SNOW,
    W_ICON_COUNT,
};

enum {
    THEME_COOL = 0,
    THEME_WARM,
    THEME_MINT,
    THEME_LIGHT,
    THEME_COUNT,
};

enum {
    CH_KIND_LIGHT = 0,
    CH_KIND_CUSTOM,
};

enum {
    SC_HOME = 0,
    SC_REST,
    SC_AWAY,
    SC_MOVIE,
    SC_READ,
    SC_COUNT,
};

typedef struct {
    uint8_t hour;    /* 0..23, display hour of the forecast point */
    int8_t temp_c;
    uint8_t icon;    /* W_* */
} weather_hourly_t;

typedef struct {
    bool valid;          /* false = demo/placeholder data */
    bool offline;        /* last fetch failed, values are cached */
    uint8_t icon;
    int8_t temp_c;
    uint8_t humidity;
    char text[28];
    char suggestion[24];
    char city[12];
    char updated[6];     /* "HH:MM" of last successful fetch */
    weather_hourly_t hourly[HOURLY_POINTS];
} weather_t;

typedef struct {
    char panel_name[PANEL_DISPLAY_NAME_MAX];
    uint8_t theme;           /* THEME_* */
    uint8_t city_index;
    bool weather_auto_bg;
    bool time_auto_theme;
    uint8_t pinned[PINNED_SCENE_MAX]; /* SC_* or 0xFF = empty slot */
} app_config_t;

typedef struct {
    char name[PANEL_CHANNEL_NAME_MAX];
    uint8_t kind;        /* CH_KIND_* */
    bool on;             /* light on/off state */
    bool triggered;      /* custom switch state */
    uint8_t switch_gpio;
    int8_t relay_gpio;   /* -1 = disabled */
} channel_t;

typedef struct {
    bool synced;         /* SNTP synced at least once */
    bool is_day;         /* 06:00..17:59 */
    char clock[6];       /* "HH:MM" */
    char date_label[32]; /* "10月5日 周一" */
    char greeting[24];   /* "晚上好，欢迎回家" */
} time_info_t;

typedef struct {
    channel_t channels[PANEL_CHANNEL_COUNT];
    app_config_t config;
    weather_t weather;
    time_info_t time;
    uint8_t lights_on;
    uint8_t lights_total; /* CH_LIGHT channels only; custom switch excluded */
    uint8_t active_scene; /* SC_* or 0xFF */
} app_snapshot_t;

typedef void (*app_state_listener_t)(const app_snapshot_t *snapshot, void *ctx);

esp_err_t app_state_init(void);

/* Bind the Kconfig-resolved GPIO numbers into the channel descriptions (boot-time only). */
void app_state_bind_gpio(const uint8_t switch_gpio[PANEL_CHANNEL_COUNT],
                         const int8_t relay_gpio[PANEL_CHANNEL_COUNT]);

/* Mutators commit, persist user settings and notify listeners. Call from any task. */
esp_err_t app_state_set_channel(uint8_t index, bool on, bool triggered);
esp_err_t app_state_set_config(const app_config_t *config);
typedef void (*app_config_mutator_t)(app_config_t *config, void *ctx);
esp_err_t app_state_update_config(app_config_mutator_t mutator, void *ctx); /* atomic read-modify-write */
esp_err_t app_state_set_weather(const weather_t *weather);
esp_err_t app_state_set_time_info(const time_info_t *info);
esp_err_t app_state_set_active_scene(uint8_t scene_id);

void app_state_get_snapshot(app_snapshot_t *out);
uint8_t app_state_channel_kind(uint8_t index); /* CH_KIND_* without copying the full snapshot */

/* Listeners run serially in the notifier task, never under the state mutex. */
esp_err_t app_state_subscribe(app_state_listener_t listener, void *ctx);

#ifdef __cplusplus
}
#endif
