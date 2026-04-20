#include "mqtt_core_internal.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "mqtt_core";

static void retain_free_entry(retain_entry_t *slot)
{
    if (!slot) {
        return;
    }
    if (slot->payload) {
        heap_caps_free(slot->payload);
        slot->payload = NULL;
    }
    slot->payload_len = 0;
}

static retain_entry_t *retain_get(const char *topic)
{
    if (!s_retain || !topic) {
        return NULL;
    }
    for (size_t i = 0; i < MQTT_RETAIN_MAX; ++i) {
        if (s_retain[i].in_use && strcmp(s_retain[i].topic, topic) == 0) {
            return &s_retain[i];
        }
    }
    return NULL;
}

void retain_store(const char *topic, const char *payload, uint8_t qos)
{
    if (!s_retain || !topic || !payload) {
        return;
    }
    retain_entry_t *slot = retain_get(topic);
    size_t len = strnlen(payload, MQTT_MAX_PAYLOAD - 1);

    // MQTT retained clear semantics: retained publish with empty payload deletes stored entry.
    if (len == 0) {
        if (slot) {
            retain_free_entry(slot);
            slot->in_use = false;
            slot->topic[0] = '\0';
            slot->qos = 0;
        }
        return;
    }

    if (!slot) {
        for (size_t i = 0; i < MQTT_RETAIN_MAX; ++i) {
            if (!s_retain[i].in_use) {
                slot = &s_retain[i];
                break;
            }
        }
    }
    if (!slot) {
        ESP_LOGW(TAG, "retain table full, dropping %s", topic);
        return;
    }
    char *buf = heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        ESP_LOGW(TAG, "retain alloc failed for %s", topic);
        return;
    }
    memcpy(buf, payload, len);
    buf[len] = '\0';
    retain_free_entry(slot);
    slot->in_use = true;
    strncpy(slot->topic, topic, sizeof(slot->topic) - 1);
    slot->topic[sizeof(slot->topic) - 1] = '\0';
    slot->payload = buf;
    slot->payload_len = len;
    slot->qos = qos;
}

void deliver_retain(mqtt_session_t *sess, const char *filter)
{
    if (!s_retain) {
        return;
    }
    lock();
    for (size_t i = 0; i < MQTT_RETAIN_MAX; ++i) {
        if (!s_retain[i].in_use) {
            continue;
        }
        if (topic_matches_filter(filter, s_retain[i].topic)) {
            const char *payload = s_retain[i].payload ? s_retain[i].payload : "";
            send_publish_packet(sess, s_retain[i].topic, payload, s_retain[i].qos, true, 0);
        }
    }
    unlock();
}
