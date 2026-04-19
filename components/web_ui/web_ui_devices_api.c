#include "web_ui_handlers.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "device_manager.h"
#include "automation_engine.h"
#include "dm_template_runtime.h"
#include "cJSON.h"

#include "web_ui_utils.h"

static esp_err_t web_http_check(esp_err_t err, const char *context)
{
    if (err != ESP_OK) {
        web_ui_report_httpd_error(err, context);
    }
    return err;
}

#define WEB_HTTP_CHECK(call) web_http_check((call), __func__)
#define WEB_HTTP_CHECK_CTX(call, ctx) web_http_check((call), (ctx))

esp_err_t devices_config_handler(httpd_req_t *req)
{
    char *json = NULL;
    size_t len = 0;
    char query[128];
    char profile[DEVICE_MANAGER_ID_MAX_LEN] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "profile", profile, sizeof(profile));
    }
    esp_err_t err = profile[0] ? device_manager_export_profile_json(profile, &json, &len)
                               : device_manager_export_json(&json, &len);
    if (err != ESP_OK || !json) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "device config unavailable"));
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t res = WEB_HTTP_CHECK(httpd_resp_send(req, json, len));
    heap_caps_free(json);
    return res;
}

esp_err_t devices_apply_handler(httpd_req_t *req)
{
    size_t len = req->content_len;
    if (len == 0 || len > 128 * 1024) {
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
    char query[128];
    char profile[DEVICE_MANAGER_ID_MAX_LEN] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "profile", profile, sizeof(profile));
    }
    esp_err_t err = device_manager_apply_profile_json(profile[0] ? profile : NULL, body, len);
    heap_caps_free(body);
    if (err != ESP_OK) {
        const char *ctx = req ? req->uri : __func__;
        if (!ctx || !ctx[0]) {
            ctx = __func__;
        }
        return WEB_HTTP_CHECK_CTX(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, esp_err_to_name(err)), ctx);
    }
    device_manager_sync_file();
    return web_ui_send_ok(req, "application/json", "{\"status\":\"ok\"}");
}

esp_err_t devices_run_handler(httpd_req_t *req)
{
    char query[192] = {0};
    char devid[DEVICE_MANAGER_ID_MAX_LEN] = {0};
    char scenid[DEVICE_MANAGER_ID_MAX_LEN] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "device", devid, sizeof(devid));
        httpd_query_key_value(query, "scenario", scenid, sizeof(scenid));
    }
    if (!devid[0] || !scenid[0]) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "device/scenario required"));
    }
    esp_err_t err = automation_engine_trigger(devid, scenid);
    if (err != ESP_OK) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, esp_err_to_name(err)));
    }
    return web_ui_send_ok(req, "application/json", "{\"status\":\"queued\"}");
}

esp_err_t devices_profile_create_handler(httpd_req_t *req)
{
    char query[256];
    char id[DEVICE_MANAGER_ID_MAX_LEN] = {0};
    char name[DEVICE_MANAGER_NAME_MAX_LEN] = {0};
    char clone[DEVICE_MANAGER_ID_MAX_LEN] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "id", id, sizeof(id));
        httpd_query_key_value(query, "name", name, sizeof(name));
        httpd_query_key_value(query, "clone", clone, sizeof(clone));
    }
    if (!id[0]) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "id required"));
    }
    esp_err_t err = device_manager_profile_create(id, name[0] ? name : NULL, clone[0] ? clone : NULL);
    if (err != ESP_OK) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, esp_err_to_name(err)));
    }
    return web_ui_send_ok(req, "application/json", "{\"status\":\"ok\"}");
}

esp_err_t devices_profile_delete_handler(httpd_req_t *req)
{
    char query[128];
    char id[DEVICE_MANAGER_ID_MAX_LEN] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "id", id, sizeof(id));
    }
    if (!id[0]) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "id required"));
    }
    esp_err_t err = device_manager_profile_delete(id);
    if (err != ESP_OK) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, esp_err_to_name(err)));
    }
    return web_ui_send_ok(req, "application/json", "{\"status\":\"ok\"}");
}

esp_err_t devices_profile_rename_handler(httpd_req_t *req)
{
    char query[256];
    char id[DEVICE_MANAGER_ID_MAX_LEN] = {0};
    char name[DEVICE_MANAGER_NAME_MAX_LEN] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "id", id, sizeof(id));
        httpd_query_key_value(query, "name", name, sizeof(name));
    }
    if (!id[0] || !name[0]) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "id/name required"));
    }
    esp_err_t err = device_manager_profile_rename(id, name);
    if (err != ESP_OK) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, esp_err_to_name(err)));
    }
    return web_ui_send_ok(req, "application/json", "{\"status\":\"ok\"}");
}

esp_err_t devices_profile_activate_handler(httpd_req_t *req)
{
    char query[128];
    char id[DEVICE_MANAGER_ID_MAX_LEN] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "id", id, sizeof(id));
    }
    if (!id[0]) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "id required"));
    }
    esp_err_t err = device_manager_profile_activate(id);
    if (err != ESP_OK) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, esp_err_to_name(err)));
    }
    return web_ui_send_ok(req, "application/json", "{\"status\":\"ok\"}");
}

esp_err_t devices_profile_download_handler(httpd_req_t *req)
{
    char query[128];
    char id[DEVICE_MANAGER_ID_MAX_LEN] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "profile", id, sizeof(id));
    }
    if (!id[0]) {
        strncpy(id, device_manager_default_profile_id(), sizeof(id) - 1);
        id[sizeof(id) - 1] = 0;
    }
    uint8_t *data = NULL;
    size_t size = 0;
    esp_err_t err = device_manager_export_profile_raw(id, &data, &size);
    if (err != ESP_OK || !data || size == 0) {
        if (data) {
            heap_caps_free(data);
        }
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "profile unavailable"));
    }
    char safe_id[DEVICE_MANAGER_ID_MAX_LEN] = {0};
    web_ui_sanitize_filename_token(safe_id, sizeof(safe_id), id, "profile");
    char disposition[128];
    snprintf(disposition, sizeof(disposition), "attachment; filename=\"profile_%s.bin\"", safe_id);
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Content-Disposition", disposition);
    esp_err_t res = WEB_HTTP_CHECK(httpd_resp_send(req, (const char *)data, size));
    heap_caps_free(data);
    return res;
}

esp_err_t devices_variables_handler(httpd_req_t *req)
{
    return web_ui_send_ok(req, "application/json", "[]");
}

esp_err_t devices_templates_handler(httpd_req_t *req)
{
    size_t count = 0;
    const dm_template_descriptor_t *templates = dm_template_registry_get_all(&count);
    cJSON *root = cJSON_CreateArray();
    if (!root) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem"));
    }
    for (size_t i = 0; i < count; ++i) {
        const dm_template_descriptor_t *tpl = &templates[i];
        cJSON *obj = cJSON_CreateObject();
        if (!obj) {
            cJSON_Delete(root);
            return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem"));
        }
        cJSON_AddStringToObject(obj, "id", tpl->id);
        cJSON_AddStringToObject(obj, "label", tpl->label);
        cJSON_AddStringToObject(obj, "description", tpl->description);
        cJSON_AddStringToObject(obj, "type", dm_template_type_to_string(tpl->type));
        cJSON_AddItemToArray(root, obj);
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem"));
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t res = WEB_HTTP_CHECK(httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN));
    free(json);
    return res;
}
