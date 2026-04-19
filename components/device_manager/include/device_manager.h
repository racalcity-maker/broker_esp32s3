#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "device_model.h"

esp_err_t device_manager_init(void);
esp_err_t device_manager_reload_from_nvs(void);
esp_err_t device_manager_save_snapshot(void);
const device_manager_config_t *device_manager_lock_config(void);
void device_manager_unlock_config(void);
esp_err_t device_manager_apply(const device_manager_config_t *next);
esp_err_t device_manager_sync_file(void);
esp_err_t device_manager_export_json(char **out_json, size_t *out_len);
esp_err_t device_manager_export_profile_json(const char *profile_id, char **out_json, size_t *out_len);
esp_err_t device_manager_export_profile_raw(const char *profile_id, uint8_t **out_data, size_t *out_size);
esp_err_t device_manager_apply_json(const char *json, size_t len);
esp_err_t device_manager_apply_profile_json(const char *profile_id, const char *json, size_t len);
esp_err_t device_manager_profile_create(const char *id, const char *name, const char *clone_id);
esp_err_t device_manager_profile_delete(const char *id);
esp_err_t device_manager_profile_rename(const char *id, const char *new_name);
esp_err_t device_manager_profile_activate(const char *id);
const char *device_manager_default_profile_id(void);

#ifdef __cplusplus
}
#endif
