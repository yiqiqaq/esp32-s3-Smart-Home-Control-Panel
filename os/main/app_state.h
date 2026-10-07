#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PANEL_CHANNEL_COUNT 3
#define PANEL_LIGHT_COUNT 2
#define PANEL_AC_COUNT 2
#define PANEL_ROOM_COUNT 5
#define PANEL_DISPLAY_NAME_MAX 13
#define PANEL_CHANNEL_NAME_MAX 17
#define PINNED_SCENE_MAX 3
#define HOURLY_POINTS 4

/* Fixed room order, mirrors the prototype light/scene target order
 * [客厅,餐厅,卧室,走廊,阳台]. Remote lights synced by the family controller
 * reuse the same indices. */
extern const char *const kPanelRooms[PANEL_ROOM_COUNT];

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
    SC_HOME = 0,
    SC_REST,
    SC_AWAY,
    SC_MOVIE,
    SC_READ,
    SC_NIGHT,
    SC_COUNT, /* fixed set of six, shown in full on the home bar */
};

/* Manual-binding action: 0xFF = plain switch events only, otherwise SC_* —
 * a press on that channel runs the scene (场景键). */
#define PANEL_ACTION_EVENTS 0xFF

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

/* Every channel is a custom switch. Its binding decides what a press does:
 * drive a bound device (light wall-switch / AC power) or act as a manual
 * custom control that only reports Generic Switch events (plus the local
 * action hook). */
enum {
    CH_BIND_MANUAL = 0, /* custom manual control */
    CH_BIND_LIGHT,      /* bound to a device as its switch */
};

enum {
    DEV_LIGHT = 0, /* channel_dev[] indexes lights[] */
    DEV_AC,        /* channel_dev[] indexes acs[] */
};

enum {
    AC_MODE_COOL = 0,
    AC_MODE_HEAT,
};

enum {
    AC_FAN_AUTO = 0,
    AC_FAN_LOW,
    AC_FAN_MID,
    AC_FAN_HIGH,
};

typedef struct {
    char name[PANEL_CHANNEL_NAME_MAX];
    bool on;
    uint8_t room;        /* kPanelRooms index */
    int8_t relay_gpio;   /* mains variant: relay in this light's circuit */
} light_t;

typedef struct {
    char name[PANEL_CHANNEL_NAME_MAX];
    bool on;
    uint8_t mode;        /* AC_MODE_* */
    uint8_t fan;         /* AC_FAN_* */
    int8_t temp_set;     /* setpoint, 16..30 degC */
    int8_t temp_now;     /* reported room temperature */
    uint8_t room;        /* kPanelRooms index */
} ac_t;

typedef struct {
    char name[PANEL_CHANNEL_NAME_MAX];
    bool triggered;      /* manual mode: momentary state (已触发/待触发) */
    uint8_t switch_gpio;
    int8_t relay_gpio;   /* mains variant: per-key relay; follows the bound light */
} channel_t;

typedef struct {
    char panel_name[PANEL_DISPLAY_NAME_MAX];
    uint8_t theme;           /* THEME_* */
    uint8_t city_index;
    bool weather_auto_bg;
    bool time_auto_theme;
    uint8_t pinned[PINNED_SCENE_MAX]; /* SC_* or 0xFF = empty slot */
    uint8_t channel_binding[PANEL_CHANNEL_COUNT];      /* CH_BIND_* */
    uint8_t channel_dev_kind[PANEL_CHANNEL_COUNT];     /* DEV_LIGHT / DEV_AC */
    int8_t channel_dev[PANEL_CHANNEL_COUNT];           /* device index or -1 */
    uint8_t channel_action[PANEL_CHANNEL_COUNT];       /* PANEL_ACTION_EVENTS or SC_* */
    /* persisted AC state (temp_now is runtime-only, starts at temp_set) */
    bool ac_on[PANEL_AC_COUNT];
    uint8_t ac_mode[PANEL_AC_COUNT];
    uint8_t ac_fan[PANEL_AC_COUNT];
    int8_t ac_temp[PANEL_AC_COUNT];
} app_config_t;

typedef struct {
    bool synced;         /* SNTP synced at least once */
    bool is_day;         /* 06:00..17:59 */
    char clock[6];       /* "HH:MM" */
    char date_label[32]; /* "10月5日 周一" */
    char greeting[24];   /* "晚上好，欢迎回家" */
} time_info_t;

typedef struct {
    light_t lights[PANEL_LIGHT_COUNT];
    ac_t acs[PANEL_AC_COUNT];
    channel_t channels[PANEL_CHANNEL_COUNT];
    app_config_t config;
    weather_t weather;
    time_info_t time;
    uint8_t lights_on;
    uint8_t lights_total; /* PANEL_LIGHT_COUNT */
    uint8_t active_scene; /* SC_* or 0xFF */
} app_snapshot_t;

typedef void (*app_state_listener_t)(const app_snapshot_t *snapshot, void *ctx);

esp_err_t app_state_init(void);

/* Bind the Kconfig-resolved GPIO numbers into the channel descriptions (boot-time only). */
void app_state_bind_gpio(const uint8_t switch_gpio[PANEL_CHANNEL_COUNT],
                         const int8_t relay_gpio[PANEL_CHANNEL_COUNT]);

/* Mutators commit, persist user settings and notify listeners. Call from any task. */
esp_err_t app_state_set_light(uint8_t light_index, bool on);
esp_err_t app_state_set_ac(uint8_t ac_index, bool on, uint8_t mode, uint8_t fan, int8_t temp_set);
esp_err_t app_state_set_ac_room_temp(uint8_t ac_index, int8_t temp_now);
esp_err_t app_state_set_switch(uint8_t index, bool triggered);
esp_err_t app_state_set_config(const app_config_t *config);
typedef void (*app_config_mutator_t)(app_config_t *config, void *ctx);
esp_err_t app_state_update_config(app_config_mutator_t mutator, void *ctx); /* atomic read-modify-write */
esp_err_t app_state_set_weather(const weather_t *weather);
esp_err_t app_state_set_time_info(const time_info_t *info);
esp_err_t app_state_set_active_scene(uint8_t scene_id);

void app_state_get_snapshot(app_snapshot_t *out);

/* Listeners run serially in the notifier task, never under the state mutex. */
esp_err_t app_state_subscribe(app_state_listener_t listener, void *ctx);

#ifdef __cplusplus
}
#endif
