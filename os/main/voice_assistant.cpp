#include "voice_assistant.h"
#include "bsp_audio.h"
#include "channel_router.h"
#include "scene_engine.h"
#include "app_state.h"
#include "app_nvs.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "voice";

#define VOICE_KEY "voic"
#define VOICE_MAGIC 0x564F4943 /* "VOIC" */
#define VOICE_VERSION 1
#define VOICE_URL_MAX 160
#define VOICE_KEY_MAX 64
#define VOICE_MAX_RECORD_MS 8000
#define VOICE_CHUNK_MS 250
#define VOICE_TEXT_MAX 128
#define VOICE_TITLE_MAX 48
#define VOICE_TASK_STACK (6 * 1024)

typedef enum {
    EVT_START,
    EVT_FINISH,
    EVT_CANCEL,
    EVT_MUSIC_CMD,
    EVT_MUSIC_VOLUME,
} voice_evt_t;

static char s_music_cmd[12];

typedef struct {
    char url[VOICE_URL_MAX];
    char key[VOICE_KEY_MAX];
} voice_config_t;

static voice_config_t s_cfg;
static SemaphoreHandle_t s_lock;
static QueueHandle_t s_queue;
static voice_state_t s_state = VOICE_IDLE;
static char s_text[VOICE_TEXT_MAX];
static volatile bool s_cancel_req;
static volatile bool s_finish_req;

/* media state */
static bool s_music_active;
static char s_music_title[VOICE_TITLE_MAX];
static int s_music_volume = 70;

static voice_state_cb_t s_cb;
static void *s_cb_ctx;
static bool s_stack_ready;

static void set_state_locked(voice_state_t st, const char *text) {
    s_state = st;
    if (text) strlcpy(s_text, text, sizeof(s_text));
}

static void notify(voice_state_t st, const char *text) {
    if (s_cb) s_cb(st, text, s_cb_ctx);
}

static void set_state(voice_state_t st, const char *text) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    set_state_locked(st, text);
    xSemaphoreGive(s_lock);
    notify(st, text);
}

voice_state_t voice_assistant_state(void) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    voice_state_t st = s_state;
    xSemaphoreGive(s_lock);
    return st;
}

const char *voice_assistant_text(void) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    static char out[VOICE_TEXT_MAX]; /* single UI consumer; timer copies at once */
    strlcpy(out, s_text, sizeof(out));
    xSemaphoreGive(s_lock);
    return out;
}

bool voice_assistant_music_active(void) { return s_music_active; }
const char *voice_assistant_music_title(void) { return s_music_title; }
int voice_assistant_music_volume(void) { return s_music_volume; }

/* ---------------- config ---------------- */

bool voice_assistant_configured(void) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool ok = s_cfg.url[0] != '\0';
    xSemaphoreGive(s_lock);
    return ok;
}

esp_err_t voice_assistant_set_config(const char *api_url, const char *api_key) {
    if (!api_url) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(s_cfg.url, api_url, sizeof(s_cfg.url));
    if (api_key) strlcpy(s_cfg.key, api_key, sizeof(s_cfg.key));
    voice_config_t cfg = s_cfg;
    xSemaphoreGive(s_lock);
    return app_nvs_save(VOICE_KEY, &cfg, sizeof(cfg), VOICE_MAGIC, VOICE_VERSION);
}

esp_err_t voice_assistant_get_config(char *api_url, size_t url_len) {
    if (!api_url) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(api_url, s_cfg.url, url_len);
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

/* ---------------- action dispatch ---------------- */

static void apply_action(const cJSON *action) {
    const cJSON *type = cJSON_GetObjectItem(action, "type");
    if (!cJSON_IsString(type)) return;
    if (strcmp(type->valuestring, "light") == 0) {
        const cJSON *light = cJSON_GetObjectItem(action, "light");
        const cJSON *on = cJSON_GetObjectItem(action, "on");
        if (cJSON_IsNumber(light) && cJSON_IsBool(on)) {
            channel_router_set_light((uint8_t)(light->valueint - 1),
                                     cJSON_IsTrue(on), CH_SRC_LOCAL);
        }
    } else if (strcmp(type->valuestring, "scene") == 0) {
        const cJSON *scene = cJSON_GetObjectItem(action, "scene");
        if (cJSON_IsNumber(scene) && scene->valueint >= 0 && scene->valueint < SC_COUNT) {
            scene_engine_run((uint8_t)scene->valueint);
        }
    } else if (strcmp(type->valuestring, "bind") == 0) {
        const cJSON *ch = cJSON_GetObjectItem(action, "channel");
        const cJSON *mode = cJSON_GetObjectItem(action, "mode");
        if (!cJSON_IsNumber(ch) || ch->valueint < 1 || ch->valueint > PANEL_CHANNEL_COUNT) return;
        struct ctx {
            uint8_t idx;
            uint8_t kind;
            int8_t dev;
            uint8_t act;
        } c = {(uint8_t)(ch->valueint - 1), DEV_LIGHT, -1, PANEL_ACTION_EVENTS};
        if (cJSON_IsString(mode) && strcmp(mode->valuestring, "light") == 0) {
            const cJSON *light = cJSON_GetObjectItem(action, "light");
            if (!cJSON_IsNumber(light) || light->valueint < 1 || light->valueint > PANEL_LIGHT_COUNT) return;
            c.kind = DEV_LIGHT;
            c.dev = (int8_t)(light->valueint - 1);
        } else if (cJSON_IsString(mode) && strcmp(mode->valuestring, "manual") == 0) {
            c.kind = DEV_LIGHT; /* unused for manual */
            const cJSON *act = cJSON_GetObjectItem(action, "action_scene");
            if (cJSON_IsNumber(act) && act->valueint >= 0 && act->valueint < SC_COUNT) {
                c.act = (uint8_t)act->valueint;
            }
        } else {
            return; /* unknown mode */
        }
        app_state_update_config([](app_config_t *cfg, void *v) {
            auto *p = (struct ctx *)v;
            cfg->channel_binding[p->idx] = p->dev >= 0 ? CH_BIND_LIGHT : CH_BIND_MANUAL;
            cfg->channel_dev_kind[p->idx] = p->kind;
            cfg->channel_dev[p->idx] = p->dev;
            cfg->channel_action[p->idx] = p->dev >= 0 ? PANEL_ACTION_EVENTS : p->act;
        }, &c);
    } else if (strcmp(type->valuestring, "ac") == 0) {
        const cJSON *ac = cJSON_GetObjectItem(action, "ac");
        if (!cJSON_IsNumber(ac) || ac->valueint < 1 || ac->valueint > PANEL_AC_COUNT) return;
        const uint8_t idx = (uint8_t)(ac->valueint - 1);
        app_snapshot_t snap;
        app_state_get_snapshot(&snap);
        bool on = snap.acs[idx].on;
        uint8_t mode = snap.acs[idx].mode;
        uint8_t fan = snap.acs[idx].fan;
        int8_t temp = snap.acs[idx].temp_set;
        const cJSON *j_on = cJSON_GetObjectItem(action, "on");
        const cJSON *j_mode = cJSON_GetObjectItem(action, "mode");
        const cJSON *j_fan = cJSON_GetObjectItem(action, "fan");
        const cJSON *j_temp = cJSON_GetObjectItem(action, "temp");
        const cJSON *j_now = cJSON_GetObjectItem(action, "temp_now");
        if (cJSON_IsBool(j_on)) on = cJSON_IsTrue(j_on);
        if (cJSON_IsString(j_mode) && strcmp(j_mode->valuestring, "heat") == 0) mode = AC_MODE_HEAT;
        if (cJSON_IsString(j_mode) && strcmp(j_mode->valuestring, "cool") == 0) mode = AC_MODE_COOL;
        if (cJSON_IsString(j_fan)) {
            if (strcmp(j_fan->valuestring, "auto") == 0) fan = AC_FAN_AUTO;
            else if (strcmp(j_fan->valuestring, "low") == 0) fan = AC_FAN_LOW;
            else if (strcmp(j_fan->valuestring, "mid") == 0) fan = AC_FAN_MID;
            else if (strcmp(j_fan->valuestring, "high") == 0) fan = AC_FAN_HIGH;
        }
        if (cJSON_IsNumber(j_temp) && j_temp->valueint >= 16 && j_temp->valueint <= 30) {
            temp = (int8_t)j_temp->valueint;
        }
        app_state_set_ac(idx, on, mode, fan, temp);
        if (cJSON_IsNumber(j_now)) {
            app_state_set_ac_room_temp(idx, (int8_t)j_now->valueint);
        }
    } else if (strcmp(type->valuestring, "music") == 0) {
        const cJSON *playing = cJSON_GetObjectItem(action, "playing");
        const cJSON *title = cJSON_GetObjectItem(action, "title");
        const cJSON *vol = cJSON_GetObjectItem(action, "volume");
        if (cJSON_IsBool(playing)) s_music_active = cJSON_IsTrue(playing);
        if (cJSON_IsString(title)) strlcpy(s_music_title, title->valuestring, sizeof(s_music_title));
        if (cJSON_IsNumber(vol)) {
            s_music_volume = vol->valueint;
            bsp_audio_set_volume(s_music_volume);
        }
        notify(voice_assistant_state(), NULL); /* let the UI refresh the media bar */
    }
}

/* ---------------- HTTP ---------------- */

/* POST a JSON command (music control) and apply the reply action. */
static void post_command_json(const char *body) {
    if (s_cfg.url[0] == '\0') return;
    char url[VOICE_URL_MAX];
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(url, s_cfg.url, sizeof(url));
    xSemaphoreGive(s_lock);

    esp_http_client_config_t http_cfg = {};
    http_cfg.url = url;
    http_cfg.timeout_ms = 8000;
    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) return;
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (s_cfg.key[0]) esp_http_client_set_header(client, "X-Api-Key", s_cfg.key);
    esp_err_t err = esp_http_client_open(client, strlen(body));
    if (err == ESP_OK) {
        esp_http_client_write(client, body, strlen(body));
        char buf[1024];
        int len = esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        int total = 0;
        if (len >= 0 || status > 0) {
            int n;
            while ((n = esp_http_client_read(client, buf + total, sizeof(buf) - 1 - total)) > 0) {
                total += n;
                if (total >= (int)sizeof(buf) - 1) break;
            }
        }
        buf[total] = '\0';
        if (status >= 200 && status < 300 && total > 0) {
            cJSON *root = cJSON_Parse(buf);
            if (root) {
                const cJSON *action = cJSON_GetObjectItem(root, "action");
                if (cJSON_IsObject(action)) apply_action(action);
                cJSON_Delete(root);
            }
        } else {
            ESP_LOGW(TAG, "music cmd http %d", status);
        }
    } else {
        ESP_LOGW(TAG, "music cmd open failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

/* POST the recorded WAV, parse reply + action; optional tts_url is streamed
 * back through the speaker (contract: 16k/16-bit/mono WAV). */
static void post_query(const uint8_t *pcm, size_t bytes) {
    set_state(VOICE_THINKING, NULL);
    if (s_cfg.url[0] == '\0') {
        set_state(VOICE_ERROR, "语音服务未配置");
        return;
    }

    esp_http_client_config_t http_cfg = {};
    http_cfg.url = s_cfg.url;
    http_cfg.timeout_ms = 15000;
    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        set_state(VOICE_ERROR, "服务异常");
        return;
    }
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "audio/wav");
    if (s_cfg.key[0]) esp_http_client_set_header(client, "X-Api-Key", s_cfg.key);

    /* 44-byte canonical WAV header for 16k/16-bit/mono PCM */
    uint8_t wav_header[44] = "RIFF----WAVEfmt ----D----";
    uint32_t data_len = (uint32_t)bytes;
    uint32_t riff_len = data_len + 36;
    uint32_t u32 = 16;
    uint16_t u16_pcm = 1, u16_mono = 1, u16_align = 2, u16_bits = 16;
    uint32_t rate = BSP_AUDIO_SAMPLE_RATE, byte_rate = BSP_AUDIO_SAMPLE_RATE * 2;
    memcpy(wav_header + 4, &riff_len, 4);
    memcpy(wav_header + 16, &u32, 4);
    memcpy(wav_header + 20, &u16_pcm, 2);       /* PCM */
    memcpy(wav_header + 22, &u16_mono, 2);      /* mono */
    memcpy(wav_header + 24, &rate, 4);
    memcpy(wav_header + 28, &byte_rate, 4);
    memcpy(wav_header + 32, &u16_align, 2);
    memcpy(wav_header + 34, &u16_bits, 2);
    memcpy(wav_header + 40, &data_len, 4);

    esp_err_t err = esp_http_client_open(client, sizeof(wav_header) + (int)bytes);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        set_state(VOICE_ERROR, "服务异常");
        return;
    }
    esp_http_client_write(client, (const char *)wav_header, sizeof(wav_header));
    /* stream the record buffer in chunks to keep the stack flat */
    const uint8_t *p = pcm;
    size_t left = bytes;
    while (left > 0) {
        size_t chunk = left > 4096 ? 4096 : left;
        if (esp_http_client_write(client, (const char *)p, chunk) <= 0) break;
        p += chunk;
        left -= chunk;
    }

    char buf[2048];
    int status = esp_http_client_get_status_code(client);
    int len = esp_http_client_fetch_headers(client);
    int total = 0;
    if (len >= 0 || status > 0) {
        int n;
        while ((n = esp_http_client_read(client, buf + total, sizeof(buf) - 1 - total)) > 0) {
            total += n;
            if (total >= (int)sizeof(buf) - 1) break;
        }
    }
    buf[total] = '\0';
    esp_http_client_cleanup(client);

    if (status < 200 || status >= 300 || total == 0) {
        ESP_LOGW(TAG, "query http %d (%d bytes)", status, total);
        set_state(VOICE_ERROR, "服务异常");
        return;
    }

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        set_state(VOICE_ERROR, "服务异常");
        return;
    }
    const cJSON *reply = cJSON_GetObjectItem(root, "reply");
    const cJSON *tts = cJSON_GetObjectItem(root, "tts_url");
    const cJSON *action = cJSON_GetObjectItem(root, "action");

    char reply_text[VOICE_TEXT_MAX] = "完成";
    if (cJSON_IsString(reply) && reply->valuestring[0]) {
        strlcpy(reply_text, reply->valuestring, sizeof(reply_text));
    }
    if (cJSON_IsObject(action)) apply_action(action);
    set_state(VOICE_THINKING, reply_text);

    if (cJSON_IsString(tts) && tts->valuestring[0]) {
        set_state(VOICE_SPEAKING, reply_text);
        /* stream the TTS wav (16k/16-bit/mono) to the speaker */
        esp_http_client_config_t tts_cfg = {};
        tts_cfg.url = tts->valuestring;
        tts_cfg.timeout_ms = 15000;
        esp_http_client_handle_t tc = esp_http_client_init(&tts_cfg);
        if (tc) {
            esp_http_client_set_header(tc, "X-Api-Key", s_cfg.key);
            if (esp_http_client_open(tc, 0) == ESP_OK) {
                esp_http_client_fetch_headers(tc);
                uint8_t chunk[4096];
                bool header_skipped = false;
                int n;
                while ((n = esp_http_client_read(tc, (char *)chunk, sizeof(chunk))) > 0) {
                    int off = 0;
                    if (!header_skipped) {
                        /* skip the 44-byte canonical wav header */
                        off = n > 44 ? 44 : n;
                        header_skipped = true;
                    }
                    if (n > off) bsp_audio_play(chunk + off, n - off);
                }
                esp_http_client_cleanup(tc);
            } else {
                esp_http_client_cleanup(tc);
            }
        }
    }
    cJSON_Delete(root);
    set_state(VOICE_IDLE, reply_text);
}

/* ---------------- task ---------------- */

static void voice_task(void *) {
    static uint8_t *record_buf = NULL;
    for (;;) {
        voice_evt_t evt;
        if (xQueueReceive(s_queue, &evt, portMAX_DELAY) != pdTRUE) continue;
        switch (evt) {
        case EVT_START: {
            if (!bsp_audio_ready()) {
                set_state(VOICE_ERROR, "服务异常");
                break;
            }
            if (!record_buf) record_buf = (uint8_t *)heap_caps_malloc(
                BSP_AUDIO_MS_TO_BYTES(VOICE_MAX_RECORD_MS) * 2, /* stereo capture */
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!record_buf) {
                set_state(VOICE_ERROR, "服务异常");
                break;
            }
            s_cancel_req = false;
            s_finish_req = false;
            set_state(VOICE_RECORDING, NULL);
            size_t total = 0; /* stereo bytes captured */
            const size_t chunk = BSP_AUDIO_MS_TO_BYTES(VOICE_CHUNK_MS) * 2;
            int64_t start = esp_timer_get_time();
            while (total < BSP_AUDIO_MS_TO_BYTES(VOICE_MAX_RECORD_MS) * 2 && !s_cancel_req &&
                   !s_finish_req) {
                if (bsp_audio_record(record_buf + total, chunk, 500) == ESP_OK) {
                    total += chunk;
                }
                /* auto-finish on silence is the API's job; hard stop at max */
                if ((esp_timer_get_time() - start) / 1000 > VOICE_MAX_RECORD_MS + 500) break;
            }
            xSemaphoreTake(s_lock, portMAX_DELAY);
            bool cancelled = s_cancel_req;
            xSemaphoreGive(s_lock);
            if (cancelled) {
                set_state(VOICE_IDLE, NULL);
                break;
            }
            /* downmix stereo -> mono in place (L+R)/2, halving the length */
            const int16_t *src = (const int16_t *)record_buf;
            int16_t *dst = (int16_t *)record_buf;
            size_t frames = total / 4;
            for (size_t i = 0; i < frames; ++i) {
                dst[i] = (int16_t)(((int32_t)src[i * 2] + src[i * 2 + 1]) / 2);
            }
            post_query(record_buf, frames * 2);
            break;
        }
        case EVT_FINISH:
            s_finish_req = true; /* stop capture, then post the query */
            break;
        case EVT_CANCEL:
            s_cancel_req = true;
            set_state(VOICE_IDLE, NULL);
            break;
        case EVT_MUSIC_CMD: {
            char cmd[12];
            xSemaphoreTake(s_lock, portMAX_DELAY);
            strlcpy(cmd, s_music_cmd, sizeof(cmd));
            xSemaphoreGive(s_lock);
            char body[64];
            snprintf(body, sizeof(body), "{\"cmd\":\"%s\"}", cmd);
            post_command_json(body);
            break;
        }
        case EVT_MUSIC_VOLUME: {
            char body[32];
            xSemaphoreTake(s_lock, portMAX_DELAY);
            int vol = s_music_volume;
            xSemaphoreGive(s_lock);
            snprintf(body, sizeof(body), "{\"volume\":%d}", vol);
            post_command_json(body);
            break;
        }
        }
    }
}

/* ---------------- public API ---------------- */

void voice_assistant_start(void) {
    voice_evt_t evt = EVT_START;
    xQueueSend(s_queue, &evt, 0);
}

void voice_assistant_finish(void) {
    voice_evt_t evt = EVT_FINISH;
    xQueueSend(s_queue, &evt, 0);
}

void voice_assistant_cancel(void) {
    voice_evt_t evt = EVT_CANCEL;
    xQueueSend(s_queue, &evt, 0);
}

void voice_assistant_music_cmd(const char *cmd) {
    if (!cmd) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(s_music_cmd, cmd, sizeof(s_music_cmd));
    xSemaphoreGive(s_lock);
    voice_evt_t evt = EVT_MUSIC_CMD;
    xQueueSend(s_queue, &evt, 0);
}

void voice_assistant_music_set_volume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_music_volume = volume;
    xSemaphoreGive(s_lock);
    bsp_audio_set_volume(volume);
    voice_evt_t evt = EVT_MUSIC_VOLUME;
    xQueueSend(s_queue, &evt, 0);
}

void voice_assistant_on_state(voice_state_cb_t cb, void *ctx) {
    s_cb = cb;
    s_cb_ctx = ctx;
}

esp_err_t voice_assistant_init(void) {
    s_lock = xSemaphoreCreateMutex();
    s_queue = xQueueCreate(4, sizeof(voice_evt_t));
    if (!s_lock || !s_queue) return ESP_ERR_NO_MEM;

    voice_config_t cfg = {};
    if (app_nvs_load(VOICE_KEY, &cfg, sizeof(cfg), VOICE_MAGIC, VOICE_VERSION) != ESP_OK) {
        app_nvs_save(VOICE_KEY, &cfg, sizeof(cfg), VOICE_MAGIC, VOICE_VERSION);
    }
    s_cfg = cfg;
    s_stack_ready = true;
    (void)s_stack_ready;
    if (xTaskCreatePinnedToCore(voice_task, "voice", VOICE_TASK_STACK, NULL, 4, NULL, 1) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "voice assistant ready, api %s", s_cfg.url[0] ? "configured" : "not configured");
    return ESP_OK;
}
