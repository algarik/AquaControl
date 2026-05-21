idf_component_register(
    SRCS "trigger_validator.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        logger
        devices
        triggers
        storage
)
