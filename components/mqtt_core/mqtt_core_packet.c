#include "mqtt_core_internal.h"

#include <errno.h>
#include <string.h>

#include "esp_log.h"
#include "lwip/sockets.h"

static const char *TAG = "mqtt_core";

static size_t encode_remaining_length(uint8_t *out, size_t rem_len)
{
    size_t idx = 0;
    do {
        uint8_t byte = rem_len % 128;
        rem_len /= 128;
        if (rem_len > 0) {
            byte |= 0x80;
        }
        out[idx++] = byte;
    } while (rem_len > 0 && idx < 4);
    return idx;
}

int recv_all(int sock, uint8_t *buf, size_t len)
{
    size_t got = 0;
    while (got < len) {
        int r = recv(sock, buf + got, len - got, 0);
        if (r <= 0) {
            return -1;
        }
        got += (size_t)r;
    }
    return (int)got;
}

int send_all(int sock, const uint8_t *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        int r = send(sock, buf + sent, len - sent, 0);
        if (r <= 0) {
            return -1;
        }
        sent += (size_t)r;
    }
    return (int)sent;
}

int read_remaining_length(int sock, int *out_rem)
{
    int multiplier = 1;
    int value = 0;
    uint8_t encoded = 0;
    do {
        if (recv_all(sock, &encoded, 1) != 1) {
            return -1;
        }
        value += (encoded & 127) * multiplier;
        multiplier *= 128;
        if (multiplier > 128 * 128 * 128) {
            return -1;
        }
    } while ((encoded & 128) != 0);
    *out_rem = value;
    return 0;
}

int send_connack(int sock, uint8_t rc)
{
    uint8_t pkt[4] = {0x20, 0x02, 0x00, rc};
    return send_all(sock, pkt, sizeof(pkt));
}

int send_suback(mqtt_session_t *sess, uint16_t pid, uint8_t *qos, size_t count)
{
    size_t slot = session_index(sess);
    uint8_t *buf = ensure_session_tx_buffer(slot);
    if (!buf) {
        ESP_LOGE(TAG, "suback buffer alloc failed");
        return -1;
    }
    size_t idx = 0;
    buf[idx++] = 0x90;
    size_t rem_idx = idx++;
    buf[idx++] = (uint8_t)(pid >> 8);
    buf[idx++] = (uint8_t)(pid & 0xFF);
    for (size_t i = 0; i < count; ++i) {
        buf[idx++] = qos[i];
    }
    buf[rem_idx] = (uint8_t)(idx - 2);
    return send_all(sess->sock, buf, idx);
}

int send_puback(int sock, uint16_t pid)
{
    uint8_t buf[4] = {0x40, 0x02, (uint8_t)(pid >> 8), (uint8_t)(pid & 0xFF)};
    return send_all(sock, buf, sizeof(buf));
}

int send_unsuback(int sock, uint16_t pid)
{
    uint8_t buf[4] = {0xB0, 0x02, (uint8_t)(pid >> 8), (uint8_t)(pid & 0xFF)};
    return send_all(sock, buf, sizeof(buf));
}

int send_pingresp(int sock)
{
    uint8_t buf[2] = {0xD0, 0x00};
    return send_all(sock, buf, sizeof(buf));
}

int send_publish_packet(mqtt_session_t *sess, const char *topic, const char *payload, uint8_t qos, bool retain, uint16_t pid)
{
    if (!sess || !topic || !payload) {
        return -1;
    }
    size_t slot = session_index(sess);
    uint8_t *buf = ensure_session_tx_buffer(slot);
    if (!buf) {
        ESP_LOGE(TAG, "publish buffer alloc failed");
        return -1;
    }
    size_t topic_len = strlen(topic);
    size_t payload_len = strlen(payload);
    if (topic_len > UINT16_MAX) {
        ESP_LOGW(TAG, "publish topic too long (%zu)", topic_len);
        return -1;
    }
    size_t rem_len = 2 + topic_len + payload_len + (qos ? 2 : 0);
    if (rem_len > MQTT_MAX_PACKET) {
        ESP_LOGW(TAG, "publish payload too large (%zu)", rem_len);
        return -1;
    }

    uint8_t header = 0x30 | (qos << 1) | (retain ? 0x01 : 0x00);
    uint8_t rem_enc[4];
    size_t rem_enc_len = encode_remaining_length(rem_enc, rem_len);
    size_t total_len = 1 + rem_enc_len + rem_len;
    if (rem_enc_len == 0 || total_len > MQTT_MAX_PACKET) {
        ESP_LOGW(TAG, "publish packet exceeds buffer (topic=%zu payload=%zu total=%zu)", topic_len, payload_len, total_len);
        return -1;
    }

    size_t idx = 0;
    buf[idx++] = header;
    memcpy(&buf[idx], rem_enc, rem_enc_len);
    idx += rem_enc_len;
    buf[idx++] = (uint8_t)(topic_len >> 8);
    buf[idx++] = (uint8_t)(topic_len & 0xFF);
    memcpy(&buf[idx], topic, topic_len);
    idx += topic_len;
    if (qos) {
        buf[idx++] = (uint8_t)(pid >> 8);
        buf[idx++] = (uint8_t)(pid & 0xFF);
    }
    memcpy(&buf[idx], payload, payload_len);
    idx += payload_len;
    int sent = send_all(sess->sock, buf, idx);
    if (sent < 0) {
        int err = errno;
        request_session_close(sess, "publish send failed", err);
        return -1;
    }
    return sent;
}
