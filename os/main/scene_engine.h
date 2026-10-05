#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Scene definitions mirror the prototype's states matrix. targets[] follow the
 * prototype light order: [channel1, channel2, bedroom, corridor, balcony].
 * Firmware executes only its own two light channels; the three remote lights
 * stay the family controller's job (see README "全屋统计" limitation). */

typedef struct {
    uint8_t id;          /* SC_* */
    const char *icon;    /* unicode glyph */
    const char *name;
    const char *desc;
    bool targets[5];
} scene_def_t;

const scene_def_t *scene_engine_all(uint8_t *count);
esp_err_t scene_engine_run(uint8_t scene_id);        /* applies targets and marks the scene active */
esp_err_t scene_engine_set_pinned(uint8_t scene_id, bool pinned); /* persists, max 3 */
uint8_t scene_engine_active(void);

#ifdef __cplusplus
}
#endif
