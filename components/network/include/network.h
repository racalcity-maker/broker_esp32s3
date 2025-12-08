#pragma once

#include <stdbool.h>
#include "esp_err.h"

esp_err_t network_init(void);
esp_err_t network_start(void);
esp_err_t network_apply_wifi_config(void);
esp_err_t network_stop_ap(void);
bool network_is_ap_mode(void);
