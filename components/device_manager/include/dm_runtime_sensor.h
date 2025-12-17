#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "dm_templates.h"

typedef enum {
    DM_SENSOR_STATUS_UNKNOWN = 0,
    DM_SENSOR_STATUS_OK,
    DM_SENSOR_STATUS_WARN,
    DM_SENSOR_STATUS_ALARM,
} dm_sensor_status_t;

typedef struct {
    float value;
    dm_sensor_status_t status;
    uint64_t timestamp_ms;
} dm_sensor_history_sample_t;

typedef struct {
    dm_sensor_template_t config;
    bool has_value;
    float last_value;
    dm_sensor_status_t status;
    uint64_t last_update_ms;
    dm_sensor_history_sample_t history[DM_SENSOR_HISTORY_MAX_SAMPLES];
    uint8_t history_count;
    uint8_t history_index;
} dm_sensor_runtime_t;

void dm_sensor_runtime_init(dm_sensor_runtime_t *rt, const dm_sensor_template_t *tpl);
dm_sensor_status_t dm_sensor_runtime_eval(const dm_sensor_template_t *tpl, float value);
bool dm_sensor_runtime_record(dm_sensor_runtime_t *rt,
                              float value,
                              uint64_t timestamp_ms,
                              dm_sensor_status_t *out_status,
                              bool *status_changed);
