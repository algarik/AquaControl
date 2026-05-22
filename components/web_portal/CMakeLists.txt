idf_component_register(
    SRCS "web_portal.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        logger
        wifi
        devices
        sensors
        storage
        esp_http_server
    PRIV_REQUIRES
        esp_timer
        esp_wifi
        json
        nvs_flash
)
