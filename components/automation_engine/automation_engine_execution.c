#include "automation_engine_internal.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "audio_player.h"
#include "mqtt_core.h"

#define AUTOMATION_QUEUE_LENGTH 16
#define AUTOMATION_WORKER_STACK 4096
#define AUTOMATION_WORKER_PRIO 5
#define AUTOMATION_WORKER_COUNT 2

static const char *TAG = "automation_exec";
static QueueHandle_t s_job_queue = NULL;
static TaskHandle_t s_workers[AUTOMATION_WORKER_COUNT] = {0};

static void automation_worker(void *param);
static void automation_execute_job(const automation_job_t *job);

esp_err_t automation_engine_execution_init(void)
{
    if (!s_job_queue) {
        s_job_queue = xQueueCreate(AUTOMATION_QUEUE_LENGTH, sizeof(automation_job_t));
    }
    return s_job_queue ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t automation_engine_execution_start(void)
{
    for (size_t i = 0; i < AUTOMATION_WORKER_COUNT; ++i) {
        if (!s_workers[i]) {
            char name[16];
            snprintf(name, sizeof(name), "automation%u", (unsigned)i);
            BaseType_t ok = xTaskCreate(automation_worker,
                                        name,
                                        AUTOMATION_WORKER_STACK,
                                        NULL,
                                        AUTOMATION_WORKER_PRIO,
                                        &s_workers[i]);
            if (ok != pdPASS) {
                return ESP_FAIL;
            }
        }
    }
    return ESP_OK;
}

esp_err_t automation_engine_enqueue_job(const device_descriptor_t *device, const device_scenario_t *scenario)
{
    if (!device || !scenario || !s_job_queue) {
        return ESP_ERR_INVALID_ARG;
    }
    automation_job_t job = {
        .device = device,
        .scenario = scenario,
    };
    if (xQueueSend(s_job_queue, &job, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "job queue full");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static void automation_worker(void *param)
{
    (void)param;
    automation_job_t job;
    while (1) {
        if (xQueueReceive(s_job_queue, &job, portMAX_DELAY) == pdTRUE) {
            automation_execute_job(&job);
        }
    }
}

static void automation_execute_job(const automation_job_t *job)
{
    if (!job || !job->scenario || job->scenario->step_count == 0) {
        return;
    }
    const device_action_step_t *steps = job->scenario->steps;
    uint8_t total = job->scenario->step_count;
    uint16_t loop_counters[DEVICE_MANAGER_MAX_STEPS_PER_SCENARIO] = {0};
    ESP_LOGI(TAG, "run scenario %s/%s (%u steps)",
             job->device ? job->device->display_name : "device",
             job->scenario->name[0] ? job->scenario->name : job->scenario->id,
             total);
    uint8_t idx = 0;
    while (idx < total) {
        const device_action_step_t *step = &steps[idx];
        if (step->delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(step->delay_ms));
        }
        switch (step->type) {
        case DEVICE_ACTION_MQTT_PUBLISH:
            if (step->data.mqtt.topic[0]) {
                char topic[DEVICE_MANAGER_TOPIC_MAX_LEN];
                char payload[DEVICE_MANAGER_PAYLOAD_MAX_LEN];
                automation_engine_render_template(step->data.mqtt.topic, topic, sizeof(topic));
                automation_engine_render_template(step->data.mqtt.payload, payload, sizeof(payload));
                if (topic[0]) {
                    mqtt_core_publish(topic, payload);
                    mqtt_core_inject_message(topic, payload);
                } else {
                    ESP_LOGW(TAG, "mqtt_publish skipped (empty topic)");
                }
            }
            break;
        case DEVICE_ACTION_AUDIO_PLAY:
            if (step->data.audio.track[0]) {
                char track[DEVICE_MANAGER_TRACK_NAME_MAX_LEN];
                automation_engine_render_template(step->data.audio.track, track, sizeof(track));
                if (track[0]) {
                    audio_player_play(track);
                } else {
                    ESP_LOGW(TAG, "audio_play skipped (empty track)");
                }
            }
            break;
        case DEVICE_ACTION_AUDIO_STOP:
            audio_player_stop();
            break;
        case DEVICE_ACTION_SET_FLAG:
            automation_engine_set_flag_internal(step->data.flag.flag, step->data.flag.value);
            break;
        case DEVICE_ACTION_WAIT_FLAGS:
            automation_engine_wait_for_flags(&step->data.wait_flags);
            break;
        case DEVICE_ACTION_LOOP: {
            uint16_t target = step->data.loop.target_step;
            uint16_t max_iter = step->data.loop.max_iterations;
            if (target < total) {
                uint16_t *counter = &loop_counters[idx];
                if (max_iter == 0 || *counter < max_iter) {
                    (*counter)++;
                    idx = target;
                    continue;
                }
            }
            break;
        }
        case DEVICE_ACTION_DELAY:
            break;
        case DEVICE_ACTION_EVENT_BUS: {
            event_bus_type_t type = automation_engine_event_name_to_type(step->data.event.event);
            if (type == EVENT_NONE) {
                ESP_LOGW(TAG, "unknown event action: %s", step->data.event.event);
                break;
            }
            event_bus_message_t msg = {
                .type = type,
            };
            if (step->data.event.topic[0]) {
                automation_engine_render_template(step->data.event.topic, msg.topic, sizeof(msg.topic));
            }
            if (step->data.event.payload[0]) {
                automation_engine_render_template(step->data.event.payload, msg.payload, sizeof(msg.payload));
            }
            event_bus_post(&msg, pdMS_TO_TICKS(50));
            break;
        }
        case DEVICE_ACTION_NOP:
        default:
            break;
        }
        idx++;
    }
    ESP_LOGI(TAG, "scenario finished");
}
