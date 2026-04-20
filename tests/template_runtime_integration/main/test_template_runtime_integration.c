#include "unity.h"

#include <string.h>

#include "esp_err.h"
#include "event_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "dm_template_runtime.h"
#include "test_template_helpers.h"

static bool s_event_bus_ready = false;
static bool s_event_bus_started = false;
static bool s_capture_handler_registered = false;
static portMUX_TYPE s_capture_lock = portMUX_INITIALIZER_UNLOCKED;

typedef struct {
    uint32_t count;
    char device_id[8][DEVICE_MANAGER_ID_MAX_LEN];
    char scenario_id[8][DEVICE_MANAGER_ID_MAX_LEN];
} scenario_capture_t;

static scenario_capture_t s_capture = {0};

static const char *k_sequence_runtime_json =
    "{"
    " \"schema\":1,"
    " \"devices\":[{"
    "   \"id\":\"seq_lock\","
    "   \"display_name\":\"Sequence Lock\","
    "   \"template\":{"
    "     \"type\":\"sequence_lock\","
    "     \"sequence\":{"
    "       \"timeout_ms\":120,"
    "       \"reset_on_error\":true,"
    "       \"steps\":["
    "         {\"topic\":\"quest/seq/1\",\"payload\":\"red\",\"payload_required\":true},"
    "         {\"topic\":\"quest/seq/2\",\"payload\":\"blue\",\"payload_required\":true}"
    "       ]"
    "     }"
    "   }"
    " }]"
    "}";

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

static const char *k_flag_runtime_json =
    "{"
    " \"schema\":1,"
    " \"devices\":[{"
    "   \"id\":\"flag_gate\","
    "   \"display_name\":\"Flag Gate\","
    "   \"template\":{"
    "     \"type\":\"on_flag\","
    "     \"flag\":{"
    "       \"rules\":["
    "         {\"flag\":\"beam_ok\",\"state\":true,\"scenario\":\"beam_ready\"},"
    "         {\"flag\":\"beam_ok\",\"state\":false,\"scenario\":\"beam_lost\"}"
    "       ]"
    "     }"
    "   }"
    " }]"
    "}";

static const char *k_mqtt_runtime_json =
    "{"
    " \"schema\":1,"
    " \"devices\":[{"
    "   \"id\":\"mqtt_gate\","
    "   \"display_name\":\"MQTT Gate\","
    "   \"template\":{"
    "     \"type\":\"on_mqtt_event\","
    "     \"mqtt\":{"
    "       \"rules\":["
    "         {\"topic\":\"quest/button\",\"payload\":\"press\",\"payload_required\":true,\"scenario\":\"button_press\"}"
    "       ]"
    "     }"
    "   }"
    " }]"
    "}";

static const char *k_mqtt_optional_runtime_json =
    "{"
    " \"schema\":1,"
    " \"devices\":[{"
    "   \"id\":\"mqtt_optional_gate\","
    "   \"display_name\":\"MQTT Optional Gate\","
    "   \"template\":{"
    "     \"type\":\"on_mqtt_event\","
    "     \"mqtt\":{"
    "       \"rules\":["
    "         {\"topic\":\"quest/optional\",\"scenario\":\"optional_press\"}"
    "       ]"
    "     }"
    "   }"
    " }]"
    "}";

static const char *k_condition_runtime_json =
    "{"
    " \"schema\":1,"
    " \"devices\":[{"
    "   \"id\":\"cond_gate\","
    "   \"display_name\":\"Condition Gate\","
    "   \"template\":{"
    "     \"type\":\"if_condition\","
    "     \"condition\":{"
    "       \"mode\":\"all\","
    "       \"true_scenario\":\"all_ready\","
    "       \"false_scenario\":\"not_ready\","
    "       \"rules\":["
    "         {\"flag\":\"beam_ok\",\"state\":true},"
    "         {\"flag\":\"door_closed\",\"state\":true}"
    "       ]"
    "     }"
    "   }"
    " }]"
    "}";

static const char *k_condition_any_runtime_json =
    "{"
    " \"schema\":1,"
    " \"devices\":[{"
    "   \"id\":\"cond_any_gate\","
    "   \"display_name\":\"Condition Any Gate\","
    "   \"template\":{"
    "     \"type\":\"if_condition\","
    "     \"condition\":{"
    "       \"mode\":\"any\","
    "       \"true_scenario\":\"any_ready\","
    "       \"false_scenario\":\"none_ready\","
    "       \"rules\":["
    "         {\"flag\":\"beam_ok\",\"state\":true},"
    "         {\"flag\":\"door_closed\",\"state\":true}"
    "       ]"
    "     }"
    "   }"
    " }]"
    "}";

static const char *k_interval_runtime_json =
    "{"
    " \"schema\":1,"
    " \"devices\":[{"
    "   \"id\":\"interval_gate\","
    "   \"display_name\":\"Interval Gate\","
    "   \"template\":{"
    "     \"type\":\"interval_task\","
    "     \"interval\":{"
    "       \"interval_ms\":80,"
    "       \"scenario\":\"pulse\""
    "     }"
    "   }"
    " }]"
    "}";

static const char *k_sequence_reset_runtime_json =
    "{"
    " \"schema\":1,"
    " \"devices\":[{"
    "   \"id\":\"seq_reset_lock\","
    "   \"display_name\":\"Sequence Reset Lock\","
    "   \"template\":{"
    "     \"type\":\"sequence_lock\","
    "     \"sequence\":{"
    "       \"timeout_ms\":120,"
    "       \"reset_on_error\":true,"
    "       \"fail_scenario\":\"seq_fail\","
    "       \"steps\":["
    "         {\"topic\":\"quest/reset/1\",\"payload\":\"red\",\"payload_required\":true},"
    "         {\"topic\":\"quest/reset/2\",\"payload\":\"blue\",\"payload_required\":true}"
    "       ]"
    "     }"
    "   }"
    " }]"
    "}";

static const char *k_sequence_no_reset_runtime_json =
    "{"
    " \"schema\":1,"
    " \"devices\":[{"
    "   \"id\":\"seq_no_reset_lock\","
    "   \"display_name\":\"Sequence No Reset Lock\","
    "   \"template\":{"
    "     \"type\":\"sequence_lock\","
    "     \"sequence\":{"
    "       \"timeout_ms\":120,"
    "       \"reset_on_error\":false,"
    "       \"success_scenario\":\"seq_ok\","
    "       \"fail_scenario\":\"seq_fail\","
    "       \"steps\":["
    "         {\"topic\":\"quest/nr/1\",\"payload\":\"red\",\"payload_required\":true},"
    "         {\"topic\":\"quest/nr/2\",\"payload\":\"blue\",\"payload_required\":true}"
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
    "       \"required_hold_ms\":120,"
    "       \"heartbeat_timeout_ms\":100"
    "     }"
    "   }"
    " }]"
    "}";

static const char *k_uid_fail_runtime_json =
    "{"
    " \"schema\":1,"
    " \"devices\":[{"
    "   \"id\":\"uid_fail_gate\","
    "   \"display_name\":\"UID Fail Gate\","
    "   \"template\":{"
    "     \"type\":\"uid_validator\","
    "     \"uid\":{"
    "       \"fail_scenario\":\"uid_fail\","
    "       \"slots\":["
    "         {\"source_id\":\"reader/f1\",\"label\":\"Reader F1\",\"values\":[\"A1\"]},"
    "         {\"source_id\":\"reader/f2\",\"label\":\"Reader F2\",\"values\":[\"B1\"]}"
    "       ]"
    "     }"
    "   }"
    " }]"
    "}";

static const char *k_uid_edge_runtime_json =
    "{"
    " \"schema\":1,"
    " \"devices\":[{"
    "   \"id\":\"uid_edge_gate\","
    "   \"display_name\":\"UID Edge Gate\","
    "   \"template\":{"
    "     \"type\":\"uid_validator\","
    "     \"uid\":{"
    "       \"start_topic\":\"quest/edge/start\","
    "       \"start_payload\":\"go\","
    "       \"bg_start_topic\":\"quest/edge/bg\","
    "       \"success_scenario\":\"uid_ok\","
    "       \"fail_scenario\":\"uid_fail\","
    "       \"slots\":["
    "         {\"source_id\":\"reader/e1\",\"label\":\"Reader E1\",\"values\":[\"A1\"]},"
    "         {\"source_id\":\"reader/e2\",\"label\":\"Reader E2\",\"values\":[\"B1\"]}"
    "       ]"
    "     }"
    "   }"
    " }]"
    "}";

static void capture_event_handler(const event_bus_message_t *msg)
{
    if (!msg || msg->type != EVENT_SCENARIO_TRIGGER) {
        return;
    }
    taskENTER_CRITICAL(&s_capture_lock);
    uint32_t idx = s_capture.count;
    if (idx < 8) {
        strncpy(s_capture.device_id[idx], msg->topic, sizeof(s_capture.device_id[idx]) - 1);
        s_capture.device_id[idx][sizeof(s_capture.device_id[idx]) - 1] = '\0';
        strncpy(s_capture.scenario_id[idx], msg->payload, sizeof(s_capture.scenario_id[idx]) - 1);
        s_capture.scenario_id[idx][sizeof(s_capture.scenario_id[idx]) - 1] = '\0';
    }
    s_capture.count++;
    taskEXIT_CRITICAL(&s_capture_lock);
}

static void reset_scenario_capture(void)
{
    taskENTER_CRITICAL(&s_capture_lock);
    memset(&s_capture, 0, sizeof(s_capture));
    taskEXIT_CRITICAL(&s_capture_lock);
}

static bool wait_for_scenario_capture_count(uint32_t expected_count, TickType_t timeout_ticks)
{
    TickType_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < timeout_ticks) {
        uint32_t count = 0;
        taskENTER_CRITICAL(&s_capture_lock);
        count = s_capture.count;
        taskEXIT_CRITICAL(&s_capture_lock);
        if (count >= expected_count) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    uint32_t count = 0;
    taskENTER_CRITICAL(&s_capture_lock);
    count = s_capture.count;
    taskEXIT_CRITICAL(&s_capture_lock);
    return count >= expected_count;
}

static void prepare_template_runtime(bool start_event_bus)
{
    if (!s_event_bus_ready) {
        TEST_ASSERT_EQUAL(ESP_OK, event_bus_init());
        s_event_bus_ready = true;
    }
    if (!s_capture_handler_registered) {
        TEST_ASSERT_EQUAL(ESP_OK, event_bus_register_handler(capture_event_handler));
        s_capture_handler_registered = true;
    }
    if (start_event_bus && !s_event_bus_started) {
        TEST_ASSERT_EQUAL(ESP_OK, event_bus_start());
        s_event_bus_started = true;
    }
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_init());
}

static void cleanup_template_runtime(void)
{
    dm_template_runtime_reset();
}

static void register_sequence_runtime_from_json(void)
{
    device_manager_config_t *cfg = template_test_parse_config_json(k_sequence_runtime_json, 1);
    const device_descriptor_t *dev =
        template_test_require_template(cfg, "seq_lock", DM_TEMPLATE_TYPE_SEQUENCE_LOCK);
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_register(&dev->template_config, dev->id));
    template_test_free_config(cfg);
}

static void register_uid_runtime_from_json(void)
{
    device_manager_config_t *cfg = template_test_parse_config_json(k_uid_runtime_json, 1);
    const device_descriptor_t *dev =
        template_test_require_template(cfg, "uid_gate", DM_TEMPLATE_TYPE_UID);
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_register(&dev->template_config, dev->id));
    template_test_free_config(cfg);
}

static void register_flag_runtime_from_json(void)
{
    device_manager_config_t *cfg = template_test_parse_config_json(k_flag_runtime_json, 1);
    const device_descriptor_t *dev =
        template_test_require_template(cfg, "flag_gate", DM_TEMPLATE_TYPE_FLAG_TRIGGER);
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_register(&dev->template_config, dev->id));
    template_test_free_config(cfg);
}

static void register_mqtt_runtime_from_json(void)
{
    device_manager_config_t *cfg = template_test_parse_config_json(k_mqtt_runtime_json, 1);
    const device_descriptor_t *dev =
        template_test_require_template(cfg, "mqtt_gate", DM_TEMPLATE_TYPE_MQTT_TRIGGER);
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_register(&dev->template_config, dev->id));
    template_test_free_config(cfg);
}

static void register_mqtt_optional_runtime_from_json(void)
{
    device_manager_config_t *cfg = template_test_parse_config_json(k_mqtt_optional_runtime_json, 1);
    const device_descriptor_t *dev =
        template_test_require_template(cfg, "mqtt_optional_gate", DM_TEMPLATE_TYPE_MQTT_TRIGGER);
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_register(&dev->template_config, dev->id));
    template_test_free_config(cfg);
}

static void register_condition_runtime_from_json(void)
{
    device_manager_config_t *cfg = template_test_parse_config_json(k_condition_runtime_json, 1);
    const device_descriptor_t *dev =
        template_test_require_template(cfg, "cond_gate", DM_TEMPLATE_TYPE_IF_CONDITION);
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_register(&dev->template_config, dev->id));
    template_test_free_config(cfg);
}

static void register_condition_any_runtime_from_json(void)
{
    device_manager_config_t *cfg = template_test_parse_config_json(k_condition_any_runtime_json, 1);
    const device_descriptor_t *dev =
        template_test_require_template(cfg, "cond_any_gate", DM_TEMPLATE_TYPE_IF_CONDITION);
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_register(&dev->template_config, dev->id));
    template_test_free_config(cfg);
}

static void register_interval_runtime_from_json(void)
{
    device_manager_config_t *cfg = template_test_parse_config_json(k_interval_runtime_json, 1);
    const device_descriptor_t *dev =
        template_test_require_template(cfg, "interval_gate", DM_TEMPLATE_TYPE_INTERVAL_TASK);
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_register(&dev->template_config, dev->id));
    template_test_free_config(cfg);
}

static void register_sequence_reset_runtime_from_json(void)
{
    device_manager_config_t *cfg = template_test_parse_config_json(k_sequence_reset_runtime_json, 1);
    const device_descriptor_t *dev =
        template_test_require_template(cfg, "seq_reset_lock", DM_TEMPLATE_TYPE_SEQUENCE_LOCK);
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_register(&dev->template_config, dev->id));
    template_test_free_config(cfg);
}

static void register_sequence_no_reset_runtime_from_json(void)
{
    device_manager_config_t *cfg = template_test_parse_config_json(k_sequence_no_reset_runtime_json, 1);
    const device_descriptor_t *dev =
        template_test_require_template(cfg, "seq_no_reset_lock", DM_TEMPLATE_TYPE_SEQUENCE_LOCK);
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_register(&dev->template_config, dev->id));
    template_test_free_config(cfg);
}

static void register_signal_runtime_from_json(void)
{
    device_manager_config_t *cfg = template_test_parse_config_json(k_signal_runtime_json, 1);
    const device_descriptor_t *dev =
        template_test_require_template(cfg, "signal_hold", DM_TEMPLATE_TYPE_SIGNAL_HOLD);
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_register(&dev->template_config, dev->id));
    template_test_free_config(cfg);
}

static void register_uid_fail_runtime_from_json(void)
{
    device_manager_config_t *cfg = template_test_parse_config_json(k_uid_fail_runtime_json, 1);
    const device_descriptor_t *dev =
        template_test_require_template(cfg, "uid_fail_gate", DM_TEMPLATE_TYPE_UID);
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_register(&dev->template_config, dev->id));
    template_test_free_config(cfg);
}

static void register_uid_edge_runtime_from_json(void)
{
    device_manager_config_t *cfg = template_test_parse_config_json(k_uid_edge_runtime_json, 1);
    const device_descriptor_t *dev =
        template_test_require_template(cfg, "uid_edge_gate", DM_TEMPLATE_TYPE_UID);
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_register(&dev->template_config, dev->id));
    template_test_free_config(cfg);
}

static bool wait_for_sequence_state(const char *device_id, const char *state, TickType_t timeout_ticks)
{
    TickType_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < timeout_ticks) {
        dm_sequence_runtime_snapshot_t snap = {0};
        if (dm_template_runtime_get_sequence_snapshot(device_id, &snap) == ESP_OK &&
            strcmp(snap.state, state) == 0) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    dm_sequence_runtime_snapshot_t snap = {0};
    return dm_template_runtime_get_sequence_snapshot(device_id, &snap) == ESP_OK &&
           strcmp(snap.state, state) == 0;
}

static bool wait_for_sequence_step(const char *device_id, uint8_t step_index, TickType_t timeout_ticks)
{
    TickType_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < timeout_ticks) {
        dm_sequence_runtime_snapshot_t snap = {0};
        if (dm_template_runtime_get_sequence_snapshot(device_id, &snap) == ESP_OK &&
            snap.current_step_index == step_index) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    dm_sequence_runtime_snapshot_t snap = {0};
    return dm_template_runtime_get_sequence_snapshot(device_id, &snap) == ESP_OK &&
           snap.current_step_index == step_index;
}

static bool wait_for_signal_state(const char *device_id, const char *state, TickType_t timeout_ticks)
{
    TickType_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < timeout_ticks) {
        dm_signal_runtime_snapshot_t snap = {0};
        if (dm_template_runtime_get_signal_snapshot(device_id, &snap) == ESP_OK &&
            strcmp(snap.state, state) == 0) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    dm_signal_runtime_snapshot_t snap = {0};
    return dm_template_runtime_get_signal_snapshot(device_id, &snap) == ESP_OK &&
           strcmp(snap.state, state) == 0;
}

static bool wait_for_uid_slot_value(const char *device_id,
                                    uint8_t slot_index,
                                    const char *expected_value,
                                    TickType_t timeout_ticks)
{
    TickType_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < timeout_ticks) {
        dm_uid_runtime_snapshot_t snap = {0};
        if (dm_template_runtime_get_uid_snapshot(device_id, &snap) == ESP_OK &&
            slot_index < snap.slot_count &&
            snap.slots[slot_index].has_value &&
            strcmp(snap.slots[slot_index].last_value, expected_value) == 0) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    dm_uid_runtime_snapshot_t snap = {0};
    return dm_template_runtime_get_uid_snapshot(device_id, &snap) == ESP_OK &&
           slot_index < snap.slot_count &&
           snap.slots[slot_index].has_value &&
           strcmp(snap.slots[slot_index].last_value, expected_value) == 0;
}

static void test_template_runtime_init_reset_smoke(void)
{
    prepare_template_runtime(false);
    cleanup_template_runtime();
}

static void test_template_runtime_missing_snapshots_return_not_found(void)
{
    prepare_template_runtime(false);

    dm_uid_runtime_snapshot_t uid = {0};
    dm_sequence_runtime_snapshot_t seq = {0};
    dm_signal_runtime_snapshot_t sig = {0};

    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, dm_template_runtime_get_uid_snapshot("missing", &uid));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, dm_template_runtime_get_sequence_snapshot("missing", &seq));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, dm_template_runtime_get_signal_snapshot("missing", &sig));

    cleanup_template_runtime();
}

static void test_template_runtime_missing_manual_resets_return_not_found(void)
{
    prepare_template_runtime(false);

    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, dm_template_runtime_reset_sequence("missing"));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, dm_template_runtime_reset_signal("missing"));

    cleanup_template_runtime();
}

static void test_template_runtime_invalid_args_return_invalid_arg(void)
{
    prepare_template_runtime(false);

    dm_uid_runtime_snapshot_t uid = {0};
    dm_sequence_runtime_snapshot_t seq = {0};
    dm_signal_runtime_snapshot_t sig = {0};

    TEST_ASSERT_FALSE(dm_template_runtime_handle_mqtt(NULL, "x"));
    TEST_ASSERT_FALSE(dm_template_runtime_handle_flag(NULL, true));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, dm_template_runtime_get_uid_snapshot(NULL, &uid));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, dm_template_runtime_get_uid_snapshot("uid", NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, dm_template_runtime_get_sequence_snapshot(NULL, &seq));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, dm_template_runtime_get_sequence_snapshot("seq", NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, dm_template_runtime_get_signal_snapshot(NULL, &sig));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, dm_template_runtime_get_signal_snapshot("sig", NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, dm_template_runtime_reset_sequence(""));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, dm_template_runtime_reset_signal(""));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, dm_template_runtime_register(NULL, "dev"));

    cleanup_template_runtime();
}

static void test_sequence_runtime_snapshot_and_manual_reset(void)
{
    prepare_template_runtime(false);
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
    TEST_ASSERT_GREATER_THAN_UINT32(0, snap.time_left_ms);

    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_reset_sequence("seq_lock"));

    memset(&snap, 0, sizeof(snap));
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_get_sequence_snapshot("seq_lock", &snap));
    TEST_ASSERT_FALSE(snap.active);
    TEST_ASSERT_EQUAL_UINT8(0, snap.current_step_index);
    TEST_ASSERT_EQUAL_STRING("idle", snap.state);
    TEST_ASSERT_EQUAL_STRING("", snap.expected_topic);
    TEST_ASSERT_EQUAL_STRING("", snap.expected_payload);
    cleanup_template_runtime();
}

static void test_sequence_runtime_completion_snapshot(void)
{
    prepare_template_runtime(false);
    register_sequence_runtime_from_json();

    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("quest/seq/1", "red"));
    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("quest/seq/2", "blue"));

    dm_sequence_runtime_snapshot_t snap = {0};
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_get_sequence_snapshot("seq_lock", &snap));
    TEST_ASSERT_FALSE(snap.active);
    TEST_ASSERT_EQUAL_UINT8(0, snap.current_step_index);
    TEST_ASSERT_EQUAL_UINT8(2, snap.total_steps);
    TEST_ASSERT_EQUAL_STRING("completed", snap.state);
    TEST_ASSERT_EQUAL_STRING("", snap.expected_topic);
    TEST_ASSERT_EQUAL_STRING("", snap.expected_payload);
    cleanup_template_runtime();
}

static void test_sequence_runtime_timeout_snapshot(void)
{
    prepare_template_runtime(false);
    register_sequence_runtime_from_json();

    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("quest/seq/1", "red"));
    TEST_ASSERT_TRUE(wait_for_sequence_state("seq_lock", "active", pdMS_TO_TICKS(30)));
    TEST_ASSERT_TRUE(wait_for_sequence_state("seq_lock", "timeout", pdMS_TO_TICKS(300)));

    dm_sequence_runtime_snapshot_t snap = {0};
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_get_sequence_snapshot("seq_lock", &snap));
    TEST_ASSERT_FALSE(snap.active);
    TEST_ASSERT_EQUAL_UINT8(0, snap.current_step_index);
    TEST_ASSERT_EQUAL_STRING("timeout", snap.state);
    TEST_ASSERT_EQUAL_UINT32(0, snap.time_left_ms);
    cleanup_template_runtime();
}

static void test_sequence_runtime_event_bus_mqtt_routing_updates_snapshot(void)
{
    prepare_template_runtime(true);
    register_sequence_runtime_from_json();

    event_bus_message_t msg = {
        .type = EVENT_MQTT_MESSAGE,
    };
    strncpy(msg.topic, "quest/seq/1", sizeof(msg.topic) - 1);
    strncpy(msg.payload, "red", sizeof(msg.payload) - 1);
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_post(&msg, pdMS_TO_TICKS(100)));

    TEST_ASSERT_TRUE(wait_for_sequence_step("seq_lock", 1, pdMS_TO_TICKS(200)));

    dm_sequence_runtime_snapshot_t snap = {0};
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_get_sequence_snapshot("seq_lock", &snap));
    TEST_ASSERT_TRUE(snap.active);
    TEST_ASSERT_EQUAL_STRING("active", snap.state);
    TEST_ASSERT_EQUAL_STRING("quest/seq/2", snap.expected_topic);
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_reset_sequence("seq_lock"));
    cleanup_template_runtime();
}

static void test_signal_runtime_snapshot_and_manual_reset(void)
{
    prepare_template_runtime(false);
    register_signal_runtime_from_json();

    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("quest/heartbeat", ""));
    vTaskDelay(pdMS_TO_TICKS(40));
    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("quest/heartbeat", ""));

    dm_signal_runtime_snapshot_t snap = {0};
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_get_signal_snapshot("signal_hold", &snap));
    TEST_ASSERT_TRUE(snap.active);
    TEST_ASSERT_FALSE(snap.completed);
    TEST_ASSERT_EQUAL_STRING("active", snap.state);
    TEST_ASSERT_EQUAL_STRING("quest/heartbeat", snap.heartbeat_topic);
    TEST_ASSERT_EQUAL_STRING("quest/reset", snap.reset_topic);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(20, snap.progress_ms);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(120, snap.progress_ms);
    TEST_ASSERT_GREATER_THAN_UINT32(0, snap.time_left_ms);

    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_reset_signal("signal_hold"));

    memset(&snap, 0, sizeof(snap));
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_get_signal_snapshot("signal_hold", &snap));
    TEST_ASSERT_FALSE(snap.active);
    TEST_ASSERT_FALSE(snap.completed);
    TEST_ASSERT_EQUAL_UINT32(0, snap.progress_ms);
    TEST_ASSERT_EQUAL_STRING("idle", snap.state);
    cleanup_template_runtime();
}

static void test_signal_runtime_completion_snapshot(void)
{
    prepare_template_runtime(false);
    register_signal_runtime_from_json();

    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("quest/heartbeat", ""));
    vTaskDelay(pdMS_TO_TICKS(70));
    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("quest/heartbeat", ""));
    vTaskDelay(pdMS_TO_TICKS(70));
    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("quest/heartbeat", ""));

    dm_signal_runtime_snapshot_t snap = {0};
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_get_signal_snapshot("signal_hold", &snap));
    TEST_ASSERT_FALSE(snap.active);
    TEST_ASSERT_TRUE(snap.completed);
    TEST_ASSERT_EQUAL_STRING("completed", snap.state);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(120, snap.progress_ms);
    cleanup_template_runtime();
}

static void test_signal_runtime_timeout_snapshot(void)
{
    prepare_template_runtime(false);
    register_signal_runtime_from_json();

    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("quest/heartbeat", ""));
    vTaskDelay(pdMS_TO_TICKS(40));
    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("quest/heartbeat", ""));
    TEST_ASSERT_TRUE(wait_for_signal_state("signal_hold", "active", pdMS_TO_TICKS(30)));
    TEST_ASSERT_TRUE(wait_for_signal_state("signal_hold", "paused", pdMS_TO_TICKS(250)));

    dm_signal_runtime_snapshot_t snap = {0};
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_get_signal_snapshot("signal_hold", &snap));
    TEST_ASSERT_FALSE(snap.active);
    TEST_ASSERT_FALSE(snap.completed);
    TEST_ASSERT_EQUAL_STRING("paused", snap.state);
    TEST_ASSERT_GREATER_THAN_UINT32(0, snap.progress_ms);
    TEST_ASSERT_EQUAL_UINT32(0, snap.time_left_ms);
    cleanup_template_runtime();
}

static void test_signal_runtime_event_bus_routing_updates_snapshot(void)
{
    prepare_template_runtime(true);
    register_signal_runtime_from_json();

    event_bus_message_t msg = {
        .type = EVENT_MQTT_MESSAGE,
    };
    strncpy(msg.topic, "quest/heartbeat", sizeof(msg.topic) - 1);
    msg.topic[sizeof(msg.topic) - 1] = '\0';
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_post(&msg, pdMS_TO_TICKS(100)));

    TEST_ASSERT_TRUE(wait_for_signal_state("signal_hold", "active", pdMS_TO_TICKS(200)));

    dm_signal_runtime_snapshot_t snap = {0};
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_get_signal_snapshot("signal_hold", &snap));
    TEST_ASSERT_TRUE(snap.active);
    TEST_ASSERT_EQUAL_STRING("active", snap.state);

    memset(&msg, 0, sizeof(msg));
    msg.type = EVENT_MQTT_MESSAGE;
    strncpy(msg.topic, "quest/reset", sizeof(msg.topic) - 1);
    msg.topic[sizeof(msg.topic) - 1] = '\0';
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_post(&msg, pdMS_TO_TICKS(100)));

    TEST_ASSERT_TRUE(wait_for_signal_state("signal_hold", "idle", pdMS_TO_TICKS(200)));
    cleanup_template_runtime();
}

static void test_uid_runtime_snapshot_tracks_last_values(void)
{
    prepare_template_runtime(false);
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
    cleanup_template_runtime();
}

static void test_uid_start_topic_resets_snapshot_values(void)
{
    prepare_template_runtime(false);
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
    cleanup_template_runtime();
}

static void test_uid_runtime_event_bus_routing_updates_snapshot(void)
{
    prepare_template_runtime(true);
    register_uid_runtime_from_json();

    event_bus_message_t msg = {
        .type = EVENT_MQTT_MESSAGE,
    };
    strncpy(msg.topic, "reader/1", sizeof(msg.topic) - 1);
    msg.topic[sizeof(msg.topic) - 1] = '\0';
    strncpy(msg.payload, "A1", sizeof(msg.payload) - 1);
    msg.payload[sizeof(msg.payload) - 1] = '\0';
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_post(&msg, pdMS_TO_TICKS(100)));

    TEST_ASSERT_TRUE(wait_for_uid_slot_value("uid_gate", 0, "A1", pdMS_TO_TICKS(200)));

    dm_uid_runtime_snapshot_t snap = {0};
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_get_uid_snapshot("uid_gate", &snap));
    TEST_ASSERT_TRUE(snap.slots[0].has_value);
    TEST_ASSERT_EQUAL_STRING("A1", snap.slots[0].last_value);

    memset(&msg, 0, sizeof(msg));
    msg.type = EVENT_MQTT_MESSAGE;
    strncpy(msg.topic, "quest/start", sizeof(msg.topic) - 1);
    msg.topic[sizeof(msg.topic) - 1] = '\0';
    strncpy(msg.payload, "go", sizeof(msg.payload) - 1);
    msg.payload[sizeof(msg.payload) - 1] = '\0';
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_post(&msg, pdMS_TO_TICKS(100)));

    TickType_t start = xTaskGetTickCount();
    bool cleared = false;
    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(200)) {
        memset(&snap, 0, sizeof(snap));
        if (dm_template_runtime_get_uid_snapshot("uid_gate", &snap) == ESP_OK &&
            !snap.slots[0].has_value && !snap.slots[1].has_value) {
            cleared = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    TEST_ASSERT_TRUE(cleared);
    cleanup_template_runtime();
}

static void test_uid_invalid_value_posts_fail_scenario(void)
{
    prepare_template_runtime(true);
    register_uid_fail_runtime_from_json();
    reset_scenario_capture();

    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("reader/f1", "A1"));
    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("reader/f2", "BAD"));
    TEST_ASSERT_TRUE(wait_for_scenario_capture_count(1, pdMS_TO_TICKS(200)));

    taskENTER_CRITICAL(&s_capture_lock);
    TEST_ASSERT_EQUAL_UINT32(1, s_capture.count);
    TEST_ASSERT_EQUAL_STRING("uid_fail_gate", s_capture.device_id[0]);
    TEST_ASSERT_EQUAL_STRING("uid_fail", s_capture.scenario_id[0]);
    taskEXIT_CRITICAL(&s_capture_lock);

    dm_uid_runtime_snapshot_t snap = {0};
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_get_uid_snapshot("uid_fail_gate", &snap));
    TEST_ASSERT_TRUE(snap.slots[0].has_value);
    TEST_ASSERT_EQUAL_STRING("A1", snap.slots[0].last_value);
    TEST_ASSERT_TRUE(snap.slots[1].has_value);
    TEST_ASSERT_EQUAL_STRING("BAD", snap.slots[1].last_value);
    cleanup_template_runtime();
}

static void test_uid_start_topic_payload_mismatch_does_not_reset_snapshot(void)
{
    prepare_template_runtime(true);
    register_uid_edge_runtime_from_json();

    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("reader/e1", "A1"));
    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("reader/e2", "B1"));

    dm_uid_runtime_snapshot_t snap = {0};
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_get_uid_snapshot("uid_edge_gate", &snap));
    TEST_ASSERT_TRUE(snap.slots[0].has_value);
    TEST_ASSERT_TRUE(snap.slots[1].has_value);

    TEST_ASSERT_FALSE(dm_template_runtime_handle_mqtt("quest/edge/start", "wrong"));
    vTaskDelay(pdMS_TO_TICKS(30));

    memset(&snap, 0, sizeof(snap));
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_get_uid_snapshot("uid_edge_gate", &snap));
    TEST_ASSERT_TRUE(snap.slots[0].has_value);
    TEST_ASSERT_EQUAL_STRING("A1", snap.slots[0].last_value);
    TEST_ASSERT_TRUE(snap.slots[1].has_value);
    TEST_ASSERT_EQUAL_STRING("B1", snap.slots[1].last_value);
    cleanup_template_runtime();
}

static void test_uid_start_topic_debounce_suppresses_immediate_second_reset(void)
{
    prepare_template_runtime(true);
    register_uid_edge_runtime_from_json();

    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("reader/e1", "A1"));
    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("quest/edge/start", "go"));

    dm_uid_runtime_snapshot_t snap = {0};
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_get_uid_snapshot("uid_edge_gate", &snap));
    TEST_ASSERT_FALSE(snap.slots[0].has_value);
    TEST_ASSERT_FALSE(snap.slots[1].has_value);

    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("reader/e1", "A1"));
    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("quest/edge/start", "go"));
    vTaskDelay(pdMS_TO_TICKS(30));

    memset(&snap, 0, sizeof(snap));
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_get_uid_snapshot("uid_edge_gate", &snap));
    TEST_ASSERT_TRUE(snap.slots[0].has_value);
    TEST_ASSERT_EQUAL_STRING("A1", snap.slots[0].last_value);
    TEST_ASSERT_FALSE(snap.slots[1].has_value);
    cleanup_template_runtime();
}

static void test_uid_bg_start_topic_without_track_is_handled_without_side_effects(void)
{
    prepare_template_runtime(true);
    register_uid_edge_runtime_from_json();
    reset_scenario_capture();

    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("reader/e1", "A1"));
    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("quest/edge/bg", ""));
    vTaskDelay(pdMS_TO_TICKS(30));

    dm_uid_runtime_snapshot_t snap = {0};
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_get_uid_snapshot("uid_edge_gate", &snap));
    TEST_ASSERT_TRUE(snap.slots[0].has_value);
    TEST_ASSERT_EQUAL_STRING("A1", snap.slots[0].last_value);
    TEST_ASSERT_FALSE(snap.slots[1].has_value);

    taskENTER_CRITICAL(&s_capture_lock);
    TEST_ASSERT_EQUAL_UINT32(0, s_capture.count);
    taskEXIT_CRITICAL(&s_capture_lock);
    cleanup_template_runtime();
}

static void test_uid_duplicate_invalid_event_is_suppressed(void)
{
    prepare_template_runtime(true);
    register_uid_edge_runtime_from_json();
    reset_scenario_capture();

    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("reader/e1", "A1"));
    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("reader/e2", "BAD"));
    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("reader/e1", "A1"));
    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("reader/e2", "BAD"));
    TEST_ASSERT_TRUE(wait_for_scenario_capture_count(1, pdMS_TO_TICKS(200)));
    vTaskDelay(pdMS_TO_TICKS(30));

    taskENTER_CRITICAL(&s_capture_lock);
    TEST_ASSERT_EQUAL_UINT32(1, s_capture.count);
    TEST_ASSERT_EQUAL_STRING("uid_edge_gate", s_capture.device_id[0]);
    TEST_ASSERT_EQUAL_STRING("uid_fail", s_capture.scenario_id[0]);
    taskEXIT_CRITICAL(&s_capture_lock);
    cleanup_template_runtime();
}

static void test_uid_duplicate_success_event_is_suppressed(void)
{
    prepare_template_runtime(true);
    register_uid_edge_runtime_from_json();
    reset_scenario_capture();

    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("reader/e1", "A1"));
    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("reader/e2", "B1"));
    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("reader/e1", "A1"));
    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("reader/e2", "B1"));
    TEST_ASSERT_TRUE(wait_for_scenario_capture_count(1, pdMS_TO_TICKS(200)));
    vTaskDelay(pdMS_TO_TICKS(30));

    taskENTER_CRITICAL(&s_capture_lock);
    TEST_ASSERT_EQUAL_UINT32(1, s_capture.count);
    TEST_ASSERT_EQUAL_STRING("uid_edge_gate", s_capture.device_id[0]);
    TEST_ASSERT_EQUAL_STRING("uid_ok", s_capture.scenario_id[0]);
    taskEXIT_CRITICAL(&s_capture_lock);
    cleanup_template_runtime();
}

static void test_mqtt_trigger_event_bus_routing_posts_scenario_event(void)
{
    prepare_template_runtime(true);
    register_mqtt_runtime_from_json();
    reset_scenario_capture();

    event_bus_message_t msg = {
        .type = EVENT_MQTT_MESSAGE,
    };
    strncpy(msg.topic, "quest/button", sizeof(msg.topic) - 1);
    msg.topic[sizeof(msg.topic) - 1] = '\0';
    strncpy(msg.payload, "press", sizeof(msg.payload) - 1);
    msg.payload[sizeof(msg.payload) - 1] = '\0';
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_post(&msg, pdMS_TO_TICKS(100)));

    TEST_ASSERT_TRUE(wait_for_scenario_capture_count(1, pdMS_TO_TICKS(200)));

    taskENTER_CRITICAL(&s_capture_lock);
    TEST_ASSERT_EQUAL_UINT32(1, s_capture.count);
    TEST_ASSERT_EQUAL_STRING("mqtt_gate", s_capture.device_id[0]);
    TEST_ASSERT_EQUAL_STRING("button_press", s_capture.scenario_id[0]);
    taskEXIT_CRITICAL(&s_capture_lock);
    cleanup_template_runtime();
}

static void test_mqtt_trigger_optional_payload_matches_empty_and_non_empty(void)
{
    prepare_template_runtime(true);
    register_mqtt_optional_runtime_from_json();
    reset_scenario_capture();

    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("quest/optional", ""));
    TEST_ASSERT_TRUE(wait_for_scenario_capture_count(1, pdMS_TO_TICKS(200)));

    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("quest/optional", "anything"));
    TEST_ASSERT_TRUE(wait_for_scenario_capture_count(2, pdMS_TO_TICKS(200)));

    taskENTER_CRITICAL(&s_capture_lock);
    TEST_ASSERT_EQUAL_UINT32(2, s_capture.count);
    TEST_ASSERT_EQUAL_STRING("mqtt_optional_gate", s_capture.device_id[0]);
    TEST_ASSERT_EQUAL_STRING("optional_press", s_capture.scenario_id[0]);
    TEST_ASSERT_EQUAL_STRING("mqtt_optional_gate", s_capture.device_id[1]);
    TEST_ASSERT_EQUAL_STRING("optional_press", s_capture.scenario_id[1]);
    taskEXIT_CRITICAL(&s_capture_lock);
    cleanup_template_runtime();
}

static void test_flag_trigger_direct_handle_posts_scenario_events(void)
{
    prepare_template_runtime(true);
    register_flag_runtime_from_json();
    reset_scenario_capture();

    TEST_ASSERT_TRUE(dm_template_runtime_handle_flag("beam_ok", true));
    TEST_ASSERT_TRUE(wait_for_scenario_capture_count(1, pdMS_TO_TICKS(200)));

    taskENTER_CRITICAL(&s_capture_lock);
    TEST_ASSERT_EQUAL_UINT32(1, s_capture.count);
    TEST_ASSERT_EQUAL_STRING("flag_gate", s_capture.device_id[0]);
    TEST_ASSERT_EQUAL_STRING("beam_ready", s_capture.scenario_id[0]);
    taskEXIT_CRITICAL(&s_capture_lock);

    TEST_ASSERT_FALSE(dm_template_runtime_handle_flag("beam_ok", true));
    vTaskDelay(pdMS_TO_TICKS(30));
    taskENTER_CRITICAL(&s_capture_lock);
    TEST_ASSERT_EQUAL_UINT32(1, s_capture.count);
    taskEXIT_CRITICAL(&s_capture_lock);

    TEST_ASSERT_TRUE(dm_template_runtime_handle_flag("beam_ok", false));
    TEST_ASSERT_TRUE(wait_for_scenario_capture_count(2, pdMS_TO_TICKS(200)));

    taskENTER_CRITICAL(&s_capture_lock);
    TEST_ASSERT_EQUAL_UINT32(2, s_capture.count);
    TEST_ASSERT_EQUAL_STRING("flag_gate", s_capture.device_id[1]);
    TEST_ASSERT_EQUAL_STRING("beam_lost", s_capture.scenario_id[1]);
    taskEXIT_CRITICAL(&s_capture_lock);
    cleanup_template_runtime();
}

static void test_flag_trigger_event_bus_routing_posts_scenario_event(void)
{
    prepare_template_runtime(true);
    register_flag_runtime_from_json();
    reset_scenario_capture();

    event_bus_message_t msg = {
        .type = EVENT_FLAG_CHANGED,
    };
    strncpy(msg.topic, "beam_ok", sizeof(msg.topic) - 1);
    msg.topic[sizeof(msg.topic) - 1] = '\0';
    strncpy(msg.payload, "true", sizeof(msg.payload) - 1);
    msg.payload[sizeof(msg.payload) - 1] = '\0';
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_post(&msg, pdMS_TO_TICKS(100)));

    TEST_ASSERT_TRUE(wait_for_scenario_capture_count(1, pdMS_TO_TICKS(200)));

    taskENTER_CRITICAL(&s_capture_lock);
    TEST_ASSERT_EQUAL_UINT32(1, s_capture.count);
    TEST_ASSERT_EQUAL_STRING("flag_gate", s_capture.device_id[0]);
    TEST_ASSERT_EQUAL_STRING("beam_ready", s_capture.scenario_id[0]);
    taskEXIT_CRITICAL(&s_capture_lock);
    cleanup_template_runtime();
}

static void test_flag_trigger_matches_case_insensitive_flag_name(void)
{
    prepare_template_runtime(true);
    register_flag_runtime_from_json();
    reset_scenario_capture();

    TEST_ASSERT_TRUE(dm_template_runtime_handle_flag("BEAM_OK", true));
    TEST_ASSERT_TRUE(wait_for_scenario_capture_count(1, pdMS_TO_TICKS(200)));

    taskENTER_CRITICAL(&s_capture_lock);
    TEST_ASSERT_EQUAL_UINT32(1, s_capture.count);
    TEST_ASSERT_EQUAL_STRING("flag_gate", s_capture.device_id[0]);
    TEST_ASSERT_EQUAL_STRING("beam_ready", s_capture.scenario_id[0]);
    taskEXIT_CRITICAL(&s_capture_lock);
    cleanup_template_runtime();
}

static void test_condition_direct_handle_posts_false_then_true_scenarios(void)
{
    prepare_template_runtime(true);
    register_condition_runtime_from_json();
    reset_scenario_capture();

    TEST_ASSERT_TRUE(dm_template_runtime_handle_flag("beam_ok", true));
    TEST_ASSERT_TRUE(wait_for_scenario_capture_count(1, pdMS_TO_TICKS(200)));

    taskENTER_CRITICAL(&s_capture_lock);
    TEST_ASSERT_EQUAL_UINT32(1, s_capture.count);
    TEST_ASSERT_EQUAL_STRING("cond_gate", s_capture.device_id[0]);
    TEST_ASSERT_EQUAL_STRING("not_ready", s_capture.scenario_id[0]);
    taskEXIT_CRITICAL(&s_capture_lock);

    TEST_ASSERT_TRUE(dm_template_runtime_handle_flag("door_closed", true));
    TEST_ASSERT_TRUE(wait_for_scenario_capture_count(2, pdMS_TO_TICKS(200)));

    taskENTER_CRITICAL(&s_capture_lock);
    TEST_ASSERT_EQUAL_UINT32(2, s_capture.count);
    TEST_ASSERT_EQUAL_STRING("cond_gate", s_capture.device_id[1]);
    TEST_ASSERT_EQUAL_STRING("all_ready", s_capture.scenario_id[1]);
    taskEXIT_CRITICAL(&s_capture_lock);

    TEST_ASSERT_TRUE(dm_template_runtime_handle_flag("door_closed", true));
    vTaskDelay(pdMS_TO_TICKS(30));
    taskENTER_CRITICAL(&s_capture_lock);
    TEST_ASSERT_EQUAL_UINT32(2, s_capture.count);
    taskEXIT_CRITICAL(&s_capture_lock);

    TEST_ASSERT_TRUE(dm_template_runtime_handle_flag("beam_ok", false));
    TEST_ASSERT_TRUE(wait_for_scenario_capture_count(3, pdMS_TO_TICKS(200)));

    taskENTER_CRITICAL(&s_capture_lock);
    TEST_ASSERT_EQUAL_UINT32(3, s_capture.count);
    TEST_ASSERT_EQUAL_STRING("cond_gate", s_capture.device_id[2]);
    TEST_ASSERT_EQUAL_STRING("not_ready", s_capture.scenario_id[2]);
    taskEXIT_CRITICAL(&s_capture_lock);
    cleanup_template_runtime();
}

static void test_condition_event_bus_routing_posts_true_scenario(void)
{
    prepare_template_runtime(true);
    register_condition_runtime_from_json();
    reset_scenario_capture();

    event_bus_message_t msg = {
        .type = EVENT_FLAG_CHANGED,
    };
    strncpy(msg.topic, "beam_ok", sizeof(msg.topic) - 1);
    msg.topic[sizeof(msg.topic) - 1] = '\0';
    strncpy(msg.payload, "true", sizeof(msg.payload) - 1);
    msg.payload[sizeof(msg.payload) - 1] = '\0';
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_post(&msg, pdMS_TO_TICKS(100)));
    TEST_ASSERT_TRUE(wait_for_scenario_capture_count(1, pdMS_TO_TICKS(200)));

    memset(&msg, 0, sizeof(msg));
    msg.type = EVENT_FLAG_CHANGED;
    strncpy(msg.topic, "door_closed", sizeof(msg.topic) - 1);
    msg.topic[sizeof(msg.topic) - 1] = '\0';
    strncpy(msg.payload, "true", sizeof(msg.payload) - 1);
    msg.payload[sizeof(msg.payload) - 1] = '\0';
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_post(&msg, pdMS_TO_TICKS(100)));
    TEST_ASSERT_TRUE(wait_for_scenario_capture_count(2, pdMS_TO_TICKS(200)));

    taskENTER_CRITICAL(&s_capture_lock);
    TEST_ASSERT_EQUAL_UINT32(2, s_capture.count);
    TEST_ASSERT_EQUAL_STRING("cond_gate", s_capture.device_id[0]);
    TEST_ASSERT_EQUAL_STRING("not_ready", s_capture.scenario_id[0]);
    TEST_ASSERT_EQUAL_STRING("cond_gate", s_capture.device_id[1]);
    TEST_ASSERT_EQUAL_STRING("all_ready", s_capture.scenario_id[1]);
    taskEXIT_CRITICAL(&s_capture_lock);
    cleanup_template_runtime();
}

static void test_condition_any_direct_handle_posts_true_and_false_scenarios(void)
{
    prepare_template_runtime(true);
    register_condition_any_runtime_from_json();
    reset_scenario_capture();

    TEST_ASSERT_TRUE(dm_template_runtime_handle_flag("beam_ok", true));
    TEST_ASSERT_TRUE(wait_for_scenario_capture_count(1, pdMS_TO_TICKS(200)));

    taskENTER_CRITICAL(&s_capture_lock);
    TEST_ASSERT_EQUAL_UINT32(1, s_capture.count);
    TEST_ASSERT_EQUAL_STRING("cond_any_gate", s_capture.device_id[0]);
    TEST_ASSERT_EQUAL_STRING("any_ready", s_capture.scenario_id[0]);
    taskEXIT_CRITICAL(&s_capture_lock);

    TEST_ASSERT_TRUE(dm_template_runtime_handle_flag("beam_ok", false));
    TEST_ASSERT_TRUE(wait_for_scenario_capture_count(2, pdMS_TO_TICKS(200)));

    taskENTER_CRITICAL(&s_capture_lock);
    TEST_ASSERT_EQUAL_UINT32(2, s_capture.count);
    TEST_ASSERT_EQUAL_STRING("cond_any_gate", s_capture.device_id[1]);
    TEST_ASSERT_EQUAL_STRING("none_ready", s_capture.scenario_id[1]);
    taskEXIT_CRITICAL(&s_capture_lock);

    TEST_ASSERT_TRUE(dm_template_runtime_handle_flag("door_closed", true));
    TEST_ASSERT_TRUE(wait_for_scenario_capture_count(3, pdMS_TO_TICKS(200)));

    taskENTER_CRITICAL(&s_capture_lock);
    TEST_ASSERT_EQUAL_UINT32(3, s_capture.count);
    TEST_ASSERT_EQUAL_STRING("cond_any_gate", s_capture.device_id[2]);
    TEST_ASSERT_EQUAL_STRING("any_ready", s_capture.scenario_id[2]);
    taskEXIT_CRITICAL(&s_capture_lock);
    cleanup_template_runtime();
}

static void test_condition_matches_case_insensitive_flag_names(void)
{
    prepare_template_runtime(true);
    register_condition_runtime_from_json();
    reset_scenario_capture();

    TEST_ASSERT_TRUE(dm_template_runtime_handle_flag("BEAM_OK", true));
    TEST_ASSERT_TRUE(wait_for_scenario_capture_count(1, pdMS_TO_TICKS(200)));

    TEST_ASSERT_TRUE(dm_template_runtime_handle_flag("DOOR_CLOSED", true));
    TEST_ASSERT_TRUE(wait_for_scenario_capture_count(2, pdMS_TO_TICKS(200)));

    taskENTER_CRITICAL(&s_capture_lock);
    TEST_ASSERT_EQUAL_UINT32(2, s_capture.count);
    TEST_ASSERT_EQUAL_STRING("cond_gate", s_capture.device_id[0]);
    TEST_ASSERT_EQUAL_STRING("not_ready", s_capture.scenario_id[0]);
    TEST_ASSERT_EQUAL_STRING("cond_gate", s_capture.device_id[1]);
    TEST_ASSERT_EQUAL_STRING("all_ready", s_capture.scenario_id[1]);
    taskEXIT_CRITICAL(&s_capture_lock);
    cleanup_template_runtime();
}

static void test_interval_runtime_posts_periodic_scenario_events(void)
{
    prepare_template_runtime(true);
    register_interval_runtime_from_json();
    reset_scenario_capture();

    TEST_ASSERT_TRUE(wait_for_scenario_capture_count(2, pdMS_TO_TICKS(400)));

    taskENTER_CRITICAL(&s_capture_lock);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(2, s_capture.count);
    TEST_ASSERT_EQUAL_STRING("interval_gate", s_capture.device_id[0]);
    TEST_ASSERT_EQUAL_STRING("pulse", s_capture.scenario_id[0]);
    TEST_ASSERT_EQUAL_STRING("interval_gate", s_capture.device_id[1]);
    TEST_ASSERT_EQUAL_STRING("pulse", s_capture.scenario_id[1]);
    taskEXIT_CRITICAL(&s_capture_lock);
    cleanup_template_runtime();
}

static void test_sequence_runtime_reset_on_error_posts_fail_and_restarts(void)
{
    prepare_template_runtime(true);
    register_sequence_reset_runtime_from_json();
    reset_scenario_capture();

    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("quest/reset/1", "red"));
    TEST_ASSERT_TRUE(wait_for_sequence_step("seq_reset_lock", 1, pdMS_TO_TICKS(200)));

    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("quest/reset/1", "red"));
    TEST_ASSERT_TRUE(wait_for_scenario_capture_count(1, pdMS_TO_TICKS(200)));

    taskENTER_CRITICAL(&s_capture_lock);
    TEST_ASSERT_EQUAL_UINT32(1, s_capture.count);
    TEST_ASSERT_EQUAL_STRING("seq_reset_lock", s_capture.device_id[0]);
    TEST_ASSERT_EQUAL_STRING("seq_fail", s_capture.scenario_id[0]);
    taskEXIT_CRITICAL(&s_capture_lock);

    dm_sequence_runtime_snapshot_t snap = {0};
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_get_sequence_snapshot("seq_reset_lock", &snap));
    TEST_ASSERT_FALSE(snap.active);
    TEST_ASSERT_EQUAL_UINT8(0, snap.current_step_index);
    TEST_ASSERT_EQUAL_STRING("failed", snap.state);

    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("quest/reset/1", "red"));
    TEST_ASSERT_TRUE(wait_for_sequence_step("seq_reset_lock", 1, pdMS_TO_TICKS(200)));

    memset(&snap, 0, sizeof(snap));
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_get_sequence_snapshot("seq_reset_lock", &snap));
    TEST_ASSERT_TRUE(snap.active);
    TEST_ASSERT_EQUAL_UINT8(1, snap.current_step_index);
    TEST_ASSERT_EQUAL_STRING("active", snap.state);
    TEST_ASSERT_EQUAL_STRING("quest/reset/2", snap.expected_topic);
    cleanup_template_runtime();
}

static void test_sequence_runtime_without_reset_on_error_keeps_progress(void)
{
    prepare_template_runtime(true);
    register_sequence_no_reset_runtime_from_json();
    reset_scenario_capture();

    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("quest/nr/1", "red"));
    TEST_ASSERT_TRUE(wait_for_sequence_step("seq_no_reset_lock", 1, pdMS_TO_TICKS(200)));

    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("quest/nr/1", "red"));
    vTaskDelay(pdMS_TO_TICKS(30));

    dm_sequence_runtime_snapshot_t snap = {0};
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_get_sequence_snapshot("seq_no_reset_lock", &snap));
    TEST_ASSERT_TRUE(snap.active);
    TEST_ASSERT_EQUAL_UINT8(1, snap.current_step_index);
    TEST_ASSERT_FALSE(snap.reset_on_error);
    TEST_ASSERT_EQUAL_STRING("active", snap.state);

    taskENTER_CRITICAL(&s_capture_lock);
    TEST_ASSERT_EQUAL_UINT32(0, s_capture.count);
    taskEXIT_CRITICAL(&s_capture_lock);

    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("quest/nr/2", "blue"));
    TEST_ASSERT_TRUE(wait_for_scenario_capture_count(1, pdMS_TO_TICKS(200)));

    taskENTER_CRITICAL(&s_capture_lock);
    TEST_ASSERT_EQUAL_UINT32(1, s_capture.count);
    TEST_ASSERT_EQUAL_STRING("seq_no_reset_lock", s_capture.device_id[0]);
    TEST_ASSERT_EQUAL_STRING("seq_ok", s_capture.scenario_id[0]);
    taskEXIT_CRITICAL(&s_capture_lock);
    cleanup_template_runtime();
}

static void test_signal_runtime_completion_does_not_repeat_on_extra_heartbeats(void)
{
    prepare_template_runtime(true);
    register_signal_runtime_from_json();
    reset_scenario_capture();

    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("quest/heartbeat", ""));
    vTaskDelay(pdMS_TO_TICKS(70));
    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("quest/heartbeat", ""));
    vTaskDelay(pdMS_TO_TICKS(70));
    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("quest/heartbeat", ""));
    TEST_ASSERT_TRUE(wait_for_scenario_capture_count(1, pdMS_TO_TICKS(200)));

    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("quest/heartbeat", ""));
    vTaskDelay(pdMS_TO_TICKS(30));
    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("quest/heartbeat", ""));
    vTaskDelay(pdMS_TO_TICKS(30));

    taskENTER_CRITICAL(&s_capture_lock);
    TEST_ASSERT_EQUAL_UINT32(1, s_capture.count);
    TEST_ASSERT_EQUAL_STRING("signal_hold", s_capture.device_id[0]);
    TEST_ASSERT_EQUAL_STRING("signal_complete", s_capture.scenario_id[0]);
    taskEXIT_CRITICAL(&s_capture_lock);

    dm_signal_runtime_snapshot_t snap = {0};
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_get_signal_snapshot("signal_hold", &snap));
    TEST_ASSERT_FALSE(snap.active);
    TEST_ASSERT_TRUE(snap.completed);
    TEST_ASSERT_EQUAL_STRING("completed", snap.state);
    cleanup_template_runtime();
}

static void test_signal_runtime_reset_topic_via_mqtt_returns_to_idle(void)
{
    prepare_template_runtime(true);
    register_signal_runtime_from_json();
    reset_scenario_capture();

    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("quest/heartbeat", ""));
    TEST_ASSERT_TRUE(wait_for_signal_state("signal_hold", "active", pdMS_TO_TICKS(200)));

    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("quest/reset", ""));
    TEST_ASSERT_TRUE(wait_for_signal_state("signal_hold", "idle", pdMS_TO_TICKS(200)));

    dm_signal_runtime_snapshot_t snap = {0};
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_get_signal_snapshot("signal_hold", &snap));
    TEST_ASSERT_FALSE(snap.active);
    TEST_ASSERT_FALSE(snap.completed);
    TEST_ASSERT_EQUAL_UINT32(0, snap.progress_ms);
    TEST_ASSERT_EQUAL_STRING("idle", snap.state);

    taskENTER_CRITICAL(&s_capture_lock);
    TEST_ASSERT_EQUAL_UINT32(0, s_capture.count);
    taskEXIT_CRITICAL(&s_capture_lock);
    cleanup_template_runtime();
}

void register_template_runtime_integration_tests(void)
{
    RUN_TEST(test_template_runtime_init_reset_smoke);
    RUN_TEST(test_template_runtime_missing_snapshots_return_not_found);
    RUN_TEST(test_template_runtime_missing_manual_resets_return_not_found);
    RUN_TEST(test_template_runtime_invalid_args_return_invalid_arg);
    RUN_TEST(test_uid_runtime_snapshot_tracks_last_values);
    RUN_TEST(test_uid_start_topic_resets_snapshot_values);
    RUN_TEST(test_uid_runtime_event_bus_routing_updates_snapshot);
    RUN_TEST(test_uid_invalid_value_posts_fail_scenario);
    RUN_TEST(test_uid_start_topic_payload_mismatch_does_not_reset_snapshot);
    RUN_TEST(test_uid_start_topic_debounce_suppresses_immediate_second_reset);
    RUN_TEST(test_uid_bg_start_topic_without_track_is_handled_without_side_effects);
    RUN_TEST(test_uid_duplicate_invalid_event_is_suppressed);
    RUN_TEST(test_uid_duplicate_success_event_is_suppressed);
    RUN_TEST(test_mqtt_trigger_event_bus_routing_posts_scenario_event);
    RUN_TEST(test_mqtt_trigger_optional_payload_matches_empty_and_non_empty);
    RUN_TEST(test_flag_trigger_direct_handle_posts_scenario_events);
    RUN_TEST(test_flag_trigger_event_bus_routing_posts_scenario_event);
    RUN_TEST(test_flag_trigger_matches_case_insensitive_flag_name);
    RUN_TEST(test_condition_direct_handle_posts_false_then_true_scenarios);
    RUN_TEST(test_condition_event_bus_routing_posts_true_scenario);
    RUN_TEST(test_condition_any_direct_handle_posts_true_and_false_scenarios);
    RUN_TEST(test_condition_matches_case_insensitive_flag_names);
    RUN_TEST(test_interval_runtime_posts_periodic_scenario_events);
    RUN_TEST(test_sequence_runtime_snapshot_and_manual_reset);
    RUN_TEST(test_sequence_runtime_completion_snapshot);
    RUN_TEST(test_sequence_runtime_timeout_snapshot);
    RUN_TEST(test_sequence_runtime_event_bus_mqtt_routing_updates_snapshot);
    RUN_TEST(test_sequence_runtime_reset_on_error_posts_fail_and_restarts);
    RUN_TEST(test_sequence_runtime_without_reset_on_error_keeps_progress);
    RUN_TEST(test_signal_runtime_snapshot_and_manual_reset);
    RUN_TEST(test_signal_runtime_completion_snapshot);
    RUN_TEST(test_signal_runtime_completion_does_not_repeat_on_extra_heartbeats);
    RUN_TEST(test_signal_runtime_reset_topic_via_mqtt_returns_to_idle);
    RUN_TEST(test_signal_runtime_timeout_snapshot);
    RUN_TEST(test_signal_runtime_event_bus_routing_updates_snapshot);
}
