#include "config_store.h"

#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "config_store";
static const char *NVS_NS = "cfg";
static const uint32_t CONFIG_VERSION = 1;
static app_config_t g_config;
static portMUX_TYPE g_config_lock = portMUX_INITIALIZER_UNLOCKED;

static void config_lock(void)
{
    taskENTER_CRITICAL(&g_config_lock);
}

static void config_unlock(void)
{
    taskEXIT_CRITICAL(&g_config_lock);
}

static void load_defaults(app_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    // Пустой SSID => стартуем AP для конфигурации.
    cfg->wifi.ssid[0] = '\0';
    cfg->wifi.password[0] = '\0';
    strncpy(cfg->wifi.hostname, "brocker", sizeof(cfg->wifi.hostname) - 1);
    strncpy(cfg->mqtt.broker_id, "brocker", sizeof(cfg->mqtt.broker_id) - 1);
    cfg->mqtt.port = 1883;
    cfg->mqtt.keepalive_seconds = 30;
    strncpy(cfg->time.ntp_server, "pool.ntp.org", sizeof(cfg->time.ntp_server) - 1);
    cfg->time.timezone_offset_min = 180;
}

static bool validate_string(const char *s, size_t max_len)
{
    if (!s) {
        return false;
    }
    size_t len = strnlen(s, max_len);
    return len > 0 && len < max_len;
}

static bool validate_config(const app_config_t *cfg)
{
    if (!cfg) {
        return false;
    }
    // Wi-Fi SSID может быть пустым (тогда запускаем AP для конфигурации).
    if (cfg->wifi.hostname[0] != '\0' && !validate_string(cfg->wifi.hostname, sizeof(cfg->wifi.hostname))) {
        return false;
    }
    if (!validate_string(cfg->mqtt.broker_id, sizeof(cfg->mqtt.broker_id))) {
        return false;
    }
    if (cfg->mqtt.port <= 0 || cfg->mqtt.port > 65535) {
        return false;
    }
    if (cfg->mqtt.keepalive_seconds <= 0 || cfg->mqtt.keepalive_seconds > 600) {
        return false;
    }
    if (!validate_string(cfg->time.ntp_server, sizeof(cfg->time.ntp_server))) {
        return false;
    }
    return true;
}

static esp_err_t save_to_nvs(const app_config_t *cfg)
{
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &handle), TAG, "nvs_open failed");

    esp_err_t err = nvs_set_u32(handle, "ver", CONFIG_VERSION);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, "cfg", cfg, sizeof(*cfg));
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static esp_err_t load_from_nvs(app_config_t *cfg)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    uint32_t ver = 0;
    size_t size = sizeof(*cfg);
    err = nvs_get_u32(handle, "ver", &ver);
    if (err != ESP_OK || ver != CONFIG_VERSION) {
        nvs_close(handle);
        return ESP_ERR_INVALID_VERSION;
    }
    err = nvs_get_blob(handle, "cfg", cfg, &size);
    nvs_close(handle);
    if (err == ESP_OK && size != sizeof(*cfg)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return err;
}

esp_err_t config_store_init(void)
{
    config_lock();
    load_defaults(&g_config);
    config_unlock();

    app_config_t loaded;
    if (load_from_nvs(&loaded) == ESP_OK && validate_config(&loaded)) {
        config_lock();
        g_config = loaded;
        config_unlock();
        ESP_LOGI(TAG, "config loaded from NVS");
        return ESP_OK;
    }

    ESP_LOGW(TAG, "using default config (failed to load or invalid)");
    app_config_t snapshot;
    config_lock();
    snapshot = g_config;
    config_unlock();
    return save_to_nvs(&snapshot);
}

const app_config_t *config_store_get(void)
{
    return &g_config;
}

esp_err_t config_store_set(const app_config_t *next)
{
    if (!validate_config(next)) {
        ESP_LOGE(TAG, "config validation failed");
        return ESP_ERR_INVALID_ARG;
    }
    app_config_t snapshot;
    config_lock();
    g_config = *next;
    snapshot = g_config;
    config_unlock();
    esp_err_t err = save_to_nvs(&snapshot);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "config saved to NVS");
    } else {
        ESP_LOGE(TAG, "failed to save config: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t config_store_reset_defaults(void)
{
    app_config_t snapshot;
    config_lock();
    load_defaults(&g_config);
    snapshot = g_config;
    config_unlock();
    return save_to_nvs(&snapshot);
}
