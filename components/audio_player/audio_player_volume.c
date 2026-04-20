#include "audio_player_internal.h"

#include "nvs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "audio_player";

static int s_volume = 70;
static bool s_volume_loaded = false;
static TaskHandle_t s_save_task = NULL;
static volatile int s_pending_volume = -1;

static void save_volume_to_nvs(uint8_t vol)
{
    nvs_handle_t h;
    if (nvs_open("audiocfg", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "vol", vol);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void save_task(void *param)
{
    (void)param;
    while (1) {
        int v = s_pending_volume;
        if (v >= 0) {
            s_pending_volume = -1;
            save_volume_to_nvs((uint8_t)v);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t audio_player_volume_init(void)
{
    nvs_handle_t h;
    if (nvs_open("audiocfg", NVS_READONLY, &h) == ESP_OK) {
        uint8_t v = 0;
        if (nvs_get_u8(h, "vol", &v) == ESP_OK) {
            s_volume = v;
            ESP_LOGI(TAG, "volume loaded from NVS: %d", v);
        }
        nvs_close(h);
    }

    s_volume_loaded = true;
    if (!s_save_task) {
        BaseType_t ok = xTaskCreate(save_task, "audio_save", 2048, NULL, 4, &s_save_task);
        if (ok != pdPASS) {
            s_save_task = NULL;
            ESP_LOGE(TAG, "audio_save task create failed");
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

int audio_player_runtime_volume(void)
{
    return s_volume;
}

int *audio_player_volume_ptr(void)
{
    return &s_volume;
}

esp_err_t audio_player_set_volume(int percent)
{
    if (percent < 0) {
        percent = 0;
    } else if (percent > 100) {
        percent = 100;
    }
    if (percent == s_volume) {
        return ESP_OK;
    }

    s_volume = percent;
    audio_player_status_set_volume(s_volume);
    if (s_volume_loaded) {
        s_pending_volume = s_volume;
    }
    return ESP_OK;
}

int audio_player_get_volume(void)
{
    return s_volume;
}
