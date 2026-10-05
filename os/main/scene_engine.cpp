#include "scene_engine.h"
#include "app_state.h"
#include "channel_router.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "scenes";

/* Exact copy of the prototype's states matrix (panel-preview.html runScene). */
static const scene_def_t kScenes[SC_COUNT] = {
    {SC_HOME,  "⌂", "回家", "开启客厅、餐厅和走廊灯", {true,  true,  false, true,  false}},
    {SC_REST,  "☾", "休息", "只留卧室灯",             {false, false, true,  false, false}},
    {SC_AWAY,  "↗", "离家", "关闭所有灯具",           {false, false, false, false, false}},
    {SC_MOVIE, "▣", "观影", "只开启客厅主灯",         {true,  false, false, false, false}},
    {SC_READ,  "▤", "阅读", "只开启卧室主灯",         {false, false, true,  false, false}},
};

esp_err_t scene_engine_run(uint8_t scene_id) {
    if (scene_id >= SC_COUNT) return ESP_ERR_INVALID_ARG;
    const scene_def_t *s = &kScenes[scene_id];
    /* Local channels 0/1 are lights; remote slots 2..4 need a family controller. */
    for (uint8_t ch = 0; ch < 2; ++ch) {
        channel_router_set_channel(ch, s->targets[ch], CH_SRC_SCENE);
    }
    app_state_set_active_scene(scene_id);
    ESP_LOGI(TAG, "scene '%s' executed", s->name);
    return ESP_OK;
}

esp_err_t scene_engine_set_pinned(uint8_t scene_id, bool pinned) {
    if (scene_id >= SC_COUNT) return ESP_ERR_INVALID_ARG;

    struct ctx {
        uint8_t id;
        bool on;
        bool overflow;
    } c = {scene_id, pinned, false};

    esp_err_t err = app_state_update_config([](app_config_t *cfg, void *v) {
        auto *p = (struct ctx *)v;
        uint8_t slot = PINNED_SCENE_MAX;
        uint8_t free_slot = PINNED_SCENE_MAX;
        for (uint8_t i = 0; i < PINNED_SCENE_MAX; ++i) {
            if (cfg->pinned[i] == p->id) slot = i;
            if (cfg->pinned[i] == 0xFF && free_slot == PINNED_SCENE_MAX) free_slot = i;
        }
        if (p->on) {
            if (slot == PINNED_SCENE_MAX) {
                if (free_slot == PINNED_SCENE_MAX) {
                    p->overflow = true; /* already 3 pinned */
                    return;
                }
                cfg->pinned[free_slot] = p->id;
            }
        } else if (slot != PINNED_SCENE_MAX) {
            cfg->pinned[slot] = 0xFF;
        }
    }, &c);

    if (err != ESP_OK) return err;
    return c.overflow ? ESP_ERR_INVALID_STATE : ESP_OK;
}

const scene_def_t *scene_engine_all(uint8_t *count) {
    if (count) *count = SC_COUNT;
    return kScenes;
}

uint8_t scene_engine_active(void) {
    app_snapshot_t snap;
    app_state_get_snapshot(&snap);
    return snap.active_scene;
}
