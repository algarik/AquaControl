idf_component_register(
    SRCS
        "pcf8575.cpp"
        "pca9685.cpp"
        "ds1307.cpp"
        "sht3x.cpp"
    INCLUDE_DIRS "include" "../../main"
    REQUIRES
        driver
        i2c_bus
        logger
)
