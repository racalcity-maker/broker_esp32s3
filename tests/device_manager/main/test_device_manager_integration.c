#include <string.h>

#include "unity.h"
#include "event_bus.h"
#include "dm_template_runtime.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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

static const char *k_sequence_runtime_json =
    "{"
    " \"schema\":1,"
    " \"devices\":[{"
    "   \"id\":\"seq_lock\","
    "   \"display_name\":\"Sequence Lock\","
    "   \"template\":{"
    "     \"type\":\"sequence_lock\","
    "     \"sequence\":{"
    "       \"timeout_ms\":1000,"
    "       \"reset_on_error\":true,"
    "       \"steps\":["
    "         {\"topic\":\"quest/seq/1\",\"payload\":\"red\",\"payload_required\":true},"
    "         {\"topic\":\"quest/seq/2\",\"payload\":\"blue\",\"payload_required\":true}"
    "       ]"
    "     }"
    "   }"
    " }]"
    "}";

static const char *k_signal_runtime_json =
    "{"
    " \"schema\":1,"
    " \"devices\":[{"
    "   \"id\":\"signal_hold\","
    "   \"display_name\":\"Signal Hold\","
    "   \"template\":{"
    "     \"type\":\"signal_hold\","
    "     \"signal\":{"
    "       \"signal_topic\":\"quest/signal/cmd\","
    "       \"signal_payload_on\":\"ON\","
    "       \"signal_payload_off\":\"OFF\","
    "       \"heartbeat_topic\":\"quest/heartbeat\","
    "       \"reset_topic\":\"quest/reset\","
    "       \"required_hold_ms\":100,"
    "       \"heartbeat_timeout_ms\":100"
    "     }"
    "   }"
    " }]"
    "}";

static void prepare_integration_runtime(void)
{
    if (!s_event_bus_ready) {
        TEST_ASSERT_EQUAL(ESP_OK, event_bus_init());
        s_event_bus_ready = true;
    }
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_init());
}

static void cleanup_integration_runtime(void)
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

static void register_sequence_runtime_from_json(void)
{
    device_manager_config_t *cfg = dm_test_parse_config_json(k_sequence_runtime_json, 1);
    const device_descriptor_t *dev =
        dm_test_require_template(cfg, "seq_lock", DM_TEMPLATE_TYPE_SEQUENCE_LOCK);
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_register(&dev->template_config, dev->id));
    dm_test_free_config(cfg);
}

static void register_signal_runtime_from_json(void)
{
    device_manager_config_t *cfg = dm_test_parse_config_json(k_signal_runtime_json, 1);
    const device_descriptor_t *dev =
        dm_test_require_template(cfg, "signal_hold", DM_TEMPLATE_TYPE_SIGNAL_HOLD);
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_register(&dev->template_config, dev->id));
    dm_test_free_config(cfg);
}

static void test_uid_runtime_snapshot_tracks_last_values(void)
{
    prepare_integration_runtime();
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
    cleanup_integration_runtime();
}

static void test_uid_start_topic_resets_snapshot_values(void)
{
    prepare_integration_runtime();
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
    cleanup_integration_runtime();
}

static void test_sequence_runtime_snapshot_and_manual_reset(void)
{
    prepare_integration_runtime();
    register_sequence_runtime_from_json();

    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("quest/seq/1", "red"));

    dm_sequence_runtime_snapshot_t snap = {0};
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_get_sequence_snapshot("seq_lock", &snap));
    TEST_ASSERT_TRUE(snap.active);
    TEST_ASSERT_EQUAL_UINT8(1, snap.current_step_index);
    TEST_ASSERT_EQUAL_UINT8(2, snap.total_steps);
    TEST_ASSERT_EQUAL_STRING("active", snap.state);
    TEST_ASSERT_EQUAL_STRING("quest/seq/2", snap.expected_topic);
    TEST_ASSERT_EQUAL_STRING("blue", snap.expected_payload);
    TEST_ASSERT_TRUE(snap.payload_required);

    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_reset_sequence("seq_lock"));

    memset(&snap, 0, sizeof(snap));
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_get_sequence_snapshot("seq_lock", &snap));
    TEST_ASSERT_FALSE(snap.active);
    TEST_ASSERT_EQUAL_UINT8(0, snap.current_step_index);
    TEST_ASSERT_EQUAL_STRING("idle", snap.state);
    TEST_ASSERT_EQUAL_STRING("", snap.expected_topic);
    TEST_ASSERT_EQUAL_STRING("", snap.expected_payload);
    cleanup_integration_runtime();
}

static void test_signal_runtime_snapshot_progress_and_manual_reset(void)
{
    prepare_integration_runtime();
    register_signal_runtime_from_json();

    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("quest/heartbeat", ""));
    vTaskDelay(pdMS_TO_TICKS(30));
    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("quest/heartbeat", ""));

    dm_signal_runtime_snapshot_t snap = {0};
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_get_signal_snapshot("signal_hold", &snap));
    TEST_ASSERT_TRUE(snap.active);
    TEST_ASSERT_FALSE(snap.completed);
    TEST_ASSERT_EQUAL_STRING("active", snap.state);
    TEST_ASSERT_EQUAL_STRING("quest/heartbeat", snap.heartbeat_topic);
    TEST_ASSERT_EQUAL_STRING("quest/reset", snap.reset_topic);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(10, snap.progress_ms);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(100, snap.progress_ms);

    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_reset_signal("signal_hold"));

    memset(&snap, 0, sizeof(snap));
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_get_signal_snapshot("signal_hold", &snap));
    TEST_ASSERT_FALSE(snap.active);
    TEST_ASSERT_FALSE(snap.completed);
    TEST_ASSERT_EQUAL_UINT32(0, snap.progress_ms);
    TEST_ASSERT_EQUAL_STRING("idle", snap.state);
    cleanup_integration_runtime();
}

void register_device_manager_integration_tests(void)
{
    RUN_TEST(test_uid_runtime_snapshot_tracks_last_values);
    RUN_TEST(test_uid_start_topic_resets_snapshot_values);
    RUN_TEST(test_sequence_runtime_snapshot_and_manual_reset);
    RUN_TEST(test_signal_runtime_snapshot_progress_and_manual_reset);
}
