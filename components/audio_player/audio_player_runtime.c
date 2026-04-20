#include "audio_player_internal.h"

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "error_monitor.h"
#include "sd_storage.h"

#define AUDIO_QUEUE_LEN 4
#define AUDIO_READER_STOP_TIMEOUT_MS 500
#define AUDIO_FLAG_PAUSED         BIT0
#define AUDIO_FLAG_STOP_REQUESTED BIT1

static const char *TAG = "audio_player";

static QueueHandle_t s_queue = NULL;
static TaskHandle_t s_task = NULL;
static TaskHandle_t s_reader_task = NULL;
static TaskHandle_t s_reader_waiter = NULL;
static SemaphoreHandle_t s_runtime_lock = NULL;
static EventGroupHandle_t s_audio_flags = NULL;
static bool s_reader_done = true;
static int s_last_bitrate_kbps = 0;
static audio_runtime_state_t s_runtime_state = AUDIO_RUNTIME_IDLE;
static char s_active_path[256] = {0};

static void audio_flags_set(EventBits_t flags)
{
    if (s_audio_flags) {
        xEventGroupSetBits(s_audio_flags, flags);
    }
}

static void audio_flags_clear(EventBits_t flags)
{
    if (s_audio_flags) {
        xEventGroupClearBits(s_audio_flags, flags);
    }
}

static bool runtime_lock(void)
{
    if (!s_runtime_lock) {
        return false;
    }
    return xSemaphoreTake(s_runtime_lock, portMAX_DELAY) == pdTRUE;
}

static void runtime_unlock(void)
{
    if (s_runtime_lock) {
        xSemaphoreGive(s_runtime_lock);
    }
}

static void runtime_set_state(audio_runtime_state_t state)
{
    audio_player_status_set_runtime_state(state);
    if (runtime_lock()) {
        s_runtime_state = state;
        runtime_unlock();
        return;
    }
    s_runtime_state = state;
}

static void runtime_set_active_path(const char *path)
{
    if (runtime_lock()) {
        if (path && path[0]) {
            strncpy(s_active_path, path, sizeof(s_active_path) - 1);
            s_active_path[sizeof(s_active_path) - 1] = 0;
        } else {
            s_active_path[0] = 0;
        }
        runtime_unlock();
        return;
    }
    if (path && path[0]) {
        strncpy(s_active_path, path, sizeof(s_active_path) - 1);
        s_active_path[sizeof(s_active_path) - 1] = 0;
    } else {
        s_active_path[0] = 0;
    }
}

static void runtime_clear_active_path_if_matches(const char *path)
{
    if (runtime_lock()) {
        if (!path || !path[0] || strcmp(s_active_path, path) == 0) {
            s_active_path[0] = 0;
        }
        runtime_unlock();
        return;
    }
    if (!path || !path[0] || strcmp(s_active_path, path) == 0) {
        s_active_path[0] = 0;
    }
}

static audio_runtime_state_t runtime_get_state(void)
{
    audio_runtime_state_t state = s_runtime_state;
    if (runtime_lock()) {
        state = s_runtime_state;
        runtime_unlock();
    }
    return state;
}

static bool stop_reader(void)
{
    TaskHandle_t waiter = xTaskGetCurrentTaskHandle();
    bool wait_needed = false;

    audio_flags_set(AUDIO_FLAG_STOP_REQUESTED);
    runtime_set_state(AUDIO_RUNTIME_STOPPING);

    if (runtime_lock()) {
        if (!s_reader_task) {
            s_reader_done = true;
            s_reader_waiter = NULL;
        } else if (!s_reader_done) {
            s_reader_waiter = waiter;
            wait_needed = true;
        }
        runtime_unlock();
    }

    if (wait_needed) {
        (void)ulTaskNotifyTake(pdTRUE, 0);
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(AUDIO_READER_STOP_TIMEOUT_MS)) == 0) {
            ESP_LOGW(TAG, "reader stop timeout");
            audio_player_status_set_message("Audio stop timeout");
            error_monitor_report_audio_fault();
            if (runtime_lock()) {
                if (s_reader_waiter == waiter) {
                    s_reader_waiter = NULL;
                }
                runtime_unlock();
            }
            runtime_set_state(AUDIO_RUNTIME_ERROR);
            return false;
        }
    }

    runtime_set_state(AUDIO_RUNTIME_IDLE);
    return true;
}

static void handle_pause(void)
{
    audio_runtime_state_t state = runtime_get_state();
    if (state != AUDIO_RUNTIME_PLAYING && state != AUDIO_RUNTIME_STARTING) {
        return;
    }
    audio_flags_set(AUDIO_FLAG_PAUSED);
    if (audio_player_output_channel()) {
        audio_player_output_pause();
    }
    s_last_bitrate_kbps = 0;
    runtime_set_state(AUDIO_RUNTIME_PAUSED);
}

static void handle_resume(void)
{
    if (runtime_get_state() != AUDIO_RUNTIME_PAUSED) {
        return;
    }
    audio_flags_clear(AUDIO_FLAG_PAUSED);
    if (audio_player_output_channel()) {
        audio_player_output_resume();
    }
    runtime_set_state(AUDIO_RUNTIME_PLAYING);
}

static void handle_stop(void)
{
    audio_flags_clear(AUDIO_FLAG_PAUSED);
    s_last_bitrate_kbps = 0;
    if (stop_reader() && audio_player_output_channel()) {
        audio_player_output_reset();
    }
    runtime_set_active_path(NULL);
    runtime_set_state(AUDIO_RUNTIME_IDLE);
}

static void handle_play(const audio_cmd_t *cmd)
{
    int volume = cmd->volume >= 0 ? cmd->volume : audio_player_runtime_volume();

    if (!sd_storage_available()) {
        ESP_LOGE(TAG, "SD not mounted, beep");
        audio_player_output_play_tone(660, 150, volume);
        audio_player_output_play_tone(440, 150, volume);
        error_monitor_report_sd_fault();
        runtime_set_state(AUDIO_RUNTIME_ERROR);
        return;
    }

    audio_flags_clear(AUDIO_FLAG_PAUSED);
    if (audio_player_output_channel()) {
        esp_err_t err = audio_player_output_enable();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "i2s enable failed: %s", esp_err_to_name(err));
        }
    }

    audio_reader_ctx_t *reader_ctx = audio_player_runtime_create_reader_ctx(cmd);
    if (!reader_ctx) {
        ESP_LOGE(TAG, "no mem for reader ctx");
        error_monitor_report_audio_fault();
        runtime_set_state(AUDIO_RUNTIME_ERROR);
        return;
    }

    if (!stop_reader()) {
        audio_player_runtime_destroy_reader_ctx(reader_ctx);
        return;
    }

    TaskHandle_t reader_task = NULL;
    runtime_set_state(AUDIO_RUNTIME_STARTING);
    runtime_set_active_path(cmd->path);
    BaseType_t ok = xTaskCreatePinnedToCore(
        audio_player_reader_task, "audio_reader", 8192, reader_ctx, 6, &reader_task, 1);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "reader task create failed");
        audio_player_runtime_destroy_reader_ctx(reader_ctx);
        audio_player_output_play_tone(660, 150, volume);
        audio_player_output_play_tone(440, 150, volume);
        error_monitor_report_audio_fault();
        runtime_set_active_path(NULL);
        runtime_set_state(AUDIO_RUNTIME_ERROR);
        return;
    }

    if (runtime_lock()) {
        s_reader_task = reader_task;
        s_reader_done = false;
        s_reader_waiter = NULL;
        runtime_unlock();
    }
}

static void audio_runtime_task(void *param)
{
    audio_cmd_t cmd;
    while (xQueueReceive(s_queue, &cmd, portMAX_DELAY) == pdTRUE) {
        switch (cmd.type) {
        case AUDIO_CMD_PLAY:
        case AUDIO_CMD_SEEK:
            handle_play(&cmd);
            break;
        case AUDIO_CMD_STOP:
            handle_stop();
            break;
        case AUDIO_CMD_PAUSE:
            handle_pause();
            break;
        case AUDIO_CMD_RESUME:
            handle_resume();
            break;
        case AUDIO_CMD_NONE:
        default:
            break;
        }
    }
    vTaskDelete(NULL);
}

esp_err_t audio_player_runtime_init(void)
{
    if (!s_runtime_lock) {
        s_runtime_lock = xSemaphoreCreateMutex();
    }
    ESP_RETURN_ON_FALSE(s_runtime_lock != NULL, ESP_ERR_NO_MEM, TAG, "runtime lock init failed");
    if (!s_audio_flags) {
        s_audio_flags = xEventGroupCreate();
    }
    ESP_RETURN_ON_FALSE(s_audio_flags != NULL, ESP_ERR_NO_MEM, TAG, "audio flags init failed");
    if (!s_queue) {
        s_queue = xQueueCreate(AUDIO_QUEUE_LEN, sizeof(audio_cmd_t));
    }
    ESP_RETURN_ON_FALSE(s_queue != NULL, ESP_ERR_NO_MEM, TAG, "audio queue init failed");
    audio_flags_clear(AUDIO_FLAG_PAUSED | AUDIO_FLAG_STOP_REQUESTED);
    s_last_bitrate_kbps = 0;
    if (runtime_lock()) {
        s_reader_task = NULL;
        s_reader_waiter = NULL;
        s_reader_done = true;
        audio_player_status_set_runtime_state(AUDIO_RUNTIME_IDLE);
        s_runtime_state = AUDIO_RUNTIME_IDLE;
        s_active_path[0] = 0;
        runtime_unlock();
    } else {
        s_reader_task = NULL;
        s_reader_waiter = NULL;
        s_reader_done = true;
        runtime_set_state(AUDIO_RUNTIME_IDLE);
        s_active_path[0] = 0;
    }
    return ESP_OK;
}

esp_err_t audio_player_runtime_start(void)
{
    if (!s_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_task) {
        BaseType_t ok = xTaskCreate(audio_runtime_task, "audio_task", 8192, NULL, 6, &s_task);
        if (ok != pdPASS) {
            return ESP_FAIL;
        }
    }
    ESP_LOGI(TAG, "audio runtime started");
    return ESP_OK;
}

esp_err_t audio_player_runtime_enqueue(const audio_cmd_t *cmd)
{
    if (!cmd) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    return xQueueSend(s_queue, cmd, pdMS_TO_TICKS(50)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

audio_reader_ctx_t *audio_player_runtime_create_reader_ctx(const audio_cmd_t *cmd)
{
    if (!cmd) {
        return NULL;
    }
    audio_reader_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        return NULL;
    }
    ctx->cmd = *cmd;
    ctx->flags = s_audio_flags;
    ctx->bitrate_kbps = &s_last_bitrate_kbps;
    ctx->volume_percent = audio_player_volume_ptr();
    return ctx;
}

void audio_player_runtime_destroy_reader_ctx(audio_reader_ctx_t *ctx)
{
    free(ctx);
}

esp_err_t audio_player_runtime_prepare_seek(audio_cmd_t *cmd, uint32_t pos_ms)
{
    char current_path[sizeof(s_active_path)] = {0};
    int dur_ms = 0;
    float ratio = -1.0f;

    if (!cmd) {
        return ESP_ERR_INVALID_ARG;
    }

    if (runtime_lock()) {
        if (s_active_path[0] != 0) {
            strncpy(current_path, s_active_path, sizeof(current_path) - 1);
            current_path[sizeof(current_path) - 1] = 0;
        }
        runtime_unlock();
    }

    if (current_path[0] == 0 ||
        !audio_player_status_prepare_seek(NULL, 0, &dur_ms)) {
        return ESP_ERR_INVALID_STATE;
    }

    if (dur_ms <= 0) {
        return ESP_ERR_INVALID_STATE;
    }

    ratio = (float)pos_ms / (float)dur_ms;
    if (ratio < 0.0f) {
        ratio = 0.0f;
    }
    if (ratio > 1.0f) {
        ratio = 1.0f;
    }

    memset(cmd, 0, sizeof(*cmd));
    cmd->type = AUDIO_CMD_SEEK;
    cmd->volume = -1;
    cmd->seek_ratio = ratio;
    strncpy(cmd->path, current_path, sizeof(cmd->path) - 1);
    cmd->path[sizeof(cmd->path) - 1] = 0;
    audio_player_status_set_seek_position((uint32_t)((float)dur_ms * ratio), (int)(ratio * 100.0f));
    return ESP_OK;
}

void audio_player_runtime_reader_started(const audio_reader_ctx_t *ctx)
{
    audio_flags_clear(AUDIO_FLAG_STOP_REQUESTED);
    if (runtime_lock()) {
        s_reader_done = false;
        s_reader_waiter = NULL;
        if (ctx && ctx->cmd.path[0]) {
            strncpy(s_active_path, ctx->cmd.path, sizeof(s_active_path) - 1);
            s_active_path[sizeof(s_active_path) - 1] = 0;
        }
        if (s_runtime_state == AUDIO_RUNTIME_STARTING) {
            audio_player_status_set_runtime_state(AUDIO_RUNTIME_PLAYING);
            s_runtime_state = AUDIO_RUNTIME_PLAYING;
        }
        runtime_unlock();
        return;
    }
    s_reader_done = false;
    if (ctx && ctx->cmd.path[0]) {
        strncpy(s_active_path, ctx->cmd.path, sizeof(s_active_path) - 1);
        s_active_path[sizeof(s_active_path) - 1] = 0;
    }
    runtime_set_state(AUDIO_RUNTIME_PLAYING);
}

void audio_player_runtime_reader_finished(audio_reader_ctx_t *ctx)
{
    TaskHandle_t waiter = NULL;

    runtime_clear_active_path_if_matches(ctx ? ctx->cmd.path : NULL);

    if (runtime_lock()) {
        s_reader_done = true;
        s_reader_task = NULL;
        waiter = s_reader_waiter;
        s_reader_waiter = NULL;
        audio_player_status_set_runtime_state(AUDIO_RUNTIME_IDLE);
        s_runtime_state = AUDIO_RUNTIME_IDLE;
        runtime_unlock();
    } else {
        s_reader_done = true;
        s_reader_task = NULL;
        runtime_set_state(AUDIO_RUNTIME_IDLE);
    }

    if (waiter) {
        xTaskNotifyGive(waiter);
    }
}

bool audio_player_reader_stop_requested(const audio_reader_ctx_t *ctx)
{
    if (!ctx || !ctx->flags) {
        return false;
    }
    return (xEventGroupGetBits(ctx->flags) & AUDIO_FLAG_STOP_REQUESTED) != 0;
}

void audio_player_reader_wait_while_paused(const audio_reader_ctx_t *ctx)
{
    if (!ctx || !ctx->flags) {
        return;
    }
    while ((xEventGroupGetBits(ctx->flags) & AUDIO_FLAG_PAUSED) != 0 &&
           (xEventGroupGetBits(ctx->flags) & AUDIO_FLAG_STOP_REQUESTED) == 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

int *audio_player_reader_bitrate_ptr(const audio_reader_ctx_t *ctx)
{
    return ctx ? ctx->bitrate_kbps : NULL;
}

int audio_player_reader_volume(const audio_reader_ctx_t *ctx)
{
    int volume = 100;
    if (ctx && ctx->volume_percent) {
        volume = *ctx->volume_percent;
    }
    if (volume < 0) {
        volume = 0;
    }
    if (volume > 100) {
        volume = 100;
    }
    return volume;
}
