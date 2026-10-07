#include "scene_engine.h"
#include "app_state.h"
#include "channel_router.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "scenes";

/* Exact copy of the prototype's states matrix (panel-preview.html runScene).
 * Six scenes, shown directly on the home bar; 夜灯 extends the prototype set. */
static const scene_def_t kScenes[SC_COUNT] = {
    {SC_HOME,  "⌂", "回家", "开启客厅、餐厅和走廊灯", {true,  true,  false, true,  false}},
    {SC_REST,  "☾", "休息", "只留卧室灯",             {false, false, true,  false, false}},
    {SC_AWAY,  "↗", "离家", "关闭所有灯具",           {false, false, false, false, false}},
    {SC_MOVIE, "▣", "观影", "只开启客厅主灯",         {true,  false, false, false, false}},
    {SC_READ,  "▤", "阅读", "只开启卧室主灯",         {false, false, true,  false, false}},
    {SC_NIGHT, "★", "夜灯", "卧室夜灯和走廊灯",       {false, false, true,  true,  false}},
};

esp_err_t scene_engine_run(uint8_t scene_id) {
    if (scene_id >= SC_COUNT) return ESP_ERR_INVALID_ARG;
    const scene_def_t *s = &kScenes[scene_id];
    /* Local lights 0/1 (客厅/餐厅); remote slots 2..4 need a family controller. */
    for (uint8_t light_idx = 0; light_idx < PANEL_LIGHT_COUNT; ++light_idx) {
        channel_router_set_light(light_idx, s->targets[light_idx], CH_SRC_SCENE);
    }
    app_state_set_active_scene(scene_id);
    ESP_LOGI(TAG, "scene '%s' executed", s->name);
    return ESP_OK;
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
