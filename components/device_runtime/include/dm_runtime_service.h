#pragma once

#include "esp_err.h"

#include "device_model.h"
#include "dm_template_registry.h"

esp_err_t dm_runtime_service_init(void);
esp_err_t dm_runtime_service_rebuild(const device_manager_config_t *cfg);
esp_err_t dm_runtime_service_register_template(const dm_template_config_t *tpl, const char *device_id);
