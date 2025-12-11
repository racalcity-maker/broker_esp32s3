#include "unity.h"
#include "esp_err.h"
#include "event_bus.h"
#include "mqtt_core.h"

extern void register_mqtt_core_tests(void);
esp_err_t mqtt_core_test_init_helpers(void);

void app_main(void)
{
    UNITY_BEGIN();
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_init());
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_start());
    TEST_ASSERT_EQUAL(ESP_OK, mqtt_core_init());
    TEST_ASSERT_EQUAL(ESP_OK, mqtt_core_test_init_helpers());
    register_mqtt_core_tests();
    UNITY_END();
}
