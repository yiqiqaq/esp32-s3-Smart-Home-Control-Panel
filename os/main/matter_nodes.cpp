#include "matter_nodes.h"
#include "channel_router.h"
#include "app_state.h"
#include "esp_log.h"
#include "esp_check.h"
#include <esp_matter.h>
#include <esp_matter_endpoint.h>
#include <esp_matter_cluster.h>
#include <esp_matter_event.h>
#include <app-common/zap-generated/cluster-objects.h>
#include <string.h>

using namespace chip::app::Clusters;
using namespace esp_matter;
using namespace esp_matter::endpoint;

static const char *TAG = "matter_nodes";
static uint16_t s_endpoint_ids[3];

static esp_err_t attribute_update(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                  uint32_t attribute_id, esp_matter_attr_val_t *val, void *) {
    /* PRE_UPDATE carries the incoming write from a Matter client. */
    if (type != attribute::PRE_UPDATE || cluster_id != OnOff::Id ||
        attribute_id != OnOff::Attributes::OnOff::Id || !val) {
        return ESP_OK;
    }
    for (uint8_t i = 0; i < 3; ++i) {
        if (s_endpoint_ids[i] == endpoint_id) {
            channel_router_on_matter_write(i, val->val.b);
            break;
        }
    }
    return ESP_OK;
}

static esp_err_t create_light_endpoint(node_t *node, uint8_t slot, bool initial_on) {
    endpoint::on_off_light::config_t config{};
    config.on_off.on_off = initial_on;
    endpoint_t *endpoint = endpoint::on_off_light::create(node, &config, ENDPOINT_FLAG_NONE, nullptr);
    if (!endpoint) {
        ESP_LOGE(TAG, "on_off_light endpoint %u failed", slot + 1);
        return ESP_FAIL;
    }
    s_endpoint_ids[slot] = endpoint::get_id(endpoint);
    return ESP_OK;
}

static esp_err_t create_custom_switch_endpoint(node_t *node) {
    endpoint::generic_switch::config_t config{};
    config.switch_cluster.number_of_positions = 2;
    endpoint_t *endpoint = endpoint::generic_switch::create(node, &config, ENDPOINT_FLAG_NONE, nullptr);
    if (!endpoint) {
        ESP_LOGE(TAG, "generic_switch endpoint failed");
        return ESP_FAIL;
    }
    const uint16_t id = endpoint::get_id(endpoint);
    s_endpoint_ids[2] = id;

    /* Momentary features must be added explicitly; without them the switch
     * events below are rejected by the data model. */
    cluster_t *switch_cluster = cluster::get(endpoint, Switch::Id);
    if (!switch_cluster) return ESP_FAIL;
    esp_err_t err = cluster::switch_cluster::feature::momentary_switch::add(switch_cluster);
    if (err == ESP_OK) {
        err = cluster::switch_cluster::feature::action_switch::add(switch_cluster);
    }
    if (err != ESP_OK) ESP_LOGE(TAG, "switch feature add failed: %s", esp_err_to_name(err));
    return err;
}

esp_err_t matter_nodes_init(void) {
    node::config_t node_config{};
    node_t *node = node::create(&node_config, attribute_update, nullptr);
    if (!node) {
        ESP_LOGE(TAG, "Matter node creation failed");
        return ESP_FAIL;
    }
    memset(s_endpoint_ids, 0, sizeof(s_endpoint_ids));

    /* Boot state is off on all channels; relays follow (see channel_router). */
    ESP_RETURN_ON_ERROR(create_light_endpoint(node, 0, false), TAG, "ep1");
    ESP_RETURN_ON_ERROR(create_light_endpoint(node, 1, false), TAG, "ep2");
    ESP_RETURN_ON_ERROR(create_custom_switch_endpoint(node), TAG, "ep3");

    ESP_LOGI(TAG, "endpoints %u/%u lights, %u generic switch",
             s_endpoint_ids[0], s_endpoint_ids[1], s_endpoint_ids[2]);
    return ESP_OK;
}

uint16_t matter_nodes_endpoint_id(uint8_t index) {
    return index < 3 ? s_endpoint_ids[index] : 0;
}

static bool is_light(uint8_t index) {
    return app_state_channel_kind(index) == CH_KIND_LIGHT;
}

esp_err_t matter_nodes_set_onoff(uint8_t index, bool on) {
    if (index >= 3 || !is_light(index)) return ESP_ERR_INVALID_ARG;
    if (!s_endpoint_ids[index]) return ESP_ERR_INVALID_STATE;
    esp_matter_attr_val_t value = esp_matter_bool(on);
    return attribute::update(s_endpoint_ids[index], OnOff::Id, OnOff::Attributes::OnOff::Id, &value);
}

static esp_err_t report_switch_event(uint8_t index, esp_err_t (*send)(chip::EndpointId, uint8_t), uint8_t position) {
    if (index >= 3 || is_light(index)) return ESP_ERR_INVALID_ARG;
    if (!s_endpoint_ids[index]) return ESP_ERR_INVALID_STATE;
    return send(s_endpoint_ids[index], position);
}

esp_err_t matter_nodes_report_initial_press(uint8_t index) {
    return report_switch_event(index, cluster::switch_cluster::event::send_initial_press, 1);
}

esp_err_t matter_nodes_report_short_release(uint8_t index) {
    return report_switch_event(index, cluster::switch_cluster::event::send_short_release, 0);
}

esp_err_t matter_nodes_report_latched(uint8_t index, uint8_t new_position) {
    return report_switch_event(index, cluster::switch_cluster::event::send_switch_latched, new_position);
}
