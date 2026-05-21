idf_component_register(
    SRCS
        "touch_gt911.cpp"
    INCLUDE_DIRS "include" "../../main"
    REQUIRES
        i2c_bus
        esp_lcd
        esp_lvgl_port
        espressif__esp_lcd_touch_gt911
        lvgl__lvgl
        logger
        display
        activity
)
