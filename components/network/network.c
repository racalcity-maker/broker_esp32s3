#include "network.h"

#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "lwip/apps/sntp.h"

#include "config_store.h"
#include "error_monitor.h"

static const char *TAG = "network";
static esp_netif_t *s_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;
static int s_retry_count = 0;
static const int MAX_RETRY = 5;
static const int RUNTIME_RECONNECT_DELAY_SEC = 30;
static const int64_t RUNTIME_RECONNECT_DELAY_US = 30000000LL;
static bool s_ap_mode = false;
static bool s_connected_once = false;
static SemaphoreHandle_t s_state_mutex = NULL;
static esp_timer_handle_t s_reconnect_timer = NULL;
static void start_ap_mode(const char *hostname);
static void build_sta_config(const app_config_t *cfg, wifi_config_t *wifi_cfg);

static void state_lock(void)
{
    if (s_state_mutex) {
        xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    }
}

static void state_unlock(void)
{
    if (s_state_mutex) {
        xSemaphoreGive(s_state_mutex);
    }
}

static bool ap_mode_value(void)
{
    bool mode = false;
    state_lock();
    mode = s_ap_mode;
    state_unlock();
    return mode;
}

static void ap_mode_set(bool on)
{
    state_lock();
    s_ap_mode = on;
    state_unlock();
}

static void cancel_runtime_reconnect(void)
{
    if (!s_reconnect_timer) {
        return;
    }
    if (esp_timer_is_active(s_reconnect_timer)) {
        esp_timer_stop(s_reconnect_timer);
    }
}

static void reconnect_timer_cb(void *arg)
{
    ESP_LOGI(TAG, "retry STA connect...");
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
    }
}

static void schedule_runtime_reconnect(void)
{
    if (!s_reconnect_timer) {
        return;
    }
    cancel_runtime_reconnect();
    esp_err_t err = esp_timer_start_once(s_reconnect_timer, RUNTIME_RECONNECT_DELAY_US);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to start reconnect timer: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGW(TAG, "Wi-Fi disconnected, retrying in %d s", RUNTIME_RECONNECT_DELAY_SEC);
}

static void start_mdns(const char *hostname)
{
    if (mdns_init() == ESP_OK) {
        mdns_hostname_set(hostname);
        mdns_instance_name_set("ESP32S3 Broker");
        ESP_LOGI(TAG, "mDNS started: %s.local", hostname);
    } else {
        ESP_LOGW(TAG, "mDNS init failed");
    }
}

static bool s_sntp_started = false;

static void start_sntp(const char *server)
{
    if (!server || !server[0]) {
        return;
    }
    if (s_sntp_started) {
        sntp_stop();
        s_sntp_started = false;
    }
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, (char *)server);
    sntp_init();
    s_sntp_started = true;
    ESP_LOGI(TAG, "SNTP started: %s", server);
}

static void on_ip_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_retry_count = 0;
        s_connected_once = true;
        cancel_runtime_reconnect();
        ESP_LOGI(TAG, "got IP, starting mDNS/NTP");
        const app_config_t *cfg = config_store_get();
        start_mdns(cfg->wifi.hostname);
        start_sntp(cfg->time.ntp_server);
        error_monitor_set_wifi_connected(true);
    }
}

static void on_wifi_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        error_monitor_set_wifi_connected(false);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_connected_once) {
            schedule_runtime_reconnect();
        } else if (s_retry_count < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_count++;
            ESP_LOGW(TAG, "retry connect (%d/%d)", s_retry_count, MAX_RETRY);
        } else {
            ESP_LOGE(TAG, "failed to connect after retries");
            if (!ap_mode_value()) {
                const app_config_t *cfg = config_store_get();
                start_ap_mode(cfg->wifi.hostname);
            }
        }
        error_monitor_set_wifi_connected(false);
    }
}

static void start_ap_mode(const char *hostname)
{
    state_lock();
    if (s_ap_mode) {
        state_unlock();
        return;
    }
    if (!s_ap_netif) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
    }
    wifi_config_t ap_cfg = {0};
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf((char *)ap_cfg.ap.ssid, sizeof(ap_cfg.ap.ssid), "brocker-setup-%02X%02X", mac[4], mac[5]);
    strncpy((char *)ap_cfg.ap.password, "12345678", sizeof(ap_cfg.ap.password) - 1);
    ap_cfg.ap.ssid_len = strlen((char *)ap_cfg.ap.ssid);
    ap_cfg.ap.channel = 1;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    if (strlen((char *)ap_cfg.ap.password) == 0) {
        ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    s_ap_mode = true;
    state_unlock();
    ESP_LOGW(TAG, "AP mode enabled: SSID=%s pass=%s", ap_cfg.ap.ssid, ap_cfg.ap.password);
    start_mdns(hostname);
}

static void build_sta_config(const app_config_t *cfg, wifi_config_t *wifi_cfg)
{
    if (!wifi_cfg || !cfg) {
        return;
    }
    memset(wifi_cfg, 0, sizeof(*wifi_cfg));
    wifi_cfg->sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    strncpy((char *)wifi_cfg->sta.ssid, cfg->wifi.ssid, sizeof(wifi_cfg->sta.ssid) - 1);
    strncpy((char *)wifi_cfg->sta.password, cfg->wifi.password, sizeof(wifi_cfg->sta.password) - 1);
}

esp_err_t network_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_ip_event, NULL));
    if (!s_reconnect_timer) {
        const esp_timer_create_args_t timer_args = {
            .callback = &reconnect_timer_cb,
            .name = "wifi_reconnect",
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_reconnect_timer));
    }
    if (!s_state_mutex) {
        s_state_mutex = xSemaphoreCreateMutex();
        if (!s_state_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

esp_err_t network_start(void)
{
    const app_config_t *cfg = config_store_get();
    bool have_sta = cfg->wifi.ssid[0] != '\0';

    state_lock();
    if (!s_netif) {
        s_netif = esp_netif_create_default_wifi_sta();
    }
    state_unlock();

    if (have_sta) {
        wifi_config_t wifi_cfg;
        build_sta_config(cfg, &wifi_cfg);

        if (s_netif) {
            esp_netif_set_hostname(s_netif, cfg->wifi.hostname);
        }

        ESP_LOGI(TAG, "connecting to SSID=%s host=%s", cfg->wifi.ssid, cfg->wifi.hostname);
        s_retry_count = 0;
        s_connected_once = false;
        cancel_runtime_reconnect();
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
        ESP_ERROR_CHECK(esp_wifi_start());
        error_monitor_set_wifi_connected(false);
    } else {
        start_ap_mode(cfg->wifi.hostname);
    }
    return ESP_OK;
}

esp_err_t network_apply_wifi_config(void)
{
    const app_config_t *cfg = config_store_get();
    if (!cfg) {
        return ESP_ERR_INVALID_STATE;
    }
    bool have_sta = cfg->wifi.ssid[0] != '\0';
    if (!have_sta) {
        ESP_LOGW(TAG, "Wi-Fi SSID empty, enabling AP mode only");
        start_ap_mode(cfg->wifi.hostname);
        return ESP_OK;
    }

    wifi_config_t wifi_cfg;
    build_sta_config(cfg, &wifi_cfg);
    state_lock();
    if (!s_netif) {
        s_netif = esp_netif_create_default_wifi_sta();
    }
    state_unlock();
    if (s_netif) {
        esp_netif_set_hostname(s_netif, cfg->wifi.hostname);
    }

    s_retry_count = 0;
    s_connected_once = false;
    cancel_runtime_reconnect();

    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&mode);
    bool keep_ap = ap_mode_value() || mode == WIFI_MODE_APSTA;
    esp_err_t err = esp_wifi_set_mode(keep_ap ? WIFI_MODE_APSTA : WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(err));
    }

    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "applying Wi-Fi config, connecting to SSID=%s", cfg->wifi.ssid);
    err = esp_wifi_disconnect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_CONNECT && err != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGW(TAG, "esp_wifi_disconnect: %s", esp_err_to_name(err));
    }

    err = esp_wifi_connect();
    if (err == ESP_ERR_WIFI_NOT_STARTED) {
        esp_err_t start_err = esp_wifi_start();
        if (start_err != ESP_OK && start_err != ESP_ERR_WIFI_CONN) {
            ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(start_err));
            return start_err;
        }
        err = esp_wifi_connect();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
        return err;
    }
    error_monitor_set_wifi_connected(false);
    return ESP_OK;
}

esp_err_t network_stop_ap(void)
{
    if (!ap_mode_value()) {
        return ESP_OK;
    }
    ESP_LOGI(TAG, "stopping AP, leaving STA only");
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) {
        ap_mode_set(false);
    }
    return err;
}

bool network_is_ap_mode(void)
{
    return ap_mode_value();
}
