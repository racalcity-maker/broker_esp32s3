#include "dm_runtime_interval.h"

#include <string.h>

void dm_interval_task_runtime_init(dm_interval_task_runtime_t *rt, const dm_interval_task_template_t *tpl)
{
    if (!rt) {
        return;
    }
    if (tpl) {
        memcpy(&rt->config, tpl, sizeof(*tpl));
    } else {
        memset(&rt->config, 0, sizeof(rt->config));
    }
}
