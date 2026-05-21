idf_component_register(
    SRCS
        "log_ring.cpp"
        "chip_info.cpp"
    INCLUDE_DIRS "include" "../../main"
    REQUIRES
        "log"
        "esp_system"
        "app_update"
        "spi_flash"
)
