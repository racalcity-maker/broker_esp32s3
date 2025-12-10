#include "unity.h"

#include <stdlib.h>

#include "cJSON.h"
#include "device_manager_internal.h"

static device_manager_config_t *allocate_config(uint8_t capacity)
{
    size_t bytes = sizeof(device_manager_config_t) + sizeof(device_descriptor_t) * capacity;
    device_manager_config_t *cfg = (device_manager_config_t *)calloc(1, bytes);
    TEST_ASSERT_NOT_NULL(cfg);
    cfg->device_capacity = capacity;
    return cfg;
}

static void dm_parse_populates_minimal_device(void)
{
    static const char *json =
        "{"
        "\"schema\":1,"
        "\"generation\":7,"
        "\"profiles\":[{\"id\":\"main\",\"name\":\"Main\",\"device_count\":1}],"
        "\"active_profile\":\"main\","
        "\"devices\":[{"
        "\"id\":\"pictures_pair\","
        "\"name\":\"Pictures\","
        "\"tabs\":[],"
        "\"topics\":[{\"name\":\"scan_request\",\"topic\":\"pictures/cmd/scan1\"}],"
        "\"scenarios\":[{"
        "\"id\":\"scan_request\",\"name\":\"Scan request\","
        "\"steps\":[{\"type\":\"mqtt_publish\",\"topic\":\"pictures/cmd/scan1\",\"payload\":\"scan\",\"qos\":0,\"retain\":false}]"
        "}]"
        "}]"
        "}";

    cJSON *root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);

    device_manager_config_t *cfg = allocate_config(4);
    TEST_ASSERT_TRUE(dm_populate_config_from_json(cfg, root));

    TEST_ASSERT_EQUAL_UINT32(7, cfg->generation);
    TEST_ASSERT_EQUAL_UINT8(1, cfg->profile_count);
    TEST_ASSERT_EQUAL_STRING("main", cfg->active_profile);
    TEST_ASSERT_EQUAL_STRING("main", cfg->profiles[0].id);

    TEST_ASSERT_EQUAL_UINT8(1, cfg->device_count);
    const device_descriptor_t *dev = &cfg->devices[0];
    TEST_ASSERT_EQUAL_STRING("pictures_pair", dev->id);
    TEST_ASSERT_EQUAL_STRING("Pictures", dev->display_name);
    TEST_ASSERT_EQUAL_UINT8(1, dev->topic_count);
    TEST_ASSERT_EQUAL_STRING("scan_request", dev->topics[0].name);
    TEST_ASSERT_EQUAL_STRING("pictures/cmd/scan1", dev->topics[0].topic);
    TEST_ASSERT_EQUAL_UINT8(1, dev->scenario_count);
    TEST_ASSERT_EQUAL_UINT8(1, dev->scenarios[0].step_count);
    TEST_ASSERT_EQUAL(DEVICE_ACTION_MQTT_PUBLISH, dev->scenarios[0].steps[0].type);
    TEST_ASSERT_EQUAL_STRING("pictures/cmd/scan1", dev->scenarios[0].steps[0].data.mqtt.topic);
    TEST_ASSERT_EQUAL_STRING("scan", dev->scenarios[0].steps[0].data.mqtt.payload);

    cJSON_Delete(root);
    free(cfg);
}

static void dm_parse_respects_device_capacity(void)
{
    static const char *json =
        "{"
        "\"profiles\":[{\"id\":\"main\",\"name\":\"Main\"}],"
        "\"active_profile\":\"main\","
        "\"devices\":["
        "{\"id\":\"dev_a\",\"name\":\"Device A\",\"tabs\":[],\"topics\":[],\"scenarios\":[]},"
        "{\"id\":\"dev_b\",\"name\":\"Device B\",\"tabs\":[],\"topics\":[],\"scenarios\":[]}"
        "]"
        "}";

    cJSON *root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);

    device_manager_config_t *cfg = allocate_config(1);
    TEST_ASSERT_TRUE(dm_populate_config_from_json(cfg, root));

    TEST_ASSERT_EQUAL_UINT8(1, cfg->device_count);
    TEST_ASSERT_EQUAL_STRING("dev_a", cfg->devices[0].id);
    TEST_ASSERT_EQUAL_UINT8(1, cfg->profiles[0].device_count);

    cJSON_Delete(root);
    free(cfg);
}

void register_device_manager_parse_tests(void)
{
    RUN_TEST(dm_parse_populates_minimal_device);
    RUN_TEST(dm_parse_respects_device_capacity);
}
