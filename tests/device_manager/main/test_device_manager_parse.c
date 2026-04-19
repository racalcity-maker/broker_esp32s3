#include <string.h>

#include "unity.h"
#include "device_manager_internal.h"
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

void register_device_manager_parse_tests(void)
{
    RUN_TEST(test_uid_validator_parses_actions_and_background_audio);
    RUN_TEST(test_sequence_lock_parses_steps_and_outcomes);
}
