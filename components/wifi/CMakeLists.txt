idf_component_register(
    SRCS "wifi_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        logger
        storage
        esp_wifi
        esp_netif
        esp_event
    PRIV_REQUIRES
        esp_timer
)
