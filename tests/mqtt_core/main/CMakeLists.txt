set(TEST_SRCS
    "test_runner.c"
    "../../../components/mqtt_core/test/test_mqtt_core.c"
)

idf_component_register(
    SRCS ${TEST_SRCS}
    INCLUDE_DIRS "."
    PRIV_REQUIRES mqtt_core event_bus unity
)

