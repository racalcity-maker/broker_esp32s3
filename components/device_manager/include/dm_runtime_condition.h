#pragma once

#include "device_manager.h"
#include "dm_templates.h"

typedef struct {
    dm_condition_template_t config;
    struct {
        bool valid;
        bool state;
        char flag[DEVICE_MANAGER_FLAG_NAME_MAX_LEN];
    } rules[DM_CONDITION_TEMPLATE_MAX_RULES];
    bool last_result;
    bool has_last_result;
} dm_condition_runtime_t;

void dm_condition_runtime_init(dm_condition_runtime_t *rt, const dm_condition_template_t *tpl);
bool dm_condition_runtime_handle_flag(dm_condition_runtime_t *rt,
                                      const char *flag_name,
                                      bool new_state,
                                      bool *result_changed,
                                      bool *current_result);
