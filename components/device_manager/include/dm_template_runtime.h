#pragma once

#include <stdbool.h>
#include "esp_err.h"

#include "dm_template_registry.h"
#include "dm_runtime_sensor.h"

esp_err_t dm_template_runtime_init(void);
void dm_template_runtime_reset(void);
esp_err_t dm_template_runtime_register(const dm_template_config_t *tpl, const char *device_id);
bool dm_template_runtime_handle_mqtt(const char *topic, const char *payload);
bool dm_template_runtime_handle_flag(const char *flag_name, bool state);
typedef struct {
    char device_id[DEVICE_MANAGER_ID_MAX_LEN];
    uint8_t slot_count;
    struct {
        char source_id[DEVICE_MANAGER_ID_MAX_LEN];
        char label[DEVICE_MANAGER_NAME_MAX_LEN];
        bool has_value;
        char last_value[DM_UID_TEMPLATE_VALUE_MAX_LEN];
    } slots[DM_UID_TEMPLATE_MAX_SLOTS];
} dm_uid_runtime_snapshot_t;
esp_err_t dm_template_runtime_get_uid_snapshot(const char *device_id, dm_uid_runtime_snapshot_t *out);

typedef struct {
    dm_sensor_channel_t config;
    bool has_value;
    float last_value;
    dm_sensor_status_t status;
    uint64_t last_update_ms;
    uint8_t history_count;
    dm_sensor_history_sample_t history[DM_SENSOR_HISTORY_MAX_SAMPLES];
} dm_sensor_channel_runtime_snapshot_t;

typedef struct {
    char device_id[DEVICE_MANAGER_ID_MAX_LEN];
    char device_name[DEVICE_MANAGER_NAME_MAX_LEN];
    char description[DEVICE_MANAGER_NAME_MAX_LEN];
    uint8_t channel_count;
    dm_sensor_channel_runtime_snapshot_t channels[DM_SENSOR_TEMPLATE_MAX_CHANNELS];
} dm_sensor_runtime_snapshot_t;

size_t dm_template_runtime_get_sensor_snapshots(dm_sensor_runtime_snapshot_t *out, size_t max_count);
