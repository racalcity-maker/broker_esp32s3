#include "dm_runtime_service.h"

#include "esp_log.h"

#include "dm_template_runtime.h"

static const char *TAG = "dm_runtime_srv";

esp_err_t dm_runtime_service_init(void)
{
    return dm_template_runtime_init();
}

esp_err_t dm_runtime_service_rebuild(const device_manager_config_t *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "rebuild runtime from config");
    dm_template_runtime_reset();

    uint8_t limit = cfg->device_capacity ? cfg->device_capacity : DEVICE_MANAGER_MAX_DEVICES;
    for (uint8_t i = 0; i < cfg->device_count && i < limit; ++i) {
        const device_descriptor_t *dev = &cfg->devices[i];
        if (!dev->template_assigned) {
            continue;
        }
        esp_err_t err = dm_template_runtime_register(&dev->template_config, dev->id);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "template runtime register failed for %s: %s", dev->id, esp_err_to_name(err));
        }
    }

    return ESP_OK;
}

esp_err_t dm_runtime_service_register_template(const dm_template_config_t *tpl, const char *device_id)
{
    if (!tpl || !device_id || !device_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    return dm_template_runtime_register(tpl, device_id);
}
