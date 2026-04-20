#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "unity.h"
#include "device_manager_internal.h"
#include "dm_storage.h"
#include "test_dm_helpers.h"

static void test_uid_validator_parses_actions_and_background_audio(void)
{
    const char *json =
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
        "       \"broadcast_topic\":\"quest/broadcast\","
        "       \"broadcast_payload\":\"scan\","
        "       \"success_topic\":\"quest/ok\","
        "       \"success_payload\":\"unlock\","
        "       \"success_audio_track\":\"/sdcard/ok.mp3\","
        "       \"success_signal_topic\":\"relay/cmd\","
        "       \"success_signal_payload\":\"ON\","
        "       \"fail_topic\":\"quest/fail\","
        "       \"fail_payload\":\"retry\","
        "       \"fail_audio_track\":\"/sdcard/fail.mp3\","
        "       \"fail_signal_topic\":\"relay/cmd\","
        "       \"fail_signal_payload\":\"OFF\","
        "       \"bg_start_topic\":\"quest/bg/start\","
        "       \"bg_track\":\"/sdcard/bg.mp3\","
        "       \"slots\":["
        "         {\"source_id\":\"reader/1\",\"label\":\"Reader 1\",\"values\":[\"A1\",\"A2\"]},"
        "         {\"source_id\":\"reader/2\",\"label\":\"Reader 2\",\"values\":[\"B1\"]}"
        "       ]"
        "     }"
        "   }"
        " }]"
        "}";

    device_manager_config_t *cfg = dm_test_parse_config_json(json, 1);
    const device_descriptor_t *dev =
        dm_test_require_template(cfg, "uid_gate", DM_TEMPLATE_TYPE_UID);
    const dm_uid_template_t *tpl = &dev->template_config.data.uid;

    TEST_ASSERT_EQUAL_UINT8(2, tpl->slot_count);
    TEST_ASSERT_EQUAL_STRING("quest/start", tpl->start_topic);
    TEST_ASSERT_EQUAL_STRING("go", tpl->start_payload);
    TEST_ASSERT_EQUAL_STRING("quest/broadcast", tpl->broadcast_topic);
    TEST_ASSERT_EQUAL_STRING("scan", tpl->broadcast_payload);
    TEST_ASSERT_EQUAL_STRING("quest/ok", tpl->success_topic);
    TEST_ASSERT_EQUAL_STRING("unlock", tpl->success_payload);
    TEST_ASSERT_EQUAL_STRING("/sdcard/ok.mp3", tpl->success_audio_track);
    TEST_ASSERT_EQUAL_STRING("relay/cmd", tpl->success_signal_topic);
    TEST_ASSERT_EQUAL_STRING("ON", tpl->success_signal_payload);
    TEST_ASSERT_EQUAL_STRING("quest/fail", tpl->fail_topic);
    TEST_ASSERT_EQUAL_STRING("retry", tpl->fail_payload);
    TEST_ASSERT_EQUAL_STRING("/sdcard/fail.mp3", tpl->fail_audio_track);
    TEST_ASSERT_EQUAL_STRING("relay/cmd", tpl->fail_signal_topic);
    TEST_ASSERT_EQUAL_STRING("OFF", tpl->fail_signal_payload);
    TEST_ASSERT_EQUAL_STRING("quest/bg/start", tpl->bg_start_topic);
    TEST_ASSERT_EQUAL_STRING("/sdcard/bg.mp3", tpl->bg_track);
    TEST_ASSERT_EQUAL_STRING("reader/1", tpl->slots[0].source_id);
    TEST_ASSERT_EQUAL_STRING("Reader 1", tpl->slots[0].label);
    TEST_ASSERT_EQUAL_UINT8(2, tpl->slots[0].value_count);
    TEST_ASSERT_EQUAL_STRING("A1", tpl->slots[0].values[0]);
    TEST_ASSERT_EQUAL_STRING("A2", tpl->slots[0].values[1]);
    TEST_ASSERT_EQUAL_STRING("reader/2", tpl->slots[1].source_id);
    TEST_ASSERT_EQUAL_STRING("Reader 2", tpl->slots[1].label);
    TEST_ASSERT_EQUAL_UINT8(1, tpl->slots[1].value_count);
    TEST_ASSERT_EQUAL_STRING("B1", tpl->slots[1].values[0]);

    dm_test_free_config(cfg);
}

static void test_sequence_lock_parses_steps_and_outcomes(void)
{
    const char *json =
        "{"
        " \"schema\":1,"
        " \"devices\":[{"
        "   \"id\":\"seq_lock\","
        "   \"display_name\":\"Sequence Lock\","
        "   \"template\":{"
        "     \"type\":\"sequence_lock\","
        "     \"sequence\":{"
        "       \"timeout_ms\":15000,"
        "       \"reset_on_error\":false,"
        "       \"success_topic\":\"quest/seq/ok\","
        "       \"success_payload\":\"open\","
        "       \"success_audio_track\":\"/sdcard/seq_ok.mp3\","
        "       \"success_scenario\":\"unlock_door\","
        "       \"fail_topic\":\"quest/seq/fail\","
        "       \"fail_payload\":\"reset\","
        "       \"fail_audio_track\":\"/sdcard/seq_fail.mp3\","
        "       \"fail_scenario\":\"alarm\","
        "       \"steps\":["
        "         {"
        "           \"topic\":\"quest/seq/1\","
        "           \"payload\":\"red\","
        "           \"payload_required\":true,"
        "           \"hint_topic\":\"quest/hint\","
        "           \"hint_payload\":\"step1\","
        "           \"hint_audio_track\":\"/sdcard/h1.mp3\""
        "         },"
        "         {"
        "           \"topic\":\"quest/seq/2\","
        "           \"payload\":\"blue\","
        "           \"payload_required\":true"
        "         }"
        "       ]"
        "     }"
        "   }"
        " }]"
        "}";

    device_manager_config_t *cfg = dm_test_parse_config_json(json, 1);
    const device_descriptor_t *dev =
        dm_test_require_template(cfg, "seq_lock", DM_TEMPLATE_TYPE_SEQUENCE_LOCK);
    const dm_sequence_template_t *tpl = &dev->template_config.data.sequence;

    TEST_ASSERT_EQUAL_UINT8(2, tpl->step_count);
    TEST_ASSERT_EQUAL_UINT32(15000, tpl->timeout_ms);
    TEST_ASSERT_FALSE(tpl->reset_on_error);
    TEST_ASSERT_EQUAL_STRING("quest/seq/ok", tpl->success_topic);
    TEST_ASSERT_EQUAL_STRING("open", tpl->success_payload);
    TEST_ASSERT_EQUAL_STRING("/sdcard/seq_ok.mp3", tpl->success_audio_track);
    TEST_ASSERT_EQUAL_STRING("unlock_door", tpl->success_scenario);
    TEST_ASSERT_EQUAL_STRING("quest/seq/fail", tpl->fail_topic);
    TEST_ASSERT_EQUAL_STRING("reset", tpl->fail_payload);
    TEST_ASSERT_EQUAL_STRING("/sdcard/seq_fail.mp3", tpl->fail_audio_track);
    TEST_ASSERT_EQUAL_STRING("alarm", tpl->fail_scenario);
    TEST_ASSERT_EQUAL_STRING("quest/seq/1", tpl->steps[0].topic);
    TEST_ASSERT_EQUAL_STRING("red", tpl->steps[0].payload);
    TEST_ASSERT_TRUE(tpl->steps[0].payload_required);
    TEST_ASSERT_EQUAL_STRING("quest/hint", tpl->steps[0].hint_topic);
    TEST_ASSERT_EQUAL_STRING("step1", tpl->steps[0].hint_payload);
    TEST_ASSERT_EQUAL_STRING("/sdcard/h1.mp3", tpl->steps[0].hint_audio_track);
    TEST_ASSERT_EQUAL_STRING("quest/seq/2", tpl->steps[1].topic);
    TEST_ASSERT_EQUAL_STRING("blue", tpl->steps[1].payload);
    TEST_ASSERT_TRUE(tpl->steps[1].payload_required);

    dm_test_free_config(cfg);
}

static void test_device_name_falls_back_to_display_name(void)
{
    const char *json =
        "{"
        " \"schema\":1,"
        " \"devices\":[{"
        "   \"id\":\"legacy_dev\","
        "   \"name\":\"Legacy Device\","
        "   \"scenarios\":[]"
        " }]"
        "}";

    device_manager_config_t *cfg = dm_test_parse_config_json(json, 1);
    const device_descriptor_t *dev = dm_test_find_device(cfg, "legacy_dev");
    TEST_ASSERT_NOT_NULL(dev);
    TEST_ASSERT_EQUAL_STRING("Legacy Device", dev->display_name);
    dm_test_free_config(cfg);
}

static void test_long_audio_track_path_survives_sequence_parse(void)
{
    const char *track_path = "/sdcard/fillers/elevator-music-vanoss-gaming-background-music.mp3";
    const char *json =
        "{"
        " \"schema\":1,"
        " \"devices\":[{"
        "   \"id\":\"seq_lock\","
        "   \"display_name\":\"Sequence Lock\","
        "   \"template\":{"
        "     \"type\":\"sequence_lock\","
        "     \"sequence\":{"
        "       \"steps\":["
        "         {\"topic\":\"quest/seq/1\",\"payload\":\"red\",\"payload_required\":true,"
        "          \"hint_audio_track\":\"/sdcard/h1.mp3\"}"
        "       ],"
        "       \"success_audio_track\":\"/sdcard/ok.mp3\","
        "       \"fail_audio_track\":\"";
    char buffer[1024];
    snprintf(buffer, sizeof(buffer), "%s%s%s",
             json, track_path,
             "\""
             "     }"
             "   }"
             " }]"
             "}");

    device_manager_config_t *cfg = dm_test_parse_config_json(buffer, 1);
    const device_descriptor_t *dev =
        dm_test_require_template(cfg, "seq_lock", DM_TEMPLATE_TYPE_SEQUENCE_LOCK);
    const dm_sequence_template_t *tpl = &dev->template_config.data.sequence;
    TEST_ASSERT_EQUAL_STRING(track_path, tpl->fail_audio_track);
    dm_test_free_config(cfg);
}

static void test_signal_hold_without_signal_topic_is_ignored(void)
{
    const char *json =
        "{"
        " \"schema\":1,"
        " \"devices\":[{"
        "   \"id\":\"signal_hold\","
        "   \"display_name\":\"Signal Hold\","
        "   \"template\":{"
        "     \"type\":\"signal_hold\","
        "     \"signal\":{"
        "       \"heartbeat_topic\":\"quest/heartbeat\","
        "       \"required_hold_ms\":100"
        "     }"
        "   }"
        " }]"
        "}";

    device_manager_config_t *cfg = dm_test_parse_config_json(json, 1);
    const device_descriptor_t *dev = dm_test_find_device(cfg, "signal_hold");
    TEST_ASSERT_NOT_NULL(dev);
    TEST_ASSERT_FALSE(dev->template_assigned);
    dm_test_free_config(cfg);
}

static void test_sequence_lock_without_steps_is_ignored(void)
{
    const char *json =
        "{"
        " \"schema\":1,"
        " \"devices\":[{"
        "   \"id\":\"seq_lock\","
        "   \"display_name\":\"Sequence Lock\","
        "   \"template\":{"
        "     \"type\":\"sequence_lock\","
        "     \"sequence\":{"
        "       \"timeout_ms\":15000"
        "     }"
        "   }"
        " }]"
        "}";

    device_manager_config_t *cfg = dm_test_parse_config_json(json, 1);
    const device_descriptor_t *dev = dm_test_find_device(cfg, "seq_lock");
    TEST_ASSERT_NOT_NULL(dev);
    TEST_ASSERT_FALSE(dev->template_assigned);
    dm_test_free_config(cfg);
}

static void test_export_omits_legacy_name_and_topics(void)
{
    const char *json =
        "{"
        " \"schema\":1,"
        " \"devices\":[{"
        "   \"id\":\"legacy_dev\","
        "   \"name\":\"Legacy Device\","
        "   \"topics\":[{\"topic\":\"old/topic\",\"name\":\"old_scenario\"}],"
        "   \"scenarios\":[{\"id\":\"old_scenario\",\"name\":\"Old Scenario\",\"steps\":[]}]"
        " }]"
        "}";

    device_manager_config_t *cfg = dm_test_parse_config_json(json, 1);
    char *out_json = NULL;
    size_t out_len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, dm_storage_export_json(cfg, &out_json, &out_len));
    TEST_ASSERT_NOT_NULL(out_json);
    TEST_ASSERT_TRUE(out_len > 0);

    cJSON *root = cJSON_Parse(out_json);
    TEST_ASSERT_NOT_NULL(root);
    cJSON *devices = cJSON_GetObjectItem(root, "devices");
    TEST_ASSERT_TRUE(cJSON_IsArray(devices));
    cJSON *dev = cJSON_GetArrayItem(devices, 0);
    TEST_ASSERT_NOT_NULL(dev);
    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItem(dev, "display_name"));
    TEST_ASSERT_NULL(cJSON_GetObjectItem(dev, "name"));
    TEST_ASSERT_NULL(cJSON_GetObjectItem(dev, "topics"));

    cJSON_Delete(root);
    free(out_json);
    dm_test_free_config(cfg);
}

void register_device_manager_parse_tests(void)
{
    RUN_TEST(test_uid_validator_parses_actions_and_background_audio);
    RUN_TEST(test_sequence_lock_parses_steps_and_outcomes);
    RUN_TEST(test_device_name_falls_back_to_display_name);
    RUN_TEST(test_long_audio_track_path_survives_sequence_parse);
    RUN_TEST(test_signal_hold_without_signal_topic_is_ignored);
    RUN_TEST(test_sequence_lock_without_steps_is_ignored);
    RUN_TEST(test_export_omits_legacy_name_and_topics);
}
