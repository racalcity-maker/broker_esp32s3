#include "unity.h"
#include "mqtt_core.h"
#include "event_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <inttypes.h>

#define TEST_QUEUE_DEPTH          512
#define TEST_EVENT_WAIT_MS        200
#define TEST_STRESS_COUNT         40
#define TEST_PAR_TASKS            8
#define TEST_PAR_MESSAGES_PER_TASK 16

static QueueHandle_t s_evt_queue;
static bool s_handler_registered;

static void test_event_handler(const event_bus_message_t *msg)
{
    if (!s_evt_queue || !msg) {
        return;
    }
    event_bus_message_t copy = *msg;
    xQueueSend(s_evt_queue, &copy, 0);
}

esp_err_t mqtt_core_test_init_helpers(void)
{
    if (!s_evt_queue) {
        s_evt_queue = xQueueCreate(TEST_QUEUE_DEPTH, sizeof(event_bus_message_t));
        if (!s_evt_queue) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!s_handler_registered) {
        esp_err_t err = event_bus_register_handler(test_event_handler);
        if (err != ESP_OK) {
            return err;
        }
        s_handler_registered = true;
    }
    return ESP_OK;
}

static void flush_queue(void)
{
    if (!s_evt_queue) {
        return;
    }
    event_bus_message_t msg;
    while (xQueueReceive(s_evt_queue, &msg, 0) == pdTRUE) {
        // drain pending messages
    }
}

void setUp(void)
{
    flush_queue();
}

void tearDown(void)
{
}

static void test_mqtt_topic_map(void)
{
    const char *topic = mqtt_core_topic_for_event(EVENT_AUDIO_PLAY);
    TEST_ASSERT_NOT_NULL(topic);
    TEST_ASSERT_EQUAL_STRING("audio/play", topic);
    TEST_ASSERT_NULL(mqtt_core_topic_for_event(EVENT_FLAG_CHANGED));
}

static void test_mqtt_client_stats_initial(void)
{
    mqtt_client_stats_t stats;
    mqtt_core_get_client_stats(&stats);
    TEST_ASSERT_EQUAL_UINT8(0, stats.total);
}

static void expect_event(event_bus_type_t type,
                         const char *topic,
                         const char *payload)
{
    TEST_ASSERT_NOT_NULL(topic);
    TEST_ASSERT_NOT_NULL(payload);
    event_bus_message_t msg;
    TEST_ASSERT_EQUAL(pdTRUE,
                      xQueueReceive(s_evt_queue, &msg, pdMS_TO_TICKS(TEST_EVENT_WAIT_MS)));
    TEST_ASSERT_EQUAL(type, msg.type);
    TEST_ASSERT_EQUAL_STRING(topic, msg.topic);
    TEST_ASSERT_EQUAL_STRING(payload, msg.payload);
}

static void test_mqtt_inject_dispatch(void)
{
    const char *topic = "audio/play";
    const char *payload = "/sdcard/test.mp3";
    TEST_ASSERT_EQUAL(ESP_OK, mqtt_core_inject_message(topic, payload));
    expect_event(EVENT_AUDIO_PLAY, topic, payload);
    expect_event(EVENT_MQTT_MESSAGE, topic, payload);
}

static void test_mqtt_inject_stress(void)
{
    const char *typed_topic = "audio/play";
    const char *generic_topic = "misc/topic";
    char payload[64];
    for (uint32_t i = 0; i < TEST_STRESS_COUNT; ++i) {
        snprintf(payload, sizeof(payload), "stress-%" PRIu32, i);
        const bool expect_typed = (i % 2) == 0;
        const char *topic = expect_typed ? typed_topic : generic_topic;
        TEST_ASSERT_EQUAL(ESP_OK, mqtt_core_inject_message(topic, payload));
        if (expect_typed) {
            expect_event(EVENT_AUDIO_PLAY, topic, payload);
        }
        expect_event(EVENT_MQTT_MESSAGE, topic, payload);
    }
    event_bus_message_t leftover;
    TEST_ASSERT_EQUAL(pdFALSE,
                      xQueueReceive(s_evt_queue, &leftover, pdMS_TO_TICKS(TEST_EVENT_WAIT_MS)));
}

typedef struct {
    uint32_t base;
    SemaphoreHandle_t done;
} injector_params_t;

static uint32_t typed_per_task(uint32_t base)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < TEST_PAR_MESSAGES_PER_TASK; ++i) {
        if (((i + base) % 2) == 0) {
            count++;
        }
    }
    return count;
}

static void injector_task(void *arg)
{
    injector_params_t *params = (injector_params_t *)arg;
    const char *typed_topic = "audio/play";
    const char *generic_topic = "stress/topic";
    char payload[96];
    for (uint32_t i = 0; i < TEST_PAR_MESSAGES_PER_TASK; ++i) {
        snprintf(payload, sizeof(payload), "parallel-%" PRIu32 "-%" PRIu32, params->base, i);
        const bool typed = ((i + params->base) % 2) == 0;
        const char *topic = typed ? typed_topic : generic_topic;
        TEST_ASSERT_EQUAL(ESP_OK, mqtt_core_inject_message(topic, payload));
        taskYIELD();
    }
    xSemaphoreGive(params->done);
    vTaskDelete(NULL);
}

static void drain_parallel_events(uint32_t total_messages, uint32_t typed_expected)
{
    uint32_t generic_seen = 0;
    uint32_t typed_seen = 0;
    while (generic_seen < total_messages || typed_seen < typed_expected) {
        event_bus_message_t msg;
        TEST_ASSERT_EQUAL(pdTRUE,
                          xQueueReceive(s_evt_queue, &msg, pdMS_TO_TICKS(TEST_EVENT_WAIT_MS)));
        if (msg.type == EVENT_AUDIO_PLAY) {
            typed_seen++;
        } else {
            TEST_ASSERT_EQUAL(EVENT_MQTT_MESSAGE, msg.type);
            generic_seen++;
        }
    }
    event_bus_message_t leftover;
    TEST_ASSERT_EQUAL(pdFALSE,
                      xQueueReceive(s_evt_queue, &leftover, pdMS_TO_TICKS(TEST_EVENT_WAIT_MS)));
}

static void test_mqtt_parallel_burst(void)
{
    const uint32_t total_messages = TEST_PAR_TASKS * TEST_PAR_MESSAGES_PER_TASK;
    SemaphoreHandle_t done_sem = xSemaphoreCreateCounting(TEST_PAR_TASKS, 0);
    TEST_ASSERT_NOT_NULL(done_sem);
    injector_params_t params[TEST_PAR_TASKS];
    uint32_t typed_expected = 0;
    for (uint32_t i = 0; i < TEST_PAR_TASKS; ++i) {
        params[i].base = i * TEST_PAR_MESSAGES_PER_TASK;
        params[i].done = done_sem;
        typed_expected += typed_per_task(params[i].base);
        BaseType_t ok = xTaskCreate(injector_task,
                                    "inj",
                                    3072,
                                    &params[i],
                                    tskIDLE_PRIORITY + 1,
                                    NULL);
        TEST_ASSERT_EQUAL(pdPASS, ok);
        vTaskDelay(1);
    }
    for (uint32_t i = 0; i < TEST_PAR_TASKS; ++i) {
        TEST_ASSERT_EQUAL(pdTRUE,
                          xSemaphoreTake(done_sem, pdMS_TO_TICKS(1000)));
    }
    vSemaphoreDelete(done_sem);
    drain_parallel_events(total_messages, typed_expected);
}

void register_mqtt_core_tests(void)
{
    RUN_TEST(test_mqtt_topic_map);
    RUN_TEST(test_mqtt_client_stats_initial);
    RUN_TEST(test_mqtt_inject_dispatch);
    RUN_TEST(test_mqtt_inject_stress);
    RUN_TEST(test_mqtt_parallel_burst);
}
