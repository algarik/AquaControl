idf_component_register(
    SRCS
        "nvs_store.cpp"
        "system_config.cpp"
        "config_storage.cpp"
    INCLUDE_DIRS "include" "../../main"
    REQUIRES
        nvs_flash
        logger
        json
        devices
        triggers
        drivers
)
