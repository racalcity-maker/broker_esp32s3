#include "unity.h"

extern void register_device_manager_parse_tests(void);

void app_main(void)
{
    UNITY_BEGIN();
    register_device_manager_parse_tests();
    UNITY_END();
}
