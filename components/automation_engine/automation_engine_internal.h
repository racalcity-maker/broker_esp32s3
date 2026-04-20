#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "device_model.h"
#include "event_bus.h"

typedef struct {
    const device_descriptor_t *device;
    const device_scenario_t *scenario;
} automation_job_t;

esp_err_t automation_engine_context_init(void);
void automation_engine_context_set(const char *key, const char *value);
void automation_engine_context_clear(const char *key);
size_t automation_engine_context_lookup(const char *key, char *out, size_t out_len);
void automation_engine_render_template(const char *src, char *dst, size_t dst_len);

esp_err_t automation_engine_flags_init(void);
void automation_engine_set_flag_internal(const char *name, bool value);
bool automation_engine_get_flag_internal(const char *name);
bool automation_engine_wait_for_flags(const device_wait_flags_t *wait);
event_bus_type_t automation_engine_event_name_to_type(const char *name);

const device_descriptor_t *automation_engine_find_device_by_id(const char *id);
const device_scenario_t *automation_engine_find_scenario_by_id(const device_descriptor_t *device, const char *id);

esp_err_t automation_engine_execution_init(void);
esp_err_t automation_engine_execution_start(void);
esp_err_t automation_engine_enqueue_job(const device_descriptor_t *device, const device_scenario_t *scenario);
void automation_engine_reload(void);
bool automation_engine_handle_mqtt(const char *topic, const char *payload);
