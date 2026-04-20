#pragma once

#include <stdbool.h>

#include "esp_err.h"

#include "dm_template_registry.h"

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
    char device_id[DEVICE_MANAGER_ID_MAX_LEN];
    bool active;
    uint8_t current_step_index;
    uint8_t total_steps;
    uint32_t timeout_ms;
    uint32_t time_left_ms;
    bool payload_required;
    bool reset_on_error;
    char expected_topic[DEVICE_MANAGER_TOPIC_MAX_LEN];
    char expected_payload[DEVICE_MANAGER_PAYLOAD_MAX_LEN];
    char state[16];
} dm_sequence_runtime_snapshot_t;

typedef struct {
    char device_id[DEVICE_MANAGER_ID_MAX_LEN];
    bool active;
    bool completed;
    uint32_t progress_ms;
    uint32_t required_hold_ms;
    uint32_t heartbeat_timeout_ms;
    uint32_t time_left_ms;
    char state[16];
    char heartbeat_topic[DEVICE_MANAGER_TOPIC_MAX_LEN];
    char reset_topic[DEVICE_MANAGER_TOPIC_MAX_LEN];
} dm_signal_runtime_snapshot_t;

esp_err_t dm_template_runtime_get_signal_snapshot(const char *device_id, dm_signal_runtime_snapshot_t *out);
esp_err_t dm_template_runtime_reset_signal(const char *device_id);
esp_err_t dm_template_runtime_get_sequence_snapshot(const char *device_id, dm_sequence_runtime_snapshot_t *out);
esp_err_t dm_template_runtime_reset_sequence(const char *device_id);
