idf_component_register(
    SRCS
        "theme.cpp"
        "i18n.cpp"
        "boot_screen.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        lvgl__lvgl
        esp_lvgl_port
        logger
)
