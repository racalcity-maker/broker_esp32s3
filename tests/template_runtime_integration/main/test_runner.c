#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern void register_template_runtime_integration_tests(void);

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
    register_template_runtime_integration_tests();
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
