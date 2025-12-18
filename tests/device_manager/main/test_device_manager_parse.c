#include <stdlib.h>
#include <string.h>

#include "unity.h"
#include "device_manager_internal.h"
#include "test_dm_helpers.h"

static void test_sensor_monitor_parses_channels(void)
{
    const char *json =
        "{"
        " \"schema\":1,"
        " \"devices\":[{"
        "   \"id\":\"sensor_room\","
        "   \"display_name\":\"Room sensors\","
        "   \"template\":{"
        "     \"type\":\"sensor_monitor\","
        "     \"sensor\":{"
        "       \"topic\":\"env/room\","
        "       \"name\":\"Room sensors\","
        "       \"units\":\"C\","
        "       \"decimals\":1,"
        "       \"parse_mode\":\"raw_number\","
        "       \"display_monitor\":true,"
        "       \"history_enabled\":true,"
        "       \"channels\":[{"
        "         \"id\":\"temp\","
        "         \"name\":\"Temperature\","
        "         \"topic\":\"env/room/temp\","
        "         \"parse_mode\":\"json_number\","
        "         \"json_key\":\"value\","
        "         \"units\":\"C\","
        "         \"decimals\":1,"
        "         \"warn\":{\"value\":30.0,\"compare\":\"above_or_equal\",\"scenario\":\"warn_temp\"},"
        "         \"alarm\":{\"value\":35.5,\"compare\":\"above_or_equal\",\"scenario\":\"alarm_temp\"}"
        "       },{"
        "         \"id\":\"humidity\","
        "         \"name\":\"Humidity\","
        "         \"topic\":\"env/room/h\","
        "         \"units\":\"%\","
        "         \"decimals\":0"
        "       }]"
        "     }"
        "   }"
        " }]"
        "}";

    device_manager_config_t *cfg = dm_test_parse_config_json(json, 1);
    const device_descriptor_t *dev = dm_test_require_template(cfg, "sensor_room", DM_TEMPLATE_TYPE_SENSOR_MONITOR);
    const dm_sensor_template_t *tpl = &dev->template_config.data.sensor;
    TEST_ASSERT_EQUAL_UINT8(2, tpl->channel_count);
    TEST_ASSERT_EQUAL_STRING("env/room", tpl->topic);
    TEST_ASSERT_TRUE(tpl->display_monitor);
    TEST_ASSERT_TRUE(tpl->history_enabled);

    const dm_sensor_channel_t *temp_ch = &tpl->channels[0];
    TEST_ASSERT_EQUAL_STRING("temp", temp_ch->id);
    TEST_ASSERT_EQUAL_STRING("env/room/temp", temp_ch->topic);
    TEST_ASSERT_EQUAL(DM_SENSOR_PARSE_JSON_NUMBER, temp_ch->parse_mode);
    TEST_ASSERT_EQUAL_STRING("value", temp_ch->json_key);
    TEST_ASSERT_TRUE(temp_ch->warn.enabled);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 30.0f, temp_ch->warn.threshold);
    TEST_ASSERT_TRUE(temp_ch->alarm.enabled);

    const dm_sensor_channel_t *hum_ch = &tpl->channels[1];
    TEST_ASSERT_EQUAL(DM_SENSOR_PARSE_RAW_NUMBER, hum_ch->parse_mode);
    TEST_ASSERT_EQUAL_STRING("", hum_ch->json_key);
    TEST_ASSERT_FALSE(hum_ch->warn.enabled);

    dm_test_free_config(cfg);
}

static void test_sensor_monitor_invalid_channel_falls_back_to_default(void)
{
    const char *json =
        "{"
        " \"schema\":1,"
        " \"devices\":[{"
        "   \"id\":\"sensor_bad\","
        "   \"template\":{"
        "     \"type\":\"sensor_monitor\","
        "     \"sensor\":{"
        "       \"topic\":\"env/base\","
        "       \"parse_mode\":\"raw_number\","
        "       \"channels\":[{"
        "         \"id\":\"ch\","
        "         \"topic\":\"env/base/ch\","
        "         \"parse_mode\":\"json_number\""
        "       }]"
        "     }"
        "   }"
        " }]"
        "}";

    device_manager_config_t *cfg = dm_test_parse_config_json(json, 1);
    const device_descriptor_t *dev = dm_test_require_template(cfg, "sensor_bad", DM_TEMPLATE_TYPE_SENSOR_MONITOR);
    const dm_sensor_template_t *tpl = &dev->template_config.data.sensor;
    TEST_ASSERT_EQUAL_UINT8(1, tpl->channel_count);
    const dm_sensor_channel_t *ch = &tpl->channels[0];
    TEST_ASSERT_EQUAL_STRING("env/base", ch->topic);
    TEST_ASSERT_EQUAL(DM_SENSOR_PARSE_RAW_NUMBER, ch->parse_mode);
    TEST_ASSERT_EQUAL_STRING("", ch->json_key);
    dm_test_free_config(cfg);
}

static void test_sensor_monitor_requires_base_json_key_in_json_mode(void)
{
    const char *json =
        "{"
        " \"schema\":1,"
        " \"devices\":[{"
        "   \"id\":\"sensor_defaults\","
        "   \"template\":{"
        "     \"type\":\"sensor_monitor\","
        "     \"sensor\":{"
        "       \"topic\":\"env/default\","
        "       \"name\":\"Defaults\","
        "       \"parse_mode\":\"json_number\""
        "     }"
        "   }"
        " }]"
        "}";

    device_manager_config_t *cfg = dm_test_alloc_config(1);
    dm_cjson_install_hooks();
    cJSON *root = cJSON_Parse(json);
    dm_cjson_reset_hooks();
    TEST_ASSERT_NOT_NULL(root);
    bool ok = dm_populate_config_from_json(cfg, root);
    cJSON_Delete(root);
    TEST_ASSERT_TRUE(ok);
    const device_descriptor_t *dev = &cfg->devices[0];
    TEST_ASSERT_FALSE(dev->template_assigned);
    dm_test_free_config(cfg);
}

static void test_all_template_types_parse(void)
{
    const char *json =
        "{"
        " \"schema\":1,"
        " \"devices\":["
        " {\"id\":\"uid_dev\",\"template\":{\"type\":\"uid_validator\",\"uid\":{"
        "    \"slots\":[{\"source_id\":\"reader1\",\"label\":\"Entrance\",\"values\":[\"A\",\"B\"]}],"
        "    \"success_topic\":\"quest/ok\",\"success_payload\":\"open\",\"fail_topic\":\"quest/fail\"}}},"
        " {\"id\":\"signal_dev\",\"template\":{\"type\":\"signal_hold\",\"signal\":{"
        "    \"signal_topic\":\"laser/cmd\",\"signal_payload_on\":\"ON\",\"signal_payload_off\":\"OFF\","
        "    \"heartbeat_topic\":\"laser/hb\",\"reset_topic\":\"laser/reset\","
        "    \"required_hold_ms\":5000,\"heartbeat_timeout_ms\":1500,"
        "    \"hold_track\":\"/sdcard/hold.mp3\",\"complete_track\":\"/sdcard/done.mp3\"}}},"
        " {\"id\":\"mqtt_dev\",\"template\":{\"type\":\"on_mqtt_event\",\"mqtt\":{"
        "    \"rules\":[{\"name\":\"scan\",\"topic\":\"quest/scan\",\"payload\":\"start\",\"payload_required\":true,\"scenario\":\"scan_go\"}]}}},"
        " {\"id\":\"flag_dev\",\"template\":{\"type\":\"on_flag\",\"flag\":{"
        "    \"rules\":[{\"name\":\"beam\",\"flag\":\"beam_ok\",\"required_state\":true,\"scenario\":\"beam_ready\"}]}}},"
        " {\"id\":\"cond_dev\",\"template\":{\"type\":\"if_condition\",\"condition\":{"
        "    \"mode\":\"all\",\"true_scenario\":\"all_ok\",\"false_scenario\":\"wait\","
        "    \"rules\":[{\"flag\":\"beam_ok\",\"required_state\":true}]}}},"
        " {\"id\":\"interval_dev\",\"template\":{\"type\":\"interval_task\",\"interval\":{"
        "    \"interval_ms\":2000,\"scenario\":\"tick\"}}},"
        " {\"id\":\"seq_dev\",\"template\":{\"type\":\"sequence_lock\",\"sequence\":{"
        "    \"steps\":[{\"topic\":\"seq/step\",\"payload\":\"1\",\"payload_required\":true,"
        "                \"hint_topic\":\"seq/hint\",\"hint_payload\":\"try\",\"hint_audio_track\":\"/sdcard/hint.mp3\"}],"
        "    \"timeout_ms\":3000,\"reset_on_error\":true,"
        "    \"success_topic\":\"seq/success\",\"success_payload\":\"ok\","
        "    \"fail_topic\":\"seq/fail\",\"fail_payload\":\"no\"}}},"
        " {\"id\":\"sensor_dev\",\"template\":{\"type\":\"sensor_monitor\",\"sensor\":{"
        "    \"topic\":\"env/main\",\"channels\":[{\"id\":\"temp\",\"topic\":\"env/main/temp\",\"parse_mode\":\"json_number\",\"json_key\":\"value\"}]}}}"
        " ]"
        "}";

    device_manager_config_t *cfg = dm_test_parse_config_json(json, 8);

    const device_descriptor_t *uid_dev = dm_test_require_template(cfg, "uid_dev", DM_TEMPLATE_TYPE_UID);
    const dm_uid_template_t *uid_tpl = &uid_dev->template_config.data.uid;
    TEST_ASSERT_EQUAL_UINT8(1, uid_tpl->slot_count);
    TEST_ASSERT_EQUAL_UINT8(2, uid_tpl->slots[0].value_count);
    TEST_ASSERT_EQUAL_STRING("quest/ok", uid_tpl->success_topic);

    const device_descriptor_t *signal_dev = dm_test_require_template(cfg, "signal_dev", DM_TEMPLATE_TYPE_SIGNAL_HOLD);
    const dm_signal_hold_template_t *signal_tpl = &signal_dev->template_config.data.signal;
    TEST_ASSERT_EQUAL_STRING("laser/hb", signal_tpl->heartbeat_topic);
    TEST_ASSERT_EQUAL_UINT32(5000, signal_tpl->required_hold_ms);

    const device_descriptor_t *mqtt_dev = dm_test_require_template(cfg, "mqtt_dev", DM_TEMPLATE_TYPE_MQTT_TRIGGER);
    const dm_mqtt_trigger_template_t *mqtt_tpl = &mqtt_dev->template_config.data.mqtt;
    TEST_ASSERT_EQUAL_UINT8(1, mqtt_tpl->rule_count);
    TEST_ASSERT_TRUE(mqtt_tpl->rules[0].payload_required);

    const device_descriptor_t *flag_dev = dm_test_require_template(cfg, "flag_dev", DM_TEMPLATE_TYPE_FLAG_TRIGGER);
    const dm_flag_trigger_template_t *flag_tpl = &flag_dev->template_config.data.flag;
    TEST_ASSERT_EQUAL_UINT8(1, flag_tpl->rule_count);
    TEST_ASSERT_TRUE(flag_tpl->rules[0].required_state);

    const device_descriptor_t *cond_dev = dm_test_require_template(cfg, "cond_dev", DM_TEMPLATE_TYPE_IF_CONDITION);
    const dm_condition_template_t *cond_tpl = &cond_dev->template_config.data.condition;
    TEST_ASSERT_EQUAL(DEVICE_CONDITION_ALL, cond_tpl->mode);
    TEST_ASSERT_EQUAL_STRING("all_ok", cond_tpl->true_scenario);

    const device_descriptor_t *interval_dev = dm_test_require_template(cfg, "interval_dev", DM_TEMPLATE_TYPE_INTERVAL_TASK);
    const dm_interval_task_template_t *interval_tpl = &interval_dev->template_config.data.interval;
    TEST_ASSERT_EQUAL_UINT32(2000, interval_tpl->interval_ms);
    TEST_ASSERT_EQUAL_STRING("tick", interval_tpl->scenario);

    const device_descriptor_t *seq_dev = dm_test_require_template(cfg, "seq_dev", DM_TEMPLATE_TYPE_SEQUENCE_LOCK);
    const dm_sequence_template_t *seq_tpl = &seq_dev->template_config.data.sequence;
    TEST_ASSERT_EQUAL_UINT8(1, seq_tpl->step_count);
    TEST_ASSERT_TRUE(seq_tpl->steps[0].payload_required);
    TEST_ASSERT_EQUAL_STRING("seq/success", seq_tpl->success_topic);

    const device_descriptor_t *sensor_dev = dm_test_require_template(cfg, "sensor_dev", DM_TEMPLATE_TYPE_SENSOR_MONITOR);
    const dm_sensor_template_t *sensor_tpl = &sensor_dev->template_config.data.sensor;
    TEST_ASSERT_EQUAL_UINT8(1, sensor_tpl->channel_count);
    TEST_ASSERT_EQUAL(DM_SENSOR_PARSE_JSON_NUMBER, sensor_tpl->channels[0].parse_mode);

    dm_test_free_config(cfg);
}

void register_device_manager_parse_tests(void)
{
    RUN_TEST(test_sensor_monitor_parses_channels);
    RUN_TEST(test_sensor_monitor_invalid_channel_falls_back_to_default);
    RUN_TEST(test_sensor_monitor_requires_base_json_key_in_json_mode);
    RUN_TEST(test_all_template_types_parse);
}
