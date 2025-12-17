#include "dm_runtime_sensor.h"

#include <string.h>

#include "device_manager_utils.h"

static bool sensor_threshold_hit(const dm_sensor_threshold_t *th, float value)
{
    if (!th || !th->enabled) {
        return false;
    }
    switch (th->compare) {
    case DM_SENSOR_COMPARE_BELOW_OR_EQUAL:
        return value <= th->threshold;
    case DM_SENSOR_COMPARE_ABOVE_OR_EQUAL:
    default:
        return value >= th->threshold;
    }
}

void dm_sensor_runtime_init(dm_sensor_runtime_t *rt, const dm_sensor_template_t *tpl)
{
    if (!rt) {
        return;
    }
    memset(rt, 0, sizeof(*rt));
    rt->status = DM_SENSOR_STATUS_UNKNOWN;
    if (tpl) {
        memcpy(&rt->config, tpl, sizeof(rt->config));
    } else {
        dm_sensor_template_clear(&rt->config);
    }
}

dm_sensor_status_t dm_sensor_runtime_eval(const dm_sensor_template_t *tpl, float value)
{
    if (!tpl) {
        return DM_SENSOR_STATUS_UNKNOWN;
    }
    if (sensor_threshold_hit(&tpl->alarm, value)) {
        return DM_SENSOR_STATUS_ALARM;
    }
    if (sensor_threshold_hit(&tpl->warn, value)) {
        return DM_SENSOR_STATUS_WARN;
    }
    return DM_SENSOR_STATUS_OK;
}

bool dm_sensor_runtime_record(dm_sensor_runtime_t *rt,
                              float value,
                              uint64_t timestamp_ms,
                              dm_sensor_status_t *out_status,
                              bool *status_changed)
{
    if (!rt) {
        return false;
    }
    dm_sensor_status_t next = dm_sensor_runtime_eval(&rt->config, value);
    bool changed = (next != rt->status);
    rt->has_value = true;
    rt->last_value = value;
    rt->last_update_ms = timestamp_ms;
    rt->status = next;
    if (rt->config.history_enabled) {
        dm_sensor_history_sample_t *slot = &rt->history[rt->history_index];
        slot->value = value;
        slot->status = next;
        slot->timestamp_ms = timestamp_ms;
        rt->history_index = (rt->history_index + 1) % DM_SENSOR_HISTORY_MAX_SAMPLES;
        if (rt->history_count < DM_SENSOR_HISTORY_MAX_SAMPLES) {
            rt->history_count++;
        }
    } else {
        rt->history_count = 0;
        rt->history_index = 0;
    }
    if (out_status) {
        *out_status = next;
    }
    if (status_changed) {
        *status_changed = changed;
    }
    return true;
}
