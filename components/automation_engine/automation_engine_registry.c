#include "automation_engine_internal.h"

#include <strings.h>

#include "device_manager.h"

static const device_scenario_t *find_scenario_by_name(const device_descriptor_t *device, const char *id)
{
    if (!device || !id || !id[0]) {
        return NULL;
    }
    for (uint8_t i = 0; i < device->scenario_count; ++i) {
        const device_scenario_t *sc = &device->scenarios[i];
        if ((sc->id[0] && strcasecmp(sc->id, id) == 0) ||
            (sc->name[0] && strcasecmp(sc->name, id) == 0)) {
            return sc;
        }
    }
    return NULL;
}

const device_descriptor_t *automation_engine_find_device_by_id(const char *id)
{
    if (!id || !id[0]) {
        return NULL;
    }
    const device_manager_config_t *cfg = device_manager_lock_config();
    if (!cfg) {
        device_manager_unlock_config();
        return NULL;
    }
    const device_descriptor_t *result = NULL;
    uint8_t limit = cfg->device_capacity ? cfg->device_capacity : DEVICE_MANAGER_MAX_DEVICES;
    for (uint8_t i = 0; i < cfg->device_count && i < limit; ++i) {
        const device_descriptor_t *dev = &cfg->devices[i];
        if ((dev->id[0] && strcasecmp(dev->id, id) == 0) ||
            (dev->display_name[0] && strcasecmp(dev->display_name, id) == 0)) {
            result = dev;
            break;
        }
    }
    device_manager_unlock_config();
    return result;
}

const device_scenario_t *automation_engine_find_scenario_by_id(const device_descriptor_t *device, const char *id)
{
    return find_scenario_by_name(device, id);
}
