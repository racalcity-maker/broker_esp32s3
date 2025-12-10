#pragma once

#include "device_manager.h"
#include "dm_templates.h"

typedef struct {
    dm_interval_task_template_t config;
} dm_interval_task_runtime_t;

void dm_interval_task_runtime_init(dm_interval_task_runtime_t *rt, const dm_interval_task_template_t *tpl);
