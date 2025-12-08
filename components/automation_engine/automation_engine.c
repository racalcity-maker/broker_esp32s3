#include "automation_engine.h"

#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "audio_player.h"
#include "device_manager.h"
#include "event_bus.h"
#include "mqtt_core.h"

#define AUTOMATION_QUEUE_LENGTH 8
#define AUTOMATION_WORKER_STACK 4096
#define AUTOMATION_WORKER_PRIO 5
#define AUTOMATION_FLAG_CAPACITY (DEVICE_MANAGER_MAX_DEVICES * DEVICE_MANAGER_MAX_SCENARIOS_PER_DEVICE)
#define AUTOMATION_RELOAD_LOCK_TIMEOUT pdMS_TO_TICKS(200)
#define AUTOMATION_TRIGGER_CAPACITY (DEVICE_MANAGER_MAX_DEVICES * DEVICE_MANAGER_MAX_TOPICS_PER_DEVICE)

typedef struct {
    char topic[DEVICE_MANAGER_TOPIC_MAX_LEN];
    const device_descriptor_t *device;
    const device_scenario_t *scenario;
} automation_trigger_t;

typedef struct {
    const device_descriptor_t *device;
    const device_scenario_t *scenario;
} automation_job_t;

typedef struct {
    char name[DEVICE_MANAGER_FLAG_NAME_MAX_LEN];
    bool in_use;
    bool value;
} automation_flag_t;

static const char *TAG = "automation";
static automation_trigger_t *s_triggers = NULL;
static size_t s_trigger_count = 0;
static SemaphoreHandle_t s_trigger_mutex = NULL;
static QueueHandle_t s_job_queue = NULL;
static automation_flag_t s_flags[AUTOMATION_FLAG_CAPACITY];
static SemaphoreHandle_t s_flag_mutex = NULL;
static TaskHandle_t s_worker = NULL;

typedef struct {
    const char *name;
    event_bus_type_t type;
} automation_event_map_t;

static const automation_event_map_t s_event_map[] = {
    {"card_ok", EVENT_CARD_OK},
    {"card_bad", EVENT_CARD_BAD},
    {"laser_trigger", EVENT_LASER_TRIGGER},
    {"relay_cmd", EVENT_RELAY_CMD},
    {"audio_play", EVENT_AUDIO_PLAY},
    {"volume_set", EVENT_VOLUME_SET},
    {"web_command", EVENT_WEB_COMMAND},
    {"system_status", EVENT_SYSTEM_STATUS},
    {"device_config_changed", EVENT_DEVICE_CONFIG_CHANGED},
};

static void automation_worker(void *param);
static void automation_execute_job(const automation_job_t *job);
static void automation_handle_event(const event_bus_message_t *msg);
static event_bus_type_t event_name_to_type(const char *name);
static const device_descriptor_t *find_device_by_id(const char *id);
static const device_scenario_t *find_scenario_by_id(const device_descriptor_t *device, const char *id);

static void automation_set_flag(const char *name, bool value)
{
    if (!name || !name[0]) {
        return;
    }
    if (!s_flag_mutex) {
        return;
    }
    xSemaphoreTake(s_flag_mutex, portMAX_DELAY);
    automation_flag_t *slot = NULL;
    for (size_t i = 0; i < AUTOMATION_FLAG_CAPACITY; ++i) {
        if (s_flags[i].in_use && strcasecmp(s_flags[i].name, name) == 0) {
            slot = &s_flags[i];
            break;
        }
        if (!s_flags[i].in_use && !slot) {
            slot = &s_flags[i];
        }
    }
    if (slot) {
        if (!slot->in_use) {
            strncpy(slot->name, name, sizeof(slot->name) - 1);
            slot->name[sizeof(slot->name) - 1] = 0;
            slot->in_use = true;
        }
        slot->value = value;
        ESP_LOGD(TAG, "flag %s=%d", slot->name, value);
    } else {
        ESP_LOGW(TAG, "no flag slot for %s", name);
    }
    xSemaphoreGive(s_flag_mutex);
}

static bool automation_get_flag(const char *name)
{
    if (!name || !name[0] || !s_flag_mutex) {
        return false;
    }
    bool value = false;
    xSemaphoreTake(s_flag_mutex, portMAX_DELAY);
    for (size_t i = 0; i < AUTOMATION_FLAG_CAPACITY; ++i) {
        if (s_flags[i].in_use && strcasecmp(s_flags[i].name, name) == 0) {
            value = s_flags[i].value;
            break;
        }
    }
    xSemaphoreGive(s_flag_mutex);
    return value;
}

static event_bus_type_t event_name_to_type(const char *name)
{
    if (!name || !name[0]) {
        return EVENT_NONE;
    }
    for (size_t i = 0; i < sizeof(s_event_map) / sizeof(s_event_map[0]); ++i) {
        if (strcasecmp(s_event_map[i].name, name) == 0) {
            return s_event_map[i].type;
        }
    }
    return EVENT_NONE;
}

static bool automation_requirements_met(const device_wait_flags_t *wait)
{
    if (!wait || wait->requirement_count == 0) {
        return true;
    }
    bool any_met = false;
    for (uint8_t i = 0; i < wait->requirement_count; ++i) {
        bool state = automation_get_flag(wait->requirements[i].flag);
        if (wait->requirements[i].required_state) {
            if (!state && wait->mode == DEVICE_CONDITION_ALL) {
                return false;
            }
            if (state && wait->mode == DEVICE_CONDITION_ANY) {
                return true;
            }
            if (state) {
                any_met = true;
            }
        } else {
            if (state && wait->mode == DEVICE_CONDITION_ALL) {
                return false;
            }
            if (!state && wait->mode == DEVICE_CONDITION_ANY) {
                return true;
            }
            if (!state) {
                any_met = true;
            }
        }
    }
    return wait->mode == DEVICE_CONDITION_ALL ? true : any_met;
}

static bool automation_wait_for_flags(const device_wait_flags_t *wait)
{
    if (!wait) {
        return true;
    }
    int64_t timeout_ms = wait->timeout_ms;
    const int64_t start = esp_timer_get_time() / 1000;
    while (!automation_requirements_met(wait)) {
        if (timeout_ms > 0) {
            int64_t now = esp_timer_get_time() / 1000;
            if (now - start >= timeout_ms) {
                ESP_LOGW(TAG, "wait flags timeout (%" PRIu32 " ms)", (uint32_t)wait->timeout_ms);
                return false;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return true;
}

static const device_descriptor_t *find_device_by_id(const char *id)
{
    if (!id || !id[0]) {
        return NULL;
    }
    const device_manager_config_t *cfg = device_manager_get();
    if (!cfg) {
        return NULL;
    }
    for (uint8_t i = 0; i < cfg->device_count; ++i) {
        const device_descriptor_t *dev = &cfg->devices[i];
        if ((dev->id[0] && strcasecmp(dev->id, id) == 0) ||
            (dev->display_name[0] && strcasecmp(dev->display_name, id) == 0)) {
            return dev;
        }
    }
    return NULL;
}

static const device_scenario_t *find_scenario_for_binding(const device_descriptor_t *device, const char *name)
{
    if (!device || !name || !name[0]) {
        return NULL;
    }
    for (uint8_t i = 0; i < device->scenario_count; ++i) {
        const device_scenario_t *sc = &device->scenarios[i];
        if ((sc->id[0] && strcasecmp(sc->id, name) == 0) ||
            (sc->name[0] && strcasecmp(sc->name, name) == 0)) {
            return sc;
        }
    }
    return NULL;
}

static const device_scenario_t *find_scenario_by_id(const device_descriptor_t *device, const char *id)
{
    if (!device || !id || !id[0]) {
        return NULL;
    }
    for (uint8_t i = 0; i < device->scenario_count; ++i) {
        const device_scenario_t *sc = &device->scenarios[i];
        if ((sc->id[0] && strcasecmp(sc->id, id) == 0) ||
            (sc->name[0] && strcasecmp(sc->name, id) == 0)) {
            return sc;
        }
    }
    return NULL;
}

esp_err_t automation_engine_init(void)
{
    if (!s_trigger_mutex) {
        s_trigger_mutex = xSemaphoreCreateMutex();
    }
    if (!s_flag_mutex) {
        s_flag_mutex = xSemaphoreCreateMutex();
    }
    if (!s_job_queue) {
        s_job_queue = xQueueCreate(AUTOMATION_QUEUE_LENGTH, sizeof(automation_job_t));
    }
    ESP_RETURN_ON_FALSE(s_trigger_mutex && s_flag_mutex && s_job_queue, ESP_ERR_NO_MEM, TAG, "init alloc failed");
    ESP_RETURN_ON_ERROR(event_bus_register_handler(automation_handle_event), TAG, "event reg failed");
    return ESP_OK;
}

esp_err_t automation_engine_start(void)
{
    if (!s_worker) {
        BaseType_t ok = xTaskCreate(automation_worker, "automation", AUTOMATION_WORKER_STACK, NULL, AUTOMATION_WORKER_PRIO, &s_worker);
        ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_FAIL, TAG, "worker create failed");
    }
    automation_engine_reload();
    return ESP_OK;
}

void automation_engine_reload(void)
{
    const device_manager_config_t *cfg = device_manager_get();
    if (!cfg) {
        return;
    }
    size_t capacity = AUTOMATION_TRIGGER_CAPACITY;
    automation_trigger_t *fresh = heap_caps_calloc(capacity, sizeof(automation_trigger_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!fresh) {
        ESP_LOGE(TAG, "alloc triggers failed");
        return;
    }
    size_t count = 0;
    for (uint8_t d = 0; d < cfg->device_count && d < DEVICE_MANAGER_MAX_DEVICES; ++d) {
        const device_descriptor_t *device = &cfg->devices[d];
        for (uint8_t t = 0; t < device->topic_count && t < DEVICE_MANAGER_MAX_TOPICS_PER_DEVICE; ++t) {
            const device_topic_binding_t *binding = &device->topics[t];
            const device_scenario_t *scenario = find_scenario_for_binding(device, binding->name);
            if (!scenario) {
                ESP_LOGW(TAG, "device %s topic %s has no scenario %s", device->display_name, binding->topic, binding->name);
                continue;
            }
            if (!binding->topic[0]) {
                continue;
            }
            if (count >= capacity) {
                break;
            }
            automation_trigger_t *tr = &fresh[count++];
            strncpy(tr->topic, binding->topic, sizeof(tr->topic) - 1);
            tr->device = device;
            tr->scenario = scenario;
        }
    }
    if (s_trigger_mutex) {
        xSemaphoreTake(s_trigger_mutex, portMAX_DELAY);
    }
    if (s_triggers) {
        heap_caps_free(s_triggers);
    }
    s_triggers = fresh;
    s_trigger_count = count;
    if (s_trigger_mutex) {
        xSemaphoreGive(s_trigger_mutex);
    }
    ESP_LOGI(TAG, "automation triggers: %zu", count);
}

static esp_err_t enqueue_job(const device_descriptor_t *device, const device_scenario_t *scenario)
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

bool automation_engine_handle_mqtt(const char *topic, const char *payload)
{
    (void)payload;
    if (!topic || !s_job_queue) {
        return false;
    }
    bool handled = false;
    if (s_trigger_mutex) {
        xSemaphoreTake(s_trigger_mutex, AUTOMATION_RELOAD_LOCK_TIMEOUT);
    }
    for (size_t i = 0; i < s_trigger_count; ++i) {
        const automation_trigger_t *tr = &s_triggers[i];
        if (strcmp(tr->topic, topic) == 0) {
            if (enqueue_job(tr->device, tr->scenario) == ESP_OK) {
                handled = true;
                ESP_LOGI(TAG, "queued scenario %s/%s for topic %s",
                         tr->device->display_name,
                         tr->scenario->name[0] ? tr->scenario->name : tr->scenario->id,
                         topic);
            } else {
                ESP_LOGW(TAG, "job queue full for topic %s", topic);
            }
        }
    }
    if (s_trigger_mutex) {
        xSemaphoreGive(s_trigger_mutex);
    }
    return handled;
}

esp_err_t automation_engine_trigger(const char *device_id, const char *scenario_id)
{
    const device_descriptor_t *device = find_device_by_id(device_id);
    if (!device) {
        return ESP_ERR_NOT_FOUND;
    }
    const device_scenario_t *scenario = find_scenario_by_id(device, scenario_id);
    if (!scenario) {
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "manual trigger %s/%s",
             device->display_name,
             scenario->name[0] ? scenario->name : scenario->id);
    return enqueue_job(device, scenario);
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
                mqtt_core_publish(step->data.mqtt.topic, step->data.mqtt.payload);
            }
            break;
        case DEVICE_ACTION_AUDIO_PLAY:
            if (step->data.audio.track[0]) {
                audio_player_play(step->data.audio.track);
            }
            break;
        case DEVICE_ACTION_AUDIO_STOP:
            audio_player_stop();
            break;
        case DEVICE_ACTION_LASER_TRIGGER: {
            event_bus_message_t msg = {
                .type = EVENT_LASER_TRIGGER,
            };
            event_bus_post(&msg, 0);
            break;
        }
        case DEVICE_ACTION_SET_FLAG:
            automation_set_flag(step->data.flag.flag, step->data.flag.value);
            break;
        case DEVICE_ACTION_WAIT_FLAGS:
            automation_wait_for_flags(&step->data.wait_flags);
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
            event_bus_type_t type = event_name_to_type(step->data.event.event);
            if (type == EVENT_NONE) {
                ESP_LOGW(TAG, "unknown event action: %s", step->data.event.event);
                break;
            }
            event_bus_message_t msg = {
                .type = type,
            };
            if (step->data.event.topic[0]) {
                strncpy(msg.topic, step->data.event.topic, sizeof(msg.topic) - 1);
            }
            if (step->data.event.payload[0]) {
                strncpy(msg.payload, step->data.event.payload, sizeof(msg.payload) - 1);
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

static void automation_handle_event(const event_bus_message_t *msg)
{
    if (!msg) {
        return;
    }
    if (msg->type == EVENT_DEVICE_CONFIG_CHANGED) {
        automation_engine_reload();
    }
}
