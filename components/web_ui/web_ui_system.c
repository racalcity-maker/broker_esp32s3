#include "web_ui.h"
#include "web_ui_handlers.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "event_bus.h"
#include "config_store.h"
#include "network.h"
#include "cJSON.h"

#include "web_ui_page.h"
#include "web_ui_utils.h"

static const char *TAG = "web_ui_system";
static SemaphoreHandle_t s_scan_mutex = NULL;

static esp_err_t web_http_check(esp_err_t err, const char *context)
{
    if (err != ESP_OK) {
        web_ui_report_httpd_error(err, context);
    }
    return err;
}

#define WEB_HTTP_CHECK(call) web_http_check((call), __func__)

esp_err_t web_ui_system_init(void)
{
    if (s_scan_mutex) {
        return ESP_OK;
    }
    s_scan_mutex = xSemaphoreCreateMutex();
    if (!s_scan_mutex) {
        ESP_LOGE(TAG, "failed to create wifi scan mutex");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t ping_handler(httpd_req_t *req)
{
    return web_ui_send_ok(req, "text/plain", "pong");
}

esp_err_t wifi_scan_handler(httpd_req_t *req)
{
    if (!s_scan_mutex) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "scan unavailable"));
    }
    if (!xSemaphoreTake(s_scan_mutex, pdMS_TO_TICKS(5000))) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "scan busy"));
    }
    wifi_scan_config_t scan_conf = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
    };
    esp_err_t err = esp_wifi_scan_start(&scan_conf, true);
    if (err != ESP_OK) {
        xSemaphoreGive(s_scan_mutex);
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "scan failed"));
    }

    uint16_t ap_num = 0;
    esp_wifi_scan_get_ap_num(&ap_num);
    wifi_ap_record_t *aps = calloc(ap_num, sizeof(wifi_ap_record_t));
    if (!aps) {
        xSemaphoreGive(s_scan_mutex);
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem"));
    }
    esp_wifi_scan_get_ap_records(&ap_num, aps);

    cJSON *root = cJSON_CreateArray();
    if (!root) {
        free(aps);
        xSemaphoreGive(s_scan_mutex);
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem"));
    }
    for (int i = 0; i < ap_num; ++i) {
        cJSON *item = cJSON_CreateString((const char *)aps[i].ssid);
        if (!item) {
            cJSON_Delete(root);
            free(aps);
            xSemaphoreGive(s_scan_mutex);
            return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem"));
        }
        cJSON_AddItemToArray(root, item);
    }
    free(aps);
    xSemaphoreGive(s_scan_mutex);
    return WEB_HTTP_CHECK(web_ui_send_json(req, root));
}

esp_err_t wifi_config_handler(httpd_req_t *req)
{
    char q[160];
    char ssid[32] = {0};
    char pass[64] = {0};
    char host[32] = {0};
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        httpd_query_key_value(q, "ssid", ssid, sizeof(ssid));
        httpd_query_key_value(q, "password", pass, sizeof(pass));
        httpd_query_key_value(q, "host", host, sizeof(host));
    }
    app_config_t cfg = *config_store_get();
    if (ssid[0]) {
        strncpy(cfg.wifi.ssid, ssid, sizeof(cfg.wifi.ssid) - 1);
    }
    if (pass[0]) {
        strncpy(cfg.wifi.password, pass, sizeof(cfg.wifi.password) - 1);
    }
    if (host[0]) {
        strncpy(cfg.wifi.hostname, host, sizeof(cfg.wifi.hostname) - 1);
    }
    esp_err_t err = config_store_set(&cfg);
    if (err != ESP_OK) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "failed to save wifi"));
    }
    err = network_apply_wifi_config();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "network_apply_wifi_config failed: %s", esp_err_to_name(err));
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "failed to apply wifi"));
    }
    return web_ui_send_ok(req, "text/plain", "wifi saved");
}

esp_err_t mqtt_config_handler(httpd_req_t *req)
{
    char q[160];
    char id[16] = {0};
    char port[8] = {0};
    char keep[8] = {0};
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        httpd_query_key_value(q, "id", id, sizeof(id));
        httpd_query_key_value(q, "port", port, sizeof(port));
        httpd_query_key_value(q, "keepalive", keep, sizeof(keep));
    }
    app_config_t cfg = *config_store_get();
    if (id[0]) {
        strncpy(cfg.mqtt.broker_id, id, sizeof(cfg.mqtt.broker_id) - 1);
    }
    if (port[0]) {
        cfg.mqtt.port = atoi(port);
    }
    if (keep[0]) {
        cfg.mqtt.keepalive_seconds = atoi(keep);
    }
    esp_err_t err = config_store_set(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "config_store_set(mqtt) failed: %s", esp_err_to_name(err));
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "failed to save mqtt"));
    }
    return web_ui_send_ok(req, "text/plain", "mqtt saved");
}

esp_err_t logging_config_handler(httpd_req_t *req)
{
    char q[64];
    char verbose[16] = {0};
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK ||
        httpd_query_key_value(q, "verbose", verbose, sizeof(verbose)) != ESP_OK) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing verbose"));
    }
    bool enable = false;
    if (strcasecmp(verbose, "1") == 0 || strcasecmp(verbose, "true") == 0 ||
        strcasecmp(verbose, "on") == 0 || strcasecmp(verbose, "yes") == 0) {
        enable = true;
    }
    app_config_t cfg = *config_store_get();
    cfg.verbose_logging = enable;
    esp_err_t err = config_store_set(&cfg);
    if (err != ESP_OK) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "failed to save logging"));
    }
    return web_ui_send_ok(req, "text/plain", "logging updated");
}

esp_err_t mqtt_users_handler(httpd_req_t *req)
{
    size_t len = req->content_len;
    if (len == 0 || len > 4096) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body"));
    }
    char *body = heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!body) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory"));
    }
    size_t received = 0;
    while (received < len) {
        int r = httpd_req_recv(req, body + received, len - received);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            heap_caps_free(body);
            return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv failed"));
        }
        received += (size_t)r;
    }
    body[len] = 0;
    cJSON *root = cJSON_Parse(body);
    if (!root || !cJSON_IsArray(root)) {
        if (root) {
            cJSON_Delete(root);
        }
        heap_caps_free(body);
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "array required"));
    }
    app_config_t cfg = *config_store_get();
    cfg.mqtt.user_count = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, root) {
        if (cfg.mqtt.user_count >= CONFIG_STORE_MAX_MQTT_USERS) {
            cJSON_Delete(root);
            heap_caps_free(body);
            return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "too many users"));
        }
        if (!cJSON_IsObject(item)) {
            cJSON_Delete(root);
            heap_caps_free(body);
            return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid user entry"));
        }
        const cJSON *client = cJSON_GetObjectItem(item, "client_id");
        const cJSON *username = cJSON_GetObjectItem(item, "username");
        const cJSON *password = cJSON_GetObjectItem(item, "password");
        if (!cJSON_IsString(client) || !cJSON_IsString(username) || !cJSON_IsString(password)) {
            cJSON_Delete(root);
            heap_caps_free(body);
            return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing fields"));
        }
        app_mqtt_user_t *dst = &cfg.mqtt.users[cfg.mqtt.user_count++];
        strncpy(dst->client_id, client->valuestring, sizeof(dst->client_id) - 1);
        dst->client_id[sizeof(dst->client_id) - 1] = 0;
        strncpy(dst->username, username->valuestring, sizeof(dst->username) - 1);
        dst->username[sizeof(dst->username) - 1] = 0;
        strncpy(dst->password, password->valuestring, sizeof(dst->password) - 1);
        dst->password[sizeof(dst->password) - 1] = 0;
    }
    cJSON_Delete(root);
    heap_caps_free(body);
    esp_err_t err = config_store_set(&cfg);
    if (err != ESP_OK) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed"));
    }
    return web_ui_send_ok(req, "text/plain", "mqtt users saved");
}

esp_err_t publish_handler(httpd_req_t *req)
{
    char query[160];
    char topic_enc[96] = {0};
    char payload_enc[96] = {0};
    char topic[96] = {0};
    char payload[96] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "topic", topic_enc, sizeof(topic_enc));
        httpd_query_key_value(query, "payload", payload_enc, sizeof(payload_enc));
    }
    web_ui_url_decode(topic, sizeof(topic), topic_enc);
    web_ui_url_decode(payload, sizeof(payload), payload_enc);
    if (topic[0] == '\0') {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "topic required"));
    }
#if WEB_UI_DEBUG
    ESP_LOGI(TAG, "publish request topic='%s' payload='%s'",
             topic, payload[0] ? payload : "<none>");
#endif
    event_bus_message_t msg = {.type = EVENT_WEB_COMMAND};
    strncpy(msg.topic, topic, sizeof(msg.topic) - 1);
    strncpy(msg.payload, payload, sizeof(msg.payload) - 1);
    esp_err_t err = event_bus_post(&msg, pdMS_TO_TICKS(50));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "publish event dispatch failed: %s", esp_err_to_name(err));
        httpd_resp_set_status(req, "503 Service Unavailable");
        return WEB_HTTP_CHECK(httpd_resp_send(req, "event bus unavailable", HTTPD_RESP_USE_STRLEN));
    }
    return web_ui_send_ok(req, "text/plain", "sent");
}

esp_err_t ap_stop_handler(httpd_req_t *req)
{
    network_stop_ap();
    return web_ui_send_ok(req, "text/plain", "ap stopped");
}

esp_err_t root_get_handler(httpd_req_t *req)
{
    return web_ui_send_ok(req, "text/html", web_ui_get_index_html());
}
