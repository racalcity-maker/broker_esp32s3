#pragma once

#include "esp_err.h"
#include "mqtt_core.h"
#include "audio_player.h"

#ifndef WEB_UI_DEBUG
#define WEB_UI_DEBUG 0
#endif

esp_err_t web_ui_init(void);
esp_err_t web_ui_start(void);
