#include <string.h>

#include "unity.h"
#include "event_bus.h"
#include "dm_template_runtime.h"
#include "test_dm_helpers.h"

static bool s_event_bus_ready = false;

static const char *k_uid_runtime_json =
    "{"
    " \"schema\":1,"
    " \"devices\":[{"
    "   \"id\":\"uid_gate\","
    "   \"display_name\":\"UID Gate\","
    "   \"template\":{"
    "     \"type\":\"uid_validator\","
    "     \"uid\":{"
    "       \"start_topic\":\"quest/start\","
    "       \"start_payload\":\"go\","
    "       \"slots\":["
    "         {\"source_id\":\"reader/1\",\"label\":\"Reader 1\",\"values\":[\"A1\"]},"
    "         {\"source_id\":\"reader/2\",\"label\":\"Reader 2\",\"values\":[\"B1\"]}"
    "       ]"
    "     }"
    "   }"
    " }]"
    "}";

void setUp(void)
{
    if (!s_event_bus_ready) {
        TEST_ASSERT_EQUAL(ESP_OK, event_bus_init());
        s_event_bus_ready = true;
    }
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_init());
}

void tearDown(void)
{
    dm_template_runtime_reset();
}

static void register_uid_runtime_from_json(void)
{
    device_manager_config_t *cfg = dm_test_parse_config_json(k_uid_runtime_json, 1);
    const device_descriptor_t *dev =
        dm_test_require_template(cfg, "uid_gate", DM_TEMPLATE_TYPE_UID);
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_register(&dev->template_config, dev->id));
    dm_test_free_config(cfg);
}

static void test_uid_runtime_snapshot_tracks_last_values(void)
{
    register_uid_runtime_from_json();

    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("reader/1", "  A1  "));
    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("reader/2", "B1"));

    dm_uid_runtime_snapshot_t snap = {0};
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_get_uid_snapshot("uid_gate", &snap));
    TEST_ASSERT_EQUAL_UINT8(2, snap.slot_count);
    TEST_ASSERT_TRUE(snap.slots[0].has_value);
    TEST_ASSERT_EQUAL_STRING("Reader 1", snap.slots[0].label);
    TEST_ASSERT_EQUAL_STRING("A1", snap.slots[0].last_value);
    TEST_ASSERT_TRUE(snap.slots[1].has_value);
    TEST_ASSERT_EQUAL_STRING("Reader 2", snap.slots[1].label);
    TEST_ASSERT_EQUAL_STRING("B1", snap.slots[1].last_value);
}

static void test_uid_start_topic_resets_snapshot_values(void)
{
    register_uid_runtime_from_json();

    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("reader/1", "A1"));

    dm_uid_runtime_snapshot_t snap = {0};
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_get_uid_snapshot("uid_gate", &snap));
    TEST_ASSERT_TRUE(snap.slots[0].has_value);

    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("quest/start", "go"));

    memset(&snap, 0, sizeof(snap));
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_get_uid_snapshot("uid_gate", &snap));
    TEST_ASSERT_FALSE(snap.slots[0].has_value);
    TEST_ASSERT_FALSE(snap.slots[1].has_value);
}

void register_device_manager_integration_tests(void)
{
    RUN_TEST(test_uid_runtime_snapshot_tracks_last_values);
    RUN_TEST(test_uid_start_topic_resets_snapshot_values);
}
