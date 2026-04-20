#include "mqtt_core_internal.h"

#include <string.h>

#include "esp_log.h"
#include "lwip/sockets.h"

static const char *TAG = "mqtt_core";

mqtt_session_t *alloc_session(void)
{
    if (!s_sessions) {
        return NULL;
    }
    for (size_t i = 0; i < MQTT_MAX_CLIENTS; ++i) {
        if (!s_sessions[i].active) {
            memset(&s_sessions[i], 0, sizeof(s_sessions[i]));
            s_sessions[i].active = true;
            s_sessions[i].sock = -1;
            // Pre-CONNECT sessions should not look infinitely idle to the sweep timer.
            s_sessions[i].last_rx_ms = now_ms();
            s_client_count++;
            return &s_sessions[i];
        }
    }
    return NULL;
}

mqtt_session_t *find_session_by_client_id(const char *client_id)
{
    if (!client_id || !client_id[0]) {
        return NULL;
    }
    for (size_t i = 0; i < MQTT_MAX_CLIENTS; ++i) {
        if (s_sessions[i].active && strcmp(s_sessions[i].client_id, client_id) == 0) {
            return &s_sessions[i];
        }
    }
    return NULL;
}

void free_session(mqtt_session_t *s)
{
    if (!s) {
        return;
    }
    TaskHandle_t current = xTaskGetCurrentTaskHandle();
    if (s->task && s->task != current) {
        s->suppress_will = true;
        s->closing = true;
        if (s->sock >= 0) {
            shutdown(s->sock, SHUT_RDWR);
        }
        return;
    }
    s->active = false;
    s->closing = false;
    if (s->sock >= 0) {
        shutdown(s->sock, SHUT_RDWR);
        closesocket(s->sock);
    }
    s->sock = -1;
    s->task = NULL;
    if (s_client_count > 0) {
        s_client_count--;
    }
}

void sweep_idle_sessions(void)
{
    int64_t now = now_ms();
    lock();
    for (size_t i = 0; i < MQTT_MAX_CLIENTS; ++i) {
        mqtt_session_t *s = &s_sessions[i];
        if (!s->active) {
            continue;
        }
        int64_t idle_ms = now - s->last_rx_ms;
        int64_t limit_ms = (s->keepalive > 0) ? (int64_t)s->keepalive * 1500 : 60000;
        if (idle_ms >= limit_ms) {
            request_session_close(s, "sweep: closing idle session", 0);
        }
    }
    unlock();
}

void send_will_if_needed(mqtt_session_t *sess)
{
    if (sess->will.has && !sess->suppress_will) {
        ESP_LOGI(TAG, "sending will for %s", sess->client_id);
        publish_to_subscribers(sess->will.topic, sess->will.payload, sess->will.qos, sess->will.retain, sess);
    }
}
