#pragma once

#include "esp_err.h"

typedef struct {
    char ssid[32];
    char password[64];
    char hostname[32];
} app_wifi_config_t;

typedef struct {
    char broker_id[16];
    int port;
    int keepalive_seconds;
} app_mqtt_config_t;

typedef struct {
    char ntp_server[64];
    int timezone_offset_min;
} app_time_config_t;

typedef struct {
    app_wifi_config_t wifi;
    app_mqtt_config_t mqtt;
    app_time_config_t time;
} app_config_t;

esp_err_t config_store_init(void);
const app_config_t *config_store_get(void);
esp_err_t config_store_set(const app_config_t *next);
esp_err_t config_store_reset_defaults(void);
