#include <string.h>

#include "unity.h"
#include "dm_runtime_sequence.h"
#include "dm_runtime_signal.h"

static void fill_sequence_template(dm_sequence_template_t *tpl, bool reset_on_error, uint32_t timeout_ms)
{
    TEST_ASSERT_NOT_NULL(tpl);
    memset(tpl, 0, sizeof(*tpl));
    tpl->step_count = 2;
    tpl->reset_on_error = reset_on_error;
    tpl->timeout_ms = timeout_ms;
    strncpy(tpl->steps[0].topic, "seq/1", sizeof(tpl->steps[0].topic) - 1);
    strncpy(tpl->steps[0].payload, "red", sizeof(tpl->steps[0].payload) - 1);
    tpl->steps[0].payload_required = true;
    strncpy(tpl->steps[1].topic, "seq/2", sizeof(tpl->steps[1].topic) - 1);
    strncpy(tpl->steps[1].payload, "blue", sizeof(tpl->steps[1].payload) - 1);
    tpl->steps[1].payload_required = true;
}

static void fill_signal_template(dm_signal_hold_template_t *tpl,
                                 uint32_t required_hold_ms,
                                 uint32_t heartbeat_timeout_ms)
{
    TEST_ASSERT_NOT_NULL(tpl);
    memset(tpl, 0, sizeof(*tpl));
    strncpy(tpl->signal_topic, "signal/cmd", sizeof(tpl->signal_topic) - 1);
    strncpy(tpl->signal_payload_on, "ON", sizeof(tpl->signal_payload_on) - 1);
    strncpy(tpl->signal_payload_off, "OFF", sizeof(tpl->signal_payload_off) - 1);
    strncpy(tpl->heartbeat_topic, "signal/hb", sizeof(tpl->heartbeat_topic) - 1);
    tpl->required_hold_ms = required_hold_ms;
    tpl->heartbeat_timeout_ms = heartbeat_timeout_ms;
    strncpy(tpl->hold_track, "/sdcard/hold.mp3", sizeof(tpl->hold_track) - 1);
    strncpy(tpl->complete_track, "/sdcard/done.mp3", sizeof(tpl->complete_track) - 1);
}

static void test_sequence_runtime_ignores_unrelated_topics(void)
{
    dm_sequence_template_t tpl;
    fill_sequence_template(&tpl, true, 500);
    dm_sequence_runtime_t rt;
    dm_sequence_runtime_init(&rt, &tpl);

    dm_sequence_action_t action = dm_sequence_runtime_handle(&rt, "other/topic", "noop", 100);
    TEST_ASSERT_EQUAL(DM_SEQUENCE_EVENT_NONE, action.type);
    TEST_ASSERT_NULL(action.step);
    TEST_ASSERT_EQUAL_UINT8(0, rt.current_index);
}

static void test_sequence_runtime_fails_on_wrong_step_when_strict(void)
{
    dm_sequence_template_t tpl;
    fill_sequence_template(&tpl, true, 500);
    dm_sequence_runtime_t rt;
    dm_sequence_runtime_init(&rt, &tpl);

    TEST_ASSERT_EQUAL(DM_SEQUENCE_EVENT_STEP_OK,
                      dm_sequence_runtime_handle(&rt, "seq/1", "red", 100).type);

    dm_sequence_action_t action = dm_sequence_runtime_handle(&rt, "seq/1", "red", 150);
    TEST_ASSERT_EQUAL(DM_SEQUENCE_EVENT_FAILED, action.type);
    TEST_ASSERT_FALSE(action.timeout);
    TEST_ASSERT_EQUAL_UINT8(0, rt.current_index);
}

static void test_sequence_runtime_timeout_is_reported_explicitly(void)
{
    dm_sequence_template_t tpl;
    fill_sequence_template(&tpl, true, 50);
    dm_sequence_runtime_t rt;
    dm_sequence_runtime_init(&rt, &tpl);

    TEST_ASSERT_EQUAL(DM_SEQUENCE_EVENT_STEP_OK,
                      dm_sequence_runtime_handle(&rt, "seq/1", "red", 100).type);

    dm_sequence_action_t action = dm_sequence_runtime_handle_timeout(&rt, 200);
    TEST_ASSERT_EQUAL(DM_SEQUENCE_EVENT_FAILED, action.type);
    TEST_ASSERT_TRUE(action.timeout);
    TEST_ASSERT_EQUAL_UINT8(0, rt.current_index);
}

static void test_sequence_runtime_completes_on_happy_path(void)
{
    dm_sequence_template_t tpl;
    fill_sequence_template(&tpl, true, 500);
    dm_sequence_runtime_t rt;
    dm_sequence_runtime_init(&rt, &tpl);

    TEST_ASSERT_EQUAL(DM_SEQUENCE_EVENT_STEP_OK,
                      dm_sequence_runtime_handle(&rt, "seq/1", "red", 100).type);

    dm_sequence_action_t action = dm_sequence_runtime_handle(&rt, "seq/2", "blue", 130);
    TEST_ASSERT_EQUAL(DM_SEQUENCE_EVENT_COMPLETED, action.type);
    TEST_ASSERT_FALSE(action.timeout);
    TEST_ASSERT_NOT_NULL(action.step);
    TEST_ASSERT_EQUAL_STRING("seq/2", action.step->topic);
    TEST_ASSERT_EQUAL_UINT8(0, rt.current_index);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)rt.last_step_ms);
}

static void test_sequence_runtime_wrong_step_is_ignored_when_not_strict(void)
{
    dm_sequence_template_t tpl;
    fill_sequence_template(&tpl, false, 500);
    dm_sequence_runtime_t rt;
    dm_sequence_runtime_init(&rt, &tpl);

    TEST_ASSERT_EQUAL(DM_SEQUENCE_EVENT_STEP_OK,
                      dm_sequence_runtime_handle(&rt, "seq/1", "red", 100).type);

    dm_sequence_action_t action = dm_sequence_runtime_handle(&rt, "seq/1", "red", 120);
    TEST_ASSERT_EQUAL(DM_SEQUENCE_EVENT_NONE, action.type);
    TEST_ASSERT_NOT_NULL(action.step);
    TEST_ASSERT_EQUAL_STRING("seq/1", action.step->topic);
    TEST_ASSERT_EQUAL_UINT8(1, rt.current_index);
    TEST_ASSERT_EQUAL_UINT32(100, (uint32_t)rt.last_step_ms);
}

static void test_sequence_runtime_wrong_payload_is_ignored(void)
{
    dm_sequence_template_t tpl;
    fill_sequence_template(&tpl, true, 500);
    dm_sequence_runtime_t rt;
    dm_sequence_runtime_init(&rt, &tpl);

    dm_sequence_action_t action = dm_sequence_runtime_handle(&rt, "seq/1", "green", 100);
    TEST_ASSERT_EQUAL(DM_SEQUENCE_EVENT_NONE, action.type);
    TEST_ASSERT_NULL(action.step);
    TEST_ASSERT_EQUAL_UINT8(0, rt.current_index);
}

static void test_sequence_runtime_timeout_before_first_step_is_noop(void)
{
    dm_sequence_template_t tpl;
    fill_sequence_template(&tpl, true, 50);
    dm_sequence_runtime_t rt;
    dm_sequence_runtime_init(&rt, &tpl);

    dm_sequence_action_t action = dm_sequence_runtime_handle_timeout(&rt, 200);
    TEST_ASSERT_EQUAL(DM_SEQUENCE_EVENT_NONE, action.type);
    TEST_ASSERT_FALSE(action.timeout);
    TEST_ASSERT_EQUAL_UINT8(0, rt.current_index);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)rt.last_step_ms);
}

static void test_sequence_runtime_reset_clears_progress(void)
{
    dm_sequence_template_t tpl;
    fill_sequence_template(&tpl, true, 500);
    dm_sequence_runtime_t rt;
    dm_sequence_runtime_init(&rt, &tpl);

    TEST_ASSERT_EQUAL(DM_SEQUENCE_EVENT_STEP_OK,
                      dm_sequence_runtime_handle(&rt, "seq/1", "red", 100).type);
    TEST_ASSERT_EQUAL_UINT8(1, rt.current_index);
    TEST_ASSERT_EQUAL_UINT32(100, (uint32_t)rt.last_step_ms);

    dm_sequence_runtime_reset(&rt);
    TEST_ASSERT_EQUAL_UINT8(0, rt.current_index);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)rt.last_step_ms);
}

static void test_sequence_runtime_non_required_payload_matches_topic_only(void)
{
    dm_sequence_template_t tpl;
    fill_sequence_template(&tpl, true, 500);
    tpl.steps[0].payload_required = false;
    memset(tpl.steps[0].payload, 0, sizeof(tpl.steps[0].payload));
    dm_sequence_runtime_t rt;
    dm_sequence_runtime_init(&rt, &tpl);

    dm_sequence_action_t action = dm_sequence_runtime_handle(&rt, "seq/1", "anything", 100);
    TEST_ASSERT_EQUAL(DM_SEQUENCE_EVENT_STEP_OK, action.type);
    TEST_ASSERT_NOT_NULL(action.step);
    TEST_ASSERT_EQUAL_STRING("seq/1", action.step->topic);
    TEST_ASSERT_EQUAL_UINT8(1, rt.current_index);
}

static void test_signal_runtime_starts_and_accumulates_progress(void)
{
    dm_signal_hold_template_t tpl;
    fill_signal_template(&tpl, 100, 40);
    dm_signal_runtime_t rt;
    dm_signal_runtime_init(&rt, &tpl);

    dm_signal_action_t start = dm_signal_runtime_handle_tick(&rt, 100);
    TEST_ASSERT_EQUAL(DM_SIGNAL_EVENT_START, start.event);
    TEST_ASSERT_TRUE(start.audio_play);
    TEST_ASSERT_EQUAL_STRING("/sdcard/hold.mp3", start.audio_track);
    TEST_ASSERT_TRUE(rt.state.active);
    TEST_ASSERT_EQUAL_UINT32(0, rt.state.accumulated_ms);

    dm_signal_action_t next = dm_signal_runtime_handle_tick(&rt, 130);
    TEST_ASSERT_EQUAL(DM_SIGNAL_EVENT_CONTINUE, next.event);
    TEST_ASSERT_EQUAL_UINT32(30, next.accumulated_ms);
    TEST_ASSERT_TRUE(rt.state.active);
    TEST_ASSERT_FALSE(rt.state.finished);
}

static void test_signal_runtime_completes_and_emits_finish_actions(void)
{
    dm_signal_hold_template_t tpl;
    fill_signal_template(&tpl, 60, 100);
    dm_signal_runtime_t rt;
    dm_signal_runtime_init(&rt, &tpl);

    TEST_ASSERT_EQUAL(DM_SIGNAL_EVENT_START,
                      dm_signal_runtime_handle_tick(&rt, 100).event);

    dm_signal_action_t action = dm_signal_runtime_handle_tick(&rt, 170);
    TEST_ASSERT_EQUAL(DM_SIGNAL_EVENT_COMPLETED, action.event);
    TEST_ASSERT_TRUE(action.audio_play);
    TEST_ASSERT_EQUAL_STRING("/sdcard/done.mp3", action.audio_track);
    TEST_ASSERT_TRUE(action.signal_on);
    TEST_ASSERT_TRUE(action.signal_off);
    TEST_ASSERT_EQUAL_STRING("signal/cmd", action.signal_topic);
    TEST_ASSERT_EQUAL_STRING("ON", action.signal_payload_on);
    TEST_ASSERT_EQUAL_STRING("OFF", action.signal_payload_off);
    TEST_ASSERT_FALSE(rt.state.active);
    TEST_ASSERT_TRUE(rt.state.finished);
}

static void test_signal_runtime_timeout_before_start_is_noop(void)
{
    dm_signal_hold_template_t tpl;
    fill_signal_template(&tpl, 100, 40);
    dm_signal_runtime_t rt;
    dm_signal_runtime_init(&rt, &tpl);

    dm_signal_action_t action = dm_signal_runtime_handle_timeout(&rt);
    TEST_ASSERT_EQUAL(DM_SIGNAL_EVENT_NONE, action.event);
    TEST_ASSERT_FALSE(action.audio_pause);
    TEST_ASSERT_FALSE(rt.state.active);
    TEST_ASSERT_FALSE(rt.state.finished);
}

static void test_signal_runtime_timeout_stops_active_hold(void)
{
    dm_signal_hold_template_t tpl;
    fill_signal_template(&tpl, 100, 40);
    dm_signal_runtime_t rt;
    dm_signal_runtime_init(&rt, &tpl);

    TEST_ASSERT_EQUAL(DM_SIGNAL_EVENT_START,
                      dm_signal_runtime_handle_tick(&rt, 100).event);

    dm_signal_action_t action = dm_signal_runtime_handle_timeout(&rt);
    TEST_ASSERT_EQUAL(DM_SIGNAL_EVENT_STOP, action.event);
    TEST_ASSERT_TRUE(action.audio_pause);
    TEST_ASSERT_FALSE(rt.state.active);
    TEST_ASSERT_FALSE(rt.state.finished);
}

static void test_signal_runtime_tick_with_delta_above_timeout_stops(void)
{
    dm_signal_hold_template_t tpl;
    fill_signal_template(&tpl, 100, 40);
    dm_signal_runtime_t rt;
    dm_signal_runtime_init(&rt, &tpl);

    TEST_ASSERT_EQUAL(DM_SIGNAL_EVENT_START,
                      dm_signal_runtime_handle_tick(&rt, 100).event);

    dm_signal_action_t action = dm_signal_runtime_handle_tick(&rt, 200);
    TEST_ASSERT_EQUAL(DM_SIGNAL_EVENT_STOP, action.event);
    TEST_ASSERT_TRUE(action.audio_pause);
    TEST_ASSERT_FALSE(rt.state.active);
    TEST_ASSERT_FALSE(rt.state.finished);
}

static void test_signal_runtime_timeout_after_completion_is_noop(void)
{
    dm_signal_hold_template_t tpl;
    fill_signal_template(&tpl, 60, 100);
    dm_signal_runtime_t rt;
    dm_signal_runtime_init(&rt, &tpl);

    TEST_ASSERT_EQUAL(DM_SIGNAL_EVENT_START,
                      dm_signal_runtime_handle_tick(&rt, 100).event);
    TEST_ASSERT_EQUAL(DM_SIGNAL_EVENT_COMPLETED,
                      dm_signal_runtime_handle_tick(&rt, 170).event);

    dm_signal_action_t action = dm_signal_runtime_handle_timeout(&rt);
    TEST_ASSERT_EQUAL(DM_SIGNAL_EVENT_NONE, action.event);
    TEST_ASSERT_FALSE(action.audio_pause);
    TEST_ASSERT_FALSE(rt.state.active);
    TEST_ASSERT_TRUE(rt.state.finished);
}

static void test_signal_runtime_completes_exactly_on_boundary(void)
{
    dm_signal_hold_template_t tpl;
    fill_signal_template(&tpl, 60, 100);
    dm_signal_runtime_t rt;
    dm_signal_runtime_init(&rt, &tpl);

    TEST_ASSERT_EQUAL(DM_SIGNAL_EVENT_START,
                      dm_signal_runtime_handle_tick(&rt, 100).event);

    dm_signal_action_t action = dm_signal_runtime_handle_tick(&rt, 160);
    TEST_ASSERT_EQUAL(DM_SIGNAL_EVENT_COMPLETED, action.event);
    TEST_ASSERT_TRUE(rt.state.finished);
    TEST_ASSERT_FALSE(rt.state.active);
}

static void test_signal_runtime_tick_after_completion_is_noop(void)
{
    dm_signal_hold_template_t tpl;
    fill_signal_template(&tpl, 60, 100);
    dm_signal_runtime_t rt;
    dm_signal_runtime_init(&rt, &tpl);

    TEST_ASSERT_EQUAL(DM_SIGNAL_EVENT_START,
                      dm_signal_runtime_handle_tick(&rt, 100).event);
    TEST_ASSERT_EQUAL(DM_SIGNAL_EVENT_COMPLETED,
                      dm_signal_runtime_handle_tick(&rt, 170).event);

    dm_signal_action_t action = dm_signal_runtime_handle_tick(&rt, 200);
    TEST_ASSERT_EQUAL(DM_SIGNAL_EVENT_NONE, action.event);
    TEST_ASSERT_FALSE(action.audio_play);
    TEST_ASSERT_FALSE(action.audio_pause);
    TEST_ASSERT_FALSE(rt.state.active);
    TEST_ASSERT_TRUE(rt.state.finished);
}

static void test_signal_runtime_set_template_resets_state(void)
{
    dm_signal_hold_template_t tpl_a;
    fill_signal_template(&tpl_a, 100, 40);
    dm_signal_hold_template_t tpl_b;
    fill_signal_template(&tpl_b, 200, 80);
    dm_signal_runtime_t rt;
    dm_signal_runtime_init(&rt, &tpl_a);

    TEST_ASSERT_EQUAL(DM_SIGNAL_EVENT_START,
                      dm_signal_runtime_handle_tick(&rt, 100).event);
    TEST_ASSERT_EQUAL(DM_SIGNAL_EVENT_CONTINUE,
                      dm_signal_runtime_handle_tick(&rt, 130).event);
    TEST_ASSERT_TRUE(rt.state.active);
    TEST_ASSERT_EQUAL_UINT32(30, rt.state.accumulated_ms);

    dm_signal_runtime_set_template(&rt, &tpl_b);
    TEST_ASSERT_FALSE(rt.state.active);
    TEST_ASSERT_FALSE(rt.state.finished);
    TEST_ASSERT_EQUAL_UINT32(0, rt.state.accumulated_ms);
    TEST_ASSERT_EQUAL_UINT32(200, rt.config.required_hold_ms);
    TEST_ASSERT_EQUAL_UINT32(80, rt.config.heartbeat_timeout_ms);
}

void register_runtime_pure_tests(void)
{
    RUN_TEST(test_sequence_runtime_ignores_unrelated_topics);
    RUN_TEST(test_sequence_runtime_fails_on_wrong_step_when_strict);
    RUN_TEST(test_sequence_runtime_timeout_is_reported_explicitly);
    RUN_TEST(test_sequence_runtime_completes_on_happy_path);
    RUN_TEST(test_sequence_runtime_wrong_step_is_ignored_when_not_strict);
    RUN_TEST(test_sequence_runtime_wrong_payload_is_ignored);
    RUN_TEST(test_sequence_runtime_timeout_before_first_step_is_noop);
    RUN_TEST(test_sequence_runtime_reset_clears_progress);
    RUN_TEST(test_sequence_runtime_non_required_payload_matches_topic_only);
    RUN_TEST(test_signal_runtime_starts_and_accumulates_progress);
    RUN_TEST(test_signal_runtime_completes_and_emits_finish_actions);
    RUN_TEST(test_signal_runtime_timeout_before_start_is_noop);
    RUN_TEST(test_signal_runtime_timeout_stops_active_hold);
    RUN_TEST(test_signal_runtime_tick_with_delta_above_timeout_stops);
    RUN_TEST(test_signal_runtime_timeout_after_completion_is_noop);
    RUN_TEST(test_signal_runtime_completes_exactly_on_boundary);
    RUN_TEST(test_signal_runtime_tick_after_completion_is_noop);
    RUN_TEST(test_signal_runtime_set_template_resets_state);
}
