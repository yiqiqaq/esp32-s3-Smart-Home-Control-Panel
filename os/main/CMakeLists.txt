idf_component_register(
    SRCS "app_main.cpp"
         "app_state.cpp"
         "app_nvs.cpp"
         "switch_inputs.cpp"
         "channel_router.cpp"
         "matter_nodes.cpp"
         "time_service.cpp"
         "weather_service.cpp"
         "theme_service.cpp"
         "scene_engine.cpp"
         "panel_services.cpp"
         "service_providers.cpp"
         "bsp_display.cpp"
         "bsp_audio.cpp"
         "voice_assistant.cpp"
         "service_http.cpp"
         "ui_voice.cpp"
         "ui_app.cpp"
         "ui_theme.cpp"
         "ui_home.cpp"
         "ui_settings.cpp"
         "fonts/font_panel_14.c"
         "fonts/font_panel_18.c"
         "fonts/font_panel_30.c"
         "fonts/font_cn_18.c"
    INCLUDE_DIRS "." "fonts"
    REQUIRES esp_matter driver nvs_flash esp_timer esp_http_client json
             esp_wifi esp_netif esp_event lwip mbedtls
             esp_lcd esp_lvgl_port lvgl esp_http_server)

# Generated LVGL font sources include "lvgl.h" directly (not "lvgl/lvgl.h")
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE=1)
