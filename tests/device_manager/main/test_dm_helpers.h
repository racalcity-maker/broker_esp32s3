#pragma once

#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "device_manager_internal.h"
#include "esp_heap_caps.h"

static inline device_manager_config_t *dm_test_alloc_config(uint8_t capacity)
{
    uint8_t cap = capacity;
    if (cap == 0 || cap > DEVICE_MANAGER_MAX_DEVICES) {
        cap = DEVICE_MANAGER_MAX_DEVICES;
    }
    size_t bytes = sizeof(device_manager_config_t) + sizeof(device_descriptor_t) * cap;
    device_manager_config_t *cfg =
        (device_manager_config_t *)heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!cfg) {
        cfg = (device_manager_config_t *)heap_caps_calloc(1, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    TEST_ASSERT_NOT_NULL_MESSAGE(cfg, "device manager test config alloc failed");
    cfg->device_capacity = cap;
    return cfg;
}

static inline void dm_test_free_config(device_manager_config_t *cfg)
{
    if (cfg) {
        heap_caps_free(cfg);
    }
}

static inline device_manager_config_t *dm_test_parse_config_json(const char *json, uint8_t capacity)
{
    device_manager_config_t *cfg = dm_test_alloc_config(capacity);
    dm_cjson_install_hooks();
    cJSON *root = cJSON_Parse(json);
    dm_cjson_reset_hooks();
    TEST_ASSERT_NOT_NULL(root);
    bool ok = dm_populate_config_from_json(cfg, root);
    cJSON_Delete(root);
    TEST_ASSERT_TRUE(ok);
    return cfg;
}

static inline const device_descriptor_t *dm_test_find_device(const device_manager_config_t *cfg, const char *id)
{
    if (!cfg || !id) {
        return NULL;
    }
    for (uint8_t i = 0; i < cfg->device_count; ++i) {
        const device_descriptor_t *dev = &cfg->devices[i];
        if (dev->id[0] && strcmp(dev->id, id) == 0) {
            return dev;
        }
    }
    return NULL;
}

static inline const device_descriptor_t *dm_test_require_template(device_manager_config_t *cfg,
                                                                  const char *id,
                                                                  dm_template_type_t type)
{
    const device_descriptor_t *dev = dm_test_find_device(cfg, id);
    TEST_ASSERT_NOT_NULL(dev);
    TEST_ASSERT_TRUE_MESSAGE(dev->template_assigned, "template not assigned");
    TEST_ASSERT_EQUAL(type, dev->template_config.type);
    return dev;
}
