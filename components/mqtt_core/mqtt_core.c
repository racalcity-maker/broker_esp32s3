#include "mqtt_core.h"
#include "mqtt_core_internal.h"

#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

#include "config_store.h"
#include "event_bus.h"

// Минимальный MQTT 3.1.1 брокер: QoS0/1, retain, LWT, простая ACL (prefix-based), без QoS2/username/password/TLS.

static const char *TAG = "mqtt_core";

mqtt_session_t *s_sessions = NULL;
StackType_t *s_session_stacks[MQTT_MAX_CLIENTS];
StaticTask_t *s_session_tcbs[MQTT_MAX_CLIENTS];
uint8_t *s_session_tx_bufs[MQTT_MAX_CLIENTS];
retain_entry_t *s_retain = NULL;
SemaphoreHandle_t s_lock = NULL;
uint8_t s_client_count = 0;
int s_listen_sock = -1;
TaskHandle_t s_accept_task = NULL;
StackType_t *s_accept_stack = NULL;
StaticTask_t *s_accept_tcb = NULL;
esp_timer_handle_t s_sweep_timer = NULL;
bool s_event_handler_registered = false;

size_t session_index(const mqtt_session_t *sess)
{
    if (!sess || !s_sessions) {
        return MQTT_MAX_CLIENTS;
    }
    return (size_t)(sess - s_sessions);
}

bool ensure_session_task_storage(size_t idx)
{
    if (idx >= MQTT_MAX_CLIENTS) {
        return false;
    }
    if (!s_session_stacks[idx]) {
        s_session_stacks[idx] = heap_caps_malloc(MQTT_CLIENT_STACK * sizeof(StackType_t),
                                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_session_stacks[idx]) {
            return false;
        }
    }
    if (!s_session_tcbs[idx]) {
        s_session_tcbs[idx] = heap_caps_malloc(sizeof(StaticTask_t),
                                               MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!s_session_tcbs[idx]) {
            heap_caps_free(s_session_stacks[idx]);
            s_session_stacks[idx] = NULL;
            return false;
        }
    }
    return true;
}


uint8_t *ensure_session_tx_buffer(size_t idx)
{
    if (idx >= MQTT_MAX_CLIENTS) {
        return NULL;
    }
    if (!s_session_tx_bufs[idx]) {
        s_session_tx_bufs[idx] = heap_caps_malloc(MQTT_MAX_PACKET, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    return s_session_tx_bufs[idx];
}

bool ensure_accept_task_storage(void)
{
    if (!s_accept_stack) {
        s_accept_stack = heap_caps_malloc(MQTT_ACCEPT_STACK * sizeof(StackType_t),
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_accept_stack) {
            return false;
        }
    }
    if (!s_accept_tcb) {
        s_accept_tcb = heap_caps_malloc(sizeof(StaticTask_t),
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!s_accept_tcb) {
            heap_caps_free(s_accept_stack);
            s_accept_stack = NULL;
            return false;
        }
    }
    return true;
}

void mqtt_core_get_client_stats(mqtt_client_stats_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
    for (size_t i = 0; i < MQTT_MAX_CLIENTS; ++i) {
        if (!s_sessions[i].active) continue;
        out->total++;
    }
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

uint8_t mqtt_core_client_count(void)
{
    uint8_t count = 0;
    lock();
    count = s_client_count;
    unlock();
    return count;
}

int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

void lock(void)
{
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

void unlock(void)
{
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

void request_session_close(mqtt_session_t *sess, const char *reason, int err)
{
    if (!sess) {
        return;
    }
    const char *cid = sess->client_id[0] ? sess->client_id : "<unknown>";
    ESP_LOGW(TAG, "%s for %s (err=%d)", reason ? reason : "session closing", cid, err);
    sess->closing = true;
    if (sess->sock >= 0) {
        int sock = sess->sock;
        sess->sock = -1;
        shutdown(sock, SHUT_RDWR);
        closesocket(sock);
    }
}

esp_err_t mqtt_core_init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }
    if (!s_sessions) {
        s_sessions = heap_caps_calloc(MQTT_MAX_CLIENTS, sizeof(mqtt_session_t),
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_sessions) {
            ESP_LOGE(TAG, "failed to allocate sessions in PSRAM");
            return ESP_ERR_NO_MEM;
        }
    }
    if (!s_retain) {
        s_retain = heap_caps_calloc(MQTT_RETAIN_MAX, sizeof(retain_entry_t),
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_retain) {
            ESP_LOGE(TAG, "failed to allocate retain table in PSRAM");
            // Освобождаем ранее выделенную память
            heap_caps_free(s_sessions);
            s_sessions = NULL;
            return ESP_ERR_NO_MEM;
        }
    }
    if (!s_event_handler_registered) {
        esp_err_t err = event_bus_register_handler(on_event_bus_message);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "failed to register event handler: %s", esp_err_to_name(err));
            return err;
        }
        s_event_handler_registered = true;
    }
    return ESP_OK;
}

esp_err_t mqtt_core_start(void)
{
    const app_config_t *cfg = config_store_get();
    return mqtt_core_start_server(cfg->mqtt.port);
}

esp_err_t mqtt_core_publish(const char *topic, const char *payload)
{
    if (!topic || !payload) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_sessions || !s_lock) {
        return ESP_ERR_INVALID_STATE;
    }
    publish_to_subscribers(topic, payload, 0, false, NULL);
    return ESP_OK;
}

esp_err_t mqtt_core_inject_message(const char *topic, const char *payload)
{
    if (!topic || !payload) {
        return ESP_ERR_INVALID_ARG;
    }
    event_bus_type_t type = find_type_by_topic(topic);
    if (type != EVENT_NONE) {
        event_bus_message_t typed = {
            .type = type,
        };
        strncpy(typed.topic, topic, sizeof(typed.topic) - 1);
        strncpy(typed.payload, payload, sizeof(typed.payload) - 1);
#if MQTT_CORE_DEBUG
        ESP_LOGI(TAG, "[MQTT IN] %s -> event %d", topic, type);
#endif
        event_bus_post(&typed, pdMS_TO_TICKS(100));
    }

    event_bus_message_t generic = {
        .type = EVENT_MQTT_MESSAGE,
    };
    strncpy(generic.topic, topic, sizeof(generic.topic) - 1);
    strncpy(generic.payload, payload, sizeof(generic.payload) - 1);
    return event_bus_post(&generic, pdMS_TO_TICKS(100));
}
