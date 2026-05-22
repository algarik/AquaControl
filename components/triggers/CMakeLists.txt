idf_component_register(
    SRCS
        "trigger_types.cpp"
        "schedule_trigger.cpp"
        "solar_trigger.cpp"
        "temp_trigger.cpp"
        "temp_map_trigger.cpp"
        "trigger_manager.cpp"
    INCLUDE_DIRS "include" "../../main"
    REQUIRES
        time_mgr
        logger
)
