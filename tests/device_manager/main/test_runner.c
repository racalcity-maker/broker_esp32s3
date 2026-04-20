#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern void register_device_manager_parse_tests(void);
extern void register_device_manager_integration_tests(void);
extern void register_runtime_pure_tests(void);

void setUp(void)
{
}

void tearDown(void)
{
}

static void unity_runner_task(void *arg)
{
    (void)arg;
    UNITY_BEGIN();
    register_device_manager_parse_tests();
    register_runtime_pure_tests();
    UNITY_END();
    vTaskDelete(NULL);
}

void app_main(void)
{
    BaseType_t ok = xTaskCreate(unity_runner_task,
                                "unity_runner",
                                16384,
                                NULL,
                                tskIDLE_PRIORITY + 1,
                                NULL);
    TEST_ASSERT_EQUAL(pdPASS, ok);
}
