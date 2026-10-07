#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Scene definitions mirror the prototype's states matrix. targets[] follow the
 * prototype light order: [客厅,餐厅,卧室,走廊,阳台].
 * Firmware executes only its own two lights; the remote lights stay the family
 * controller's job (see README "全屋统计" limitation). The set is capped at
 * six scenes — the home bar shows all of them directly (no scene library). */

typedef struct {
    uint8_t id;          /* SC_* */
    const char *icon;    /* unicode glyph */
    const char *name;
    const char *desc;
    bool targets[5];
} scene_def_t;

const scene_def_t *scene_engine_all(uint8_t *count);
esp_err_t scene_engine_run(uint8_t scene_id);        /* applies targets and marks the scene active */
uint8_t scene_engine_active(void);

#ifdef __cplusplus
}
#endif
