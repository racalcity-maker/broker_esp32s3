idf_component_register(
    SRCS "mqtt_core.c"
    INCLUDE_DIRS "include"
    REQUIRES event_bus config_store esp_event lwip esp_timer automation_engine
)
