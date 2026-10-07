#pragma once
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* XiaoAI-style voice assistant overlay: a full-screen layer (lv_layer_top)
 * with a breathing orb, live status ("在听/思考中/回复") and the spoken reply.
 * Opened by the microphone button on the home top bar; tap the orb to finish
 * a query, backdrop or close button cancels. */

esp_err_t ui_voice_init(void);

/* Mic button placed in the home top bar (ui_home calls this while building). */
void ui_voice_attach_button(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif
