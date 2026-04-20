set(TEST_SRCS
    "test_runner.c"
    "test_template_runtime_integration.c"
)

idf_component_register(
    SRCS ${TEST_SRCS}
    INCLUDE_DIRS "."
    PRIV_REQUIRES device_manager device_runtime event_bus unity json
)

target_include_directories(${COMPONENT_LIB} PRIVATE
    "${CMAKE_SOURCE_DIR}/../../components/device_manager"
    "${CMAKE_SOURCE_DIR}/../../components/device_runtime"
)
