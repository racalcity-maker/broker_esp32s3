#include "dm_runtime_condition.h"

#include <string.h>
#include <strings.h>

#include "device_manager_utils.h"

static bool evaluate_condition(const dm_condition_runtime_t *rt, bool *ready)
{
    if (!rt || rt->config.rule_count == 0) {
        if (ready) {
            *ready = false;
        }
        return false;
    }
    bool have_ready = true;
    bool any_true = false;
    bool all_true = true;
    for (uint8_t i = 0; i < rt->config.rule_count && i < DM_CONDITION_TEMPLATE_MAX_RULES; ++i) {
        const dm_condition_rule_t *rule = &rt->config.rules[i];
        bool state_valid = rt->rules[i].valid;
        bool state = rt->rules[i].state;
        if (!state_valid) {
            have_ready = false;
            state = false;
        }
        bool matches = state == rule->required_state;
        any_true |= matches;
        all_true &= matches;
        if (rt->config.mode == DEVICE_CONDITION_ALL && !matches) {
            // continue to ensure ready flag computed
        }
    }
    if (ready) {
        *ready = have_ready;
    }
    return (rt->config.mode == DEVICE_CONDITION_ALL) ? all_true : any_true;
}

void dm_condition_runtime_init(dm_condition_runtime_t *rt, const dm_condition_template_t *tpl)
{
    if (!rt) {
        return;
    }
    if (tpl) {
        memcpy(&rt->config, tpl, sizeof(*tpl));
    } else {
        memset(&rt->config, 0, sizeof(rt->config));
    }
    memset(rt->rules, 0, sizeof(rt->rules));
    rt->has_last_result = false;
    rt->last_result = false;
    for (uint8_t i = 0; tpl && i < tpl->rule_count && i < DM_CONDITION_TEMPLATE_MAX_RULES; ++i) {
        dm_str_copy(rt->rules[i].flag, sizeof(rt->rules[i].flag), tpl->rules[i].flag);
    }
}

bool dm_condition_runtime_handle_flag(dm_condition_runtime_t *rt,
                                      const char *flag_name,
                                      bool new_state,
                                      bool *result_changed,
                                      bool *current_result)
{
    if (!rt || !flag_name || !flag_name[0]) {
        return false;
    }
    bool matched = false;
    for (uint8_t i = 0; i < rt->config.rule_count && i < DM_CONDITION_TEMPLATE_MAX_RULES; ++i) {
        if (rt->config.rules[i].flag[0] &&
            strcasecmp(rt->config.rules[i].flag, flag_name) == 0) {
            rt->rules[i].valid = true;
            rt->rules[i].state = new_state;
            matched = true;
        }
    }
    if (!matched) {
        return false;
    }
    bool ready = false;
    bool result = evaluate_condition(rt, &ready);
    bool changed = (!rt->has_last_result) || (result != rt->last_result);
    rt->last_result = result;
    rt->has_last_result = true;
    if (result_changed) {
        *result_changed = changed;
    }
    if (current_result) {
        *current_result = result;
    }
    return true;
}
