#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "unity.h"
#include "event_bus.h"
#include "dm_template_runtime.h"
#include "test_dm_helpers.h"

static bool s_event_bus_ready = false;

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

static void register_sensor_from_json(const char *json, const char *device_id)
{
    device_manager_config_t *cfg = dm_test_parse_config_json(json, 1);
    const device_descriptor_t *dev =
        dm_test_require_template(cfg, device_id, DM_TEMPLATE_TYPE_SENSOR_MONITOR);
    TEST_ASSERT_EQUAL(ESP_OK, dm_template_runtime_register(&dev->template_config, dev->id));
    dm_test_free_config(cfg);
}

static dm_sensor_channel_runtime_snapshot_t *get_channel(dm_sensor_runtime_snapshot_t *snap,
                                                         const char *channel_id)
{
    for (uint8_t i = 0; snap && i < snap->channel_count; ++i) {
        if (strcmp(snap->channels[i].config.id, channel_id) == 0) {
            return &snap->channels[i];
        }
    }
    return NULL;
}

static void test_sensor_runtime_handles_multi_channel_messages(void)
{
    const char *json =
        "{"
        " \"schema\":1,"
        " \"devices\":[{"
        "   \"id\":\"sensor_room\","
        "   \"template\":{"
        "     \"type\":\"sensor_monitor\","
        "     \"sensor\":{"
        "       \"topic\":\"env/room\","
        "       \"name\":\"Room sensors\","
        "       \"history_enabled\":true,"
        "       \"warn\":{\"value\":30.0,\"compare\":\"above_or_equal\"},"
        "       \"channels\":["
        "         {"
        "           \"id\":\"temp\","
        "           \"name\":\"Temperature\","
        "           \"topic\":\"env/room/temp\","
        "           \"parse_mode\":\"json_number\","
        "           \"json_key\":\"value\""
        "         },"
        "         {"
        "           \"id\":\"humidity\","
        "           \"name\":\"Humidity\","
        "           \"topic\":\"env/room/h\","
        "           \"warn\":{\"enabled\":false}"
        "         }"
        "       ]"
        "     }"
        "   }"
        " }]"
        "}";

    register_sensor_from_json(json, "sensor_room");

    dm_sensor_runtime_snapshot_t *snaps =
        (dm_sensor_runtime_snapshot_t *)calloc(1, sizeof(dm_sensor_runtime_snapshot_t));
    TEST_ASSERT_NOT_NULL(snaps);
    TEST_ASSERT_EQUAL_size_t(1, dm_template_runtime_get_sensor_snapshots(snaps, 1));
    TEST_ASSERT_EQUAL_UINT8(2, snaps[0].channel_count);

    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("env/room/temp", "{\"value\":31.5}"));
    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("env/room/h", "45"));

    memset(snaps, 0, sizeof(*snaps));
    TEST_ASSERT_EQUAL_size_t(1, dm_template_runtime_get_sensor_snapshots(snaps, 1));
    dm_sensor_channel_runtime_snapshot_t *temp_ch = get_channel(&snaps[0], "temp");
    dm_sensor_channel_runtime_snapshot_t *hum_ch = get_channel(&snaps[0], "humidity");
    TEST_ASSERT_NOT_NULL(temp_ch);
    TEST_ASSERT_NOT_NULL(hum_ch);

    TEST_ASSERT_TRUE_MESSAGE(temp_ch->has_value, "temp channel missing value");
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 31.5f, temp_ch->last_value);
    TEST_ASSERT_EQUAL(DM_SENSOR_STATUS_WARN, temp_ch->status);
    TEST_ASSERT_EQUAL_UINT8(1, temp_ch->history_count);

    TEST_ASSERT_TRUE_MESSAGE(hum_ch->has_value, "humidity channel missing value");
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 45.0f, hum_ch->last_value);
    TEST_ASSERT_EQUAL(DM_SENSOR_STATUS_OK, hum_ch->status);
    TEST_ASSERT_EQUAL_UINT8(1, hum_ch->history_count);
    free(snaps);
}

static void test_sensor_runtime_supports_default_channel(void)
{
    const char *json =
        "{"
        " \"schema\":1,"
        " \"devices\":[{"
        "   \"id\":\"single_channel\","
        "   \"template\":{"
        "     \"type\":\"sensor_monitor\","
        "     \"sensor\":{"
        "       \"topic\":\"env/single\","
        "       \"name\":\"CO2\","
        "       \"units\":\"ppm\","
        "       \"decimals\":2,"
        "       \"history_enabled\":false,"
        "       \"parse_mode\":\"raw_number\""
        "     }"
        "   }"
        " }]"
        "}";

    register_sensor_from_json(json, "single_channel");

    dm_sensor_runtime_snapshot_t *snaps =
        (dm_sensor_runtime_snapshot_t *)calloc(1, sizeof(dm_sensor_runtime_snapshot_t));
    TEST_ASSERT_NOT_NULL(snaps);
    TEST_ASSERT_EQUAL_size_t(1, dm_template_runtime_get_sensor_snapshots(snaps, 1));
    TEST_ASSERT_EQUAL_UINT8(1, snaps[0].channel_count);

    TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("env/single", "123.45"));

    memset(snaps, 0, sizeof(*snaps));
    TEST_ASSERT_EQUAL_size_t(1, dm_template_runtime_get_sensor_snapshots(snaps, 1));
    dm_sensor_channel_runtime_snapshot_t *channel = &snaps[0].channels[0];
    TEST_ASSERT_EQUAL_STRING("CO2", channel->config.name);
    TEST_ASSERT_EQUAL_STRING("env/single", channel->config.topic);
    TEST_ASSERT_TRUE(channel->has_value);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 123.45f, channel->last_value);
    TEST_ASSERT_EQUAL(DM_SENSOR_STATUS_OK, channel->status);
    TEST_ASSERT_EQUAL_UINT8(0, channel->history_count);
    free(snaps);
}

static void test_sensor_runtime_history_handles_burst_updates(void)
{
    const char *json =
        "{"
        " \"schema\":1,"
        " \"devices\":[{"
        "   \"id\":\"history_sensor\","
        "   \"template\":{"
        "     \"type\":\"sensor_monitor\","
        "     \"sensor\":{"
        "       \"topic\":\"env/history\","
        "       \"name\":\"History\","
        "       \"history_enabled\":true,"
        "       \"channels\":[{"
        "         \"id\":\"temp\","
        "         \"topic\":\"env/history/temp\","
        "         \"history_enabled\":true"
        "       }]"
        "     }"
        "   }"
        " }]"
        "}";

    register_sensor_from_json(json, "history_sensor");

    const uint32_t bursts = DM_SENSOR_HISTORY_MAX_SAMPLES + 5;
    char payload[32];
    for (uint32_t i = 0; i < bursts; ++i) {
        snprintf(payload, sizeof(payload), "%u", (unsigned)i);
        TEST_ASSERT_TRUE(dm_template_runtime_handle_mqtt("env/history/temp", payload));
    }

    dm_sensor_runtime_snapshot_t *snaps =
        (dm_sensor_runtime_snapshot_t *)calloc(1, sizeof(dm_sensor_runtime_snapshot_t));
    TEST_ASSERT_NOT_NULL(snaps);
    TEST_ASSERT_EQUAL_size_t(1, dm_template_runtime_get_sensor_snapshots(snaps, 1));
    TEST_ASSERT_EQUAL_UINT8(1, snaps[0].channel_count);

    dm_sensor_channel_runtime_snapshot_t *channel = &snaps[0].channels[0];
    TEST_ASSERT_TRUE(channel->has_value);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, (float)(bursts - 1), channel->last_value);
    TEST_ASSERT_EQUAL_UINT8(DM_SENSOR_HISTORY_MAX_SAMPLES, channel->history_count);

    uint32_t expected_start = bursts - DM_SENSOR_HISTORY_MAX_SAMPLES;
    for (uint8_t i = 0; i < channel->history_count; ++i) {
        float expected = (float)(expected_start + i);
        TEST_ASSERT_FLOAT_WITHIN(0.001f, expected, channel->history[i].value);
    }

    free(snaps);
}

void register_device_manager_integration_tests(void)
{
    RUN_TEST(test_sensor_runtime_handles_multi_channel_messages);
    RUN_TEST(test_sensor_runtime_supports_default_channel);
    RUN_TEST(test_sensor_runtime_history_handles_burst_updates);
}
