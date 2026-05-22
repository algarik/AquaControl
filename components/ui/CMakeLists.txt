idf_component_register(
    SRCS
        "theme.cpp"
        "i18n.cpp"
        "boot_screen.cpp"
        "screen_manager.cpp"
        "ui_context.cpp"
        "dashboard.cpp"
        "dim_manager.cpp"
        "chrome.cpp"
        "menu_screen.cpp"
        "placeholder_screen.cpp"
        "device_list_screen.cpp"
        "device_detail_screen.cpp"
        "add_device_screen.cpp"
        "drum_roller.cpp"
        "network_settings_screen.cpp"
        "time_location_screen.cpp"
        "display_system_screen.cpp"
        "system_status_screen.cpp"
        "triggers_screen.cpp"
        "sensors_settings_screen.cpp"
        "wizard.cpp"
        "screensaver_screen.cpp"
        "fonts/roboto_14.c"
        "fonts/roboto_18.c"
        "fonts/roboto_24.c"
        "fonts/roboto_32.c"
        "fonts/roboto_48.c"
    INCLUDE_DIRS "include"
    REQUIRES
        lvgl__lvgl
        esp_lvgl_port
        logger
        activity
        faults
        sensors
        solar
        time_mgr
        storage
        devices
        triggers
        validators
        scheduler
        display
        wifi
        mqtt_aqua
        rtc
        app_update
        nvs_flash
)

# lv_font_conv emits `#include "lvgl/lvgl.h"` by default; tell it to use the
# canonical `#include "lvgl.h"` path that ESP-IDF's lvgl component exposes.
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
