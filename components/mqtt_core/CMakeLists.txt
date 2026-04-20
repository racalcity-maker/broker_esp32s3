idf_component_register(
    SRCS
        "mqtt_core.c"
        "mqtt_core_acl.c"
        "mqtt_core_bridge.c"
        "mqtt_core_packet.c"
        "mqtt_core_protocol.c"
        "mqtt_core_retain.c"
        "mqtt_core_server.c"
        "mqtt_core_session.c"
    INCLUDE_DIRS "include"
    REQUIRES event_bus config_store esp_event lwip esp_timer
)
