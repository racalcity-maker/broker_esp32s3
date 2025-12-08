#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t web_ui_laser_init(void);
esp_err_t web_ui_laser_register(httpd_handle_t server);
void web_ui_laser_handle_heartbeat(const char *payload);
