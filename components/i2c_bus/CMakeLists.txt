idf_component_register(
    SRCS
        "i2c_bus.cpp"
        "i2c_scanner.cpp"
        "i2c_watchdog.cpp"
    INCLUDE_DIRS "include" "../../main"
    REQUIRES
        driver
        logger

)
