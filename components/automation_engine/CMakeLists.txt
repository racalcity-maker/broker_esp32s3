idf_component_register(SRCS "automation_engine.c"
                       INCLUDE_DIRS "include"
                       REQUIRES device_manager audio_player mqtt_core event_bus)
