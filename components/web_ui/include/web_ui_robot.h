#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t web_ui_robot_init(void);
esp_err_t web_ui_robot_register(httpd_handle_t server);
