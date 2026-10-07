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
#include "ui_app.h"
#include "bsp_audio.h"
#include "voice_assistant.h"
#include "service_http.h"
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
 * outputs -> time/weather services -> Matter data model -> providers -> touch
 * UI -> Matter start. Physical switch inputs compile in but stay disabled
 * unless PANEL_ENABLE_SWITCH_INPUTS is set; channel commands come from the
 * touch UI, scenes and Matter writes through the same router. */
extern "C" void app_main() {
    ESP_ERROR_CHECK(app_nvs_init());
    ESP_ERROR_CHECK(panel_services_init());
    ESP_ERROR_CHECK(app_state_init());
    ESP_ERROR_CHECK(channel_router_init(kSwitchGpios, kRelayGpios));
    ESP_ERROR_CHECK(time_service_init());
    ESP_ERROR_CHECK(weather_service_init());
    ESP_ERROR_CHECK(matter_nodes_init());
    ESP_ERROR_CHECK(service_providers_register());
    esp_err_t audio_err = bsp_audio_init();
    if (audio_err != ESP_OK) ESP_LOGW(TAG, "audio init failed: %s", esp_err_to_name(audio_err));
    ESP_ERROR_CHECK(voice_assistant_init());
    ESP_ERROR_CHECK(ui_app_init());
    esp_err_t http_err = service_http_start();
    if (http_err != ESP_OK) ESP_LOGW(TAG, "http service failed: %s", esp_err_to_name(http_err));
    ESP_ERROR_CHECK(esp_matter::start(nullptr));
    matter_nodes_stack_started();
#if CONFIG_PANEL_ENABLE_SWITCH_INPUTS
    ESP_ERROR_CHECK(switch_inputs_init(channel_router_on_switch_event, kSwitchGpios));
#endif
    ESP_LOGI(TAG, "3-channel Matter panel started (2 lights + generic switch), test mode %s",
             panel_services_test_mode() ? "on (outputs suppressed)" : "off");
}
