#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "device_manager.h"
#include "dm_templates.h"

typedef enum {
    DM_SEQUENCE_EVENT_NONE = 0,
    DM_SEQUENCE_EVENT_STEP_OK,
    DM_SEQUENCE_EVENT_COMPLETED,
    DM_SEQUENCE_EVENT_FAILED,
} dm_sequence_event_type_t;

typedef struct {
    dm_sequence_template_t config;
    uint8_t current_index;
    uint64_t last_step_ms;
} dm_sequence_runtime_t;

typedef struct {
    dm_sequence_event_type_t type;
    const dm_sequence_step_t *step;
    bool timeout;
} dm_sequence_action_t;

void dm_sequence_runtime_init(dm_sequence_runtime_t *rt, const dm_sequence_template_t *tpl);
void dm_sequence_runtime_reset(dm_sequence_runtime_t *rt);
dm_sequence_action_t dm_sequence_runtime_handle(dm_sequence_runtime_t *rt,
                                                const char *topic,
                                                const char *payload,
                                                uint64_t now_ms);
