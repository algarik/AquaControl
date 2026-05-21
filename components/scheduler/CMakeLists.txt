idf_component_register(
    SRCS
        "scheduler.cpp"
    INCLUDE_DIRS "include" "../../main"
    REQUIRES
        devices
        triggers
        time_mgr
        logger
        freertos
        mqtt_aqua
        sensors
        faults
)
