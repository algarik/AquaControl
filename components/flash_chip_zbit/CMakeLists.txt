idf_component_register(SRCS "spi_flash_chip_zbit.c"
                            "chip_drivers.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES spi_flash
                       LDFRAGMENTS linker.lf)

idf_component_add_link_dependency(FROM spi_flash)
