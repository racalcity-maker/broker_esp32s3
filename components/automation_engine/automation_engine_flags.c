#include "automation_engine_internal.h"

#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "device_model.h"
#include "event_bus.h"

#define AUTOMATION_FLAG_CAPACITY (DEVICE_MANAGER_MAX_DEVICES * DEVICE_MANAGER_MAX_SCENARIOS_PER_DEVICE)

typedef struct {
    char name[DEVICE_MANAGER_FLAG_NAME_MAX_LEN];
    bool in_use;
    bool value;
} automation_flag_t;

typedef struct {
    const char *name;
    event_bus_type_t type;
} automation_event_map_t;

static const char *TAG = "automation_flags";
static automation_flag_t s_flags[AUTOMATION_FLAG_CAPACITY];
static SemaphoreHandle_t s_flag_mutex = NULL;

static const automation_event_map_t s_event_map[] = {
    {"card_ok", EVENT_CARD_OK},
    {"card_bad", EVENT_CARD_BAD},
    {"relay_cmd", EVENT_RELAY_CMD},
    {"audio_play", EVENT_AUDIO_PLAY},
    {"volume_set", EVENT_VOLUME_SET},
    {"web_command", EVENT_WEB_COMMAND},
    {"system_status", EVENT_SYSTEM_STATUS},
    {"device_config_changed", EVENT_DEVICE_CONFIG_CHANGED},
};

static bool automation_requirements_met(const device_wait_flags_t *wait)
{
    if (!wait || wait->requirement_count == 0) {
        return true;
    }
    bool any_met = false;
    for (uint8_t i = 0; i < wait->requirement_count; ++i) {
        bool state = automation_engine_get_flag_internal(wait->requirements[i].flag);
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

esp_err_t automation_engine_flags_init(void)
{
    if (!s_flag_mutex) {
        s_flag_mutex = xSemaphoreCreateMutex();
    }
    return s_flag_mutex ? ESP_OK : ESP_ERR_NO_MEM;
}

void automation_engine_set_flag_internal(const char *name, bool value)
{
    if (!name || !name[0] || !s_flag_mutex) {
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
        bool changed = true;
        if (slot->in_use) {
            changed = (slot->value != value);
        } else {
            strncpy(slot->name, name, sizeof(slot->name) - 1);
            slot->name[sizeof(slot->name) - 1] = 0;
            slot->in_use = true;
        }
        slot->value = value;
        ESP_LOGD(TAG, "flag %s=%d", slot->name, value);
        if (changed) {
            event_bus_message_t msg = {
                .type = EVENT_FLAG_CHANGED,
            };
            strncpy(msg.topic, slot->name, sizeof(msg.topic) - 1);
            strncpy(msg.payload, value ? "true" : "false", sizeof(msg.payload) - 1);
            msg.topic[sizeof(msg.topic) - 1] = 0;
            msg.payload[sizeof(msg.payload) - 1] = 0;
            event_bus_post(&msg, pdMS_TO_TICKS(20));
        }
    } else {
        ESP_LOGW(TAG, "no flag slot for %s", name);
    }
    xSemaphoreGive(s_flag_mutex);
}

bool automation_engine_get_flag_internal(const char *name)
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

bool automation_engine_wait_for_flags(const device_wait_flags_t *wait)
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

event_bus_type_t automation_engine_event_name_to_type(const char *name)
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
