#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t web_ui_pictures_init(void);
esp_err_t web_ui_pictures_register(httpd_handle_t server);
void web_ui_pictures_handle_scan(int idx, const char *uid);
bool web_ui_pictures_handle_check(void);
void web_ui_pictures_request_force_cycle(void);
