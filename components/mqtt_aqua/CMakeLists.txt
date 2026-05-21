idf_component_register(
    SRCS "mqtt_client_aqua.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        logger
        devices
        history
        json
        mqtt
        storage
)
