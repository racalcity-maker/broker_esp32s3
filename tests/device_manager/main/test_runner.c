#include "unity.h"

extern void register_device_manager_parse_tests(void);
extern void register_device_manager_integration_tests(void);

void app_main(void)
{
    UNITY_BEGIN();
    register_device_manager_parse_tests();
    register_device_manager_integration_tests();
    UNITY_END();
}
