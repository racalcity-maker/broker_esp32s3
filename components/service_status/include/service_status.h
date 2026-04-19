#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef enum {
    SERVICE_STATUS_NETWORK = 0,
    SERVICE_STATUS_MQTT,
    SERVICE_STATUS_AUDIO,
    SERVICE_STATUS_WEB_UI,
    SERVICE_STATUS_COUNT,
} service_status_id_t;

typedef struct {
    bool init_attempted;
    bool init_ok;
    bool start_attempted;
    bool start_ok;
} service_status_entry_t;

esp_err_t service_status_init(void);
void service_status_mark_init(service_status_id_t id, esp_err_t err);
void service_status_mark_start(service_status_id_t id, esp_err_t err);
bool service_status_get(service_status_id_t id, service_status_entry_t *out);
const char *service_status_name(service_status_id_t id);
