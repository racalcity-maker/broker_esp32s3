#include "web_ui.h"

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_check.h"
#include "esp_heap_caps.h"

#include "web_ui_utils.h"
#include "web_ui_handlers.h"
#include "web_ui_auth.h"
#include "web_ui_devices.h"

static const char *TAG = "web_ui";
static httpd_handle_t s_server = NULL;
static bool s_httpd_restart_pending = false;
static char s_httpd_restart_reason[64];
static void web_ui_schedule_httpd_restart(const char *reason);
static void web_ui_httpd_restart_task(void *param);
static esp_err_t start_httpd(void);
void web_ui_report_httpd_error(esp_err_t err, const char *context);

static esp_err_t web_http_check(esp_err_t err, const char *context)
{
    if (err != ESP_OK) {
        web_ui_report_httpd_error(err, context);
    }
    return err;
}

#define WEB_HTTP_CHECK(call) web_http_check((call), __func__)
#define WEB_HTTP_CHECK_CTX(call, ctx) web_http_check((call), (ctx))

static esp_err_t register_guarded_route(const char *uri, httpd_method_t method, const web_route_t *route);
static esp_err_t register_public_route(const char *uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *));

static esp_err_t register_guarded_route(const char *uri, httpd_method_t method, const web_route_t *route)
{
    if (!s_server || !route || !route->fn) {
        return ESP_ERR_INVALID_STATE;
    }
    httpd_uri_t desc = {
        .uri = uri,
        .method = method,
        .handler = auth_gate_handler,
        .user_ctx = (void *)route,
    };
    return WEB_HTTP_CHECK_CTX(httpd_register_uri_handler(s_server, &desc), uri);
}

static esp_err_t register_public_route(const char *uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *))
{
    if (!s_server || !handler) {
        return ESP_ERR_INVALID_STATE;
    }
    httpd_uri_t desc = {
        .uri = uri,
        .method = method,
        .handler = handler,
    };
    return WEB_HTTP_CHECK_CTX(httpd_register_uri_handler(s_server, &desc), uri);
}


static esp_err_t start_httpd(void)
{
    if (s_server) {
        return ESP_OK;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 40; // many handlers registered
    config.max_open_sockets = 20;  // target max clients (clamped by LWIP budget below)
    // Keep max_open_sockets within LWIP_MAX_SOCKETS budget (httpd uses ~3 internally).
#ifdef CONFIG_LWIP_MAX_SOCKETS
    int max_httpd_sockets = CONFIG_LWIP_MAX_SOCKETS - 3;
    if (max_httpd_sockets < 1) {
        max_httpd_sockets = 1;
    }
    if (config.max_open_sockets > max_httpd_sockets) {
        config.max_open_sockets = max_httpd_sockets;
    }
#endif
    config.backlog_conn = config.max_open_sockets;
    config.lru_purge_enable = true; // drop oldest sockets instead of refusing new ones
    config.keep_alive_enable = false; // close connections immediately
    config.stack_size = 16384; // avoid stack overflow with larger handlers/pages
    #if WEB_UI_DEBUG
    ESP_LOGI(TAG, "starting httpd: uri=%d sockets=%d backlog=%d keepalive=%d free_int=%u",
             config.max_uri_handlers, config.max_open_sockets, config.backlog_conn, config.keep_alive_enable,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
#endif
    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd start failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_RETURN_ON_ERROR(web_ui_devices_register_assets(s_server), TAG, "devices assets register failed");

    static web_route_t route_root = {.fn = root_get_handler, .redirect_on_fail = true, .min_role = WEB_USER_ROLE_USER};
    static web_route_t route_ping = {.fn = ping_handler, .redirect_on_fail = false, .min_role = WEB_USER_ROLE_ADMIN};
    static web_route_t route_status = {.fn = status_handler, .redirect_on_fail = false, .min_role = WEB_USER_ROLE_ADMIN};
    static web_route_t route_ota_status = {.fn = ota_status_handler, .redirect_on_fail = false, .min_role = WEB_USER_ROLE_ADMIN};
    static web_route_t route_ota_upload = {.fn = ota_upload_handler, .redirect_on_fail = false, .min_role = WEB_USER_ROLE_ADMIN};
    static web_route_t route_ota_reboot = {.fn = ota_reboot_handler, .redirect_on_fail = false, .min_role = WEB_USER_ROLE_ADMIN};
    static web_route_t route_wifi = {.fn = wifi_config_handler, .redirect_on_fail = false, .min_role = WEB_USER_ROLE_ADMIN};
    static web_route_t route_mqtt = {.fn = mqtt_config_handler, .redirect_on_fail = false, .min_role = WEB_USER_ROLE_ADMIN};
    static web_route_t route_mqtt_users = {.fn = mqtt_users_handler, .redirect_on_fail = false, .min_role = WEB_USER_ROLE_ADMIN};
    static web_route_t route_logging = {.fn = logging_config_handler, .redirect_on_fail = false, .min_role = WEB_USER_ROLE_ADMIN};
    static web_route_t route_wifi_scan = {.fn = wifi_scan_handler, .redirect_on_fail = false, .min_role = WEB_USER_ROLE_ADMIN};
    static web_route_t route_ap_stop = {.fn = ap_stop_handler, .redirect_on_fail = false, .min_role = WEB_USER_ROLE_ADMIN};
    static web_route_t route_play = {.fn = audio_play_handler, .redirect_on_fail = false, .min_role = WEB_USER_ROLE_ADMIN};
    static web_route_t route_stop = {.fn = audio_stop_handler, .redirect_on_fail = false, .min_role = WEB_USER_ROLE_ADMIN};
    static web_route_t route_pause = {.fn = audio_pause_handler, .redirect_on_fail = false, .min_role = WEB_USER_ROLE_ADMIN};
    static web_route_t route_resume = {.fn = audio_resume_handler, .redirect_on_fail = false, .min_role = WEB_USER_ROLE_ADMIN};
    static web_route_t route_vol = {.fn = audio_volume_handler, .redirect_on_fail = false, .min_role = WEB_USER_ROLE_ADMIN};
    static web_route_t route_seek = {.fn = audio_seek_handler, .redirect_on_fail = false, .min_role = WEB_USER_ROLE_ADMIN};
    static web_route_t route_pub = {.fn = publish_handler, .redirect_on_fail = false, .min_role = WEB_USER_ROLE_ADMIN};
    static web_route_t route_files = {.fn = files_handler, .redirect_on_fail = false, .min_role = WEB_USER_ROLE_ADMIN};
    static web_route_t route_devices_cfg = {.fn = devices_config_handler, .redirect_on_fail = false, .min_role = WEB_USER_ROLE_USER};
    static web_route_t route_devices_apply = {.fn = devices_apply_handler, .redirect_on_fail = false, .min_role = WEB_USER_ROLE_ADMIN};
    static web_route_t route_devices_run = {.fn = devices_run_handler, .redirect_on_fail = false, .min_role = WEB_USER_ROLE_USER};
    static web_route_t route_profile_create = {.fn = devices_profile_create_handler, .redirect_on_fail = false, .min_role = WEB_USER_ROLE_ADMIN};
    static web_route_t route_profile_delete = {.fn = devices_profile_delete_handler, .redirect_on_fail = false, .min_role = WEB_USER_ROLE_ADMIN};
    static web_route_t route_profile_rename = {.fn = devices_profile_rename_handler, .redirect_on_fail = false, .min_role = WEB_USER_ROLE_ADMIN};
    static web_route_t route_profile_activate = {.fn = devices_profile_activate_handler, .redirect_on_fail = false, .min_role = WEB_USER_ROLE_ADMIN};
    static web_route_t route_profile_download = {.fn = devices_profile_download_handler, .redirect_on_fail = false, .min_role = WEB_USER_ROLE_ADMIN};
    static web_route_t route_variables = {.fn = devices_variables_handler, .redirect_on_fail = false, .min_role = WEB_USER_ROLE_ADMIN};
    static web_route_t route_templates = {.fn = devices_templates_handler, .redirect_on_fail = false, .min_role = WEB_USER_ROLE_ADMIN};
    static web_route_t route_auth_password = {.fn = auth_password_handler, .redirect_on_fail = false, .min_role = WEB_USER_ROLE_ADMIN};
    static web_route_t route_logout = {.fn = auth_logout_handler, .redirect_on_fail = false, .min_role = WEB_USER_ROLE_USER};
    static web_route_t route_session_info = {.fn = session_info_handler, .redirect_on_fail = false, .min_role = WEB_USER_ROLE_USER};

    ESP_RETURN_ON_ERROR(register_public_route("/login", HTTP_GET, login_page_handler), TAG, "register login");
    ESP_RETURN_ON_ERROR(register_public_route("/api/auth/login", HTTP_POST, auth_login_handler), TAG, "register auth login");
    ESP_RETURN_ON_ERROR(register_guarded_route("/api/auth/logout", HTTP_POST, &route_logout), TAG, "register logout");
    ESP_RETURN_ON_ERROR(register_guarded_route("/api/auth/password", HTTP_POST, &route_auth_password), TAG, "register auth password");
    ESP_RETURN_ON_ERROR(register_guarded_route("/api/session/info", HTTP_GET, &route_session_info), TAG, "register session info");
    ESP_RETURN_ON_ERROR(register_guarded_route("/", HTTP_GET, &route_root), TAG, "register root");
    ESP_RETURN_ON_ERROR(register_guarded_route("/api/status", HTTP_GET, &route_status), TAG, "register status");
    ESP_RETURN_ON_ERROR(register_guarded_route("/api/ota/status", HTTP_GET, &route_ota_status), TAG, "register ota status");
    ESP_RETURN_ON_ERROR(register_guarded_route("/api/ota/upload", HTTP_POST, &route_ota_upload), TAG, "register ota upload");
    ESP_RETURN_ON_ERROR(register_guarded_route("/api/ota/reboot", HTTP_POST, &route_ota_reboot), TAG, "register ota reboot");
    ESP_RETURN_ON_ERROR(register_guarded_route("/api/ping", HTTP_GET, &route_ping), TAG, "register ping");
    ESP_RETURN_ON_ERROR(register_guarded_route("/api/config/wifi", HTTP_GET, &route_wifi), TAG, "register wifi cfg");
    ESP_RETURN_ON_ERROR(register_guarded_route("/api/config/mqtt", HTTP_GET, &route_mqtt), TAG, "register mqtt cfg");
    ESP_RETURN_ON_ERROR(register_guarded_route("/api/config/mqtt_users", HTTP_POST, &route_mqtt_users), TAG, "register mqtt users");
    ESP_RETURN_ON_ERROR(register_guarded_route("/api/config/logging", HTTP_GET, &route_logging), TAG, "register logging cfg");
    ESP_RETURN_ON_ERROR(register_guarded_route("/api/wifi/scan", HTTP_GET, &route_wifi_scan), TAG, "register wifi scan");
    ESP_RETURN_ON_ERROR(register_guarded_route("/api/ap/stop", HTTP_GET, &route_ap_stop), TAG, "register ap stop");
    ESP_RETURN_ON_ERROR(register_guarded_route("/api/audio/play", HTTP_GET, &route_play), TAG, "register audio play");
    ESP_RETURN_ON_ERROR(register_guarded_route("/api/audio/stop", HTTP_GET, &route_stop), TAG, "register audio stop");
    ESP_RETURN_ON_ERROR(register_guarded_route("/api/audio/pause", HTTP_GET, &route_pause), TAG, "register audio pause");
    ESP_RETURN_ON_ERROR(register_guarded_route("/api/audio/resume", HTTP_GET, &route_resume), TAG, "register audio resume");
    ESP_RETURN_ON_ERROR(register_guarded_route("/api/audio/volume", HTTP_GET, &route_vol), TAG, "register audio volume");
    ESP_RETURN_ON_ERROR(register_guarded_route("/api/audio/seek", HTTP_GET, &route_seek), TAG, "register audio seek");
    ESP_RETURN_ON_ERROR(register_guarded_route("/api/publish", HTTP_GET, &route_pub), TAG, "register publish");
    ESP_RETURN_ON_ERROR(register_guarded_route("/api/files", HTTP_GET, &route_files), TAG, "register files");
    ESP_RETURN_ON_ERROR(register_guarded_route("/api/devices/config", HTTP_GET, &route_devices_cfg), TAG, "register devices cfg");
    ESP_RETURN_ON_ERROR(register_guarded_route("/api/devices/apply", HTTP_POST, &route_devices_apply), TAG, "register devices apply");
    ESP_RETURN_ON_ERROR(register_guarded_route("/api/devices/run", HTTP_GET, &route_devices_run), TAG, "register devices run");
    ESP_RETURN_ON_ERROR(register_guarded_route("/api/devices/profile/create", HTTP_POST, &route_profile_create), TAG, "register profile create");
    ESP_RETURN_ON_ERROR(register_guarded_route("/api/devices/profile/delete", HTTP_POST, &route_profile_delete), TAG, "register profile delete");
    ESP_RETURN_ON_ERROR(register_guarded_route("/api/devices/profile/rename", HTTP_POST, &route_profile_rename), TAG, "register profile rename");
    ESP_RETURN_ON_ERROR(register_guarded_route("/api/devices/profile/activate", HTTP_POST, &route_profile_activate), TAG, "register profile activate");
    ESP_RETURN_ON_ERROR(register_guarded_route("/api/devices/profile/download", HTTP_GET, &route_profile_download), TAG, "register profile download");
    ESP_RETURN_ON_ERROR(register_guarded_route("/api/devices/variables", HTTP_GET, &route_variables), TAG, "register variables");
    ESP_RETURN_ON_ERROR(register_guarded_route("/api/devices/templates", HTTP_GET, &route_templates), TAG, "register templates");
    return ESP_OK;
}

esp_err_t web_ui_init(void)
{
    web_sessions_init();
    web_auth_start_reset_monitor();
    return ESP_OK;
}

esp_err_t web_ui_start(void)
{
    ESP_RETURN_ON_ERROR(start_httpd(), TAG, "start httpd failed");
    #if WEB_UI_DEBUG
    ESP_LOGI(TAG, "web ui started on port 80");
#endif
    return ESP_OK;
}


#if defined(ESP_ERR_HTTPD_RESP_SEND)
#define HAS_HTTPD_RESP_SEND 1
#endif
#if defined(ESP_ERR_HTTPD_INVALID_RESP)
#define HAS_HTTPD_INVALID_RESP 1
#endif

void web_ui_report_httpd_error(esp_err_t err, const char *context)
{
    if (err == ESP_OK) {
        return;
    }
    const char *ctx = (context && context[0]) ? context : "httpd";
    ESP_LOGE(TAG, "httpd error %s (0x%x) at %s", esp_err_to_name(err), err, ctx);
    bool fatal = (err == ESP_ERR_HTTPD_HANDLERS_FULL ||
                  err == ESP_ERR_HTTPD_INVALID_REQ ||
                  err == ESP_ERR_HTTPD_ALLOC_MEM ||
                  err == ESP_ERR_NO_MEM ||
                  err == ESP_ERR_TIMEOUT);
#ifdef ESP_ERR_HTTPD_INVALID_STATE
    fatal = fatal || (err == ESP_ERR_HTTPD_INVALID_STATE);
#endif
#ifdef HAS_HTTPD_RESP_SEND
    fatal = fatal || (err == ESP_ERR_HTTPD_RESP_SEND);
#endif
#ifdef HAS_HTTPD_INVALID_RESP
    fatal = fatal || (err == ESP_ERR_HTTPD_INVALID_RESP);
#endif
    if (fatal) {
        web_ui_schedule_httpd_restart(ctx);
    }
}

static void web_ui_schedule_httpd_restart(const char *reason)
{
    if (s_httpd_restart_pending) {
        return;
    }
    s_httpd_restart_pending = true;
    if (reason && reason[0]) {
        strncpy(s_httpd_restart_reason, reason, sizeof(s_httpd_restart_reason) - 1);
        s_httpd_restart_reason[sizeof(s_httpd_restart_reason) - 1] = '\0';
    } else {
        strncpy(s_httpd_restart_reason, "unknown", sizeof(s_httpd_restart_reason) - 1);
        s_httpd_restart_reason[sizeof(s_httpd_restart_reason) - 1] = '\0';
    }
    if (xTaskCreate(web_ui_httpd_restart_task, "web_ui_httpd_rst", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to schedule httpd restart");
        s_httpd_restart_pending = false;
    }
}

static void web_ui_httpd_restart_task(void *param)
{
    ESP_LOGW(TAG, "restarting httpd due to %s", s_httpd_restart_reason);
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    esp_err_t err = start_httpd();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd restart failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "httpd restarted");
    }
    s_httpd_restart_pending = false;
    s_httpd_restart_reason[0] = '\0';
    vTaskDelete(NULL);
}

esp_err_t web_ui_send_ok(httpd_req_t *req, const char *mime, const char *body)
{
    httpd_resp_set_type(req, mime);
    return WEB_HTTP_CHECK_CTX(httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN), "web_ui_send_ok");
}



