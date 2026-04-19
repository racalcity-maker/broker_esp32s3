idf_component_register(
    SRCS
        "runtime_service.c"
        "template_runtime.c"
        "runtime/dm_runtime_uid.c"
        "runtime/dm_runtime_signal.c"
        "runtime/dm_runtime_mqtt.c"
        "runtime/dm_runtime_flag.c"
        "runtime/dm_runtime_condition.c"
        "runtime/dm_runtime_interval.c"
        "runtime/dm_runtime_sequence.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES audio_player mqtt_core event_bus config_store device_model
)
