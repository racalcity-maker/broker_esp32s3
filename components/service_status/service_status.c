#include "service_status.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct {
    SemaphoreHandle_t mutex;
    service_status_entry_t entries[SERVICE_STATUS_COUNT];
} service_status_state_t;

static service_status_state_t s_status = {0};

static bool service_status_lock(void)
{
    return s_status.mutex && xSemaphoreTake(s_status.mutex, portMAX_DELAY) == pdTRUE;
}

static void service_status_unlock(void)
{
    if (s_status.mutex) {
        xSemaphoreGive(s_status.mutex);
    }
}

esp_err_t service_status_init(void)
{
    if (!s_status.mutex) {
        s_status.mutex = xSemaphoreCreateMutex();
        if (!s_status.mutex) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (service_status_lock()) {
        memset(s_status.entries, 0, sizeof(s_status.entries));
        service_status_unlock();
    }
    return ESP_OK;
}

void service_status_mark_init(service_status_id_t id, esp_err_t err)
{
    if (id < 0 || id >= SERVICE_STATUS_COUNT) {
        return;
    }
    if (service_status_lock()) {
        s_status.entries[id].init_attempted = true;
        s_status.entries[id].init_ok = (err == ESP_OK);
        if (err != ESP_OK) {
            s_status.entries[id].start_attempted = false;
            s_status.entries[id].start_ok = false;
        }
        service_status_unlock();
    }
}

void service_status_mark_start(service_status_id_t id, esp_err_t err)
{
    if (id < 0 || id >= SERVICE_STATUS_COUNT) {
        return;
    }
    if (service_status_lock()) {
        s_status.entries[id].start_attempted = true;
        s_status.entries[id].start_ok = (err == ESP_OK);
        service_status_unlock();
    }
}

bool service_status_get(service_status_id_t id, service_status_entry_t *out)
{
    if (id < 0 || id >= SERVICE_STATUS_COUNT || !out) {
        return false;
    }
    if (!service_status_lock()) {
        return false;
    }
    *out = s_status.entries[id];
    service_status_unlock();
    return true;
}

const char *service_status_name(service_status_id_t id)
{
    switch (id) {
    case SERVICE_STATUS_NETWORK:
        return "network";
    case SERVICE_STATUS_MQTT:
        return "mqtt";
    case SERVICE_STATUS_AUDIO:
        return "audio";
    case SERVICE_STATUS_WEB_UI:
        return "web_ui";
    default:
        return "unknown";
    }
}
