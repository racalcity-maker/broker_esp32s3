idf_component_register(
    SRCS
        "device_manager.c"
        "device_manager_parse.c"
        "device_manager_validate.c"
        "device_manager_export.c"
        "profiles/dm_profiles.c"
        "storage/dm_storage.c"
        "template_applier.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES nvs_flash json event_bus device_model device_runtime sd_storage
)
