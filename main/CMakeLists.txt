idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES config_store network mqtt_core web_ui audio_player event_bus error_monitor
)
