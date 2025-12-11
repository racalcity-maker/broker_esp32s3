#include "dm_runtime_sequence.h"

#include <string.h>

#include "device_manager_utils.h"

static bool payload_matches(const dm_sequence_step_t *step, const char *payload)
{
    if (!step) {
        return false;
    }
    if (!step->payload[0]) {
        return step->payload_required ? false : true;
    }
    if (!payload) {
        payload = "";
    }
    return strcmp(step->payload, payload) == 0;
}

static bool step_matches(const dm_sequence_step_t *step, const char *topic, const char *payload)
{
    if (!step || !topic || !topic[0]) {
        return false;
    }
    if (!step->topic[0] || strcmp(step->topic, topic) != 0) {
        return false;
    }
    return payload_matches(step, payload);
}

void dm_sequence_runtime_init(dm_sequence_runtime_t *rt, const dm_sequence_template_t *tpl)
{
    if (!rt) {
        return;
    }
    if (tpl) {
        memcpy(&rt->config, tpl, sizeof(*tpl));
    } else {
        memset(&rt->config, 0, sizeof(rt->config));
    }
    rt->current_index = 0;
    rt->last_step_ms = 0;
}

void dm_sequence_runtime_reset(dm_sequence_runtime_t *rt)
{
    if (!rt) {
        return;
    }
    rt->current_index = 0;
    rt->last_step_ms = 0;
}

static bool expired(const dm_sequence_template_t *cfg, const dm_sequence_runtime_t *rt, uint64_t now_ms)
{
    if (!cfg || cfg->timeout_ms == 0) {
        return false;
    }
    if (!rt || rt->current_index == 0 || rt->last_step_ms == 0) {
        return false;
    }
    if (now_ms <= rt->last_step_ms) {
        return false;
    }
    uint64_t delta = now_ms - rt->last_step_ms;
    return delta > cfg->timeout_ms;
}

dm_sequence_action_t dm_sequence_runtime_handle(dm_sequence_runtime_t *rt,
                                                const char *topic,
                                                const char *payload,
                                                uint64_t now_ms)
{
    dm_sequence_action_t action = {
        .type = DM_SEQUENCE_EVENT_NONE,
        .step = NULL,
        .timeout = false,
    };
    if (!rt || !topic || !topic[0]) {
        return action;
    }
    const dm_sequence_template_t *cfg = &rt->config;
    if (cfg->step_count == 0) {
        return action;
    }

    if (expired(cfg, rt, now_ms)) {
        dm_sequence_runtime_reset(rt);
        action.type = DM_SEQUENCE_EVENT_FAILED;
        action.timeout = true;
        return action;
    }

    int match_index = -1;
    const dm_sequence_step_t *matched = NULL;
    for (uint8_t i = 0; i < cfg->step_count && i < DM_SEQUENCE_TEMPLATE_MAX_STEPS; ++i) {
        const dm_sequence_step_t *step = &cfg->steps[i];
        if (step_matches(step, topic, payload)) {
            match_index = i;
            matched = step;
            break;
        }
    }
    if (match_index < 0) {
        return action;
    }
    action.step = matched;

    if (match_index != rt->current_index) {
        if (!cfg->reset_on_error) {
            return action;
        }
        dm_sequence_runtime_reset(rt);
        action.type = DM_SEQUENCE_EVENT_FAILED;
        return action;
    }

    rt->current_index++;
    rt->last_step_ms = now_ms;
    if (rt->current_index >= cfg->step_count) {
        dm_sequence_runtime_reset(rt);
        action.type = DM_SEQUENCE_EVENT_COMPLETED;
    } else {
        action.type = DM_SEQUENCE_EVENT_STEP_OK;
    }
    return action;
}
