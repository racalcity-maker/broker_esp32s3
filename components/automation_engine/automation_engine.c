#include "automation_engine.h"
#include "automation_engine_internal.h"

#include "esp_log.h"
#include "esp_check.h"
#include "event_bus.h"

static const char *TAG = "automation";
static void automation_handle_event(const event_bus_message_t *msg);

esp_err_t automation_engine_init(void)
{
    ESP_RETURN_ON_ERROR(automation_engine_execution_init(), TAG, "execution init failed");
    ESP_RETURN_ON_ERROR(automation_engine_flags_init(), TAG, "flags init failed");
    ESP_RETURN_ON_ERROR(automation_engine_context_init(), TAG, "context init failed");
    ESP_RETURN_ON_ERROR(event_bus_register_handler(automation_handle_event), TAG, "event reg failed");
    return ESP_OK;
}

esp_err_t automation_engine_start(void)
{
    ESP_RETURN_ON_ERROR(automation_engine_execution_start(), TAG, "execution start failed");
    return ESP_OK;
}

void automation_engine_reload(void)
{
}

bool automation_engine_handle_mqtt(const char *topic, const char *payload)
{
    (void)topic;
    (void)payload;
    return false;
}

esp_err_t automation_engine_trigger(const char *device_id, const char *scenario_id)
{
    const device_descriptor_t *device = automation_engine_find_device_by_id(device_id);
    if (!device) {
        return ESP_ERR_NOT_FOUND;
    }
    const device_scenario_t *scenario = automation_engine_find_scenario_by_id(device, scenario_id);
    if (!scenario) {
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "manual trigger %s/%s",
             device->display_name,
             scenario->name[0] ? scenario->name : scenario->id);
    return automation_engine_enqueue_job(device, scenario);
}

static void automation_handle_event(const event_bus_message_t *msg)
{
    if (!msg) {
        return;
    }
    switch (msg->type) {
    case EVENT_SCENARIO_TRIGGER:
        if (msg->topic[0] && msg->payload[0]) {
            automation_engine_trigger(msg->topic, msg->payload);
        }
        break;
    case EVENT_DEVICE_CONFIG_CHANGED:
        break;
    case EVENT_MQTT_MESSAGE:
        if (msg->topic[0]) {
            automation_engine_handle_mqtt(msg->topic, msg->payload);
        }
        break;
    default:
        break;
    }
}

void automation_engine_set_variable(const char *key, const char *value)
{
    automation_engine_context_set(key, value);
}

void automation_engine_clear_variable(const char *key)
{
    automation_engine_context_clear(key);
}
