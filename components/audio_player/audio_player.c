#include "audio_player.h"
#include "audio_player_internal.h"

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "event_bus.h"
#include "esp_check.h"
#include "error_monitor.h"
#include "sd_storage.h"

#define AUDIO_QUEUE_LEN   4

static const char *TAG = "audio_player";

static volatile bool s_paused = false;
static QueueHandle_t s_queue = NULL;
static TaskHandle_t s_task = NULL;
static TaskHandle_t s_reader_task = NULL;
static volatile bool s_reader_done = false;
static volatile bool s_stop_requested = false;
static int s_last_bitrate_kbps = 0;

static void stop_reader(void)
{
    s_stop_requested = true;
    if (s_reader_task) {
        // wait a bit for reader to exit
        for (int i = 0; i < 50 && !s_reader_done; ++i) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (!s_reader_done) {
            vTaskDelete(s_reader_task); // force kill if stuck
        }
        s_reader_task = NULL;
    }
    s_reader_done = true;
    audio_player_status_set_playing(false);
}

static void audio_task(void *param)
{
    audio_cmd_t cmd;
    while (xQueueReceive(s_queue, &cmd, portMAX_DELAY) == pdTRUE) {
        int v = cmd.volume >= 0 ? cmd.volume : audio_player_runtime_volume();
#if AUDIO_PLAYER_DEBUG
        ESP_LOGI(TAG, "play request: %s vol=%d", cmd.path, v);
#endif
        if (!sd_storage_available()) {
            ESP_LOGE(TAG, "SD not mounted, beep");
            audio_player_status_set_playing(false);
            audio_player_output_play_tone(660, 150, v);
            audio_player_output_play_tone(440, 150, v);
            error_monitor_report_sd_fault();
            continue;
        }
        s_paused = false;
        audio_player_status_set_paused(false);
        if (audio_player_output_channel()) {
            esp_err_t en = audio_player_output_enable();
            if (en != ESP_OK) {
                ESP_LOGW(TAG, "i2s enable failed: %s", esp_err_to_name(en));
            }
        }
        audio_cmd_t *pcmd = malloc(sizeof(audio_cmd_t));
        if (!pcmd) {
            ESP_LOGE(TAG, "no mem for cmd");
            error_monitor_report_audio_fault();
            continue;
        }
        *pcmd = cmd;
        stop_reader();
        BaseType_t ok = xTaskCreatePinnedToCore(audio_player_reader_task, "audio_reader", 8192, pcmd, 6, &s_reader_task, 1);
        if (ok != pdPASS) {
            ESP_LOGE(TAG, "reader task create failed");
            free(pcmd);
            audio_player_output_play_tone(660, 150, v);
            audio_player_output_play_tone(440, 150, v);
            error_monitor_report_audio_fault();
            continue;
        }

    }
    vTaskDelete(NULL);
}

static void on_event(const event_bus_message_t *msg)
{
    if (!msg) {
        return;
    }
    switch (msg->type) {
    case EVENT_AUDIO_PLAY:
        audio_player_play(msg->payload);
        break;
    case EVENT_VOLUME_SET:
        ESP_LOGW(TAG, "ignoring volume cmd topic='%s' payload='%s'", msg->topic, msg->payload);
        break;
    case EVENT_WEB_COMMAND:
        break;
    case EVENT_NONE:
        break;
    case EVENT_CARD_OK:
    case EVENT_CARD_BAD:
    case EVENT_RELAY_CMD:
    case EVENT_SYSTEM_STATUS:
        break;
    default:
        break;
    }
}

void audio_player_stop(void)
{
    s_paused = false;
    s_last_bitrate_kbps = 0;
    audio_player_status_set_playing(false);
    stop_reader();
    if (audio_player_output_channel()) {
        // fully reset I2S channel to avoid hanging frames
        audio_player_output_reset();
    }
}

esp_err_t audio_player_init(void)
{
    ESP_RETURN_ON_ERROR(sd_storage_init(), TAG, "storage init failed");
    ESP_RETURN_ON_ERROR(audio_player_volume_init(), TAG, "volume init failed");
    ESP_RETURN_ON_ERROR(audio_player_status_init(), TAG, "status init failed");
    audio_player_status_reset(audio_player_runtime_volume());
    esp_err_t sd_err = sd_storage_mount();
    if (sd_err != ESP_OK) {
        ESP_LOGW(TAG, "sd mount failed, audio will beep only: %s", esp_err_to_name(sd_err));
    }
    ESP_RETURN_ON_ERROR(audio_player_output_init(), TAG, "audio output init failed");
    if (!s_queue) {
        s_queue = xQueueCreate(AUDIO_QUEUE_LEN, sizeof(audio_cmd_t));
    }
    ESP_ERROR_CHECK(event_bus_register_handler(on_event));
    return s_queue ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t audio_player_start(void)
{
    if (!s_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_task) {
        BaseType_t ok = xTaskCreate(audio_task, "audio_task", 8192, NULL, 6, &s_task);
        if (ok != pdPASS) {
            return ESP_FAIL;
        }
    }
    ESP_LOGI(TAG, "audio player started");
    return ESP_OK;
}

esp_err_t audio_player_play(const char *path)
{
    if (!path) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    audio_cmd_t cmd = {0};
    strncpy(cmd.path, path, sizeof(cmd.path) - 1);
    cmd.path[sizeof(cmd.path) - 1] = 0;
    cmd.volume = -1; // use current volume
    cmd.seek_ratio = -1.0f;
    return xQueueSend(s_queue, &cmd, pdMS_TO_TICKS(50)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t audio_player_play_seek(const char *path, float seek_ratio)
{
    if (!path) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    audio_cmd_t cmd = {0};
    strncpy(cmd.path, path, sizeof(cmd.path) - 1);
    cmd.path[sizeof(cmd.path) - 1] = 0;
    cmd.volume = -1;
    if (seek_ratio < 0.0f) {
        cmd.seek_ratio = -1.0f;
    } else {
        if (seek_ratio > 1.0f) {
            seek_ratio = 1.0f;
        }
        cmd.seek_ratio = seek_ratio;
    }
    return xQueueSend(s_queue, &cmd, pdMS_TO_TICKS(50)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

bool audio_player_stop_requested(void)
{
    return s_stop_requested;
}

void audio_player_wait_while_paused(void)
{
    while (s_paused && !s_stop_requested) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

int *audio_player_runtime_bitrate_ptr(void)
{
    return &s_last_bitrate_kbps;
}

void audio_player_reader_begin(void)
{
    s_reader_done = false;
    s_stop_requested = false;
}

void audio_player_reader_finished(void)
{
    s_reader_done = true;
    s_reader_task = NULL;
}

esp_err_t audio_player_seek(uint32_t pos_ms)
{
    if (!s_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    char current_path[256] = {0};
    float ratio = -1.0f;
    bool can_seek = false;
    int dur_ms = 0;
    can_seek = audio_player_status_prepare_seek(current_path, sizeof(current_path), &dur_ms);
    if (can_seek) {
        if (dur_ms > 0) {
            ratio = (float)pos_ms / (float)dur_ms;
            if (ratio < 0.0f) ratio = 0.0f;
            if (ratio > 1.0f) ratio = 1.0f;
        }
        if (ratio >= 0.0f) {
            int progress = (dur_ms > 0) ? (int)((float)pos_ms * 100.0f / (float)dur_ms) : 0;
            audio_player_status_set_seek_position(pos_ms, progress);
        }
    }
    if (!can_seek || ratio < 0.0f) {
        return ESP_ERR_INVALID_STATE;
    }
    audio_cmd_t cmd = {0};
    strncpy(cmd.path, current_path, sizeof(cmd.path) - 1);
    cmd.path[sizeof(cmd.path) - 1] = 0;
    cmd.volume = -1;
    cmd.seek_ratio = ratio;
    return xQueueSend(s_queue, &cmd, pdMS_TO_TICKS(50)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

void audio_player_pause(void)
{
    if (s_paused) {
        return;
    }
    s_paused = true;
    if (audio_player_output_channel()) {
        audio_player_output_pause();
    }
    audio_player_status_set_paused(true);
    s_last_bitrate_kbps = 0;
}

void audio_player_resume(void)
{
    if (!s_paused) {
        return;
    }
    s_paused = false;
    if (audio_player_output_channel()) {
        audio_player_output_resume();
    }
    audio_player_status_set_paused(false);
}

void audio_player_get_status(audio_player_status_t *out)
{
    audio_player_status_get(out);
}
