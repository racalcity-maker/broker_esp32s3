#include "automation_engine_internal.h"

#include <stddef.h>
#include <string.h>
#include <strings.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "device_manager.h"

#define AUTOMATION_TRIGGER_CAPACITY (DEVICE_MANAGER_MAX_DEVICES * DEVICE_MANAGER_MAX_TOPICS_PER_DEVICE)

static const char *TAG = "automation_reg";
static automation_trigger_t *s_triggers = NULL;
static size_t s_trigger_count = 0;
static SemaphoreHandle_t s_trigger_mutex = NULL;

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

esp_err_t automation_engine_registry_init(void)
{
    if (!s_trigger_mutex) {
        s_trigger_mutex = xSemaphoreCreateMutex();
    }
    return s_trigger_mutex ? ESP_OK : ESP_ERR_NO_MEM;
}

void automation_engine_registry_reload(void)
{
    size_t capacity = AUTOMATION_TRIGGER_CAPACITY;
    automation_trigger_t *fresh = heap_caps_calloc(capacity, sizeof(automation_trigger_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!fresh) {
        ESP_LOGE(TAG, "alloc triggers failed");
        return;
    }
    const device_manager_config_t *cfg = device_manager_lock_config();
    if (!cfg) {
        device_manager_unlock_config();
        heap_caps_free(fresh);
        return;
    }
    size_t count = 0;
    uint8_t device_cap = cfg->device_capacity ? cfg->device_capacity : DEVICE_MANAGER_MAX_DEVICES;
    for (uint8_t d = 0; d < cfg->device_count && d < device_cap; ++d) {
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
    device_manager_unlock_config();
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

const automation_trigger_t *automation_engine_registry_lock(size_t *count)
{
    if (count) {
        *count = 0;
    }
    if (!s_trigger_mutex) {
        return NULL;
    }
    if (xSemaphoreTake(s_trigger_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return NULL;
    }
    if (count) {
        *count = s_trigger_count;
    }
    return s_triggers;
}

void automation_engine_registry_unlock(void)
{
    if (s_trigger_mutex) {
        xSemaphoreGive(s_trigger_mutex);
    }
}

const device_descriptor_t *automation_engine_find_device_by_id(const char *id)
{
    if (!id || !id[0]) {
        return NULL;
    }
    const device_manager_config_t *cfg = device_manager_lock_config();
    if (!cfg) {
        device_manager_unlock_config();
        return NULL;
    }
    const device_descriptor_t *result = NULL;
    uint8_t limit = cfg->device_capacity ? cfg->device_capacity : DEVICE_MANAGER_MAX_DEVICES;
    for (uint8_t i = 0; i < cfg->device_count && i < limit; ++i) {
        const device_descriptor_t *dev = &cfg->devices[i];
        if ((dev->id[0] && strcasecmp(dev->id, id) == 0) ||
            (dev->display_name[0] && strcasecmp(dev->display_name, id) == 0)) {
            result = dev;
            break;
        }
    }
    device_manager_unlock_config();
    return result;
}

const device_scenario_t *automation_engine_find_scenario_by_id(const device_descriptor_t *device, const char *id)
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
