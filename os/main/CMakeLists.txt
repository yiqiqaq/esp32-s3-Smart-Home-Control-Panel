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
    INCLUDE_DIRS "."
    REQUIRES esp_matter driver nvs_flash esp_timer esp_http_client json
             esp_wifi esp_netif esp_event lwip mbedtls)
