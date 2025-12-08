#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

/**
 * Registers HTTP assets (CSS/JS) required by the device wizard UI.
 */
esp_err_t web_ui_devices_register_assets(httpd_handle_t server);
