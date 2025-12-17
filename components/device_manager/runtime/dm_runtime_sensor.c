#include "dm_runtime_sensor.h"

#include <string.h>

#include "device_manager_utils.h"

static bool sensor_threshold_condition_met(const dm_sensor_threshold_t *th, float value, bool active_state)
{
    if (!th || !th->enabled) {
        return false;
    }
    float hysteresis = th->hysteresis > 0 ? th->hysteresis : 0;
    switch (th->compare) {
    case DM_SENSOR_COMPARE_BELOW_OR_EQUAL:
        if (active_state) {
            return value <= (th->threshold + hysteresis);
        }
        return value <= th->threshold;
    case DM_SENSOR_COMPARE_ABOVE_OR_EQUAL:
    default:
        if (active_state) {
            return value >= (th->threshold - hysteresis);
        }
        return value >= th->threshold;
    }
}

static bool sensor_threshold_update(dm_sensor_threshold_state_t *state,
                                    const dm_sensor_threshold_t *cfg,
                                    float value,
                                    uint64_t now_ms)
{
    if (!state || !cfg || !cfg->enabled) {
        if (state) {
            state->active = false;
            state->enter_ts_ms = 0;
        }
        return false;
    }
    bool within_band = sensor_threshold_condition_met(cfg, value, state->active);
    if (!within_band) {
        state->active = false;
        state->enter_ts_ms = 0;
        return false;
    }
    if (state->active) {
        return true;
    }
    if (state->enter_ts_ms == 0 || now_ms < state->enter_ts_ms) {
        state->enter_ts_ms = now_ms;
    }
    if (cfg->min_duration_ms > 0) {
        uint64_t ready_ts = state->enter_ts_ms + cfg->min_duration_ms;
        if (now_ms < ready_ts) {
            return false;
        }
    }
    state->active = true;
    return true;
}

void dm_sensor_runtime_init(dm_sensor_runtime_t *rt, const dm_sensor_template_t *tpl)
{
    if (!rt) {
        return;
    }
    memset(rt, 0, sizeof(*rt));
    if (tpl) {
        memcpy(&rt->config, tpl, sizeof(rt->config));
        rt->channel_count = rt->config.channel_count;
        if (rt->channel_count > DM_SENSOR_TEMPLATE_MAX_CHANNELS) {
            rt->channel_count = DM_SENSOR_TEMPLATE_MAX_CHANNELS;
            rt->config.channel_count = rt->channel_count;
        }
    } else {
        dm_sensor_template_clear(&rt->config);
        rt->channel_count = 0;
    }
    for (uint8_t i = 0; i < rt->channel_count; ++i) {
        rt->channels[i].config = rt->config.channels[i];
        rt->channels[i].status = DM_SENSOR_STATUS_UNKNOWN;
    }
}

bool dm_sensor_runtime_record(dm_sensor_runtime_t *rt,
                              uint8_t channel_index,
                              float value,
                              uint64_t timestamp_ms,
                              dm_sensor_status_t *out_status,
                              bool *status_changed,
                              bool *warn_enter,
                              bool *alarm_enter)
{
    if (!rt || channel_index >= rt->channel_count) {
        return false;
    }
    dm_sensor_channel_runtime_t *channel = &rt->channels[channel_index];
    bool prev_warn = channel->warn_state.active;
    bool prev_alarm = channel->alarm_state.active;
    bool warn_active = sensor_threshold_update(&channel->warn_state, &channel->config.warn, value, timestamp_ms);
    bool alarm_active =
        sensor_threshold_update(&channel->alarm_state, &channel->config.alarm, value, timestamp_ms);
    dm_sensor_status_t next = DM_SENSOR_STATUS_OK;
    if (alarm_active) {
        next = DM_SENSOR_STATUS_ALARM;
    } else if (warn_active) {
        next = DM_SENSOR_STATUS_WARN;
    }
    bool changed = (next != channel->status);
    channel->has_value = true;
    channel->last_value = value;
    channel->last_update_ms = timestamp_ms;
    channel->status = next;
    if (channel->config.history_enabled) {
        dm_sensor_history_sample_t *slot = &channel->history[channel->history_index];
        slot->value = value;
        slot->status = next;
        slot->timestamp_ms = timestamp_ms;
        channel->history_index = (channel->history_index + 1) % DM_SENSOR_HISTORY_MAX_SAMPLES;
        if (channel->history_count < DM_SENSOR_HISTORY_MAX_SAMPLES) {
            channel->history_count++;
        }
    } else {
        channel->history_count = 0;
        channel->history_index = 0;
    }
    if (out_status) {
        *out_status = next;
    }
    if (status_changed) {
        *status_changed = changed;
    }
    if (warn_enter) {
        *warn_enter = (!prev_warn && warn_active);
    }
    if (alarm_enter) {
        *alarm_enter = (!prev_alarm && alarm_active);
    }
    return true;
}
