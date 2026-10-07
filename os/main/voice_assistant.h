#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Cloud voice assistant: the panel records a query, POSTs it to the
 * user-provided API (URL/key set from the phone via the panel's HTTP
 * service) and executes the action the API returns. The assistant can
 * toggle lights, run scenes, re-bind channels and drive music playback.
 *
 * Contract (SERVICE_API.md "语音助手"):
 *   request  POST api_url, Content-Type: audio/wav (16k/16-bit/mono),
 *            header X-Api-Key. A JSON body (application/json) with
 *            {"cmd": "play|pause|next|prev"} or {"volume": N} is a music
 *            command instead of a query.
 *   response {"reply": " spoken text",
 *             "tts_url": "optional wav to play",
 *             "action": {"type": "light|scene|bind|music", ...}} */

typedef enum {
    VOICE_IDLE = 0,
    VOICE_RECORDING,
    VOICE_THINKING,
    VOICE_SPEAKING,
    VOICE_ERROR,
} voice_state_t;

typedef void (*voice_state_cb_t)(voice_state_t state, const char *text, void *ctx);

esp_err_t voice_assistant_init(void);
bool voice_assistant_configured(void); /* api_url set */
esp_err_t voice_assistant_set_config(const char *api_url, const char *api_key);
esp_err_t voice_assistant_get_config(char *api_url, size_t url_len);

voice_state_t voice_assistant_state(void);
const char *voice_assistant_text(void); /* last reply / error (static buffer) */

/* UI hook: called from the voice task whenever state or text changes. */
void voice_assistant_on_state(voice_state_cb_t cb, void *ctx);

void voice_assistant_start(void);      /* begin capturing (async) */
void voice_assistant_finish(void);     /* stop capture, POST to the API */
void voice_assistant_cancel(void);

/* Media controls shown on the home screen; commands are forwarded to the API. */
void voice_assistant_music_cmd(const char *cmd); /* play|pause|next|prev */
void voice_assistant_music_set_volume(int volume);
bool voice_assistant_music_active(void);
const char *voice_assistant_music_title(void);
int voice_assistant_music_volume(void);

#ifdef __cplusplus
}
#endif
