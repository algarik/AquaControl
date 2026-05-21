idf_component_register(
    SRCS
        "color_utils.cpp"
        "device_types.cpp"
        "relay_device.cpp"
        "pwm_device.cpp"
        "rgb_device.cpp"
        "device_manager.cpp"
    INCLUDE_DIRS "include" "../../main"
    REQUIRES
        drivers
        esp_timer
        logger
)
