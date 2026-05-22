idf_component_register(
    SRCS "time_manager.cpp"
    INCLUDE_DIRS "include" "../../main"
    REQUIRES
        drivers
        logger
        solar
        faults
)
