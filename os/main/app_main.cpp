#include "esp_log.h"
#include "esp_check.h"
#include "app_nvs.h"
#include "app_state.h"
#include "panel_services.h"
#include "switch_inputs.h"
#include "channel_router.h"
#include "matter_nodes.h"
#include "time_service.h"
#include "weather_service.h"
#include "scene_engine.h"
#include "service_providers.h"
#include "sdkconfig.h"
#include <esp_matter.h>

static const char *TAG = "home_panel";

static constexpr uint8_t kSwitchGpios[3] = {
    CONFIG_PANEL_SWITCH_1_GPIO, CONFIG_PANEL_SWITCH_2_GPIO, CONFIG_PANEL_SWITCH_3_GPIO,
};
static constexpr int8_t kRelayGpios[3] = {
    CONFIG_PANEL_RELAY_1_GPIO, CONFIG_PANEL_RELAY_2_GPIO, CONFIG_PANEL_RELAY_3_GPIO,
};

/* Boot order: storage -> service interface (loads test mode) -> state model ->
 * outputs -> time/weather services -> Matter data model -> providers -> inputs
 * -> Matter start. The input task starts after the endpoints exist so a press
 * can never route to an endpoint that is not there yet. */
extern "C" void app_main() {
    ESP_ERROR_CHECK(app_nvs_init());
    ESP_ERROR_CHECK(panel_services_init());
    ESP_ERROR_CHECK(app_state_init());
    ESP_ERROR_CHECK(channel_router_init(kSwitchGpios, kRelayGpios));
    ESP_ERROR_CHECK(time_service_init());
    ESP_ERROR_CHECK(weather_service_init());
    ESP_ERROR_CHECK(matter_nodes_init());
    ESP_ERROR_CHECK(service_providers_register());
    ESP_ERROR_CHECK(switch_inputs_init(channel_router_on_switch_event, kSwitchGpios));
    ESP_ERROR_CHECK(esp_matter::start(nullptr));
    ESP_LOGI(TAG, "3-channel Matter panel started (2 lights + generic switch), test mode %s",
             panel_services_test_mode() ? "on (outputs suppressed)" : "off");
}
