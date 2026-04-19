idf_component_register(
    SRCS "sd_storage.c"
    INCLUDE_DIRS "include"
    REQUIRES driver fatfs sdmmc event_bus
)
