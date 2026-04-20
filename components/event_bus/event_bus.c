#include "event_bus.h"

#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define EVENT_BUS_QUEUE_LEN 64
#define EVENT_BUS_MAX_HANDLERS 8

static const char *TAG = "event_bus";
static QueueHandle_t s_queue = NULL;
static event_bus_handler_t s_handlers[EVENT_BUS_MAX_HANDLERS];
static size_t s_handler_count = 0;
static TaskHandle_t s_task = NULL;
static portMUX_TYPE s_handler_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_drop_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_drop_count = 0;
static uint32_t s_warned_drop = 0;

static void event_bus_task(void *param)
{
    (void)param;
    event_bus_message_t msg;
    while (xQueueReceive(s_queue, &msg, portMAX_DELAY) == pdTRUE) {
        event_bus_handler_t local[EVENT_BUS_MAX_HANDLERS] = {0};
        size_t count = 0;
        taskENTER_CRITICAL(&s_handler_lock);
        count = s_handler_count;
        if (count > EVENT_BUS_MAX_HANDLERS) {
            count = EVENT_BUS_MAX_HANDLERS;
        }
        memcpy(local, s_handlers, count * sizeof(event_bus_handler_t));
        taskEXIT_CRITICAL(&s_handler_lock);
        for (size_t i = 0; i < count; ++i) {
            if (local[i]) {
                local[i](&msg);
            }
        }
    }
    taskENTER_CRITICAL(&s_handler_lock);
    s_task = NULL;
    taskEXIT_CRITICAL(&s_handler_lock);
    vTaskDelete(NULL);
}

esp_err_t event_bus_init(void)
{
    if (!s_queue) {
        s_queue = xQueueCreate(EVENT_BUS_QUEUE_LEN, sizeof(event_bus_message_t));
    }
    if (!s_queue) {
        return ESP_ERR_NO_MEM;
    }
    taskENTER_CRITICAL(&s_handler_lock);
    memset(s_handlers, 0, sizeof(s_handlers));
    s_handler_count = 0;
    taskEXIT_CRITICAL(&s_handler_lock);
    taskENTER_CRITICAL(&s_drop_lock);
    s_drop_count = 0;
    s_warned_drop = 0;
    taskEXIT_CRITICAL(&s_drop_lock);
    return ESP_OK;
}

esp_err_t event_bus_start(void)
{
    if (!s_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    taskENTER_CRITICAL(&s_handler_lock);
    bool already_started = (s_task != NULL);
    taskEXIT_CRITICAL(&s_handler_lock);
    if (already_started) {
        ESP_LOGW(TAG, "event bus already started");
        return ESP_OK;
    }

    TaskHandle_t task = NULL;
    BaseType_t ok = xTaskCreate(event_bus_task, "event_bus", 4096, NULL, 5, &task);
    if (ok == pdPASS) {
        taskENTER_CRITICAL(&s_handler_lock);
        s_task = task;
        taskEXIT_CRITICAL(&s_handler_lock);
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t event_bus_post(const event_bus_message_t *message, TickType_t timeout)
{
    if (!message || !s_queue) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xQueueSend(s_queue, message, timeout) == pdTRUE) {
        return ESP_OK;
    }
    uint32_t drops = 0;
    bool should_warn = false;
    taskENTER_CRITICAL(&s_drop_lock);
    drops = ++s_drop_count;
    if (drops == 1 || (drops % 50 == 0 && s_warned_drop < drops)) {
        s_warned_drop = drops;
        should_warn = true;
    }
    taskEXIT_CRITICAL(&s_drop_lock);
    if (should_warn) {
        ESP_LOGW(TAG, "event bus queue full (drops=%" PRIu32 ")", drops);
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t event_bus_register_handler(event_bus_handler_t handler)
{
    if (!handler) {
        return ESP_ERR_INVALID_ARG;
    }
    taskENTER_CRITICAL(&s_handler_lock);
    esp_err_t err = ESP_OK;
    if (s_handler_count >= EVENT_BUS_MAX_HANDLERS) {
        err = ESP_ERR_NO_MEM;
    } else {
        s_handlers[s_handler_count++] = handler;
    }
    taskEXIT_CRITICAL(&s_handler_lock);
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(TAG, "handler registered (%d/%d)", (int)s_handler_count, EVENT_BUS_MAX_HANDLERS);
    return ESP_OK;
}
