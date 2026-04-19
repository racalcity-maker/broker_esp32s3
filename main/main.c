#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_err.h"

#include "config_store.h"
#include "network.h"
#include "mqtt_core.h"
#include "web_ui.h"
#include "audio_player.h"
#include "event_bus.h"
#include "error_monitor.h"
#include "device_manager.h"
#include "automation_engine.h"
#include "ota_manager.h"
#include "service_status.h"
#include "esp_task_wdt.h"
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

static const char *TAG = "app_main";

#if (configUSE_TRACE_FACILITY == 1)
static void stats_task(void *param)
{
    (void)param;
    const TickType_t period = pdMS_TO_TICKS(10000);
    vTaskDelay(period);
    while (1) {
        UBaseType_t count = uxTaskGetNumberOfTasks();
        TaskStatus_t *tasks = calloc(count, sizeof(TaskStatus_t));
        uint32_t total = 0;
        uint32_t idle = 0;
        if (tasks) {
            count = uxTaskGetSystemState(tasks, count, &total);
            for (UBaseType_t i = 0; i < count; ++i) {
                const char *name = tasks[i].pcTaskName;
                if (name && strstr(name, "IDLE")) {
                    idle += tasks[i].ulRunTimeCounter;
                }
            }
        }

        float cpu = 0.0f;
        if (total > 0 && idle <= total) {
            cpu = ((float)(total - idle) * 100.0f) / (float)total;
        }
        uint32_t heap_int = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        uint32_t heap_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        uint32_t min_int = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (heap_int < 50 * 1024 || min_int < 50 * 1024) {
            ESP_LOGW("stats", "low heap: heap_int=%" PRIu32 " min_int=%" PRIu32 " psram=%" PRIu32 " cpu=%.1f%% tasks=%" PRIu32,
                     heap_int, min_int, heap_spiram, cpu, (uint32_t)count);
        }
        if (tasks) {
            free(tasks);
        }
        vTaskDelay(period);
    }
}
#endif  // configUSE_TRACE_FACILITY

static void device_manager_boot_task(void *param)
{
    bool wdt_registered = false;
    esp_err_t wdt_err = esp_task_wdt_add(NULL);
    if (wdt_err == ESP_OK) {
        wdt_registered = true;
    } else {
        ESP_LOGW(TAG, "failed to add dm_boot to task WDT: %s", esp_err_to_name(wdt_err));
    }
    ESP_LOGI(TAG, "device manager bootstrap start");
    esp_err_t err = device_manager_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "device_manager_init failed: %s", esp_err_to_name(err));
        goto exit;
    }
    ESP_LOGI(TAG, "device manager ready, init automation engine");
    err = automation_engine_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "automation_engine_init failed: %s", esp_err_to_name(err));
        goto exit;
    }
    err = automation_engine_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "automation_engine_start failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "automation engine started");
        ota_manager_notify_system_ready();
    }
exit:
    if (wdt_registered) {
        esp_task_wdt_delete(NULL);
    }
    vTaskDelete(NULL);
}

static bool app_init_optional(const char *name, service_status_id_t id, esp_err_t (*fn)(void))
{
    esp_err_t err = fn ? fn() : ESP_ERR_INVALID_ARG;
    service_status_mark_init(id, err);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "init %s ok", name ? name : "optional");
        return true;
    }
    ESP_LOGE(TAG, "init %s failed: %s", name ? name : "optional", esp_err_to_name(err));
    return false;
}

static bool app_start_optional(const char *name, service_status_id_t id, esp_err_t (*fn)(void))
{
    esp_err_t err = fn ? fn() : ESP_ERR_INVALID_ARG;
    service_status_mark_start(id, err);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "start %s ok", name ? name : "optional");
        return true;
    }
    ESP_LOGE(TAG, "start %s failed: %s", name ? name : "optional", esp_err_to_name(err));
    return false;
}

void app_main(void)
{
    static const esp_task_wdt_config_t twdt_cfg = {
        .timeout_ms = 10000,
        .idle_core_mask = 0,
        .trigger_panic = false,
    };
    esp_task_wdt_init(&twdt_cfg);

    ESP_LOGI(TAG, "init nvs_flash");
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_LOGI(TAG, "init ota_manager");
    ESP_ERROR_CHECK(ota_manager_init());
    ESP_LOGI(TAG, "ota boot notify");
    ESP_ERROR_CHECK(ota_manager_notify_boot());
    ESP_LOGI(TAG, "init config_store");
    ESP_ERROR_CHECK(config_store_init());
    ESP_LOGI(TAG, "init event_bus");
    ESP_ERROR_CHECK(event_bus_init());
    ESP_LOGI(TAG, "init service_status");
    ESP_ERROR_CHECK(service_status_init());
    ESP_LOGI(TAG, "init error_monitor");
    ESP_ERROR_CHECK(error_monitor_init());
    ESP_LOGI(TAG, "init optional services");
    bool network_ok = app_init_optional("network", SERVICE_STATUS_NETWORK, network_init);
    bool mqtt_ok = app_init_optional("mqtt_core", SERVICE_STATUS_MQTT, mqtt_core_init);
    bool audio_ok = app_init_optional("audio_player", SERVICE_STATUS_AUDIO, audio_player_init);
    bool web_ui_ok = app_init_optional("web_ui", SERVICE_STATUS_WEB_UI, web_ui_init);

    if (!audio_ok) {
        error_monitor_report_audio_fault();
    }

    ESP_LOGI(TAG, "start event_bus");
    event_bus_start();
    if (network_ok) {
        ESP_LOGI(TAG, "start network");
        app_start_optional("network", SERVICE_STATUS_NETWORK, network_start);
    } else {
        ESP_LOGW(TAG, "skip network start: init failed");
    }
    if (mqtt_ok) {
        ESP_LOGI(TAG, "start mqtt_core");
        app_start_optional("mqtt_core", SERVICE_STATUS_MQTT, mqtt_core_start);
    } else {
        ESP_LOGW(TAG, "skip mqtt_core start: init failed");
    }
    if (audio_ok) {
        ESP_LOGI(TAG, "start audio_player");
        if (!app_start_optional("audio_player", SERVICE_STATUS_AUDIO, audio_player_start)) {
            error_monitor_report_audio_fault();
        }
    } else {
        ESP_LOGW(TAG, "skip audio_player start: init failed");
    }
    if (web_ui_ok) {
        ESP_LOGI(TAG, "start web_ui");
        app_start_optional("web_ui", SERVICE_STATUS_WEB_UI, web_ui_start);
    } else {
        ESP_LOGW(TAG, "skip web_ui start: init failed");
    }

#if (configUSE_TRACE_FACILITY == 1)
    xTaskCreate(stats_task, "stats_task", 4096, NULL, 3, NULL);
#else
    ESP_LOGW(TAG, "stats task disabled (configUSE_TRACE_FACILITY not enabled)");
#endif
    ESP_LOGI(TAG, "Broker skeleton started");
    ESP_LOGI(TAG, "create device manager bootstrap task");
    xTaskCreate(device_manager_boot_task, "dm_boot", 8192, NULL, 3, NULL);
    vTaskDelete(NULL);
}
