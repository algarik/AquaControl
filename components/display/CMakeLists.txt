idf_component_register(
    SRCS
        "display_driver.cpp"
        "backlight.cpp"
        "lvgl_port.cpp"
    INCLUDE_DIRS "include" "../../main"
    REQUIRES
        driver
        esp_lcd
        esp_lvgl_port
        lvgl__lvgl
        logger
)
