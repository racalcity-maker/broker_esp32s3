idf_component_register(SRCS "automation_engine.c" "automation_engine_context.c" "automation_engine_flags.c" "automation_engine_registry.c" "automation_engine_execution.c"
                       INCLUDE_DIRS "include"
                       REQUIRES device_model device_manager device_runtime audio_player mqtt_core event_bus)
