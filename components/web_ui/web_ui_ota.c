#include "web_ui_utils.h"
#include "web_ui_handlers.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "ota_manager.h"

#define OTA_UPLOAD_CHUNK_SIZE      4096
#define OTA_CONTENT_TYPE_MAX       64
#define OTA_FILENAME_HEADER_MAX    128
#define OTA_IMAGE_HEADER_MIN_BYTES \
    (sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t))

static esp_err_t web_http_check(esp_err_t err, const char *context)
{
    if (err != ESP_OK) {
        web_ui_report_httpd_error(err, context);
    }
    return err;
}

#define WEB_HTTP_CHECK(call) web_http_check((call), __func__)

static esp_err_t ota_send_error(httpd_req_t *req, const char *status, const char *message);

static char ascii_lower(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

static bool ascii_streq_nocase_n(const char *a, const char *b, size_t len)
{
    if (!a || !b) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        if (ascii_lower(a[i]) != ascii_lower(b[i])) {
            return false;
        }
    }
    return true;
}

static bool ota_content_type_allowed(const char *value)
{
    static const char *k_allowed[] = {
        "application/octet-stream",
        "application/bin",
        "binary/octet-stream",
    };

    if (!value) {
        return false;
    }

    while (*value == ' ' || *value == '\t') {
        ++value;
    }

    size_t len = 0;
    while (value[len] && value[len] != ';' && value[len] != ' ' && value[len] != '\t') {
        ++len;
    }
    if (len == 0) {
        return false;
    }

    for (size_t i = 0; i < sizeof(k_allowed) / sizeof(k_allowed[0]); ++i) {
        const char *allowed = k_allowed[i];
        size_t allowed_len = strlen(allowed);
        if (len == allowed_len && ascii_streq_nocase_n(value, allowed, len)) {
            return true;
        }
    }
    return false;
}

static bool ota_filename_has_bin_suffix(const char *value)
{
    const char *suffix = ".bin";
    size_t value_len = value ? strlen(value) : 0;
    size_t suffix_len = strlen(suffix);
    if (value_len <= suffix_len) {
        return false;
    }
    return ascii_streq_nocase_n(value + value_len - suffix_len, suffix, suffix_len);
}

static esp_err_t ota_validate_request_headers(httpd_req_t *req)
{
    char content_type[OTA_CONTENT_TYPE_MAX] = {0};
    char firmware_name[OTA_FILENAME_HEADER_MAX] = {0};

    if (httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type)) != ESP_OK ||
        !ota_content_type_allowed(content_type)) {
        return ota_send_error(req, "415 Unsupported Media Type", "expected application/octet-stream firmware upload");
    }

    if (httpd_req_get_hdr_value_str(req, "X-Firmware-Name", firmware_name, sizeof(firmware_name)) != ESP_OK ||
        !ota_filename_has_bin_suffix(firmware_name)) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "firmware name must end with .bin"));
    }

    return ESP_OK;
}

static esp_err_t ota_validate_image_header(httpd_req_t *req, const uint8_t *data, size_t len)
{
    if (!data || len < OTA_IMAGE_HEADER_MIN_BYTES) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "image header too small"));
    }

    const esp_image_header_t *image = (const esp_image_header_t *)data;
    if (image->magic != ESP_IMAGE_HEADER_MAGIC) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid image magic"));
    }
    if (image->segment_count == 0) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid image header"));
    }

    const esp_app_desc_t *app_desc =
        (const esp_app_desc_t *)(data + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t));
    if (!app_desc->project_name[0] || !app_desc->version[0]) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid app descriptor"));
    }

    return ESP_OK;
}

static esp_err_t ota_begin_upload_checked(httpd_req_t *req, size_t len)
{
    esp_err_t err = ota_manager_begin_upload(len);
    if (err == ESP_OK) {
        return ESP_OK;
    }

    ota_manager_status_t ota = {0};
    ota_manager_get_status(&ota);
    const char *msg = ota.last_error[0] ? ota.last_error : "ota begin failed";
    if (err == ESP_ERR_INVALID_SIZE) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, msg));
    }
    if (err == ESP_ERR_INVALID_STATE) {
        return ota_send_error(req, "409 Conflict", msg);
    }
    return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, msg));
}

esp_err_t ota_status_handler(httpd_req_t *req)
{
    ota_manager_status_t ota = {0};
    ota_manager_get_status(&ota);
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem"));
    }
    cJSON_AddStringToObject(root, "version", ota.app_version);
    cJSON_AddStringToObject(root, "running_partition", ota.running_partition);
    cJSON_AddStringToObject(root, "boot_partition", ota.boot_partition);
    cJSON_AddStringToObject(root, "phase", ota.phase);
    cJSON_AddBoolToObject(root, "rollback_supported", ota.rollback_supported);
    cJSON_AddBoolToObject(root, "pending_verify", ota.pending_verify);
    cJSON_AddBoolToObject(root, "in_progress", ota.in_progress);
    cJSON_AddBoolToObject(root, "system_ready", ota.system_ready);
    cJSON_AddBoolToObject(root, "reboot_required", ota.reboot_required);
    cJSON_AddBoolToObject(root, "last_success", ota.last_success);
    cJSON_AddNumberToObject(root, "bytes_written", (double)ota.bytes_written);
    cJSON_AddNumberToObject(root, "total_bytes", (double)ota.total_bytes);
    cJSON_AddStringToObject(root, "last_error", ota.last_error);
    return WEB_HTTP_CHECK(web_ui_send_json(req, root));
}

static esp_err_t ota_send_error(httpd_req_t *req, const char *status, const char *message)
{
    httpd_resp_set_status(req, status ? status : "500 Internal Server Error");
    return WEB_HTTP_CHECK(httpd_resp_send(req, message ? message : "ota error", HTTPD_RESP_USE_STRLEN));
}

esp_err_t ota_upload_handler(httpd_req_t *req)
{
    size_t len = req->content_len;
    if (len == 0) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty image"));
    }
    if (ota_manager_is_busy()) {
        return ota_send_error(req, "409 Conflict", "ota busy");
    }
    esp_err_t err = ota_validate_request_headers(req);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t *buf = heap_caps_malloc(OTA_UPLOAD_CHUNK_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory"));
    }

    size_t received = 0;
    int first_chunk = 0;
    while (first_chunk <= 0) {
        size_t to_read = len;
        if (to_read > OTA_UPLOAD_CHUNK_SIZE) {
            to_read = OTA_UPLOAD_CHUNK_SIZE;
        }
        int r = httpd_req_recv(req, (char *)buf, to_read);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            heap_caps_free(buf);
            return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv failed"));
        }
        first_chunk = r;
    }

    err = ota_validate_image_header(req, buf, (size_t)first_chunk);
    if (err != ESP_OK) {
        heap_caps_free(buf);
        return err;
    }

    err = ota_begin_upload_checked(req, len);
    if (err != ESP_OK) {
        heap_caps_free(buf);
        return err;
    }

    err = ota_manager_write_chunk(buf, (size_t)first_chunk);
    if (err != ESP_OK) {
        ota_manager_status_t ota = {0};
        ota_manager_get_status(&ota);
        const char *msg = ota.last_error[0] ? ota.last_error : "ota write failed";
        heap_caps_free(buf);
        ota_manager_abort_upload();
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, msg));
    }
    received = (size_t)first_chunk;

    while (received < len) {
        size_t to_read = len - received;
        if (to_read > OTA_UPLOAD_CHUNK_SIZE) {
            to_read = OTA_UPLOAD_CHUNK_SIZE;
        }
        int r = httpd_req_recv(req, (char *)buf, to_read);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            heap_caps_free(buf);
            ota_manager_abort_upload();
            return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv failed"));
        }
        err = ota_manager_write_chunk(buf, (size_t)r);
        if (err != ESP_OK) {
            ota_manager_status_t ota = {0};
            ota_manager_get_status(&ota);
            const char *msg = ota.last_error[0] ? ota.last_error : "ota write failed";
            heap_caps_free(buf);
            ota_manager_abort_upload();
            return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, msg));
        }
        received += (size_t)r;
    }
    heap_caps_free(buf);

    err = ota_manager_finish_upload();
    if (err != ESP_OK) {
        ota_manager_status_t ota = {0};
        ota_manager_get_status(&ota);
        const char *msg = ota.last_error[0] ? ota.last_error : "ota finish failed";
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, msg));
    }

    return web_ui_send_ok(req,
                          "application/json",
                          "{\"status\":\"ok\",\"phase\":\"" OTA_MANAGER_PHASE_REBOOT_REQUIRED "\",\"reboot_required\":true}");
}

esp_err_t ota_reboot_handler(httpd_req_t *req)
{
    esp_err_t err = ota_manager_request_reboot();
    if (err != ESP_OK) {
        if (err == ESP_ERR_INVALID_STATE) {
            return ota_send_error(req, "409 Conflict", "reboot not allowed");
        }
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "reboot failed"));
    }
    return web_ui_send_ok(req,
                          "application/json",
                          "{\"status\":\"ok\",\"phase\":\"" OTA_MANAGER_PHASE_REBOOTING "\",\"rebooting\":true}");
}
