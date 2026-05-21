idf_component_register(
    SRCS "ntp_sync.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        logger
        time_mgr
        history
        lwip
)
