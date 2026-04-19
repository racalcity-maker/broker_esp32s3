#include "web_ui_auth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "config_store.h"
#include "driver/gpio.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "web_ui_page.h"
#include "web_ui_utils.h"

#ifndef CONFIG_BROKER_WEB_AUTH_RESET_GPIO
#define CONFIG_BROKER_WEB_AUTH_RESET_GPIO -1
#endif

#define WEB_SESSION_MAX        6
#define WEB_SESSION_TOKEN_LEN  64
#define WEB_SESSION_TTL_US     (12LL * 60 * 60 * 1000000)
#define WEB_AUTH_RESET_HOLD_US (10LL * 1000000)

typedef struct {
    bool in_use;
    char token[WEB_SESSION_TOKEN_LEN];
    int64_t expires_at;
    web_user_role_t role;
    char username[CONFIG_STORE_USERNAME_MAX];
} web_session_entry_t;

static const char *TAG = "web_ui_auth";
static SemaphoreHandle_t s_session_mutex = NULL;
static web_session_entry_t s_sessions[WEB_SESSION_MAX];
#if CONFIG_BROKER_WEB_AUTH_RESET_GPIO >= 0
static TaskHandle_t s_reset_task = NULL;
#endif

static esp_err_t web_http_check(esp_err_t err, const char *context)
{
    if (err != ESP_OK) {
        web_ui_report_httpd_error(err, context);
    }
    return err;
}

#define WEB_HTTP_CHECK(call) web_http_check((call), __func__)

static esp_err_t web_same_origin_reject(httpd_req_t *req)
{
    httpd_resp_set_status(req, "403 Forbidden");
    httpd_resp_set_type(req, "application/json");
    return WEB_HTTP_CHECK(httpd_resp_send(req,
                                          "{\"error\":\"csrf\",\"message\":\"same-origin check failed\"}",
                                          HTTPD_RESP_USE_STRLEN));
}

static void web_session_generate_token(char *out, size_t len)
{
    static const char *hex = "0123456789abcdef";
    if (!out || len < 33) {
        return;
    }
    for (size_t i = 0; i < (len - 1) / 2; ++i) {
        uint32_t r = esp_random();
        out[i * 2] = hex[(r >> 4) & 0x0F];
        out[i * 2 + 1] = hex[r & 0x0F];
    }
    out[len - 1] = 0;
}

static void web_session_store(const char *token, web_user_role_t role, const char *username)
{
    if (!token || !token[0] || !s_session_mutex) {
        return;
    }
    int64_t now = esp_timer_get_time();
    xSemaphoreTake(s_session_mutex, portMAX_DELAY);
    web_session_entry_t *slot = NULL;
    for (size_t i = 0; i < WEB_SESSION_MAX; ++i) {
        web_session_entry_t *entry = &s_sessions[i];
        if (entry->in_use && strcmp(entry->token, token) == 0) {
            slot = entry;
            break;
        }
        if (!entry->in_use || entry->expires_at < now) {
            slot = entry;
        }
    }
    if (slot) {
        memset(slot->token, 0, sizeof(slot->token));
        strncpy(slot->token, token, sizeof(slot->token) - 1);
        slot->expires_at = now + WEB_SESSION_TTL_US;
        slot->role = role;
        memset(slot->username, 0, sizeof(slot->username));
        if (username && username[0]) {
            strncpy(slot->username, username, sizeof(slot->username) - 1);
        }
        slot->in_use = true;
    }
    xSemaphoreGive(s_session_mutex);
}

static bool web_session_validate(const char *token, web_user_role_t *out_role, char *out_username, size_t username_len)
{
    if (!token || !token[0] || !s_session_mutex) {
        return false;
    }
    bool valid = false;
    int64_t now = esp_timer_get_time();
    xSemaphoreTake(s_session_mutex, portMAX_DELAY);
    for (size_t i = 0; i < WEB_SESSION_MAX; ++i) {
        web_session_entry_t *entry = &s_sessions[i];
        if (!entry->in_use) {
            continue;
        }
        if (entry->expires_at < now) {
            entry->in_use = false;
            continue;
        }
        if (strcmp(entry->token, token) == 0) {
            entry->expires_at = now + WEB_SESSION_TTL_US;
            if (out_role) {
                *out_role = entry->role;
            }
            if (out_username && username_len > 0) {
                strncpy(out_username, entry->username, username_len - 1);
                out_username[username_len - 1] = '\0';
            }
            valid = true;
            break;
        }
    }
    xSemaphoreGive(s_session_mutex);
    return valid;
}

static void web_session_remove(const char *token)
{
    if (!token || !token[0] || !s_session_mutex) {
        return;
    }
    xSemaphoreTake(s_session_mutex, portMAX_DELAY);
    for (size_t i = 0; i < WEB_SESSION_MAX; ++i) {
        web_session_entry_t *entry = &s_sessions[i];
        if (entry->in_use && strcmp(entry->token, token) == 0) {
            entry->in_use = false;
            break;
        }
    }
    xSemaphoreGive(s_session_mutex);
}

static bool read_cookie_value(httpd_req_t *req, const char *name, char *out, size_t out_len)
{
    if (!req || !name || !out || out_len == 0) {
        return false;
    }
    size_t hdr_len = httpd_req_get_hdr_value_len(req, "Cookie");
    if (hdr_len <= 0 || hdr_len >= 512) {
        return false;
    }
    char *buf = malloc(hdr_len + 1);
    if (!buf) {
        return false;
    }
    if (httpd_req_get_hdr_value_str(req, "Cookie", buf, hdr_len + 1) != ESP_OK) {
        free(buf);
        return false;
    }
    bool found = false;
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "%s=", name);
    char *start = strstr(buf, pattern);
    if (start) {
        start += strlen(pattern);
        char *end = strchr(start, ';');
        size_t copy_len = end ? (size_t)(end - start) : strlen(start);
        if (copy_len >= out_len) {
            copy_len = out_len - 1;
        }
        memcpy(out, start, copy_len);
        out[copy_len] = 0;
        found = true;
    }
    free(buf);
    return found;
}

static bool web_session_get_info(httpd_req_t *req, web_user_role_t *role_out, char *username_out, size_t username_len)
{
    char token[WEB_SESSION_TOKEN_LEN] = {0};
    if (!read_cookie_value(req, "broker_sid", token, sizeof(token))) {
        return false;
    }
    return web_session_validate(token, role_out, username_out, username_len);
}

static const char *web_role_to_string(web_user_role_t role)
{
    return (role == WEB_USER_ROLE_USER) ? "user" : "admin";
}

static bool web_role_allows(web_user_role_t have, web_user_role_t required)
{
    if (required == WEB_USER_ROLE_USER) {
        return true;
    }
    return have == WEB_USER_ROLE_ADMIN;
}

static bool web_ui_require_session(httpd_req_t *req, bool redirect_on_fail, web_user_role_t *role_out)
{
    if (!req) {
        return false;
    }
    char token[WEB_SESSION_TOKEN_LEN] = {0};
    web_user_role_t role = WEB_USER_ROLE_ADMIN;
    if (read_cookie_value(req, "broker_sid", token, sizeof(token)) &&
        web_session_validate(token, &role, NULL, 0)) {
        if (role_out) {
            *role_out = role;
        }
        return true;
    }
    if (redirect_on_fail) {
        char location[128];
        const char *prefix = "/login?next=";
        const char *next_uri = req->uri[0] ? req->uri : "/";
        size_t prefix_len = strlen(prefix);
        size_t copy_len = sizeof(location) > prefix_len ? sizeof(location) - prefix_len - 1 : 0;
        memcpy(location, prefix, prefix_len);
        location[prefix_len] = '\0';
        if (copy_len > 0) {
            strncat(location, next_uri, copy_len);
        }
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", location);
        WEB_HTTP_CHECK(httpd_resp_send(req, NULL, 0));
    } else {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_type(req, "application/json");
        WEB_HTTP_CHECK(httpd_resp_send(req, "{\"error\":\"unauthorized\"}", HTTPD_RESP_USE_STRLEN));
    }
    return false;
}

static char *read_request_body(httpd_req_t *req, size_t max_len)
{
    if (!req) {
        return NULL;
    }
    size_t len = req->content_len;
    if (len == 0 || len > max_len) {
        return NULL;
    }
    char *body = malloc(len + 1);
    if (!body) {
        return NULL;
    }
    size_t received = 0;
    while (received < len) {
        int r = httpd_req_recv(req, body + received, len - received);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            free(body);
            return NULL;
        }
        received += (size_t)r;
    }
    body[len] = 0;
    return body;
}

static esp_err_t auth_login_reject(httpd_req_t *req, const char *message)
{
    const char *reason = (message && message[0]) ? message : "Login failed.";
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem"));
    }
    cJSON_AddStringToObject(root, "error", "auth_failed");
    cJSON_AddStringToObject(root, "message", reason);
    httpd_resp_set_status(req, "401 Unauthorized");
    return WEB_HTTP_CHECK(web_ui_send_json(req, root));
}

void web_sessions_init(void)
{
    if (!s_session_mutex) {
        s_session_mutex = xSemaphoreCreateMutex();
    }
    if (s_session_mutex) {
        xSemaphoreTake(s_session_mutex, portMAX_DELAY);
        memset(s_sessions, 0, sizeof(s_sessions));
        xSemaphoreGive(s_session_mutex);
    }
}

void web_sessions_clear(void)
{
    if (!s_session_mutex) {
        return;
    }
    xSemaphoreTake(s_session_mutex, portMAX_DELAY);
    memset(s_sessions, 0, sizeof(s_sessions));
    xSemaphoreGive(s_session_mutex);
}

esp_err_t auth_gate_handler(httpd_req_t *req)
{
    const web_route_t *route = (const web_route_t *)req->user_ctx;
    if (!route || !route->fn) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "route missing"));
    }
    web_user_role_t role = WEB_USER_ROLE_ADMIN;
    if (!web_ui_require_session(req, route->redirect_on_fail, &role)) {
        return ESP_OK;
    }
    if (!web_role_allows(role, route->min_role)) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "application/json");
        WEB_HTTP_CHECK(httpd_resp_send(req, "{\"error\":\"forbidden\"}", HTTPD_RESP_USE_STRLEN));
        return ESP_OK;
    }
    if (req->method != HTTP_GET && !web_ui_is_same_origin_request(req)) {
        return web_same_origin_reject(req);
    }
    return route->fn(req);
}

esp_err_t login_page_handler(httpd_req_t *req)
{
    char token[WEB_SESSION_TOKEN_LEN] = {0};
    if (read_cookie_value(req, "broker_sid", token, sizeof(token)) &&
        web_session_validate(token, NULL, NULL, 0)) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/");
        return WEB_HTTP_CHECK(httpd_resp_send(req, NULL, 0));
    }
    httpd_resp_set_type(req, "text/html");
    return WEB_HTTP_CHECK(httpd_resp_send(req, web_ui_get_login_html(), HTTPD_RESP_USE_STRLEN));
}

esp_err_t auth_login_handler(httpd_req_t *req)
{
    if (!web_ui_is_same_origin_request(req)) {
        return web_same_origin_reject(req);
    }
    char *body = read_request_body(req, 1024);
    if (!body) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body required"));
    }
    cJSON *json = cJSON_Parse(body);
    free(body);
    if (!json) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json"));
    }
    const cJSON *username_item = cJSON_GetObjectItem(json, "username");
    const cJSON *password_item = cJSON_GetObjectItem(json, "password");
    if (!cJSON_IsString(username_item) || !cJSON_IsString(password_item)) {
        cJSON_Delete(json);
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing fields"));
    }
    const char *username = username_item->valuestring;
    const char *password = password_item->valuestring;

    uint8_t hash[CONFIG_STORE_AUTH_HASH_LEN] = {0};
    config_store_hash_password(password, hash);
    const app_config_t *cfg = config_store_get();
    if (!cfg) {
        cJSON_Delete(json);
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config missing"));
    }

    bool admin_username_match = strcasecmp(cfg->web.username, username) == 0;
    bool admin_password_match = admin_username_match &&
        memcmp(cfg->web.password_hash, hash, CONFIG_STORE_AUTH_HASH_LEN) == 0;
    bool user_username_match = cfg->web_user_enabled &&
        strcasecmp(cfg->web_user.username, username) == 0;
    bool user_password_match = user_username_match &&
        memcmp(cfg->web_user.password_hash, hash, CONFIG_STORE_AUTH_HASH_LEN) == 0;

    const app_web_auth_t *target = NULL;
    web_user_role_t session_role = WEB_USER_ROLE_ADMIN;

    if (admin_password_match) {
        target = &cfg->web;
        session_role = WEB_USER_ROLE_ADMIN;
    } else if (user_password_match) {
        target = &cfg->web_user;
        session_role = WEB_USER_ROLE_USER;
    } else if (admin_username_match || user_username_match) {
        ESP_LOGW(TAG, "login rejected for username=%s: invalid password", username);
        cJSON_Delete(json);
        return auth_login_reject(req, "Incorrect password.");
    } else {
        ESP_LOGW(TAG, "login rejected for username=%s: unknown username", username);
        cJSON_Delete(json);
        return auth_login_reject(req, "Unknown username.");
    }

    if (!target) {
        ESP_LOGW(TAG, "login rejected for username=%s: account resolution failed", username);
        cJSON_Delete(json);
        return auth_login_reject(req, "Login failed.");
    }
    if (session_role == WEB_USER_ROLE_USER && !cfg->web_user_enabled) {
        ESP_LOGW(TAG, "login rejected for username=%s: operator account disabled", username);
        cJSON_Delete(json);
        return auth_login_reject(req, "Operator account is disabled.");
    }

    ESP_LOGI(TAG, "login ok: username=%s role=%s", username, web_role_to_string(session_role));
    cJSON_Delete(json);
    char token[WEB_SESSION_TOKEN_LEN] = {0};
    web_session_generate_token(token, sizeof(token));
    web_session_store(token, session_role, target->username);
    char cookie[WEB_SESSION_TOKEN_LEN + 80];
    snprintf(cookie, sizeof(cookie), "broker_sid=%s; Path=/; HttpOnly; SameSite=Strict", token);
    httpd_resp_set_hdr(req, "Set-Cookie", cookie);
    httpd_resp_set_type(req, "application/json");
    return WEB_HTTP_CHECK(httpd_resp_send(req,
                                          session_role == WEB_USER_ROLE_ADMIN
                                              ? "{\"status\":\"ok\",\"role\":\"admin\"}"
                                              : "{\"status\":\"ok\",\"role\":\"user\"}",
                                          HTTPD_RESP_USE_STRLEN));
}

esp_err_t session_info_handler(httpd_req_t *req)
{
    web_user_role_t role = WEB_USER_ROLE_ADMIN;
    char username[CONFIG_STORE_USERNAME_MAX] = {0};
    if (!web_session_get_info(req, &role, username, sizeof(username))) {
        httpd_resp_set_status(req, "401 Unauthorized");
        WEB_HTTP_CHECK(httpd_resp_send(req, "{}", HTTPD_RESP_USE_STRLEN));
        return ESP_OK;
    }
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem"));
    }
    cJSON_AddStringToObject(root, "role", web_role_to_string(role));
    cJSON_AddStringToObject(root, "username", username);
    return WEB_HTTP_CHECK(web_ui_send_json(req, root));
}

esp_err_t auth_logout_handler(httpd_req_t *req)
{
    char token[WEB_SESSION_TOKEN_LEN] = {0};
    if (read_cookie_value(req, "broker_sid", token, sizeof(token))) {
        web_session_remove(token);
    }
    httpd_resp_set_hdr(req, "Set-Cookie", "broker_sid=deleted; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
    httpd_resp_set_type(req, "application/json");
    return WEB_HTTP_CHECK(httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN));
}

esp_err_t auth_password_handler(httpd_req_t *req)
{
    char *body = read_request_body(req, 1024);
    if (!body) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body required"));
    }
    cJSON *json = cJSON_Parse(body);
    free(body);
    if (!json) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json"));
    }
    const cJSON *new_user_item = cJSON_GetObjectItem(json, "username");
    const cJSON *current_item = cJSON_GetObjectItem(json, "current_password");
    const cJSON *next_item = cJSON_GetObjectItem(json, "new_password");
    const cJSON *role_item = cJSON_GetObjectItem(json, "role");
    const cJSON *enabled_item = cJSON_GetObjectItem(json, "enabled");
    const char *new_user = cJSON_IsString(new_user_item) ? new_user_item->valuestring : NULL;
    const char *current = cJSON_IsString(current_item) ? current_item->valuestring : NULL;
    const char *next = cJSON_IsString(next_item) ? next_item->valuestring : NULL;
    const char *role_str = cJSON_IsString(role_item) ? role_item->valuestring : NULL;
    bool target_user = role_str && strcasecmp(role_str, "user") == 0;
    bool enable_user = !target_user ? false :
        (!cJSON_IsBool(enabled_item) ? true : cJSON_IsTrue(enabled_item));
    if (!current || !current[0]) {
        cJSON_Delete(json);
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing current password"));
    }
    if (!next || !next[0]) {
        if (!target_user || enable_user) {
            cJSON_Delete(json);
            return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing new password"));
        }
    }
    const app_config_t *cfg = config_store_get();
    if (!cfg) {
        cJSON_Delete(json);
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config missing"));
    }
    uint8_t hash[CONFIG_STORE_AUTH_HASH_LEN] = {0};
    config_store_hash_password(current, hash);
    if (memcmp(cfg->web.password_hash, hash, CONFIG_STORE_AUTH_HASH_LEN) != 0) {
        cJSON_Delete(json);
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_type(req, "application/json");
        WEB_HTTP_CHECK(httpd_resp_send(req, "{\"error\":\"invalid\"}", HTTPD_RESP_USE_STRLEN));
        return ESP_OK;
    }
    esp_err_t err = ESP_OK;
    if (target_user) {
        if (!enable_user) {
            err = config_store_set_web_user(NULL, NULL, false);
        } else {
            if (!new_user || !new_user[0]) {
                cJSON_Delete(json);
                return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "username required"));
            }
            uint8_t user_hash[CONFIG_STORE_AUTH_HASH_LEN];
            config_store_hash_password(next, user_hash);
            err = config_store_set_web_user(new_user, user_hash, true);
        }
        if (err != ESP_OK) {
            cJSON_Delete(json);
            ESP_LOGE(TAG, "failed to update user auth: %s", esp_err_to_name(err));
            return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed"));
        }
        cJSON_Delete(json);
        web_sessions_clear();
        httpd_resp_set_hdr(req, "Set-Cookie", "broker_sid=deleted; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
        httpd_resp_set_type(req, "application/json");
        return WEB_HTTP_CHECK(httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN));
    }
    const char *username = (new_user && new_user[0]) ? new_user : cfg->web.username;
    uint8_t new_hash[CONFIG_STORE_AUTH_HASH_LEN];
    config_store_hash_password(next, new_hash);
    err = config_store_set_web_auth(username, new_hash);
    if (err != ESP_OK) {
        cJSON_Delete(json);
        ESP_LOGE(TAG, "failed to update web auth: %s", esp_err_to_name(err));
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed"));
    }
    cJSON_Delete(json);
    web_sessions_clear();
    httpd_resp_set_hdr(req, "Set-Cookie", "broker_sid=deleted; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
    httpd_resp_set_type(req, "application/json");
    return WEB_HTTP_CHECK(httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN));
}

#if CONFIG_BROKER_WEB_AUTH_RESET_GPIO >= 0
static void web_auth_reset_task(void *param)
{
    const int pin = CONFIG_BROKER_WEB_AUTH_RESET_GPIO;
    int64_t low_since = 0;
    const TickType_t delay_ticks = pdMS_TO_TICKS(100);
    ESP_LOGI(TAG, "web auth reset monitor on GPIO%d", pin);
    while (1) {
        int level = gpio_get_level(pin);
        int64_t now = esp_timer_get_time();
        if (level == 0) {
            if (low_since == 0) {
                low_since = now;
            } else if (now - low_since >= WEB_AUTH_RESET_HOLD_US) {
                ESP_LOGW(TAG, "web auth reset pin triggered, restoring defaults");
                config_store_reset_web_auth_defaults();
                web_sessions_clear();
                low_since = 0;
            }
        } else {
            low_since = 0;
        }
        vTaskDelay(delay_ticks);
    }
}

void web_auth_start_reset_monitor(void)
{
    if (s_reset_task) {
        return;
    }
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << CONFIG_BROKER_WEB_AUTH_RESET_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = true,
        .pull_down_en = false,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    xTaskCreate(web_auth_reset_task, "web_auth_reset", 2048, NULL, 5, &s_reset_task);
}
#else
void web_auth_start_reset_monitor(void)
{
    // feature disabled
}
#endif
