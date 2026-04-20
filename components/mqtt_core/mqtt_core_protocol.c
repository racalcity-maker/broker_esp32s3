#include "mqtt_core.h"
#include "mqtt_core_internal.h"

#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"

static const char *TAG = "mqtt_core";

static int parse_utf8_str(const uint8_t *buf, size_t len, size_t *offset, char *out, size_t out_len)
{
    if (*offset + 2 > len) {
        return -1;
    }
    uint16_t slen = (buf[*offset] << 8) | buf[*offset + 1];
    *offset += 2;
    if (*offset + slen > len || slen >= out_len) {
        return -1;
    }
    memcpy(out, buf + *offset, slen);
    out[slen] = 0;
    *offset += slen;
    return 0;
}

static bool mqtt_authenticate_client(const char *client_id, const char *username, const char *password)
{
    const app_config_t *cfg = config_store_get();
    if (!cfg) {
        return false;
    }
    if (cfg->mqtt.user_count == 0) {
        return true;
    }
    for (uint8_t i = 0; i < cfg->mqtt.user_count; ++i) {
        const app_mqtt_user_t *user = &cfg->mqtt.users[i];
        if (strcmp(user->client_id, client_id) != 0) {
            continue;
        }
        if (strcmp(user->username, username ? username : "") != 0) {
            continue;
        }
        if (strcmp(user->password, password ? password : "") != 0) {
            continue;
        }
        return true;
    }
    return false;
}

void publish_to_subscribers(const char *topic, const char *payload, uint8_t qos, bool retain_flag, mqtt_session_t *exclude)
{
    if (!s_sessions || !s_lock) {
        ESP_LOGW(TAG, "publish ignored: mqtt core not initialized");
        return;
    }
    lock();
    if (retain_flag) {
        retain_store(topic, payload, qos);
    }
    for (size_t i = 0; i < MQTT_MAX_CLIENTS; ++i) {
        mqtt_session_t *s = &s_sessions[i];
        if (!s->active || s == exclude) {
            continue;
        }
        for (size_t j = 0; j < s->sub_count; ++j) {
            if (topic_matches_filter(s->subs[j].topic, topic)) {
                uint16_t pid = (qos ? (uint16_t)(esp_random() & 0xFFFF) : 0);
                if (send_publish_packet(s, topic, payload, qos, retain_flag, pid) < 0) {
                    ESP_LOGW(TAG, "send publish failed to %s", s->client_id);
                }
                break;
            }
        }
    }
    unlock();
}

int handle_connect(mqtt_session_t *sess, const uint8_t *buf, size_t len)
{
    size_t off = 0;
    char proto[8];
    if (parse_utf8_str(buf, len, &off, proto, sizeof(proto)) != 0) {
        return -1;
    }
    if (strcmp(proto, "MQTT") != 0 && strcmp(proto, "MQIsdp") != 0) {
        return -1;
    }
    if (off + 4 > len) {
        return -1;
    }
    uint8_t level = buf[off++];
    uint8_t flags = buf[off++];
    uint16_t keepalive = (buf[off] << 8) | buf[off + 1];
    off += 2;

    char client_id[CONFIG_STORE_CLIENT_ID_MAX];
    if (parse_utf8_str(buf, len, &off, client_id, sizeof(client_id)) != 0) {
        return -1;
    }

    if (level != 4) {
        ESP_LOGE(TAG, "Unsupported MQTT protocol level %u. Only 3.1.1 is supported.", level);
        return -1;
    }

    lock();
    mqtt_session_t *old = find_session_by_client_id(client_id);
    if (old && old != sess) {
        ESP_LOGW(TAG, "Replacing session for client_id=%s", client_id);
        old->suppress_will = true;
        request_session_close(old, "duplicate client_id", 0);
    }
    unlock();

    sess->keepalive = keepalive;
    strncpy(sess->client_id, client_id, sizeof(sess->client_id) - 1);
    sess->last_rx_ms = now_ms();

    bool will_flag = flags & 0x04;
    bool will_retain = flags & 0x20;
    uint8_t will_qos = (flags >> 3) & 0x03;
    if (will_flag) {
        char will_topic[MQTT_MAX_TOPIC];
        char will_payload[MQTT_MAX_PAYLOAD];
        if (parse_utf8_str(buf, len, &off, will_topic, sizeof(will_topic)) != 0) {
            return -1;
        }
        if (parse_utf8_str(buf, len, &off, will_payload, sizeof(will_payload)) != 0) {
            return -1;
        }
        sess->will.has = true;
        strncpy(sess->will.topic, will_topic, sizeof(sess->will.topic) - 1);
        strncpy(sess->will.payload, will_payload, sizeof(sess->will.payload) - 1);
        sess->will.qos = will_qos;
        sess->will.retain = will_retain;
    }

    char username[CONFIG_STORE_USERNAME_MAX] = {0};
    char password[CONFIG_STORE_PASSWORD_MAX] = {0};
    if (flags & 0x80) {
        if (parse_utf8_str(buf, len, &off, username, sizeof(username)) != 0) {
            return -1;
        }
    }
    if (flags & 0x40) {
        if (parse_utf8_str(buf, len, &off, password, sizeof(password)) != 0) {
            return -1;
        }
    }
    if (!mqtt_authenticate_client(client_id, username, password)) {
        ESP_LOGW(TAG, "MQTT auth failed for client_id=%s", client_id);
        return -1;
    }
    return 0;
}

int handle_subscribe(mqtt_session_t *sess, const uint8_t *buf, size_t len)
{
    size_t off = 0;
    if (len < 2) {
        return -1;
    }
    uint16_t pid = (buf[off] << 8) | buf[off + 1];
    off += 2;

    uint8_t granted[MQTT_MAX_SUBS];
    size_t granted_count = 0;

    while (off + 3 <= len && granted_count < MQTT_MAX_SUBS) {
        char topic[MQTT_MAX_TOPIC];
        if (parse_utf8_str(buf, len, &off, topic, sizeof(topic)) != 0) {
            return -1;
        }
        uint8_t rqos = buf[off++];
        if (!acl_can_subscribe(sess->client_id, topic)) {
            ESP_LOGW(TAG, "ACL deny sub %s -> %s", sess->client_id, topic);
            granted[granted_count++] = 0x80;
            continue;
        }
        if (sess->sub_count < MQTT_MAX_SUBS) {
            strncpy(sess->subs[sess->sub_count].topic, topic, sizeof(sess->subs[sess->sub_count].topic) - 1);
            sess->subs[sess->sub_count].qos = rqos > 1 ? 1 : rqos;
            sess->sub_count++;
            granted[granted_count++] = rqos > 1 ? 1 : rqos;
            deliver_retain(sess, topic);
        } else {
            granted[granted_count++] = 0x80;
        }
    }

    return send_suback(sess, pid, granted, granted_count);
}

int handle_unsubscribe(mqtt_session_t *sess, const uint8_t *buf, size_t len)
{
    size_t off = 0;
    if (len < 2) {
        return -1;
    }
    uint16_t pid = (buf[off] << 8) | buf[off + 1];
    off += 2;

    while (off + 2 <= len) {
        char topic[MQTT_MAX_TOPIC];
        if (parse_utf8_str(buf, len, &off, topic, sizeof(topic)) != 0) {
            return -1;
        }

        for (size_t i = 0; i < sess->sub_count; ) {
            if (strcmp(sess->subs[i].topic, topic) == 0) {
                if (i + 1 < sess->sub_count) {
                    memmove(&sess->subs[i],
                            &sess->subs[i + 1],
                            (sess->sub_count - i - 1) * sizeof(sess->subs[0]));
                }
                memset(&sess->subs[sess->sub_count - 1], 0, sizeof(sess->subs[0]));
                sess->sub_count--;
                continue;
            }
            ++i;
        }
    }

    return send_unsuback(sess->sock, pid);
}

int handle_publish(mqtt_session_t *sess, uint8_t header, const uint8_t *buf, size_t len)
{
    size_t off = 0;
    if (len < 2) {
        return -1;
    }
    uint16_t topic_len = (buf[off] << 8) | buf[off + 1];
    off += 2;
    if (off + topic_len > len || topic_len >= MQTT_MAX_TOPIC) {
        return -1;
    }
    char topic[MQTT_MAX_TOPIC];
    memcpy(topic, buf + off, topic_len);
    topic[topic_len] = 0;
    off += topic_len;

    uint16_t pid = 0;
    uint8_t qos = (header >> 1) & 0x03;
    bool retain = header & 0x01;
    if (qos) {
        if (off + 2 > len) {
            return -1;
        }
        pid = (buf[off] << 8) | buf[off + 1];
        off += 2;
    }
    if (!acl_can_publish(sess->client_id, topic)) {
        ESP_LOGW(TAG, "ACL deny pub %s -> %s", sess->client_id, topic);
        return 0;
    }
    size_t payload_len = len - off;
    if (payload_len >= MQTT_MAX_PAYLOAD) {
        payload_len = MQTT_MAX_PAYLOAD - 1;
    }
    char payload[MQTT_MAX_PAYLOAD];
    memcpy(payload, buf + off, payload_len);
    payload[payload_len] = 0;

    mqtt_core_inject_message(topic, payload);
    publish_to_subscribers(topic, payload, qos, retain, NULL);

    if (qos == 1) {
        send_puback(sess->sock, pid);
    }
    return 0;
}
