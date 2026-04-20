#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>

#include "audio_player.h"
#include "driver/i2s_std.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

typedef enum {
    AUDIO_FMT_UNKNOWN = 0,
    AUDIO_FMT_WAV,
    AUDIO_FMT_MP3,
    AUDIO_FMT_OGG,
} audio_format_t;

typedef enum {
    AUDIO_CMD_NONE = 0,
    AUDIO_CMD_PLAY,
    AUDIO_CMD_SEEK,
    AUDIO_CMD_STOP,
    AUDIO_CMD_PAUSE,
    AUDIO_CMD_RESUME,
} audio_cmd_type_t;

typedef struct {
    audio_cmd_type_t type;
    char path[256];
    int volume;
    float seek_ratio;
} audio_cmd_t;

typedef enum {
    AUDIO_RUNTIME_IDLE = 0,
    AUDIO_RUNTIME_STARTING,
    AUDIO_RUNTIME_PLAYING,
    AUDIO_RUNTIME_PAUSED,
    AUDIO_RUNTIME_STOPPING,
    AUDIO_RUNTIME_ERROR,
} audio_runtime_state_t;

typedef struct {
    audio_format_t fmt;
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t bits_per_sample;
} audio_info_t;

typedef struct {
    audio_cmd_t cmd;
    EventGroupHandle_t flags;
    int *bitrate_kbps;
    int *volume_percent;
} audio_reader_ctx_t;

esp_err_t audio_player_volume_init(void);
int *audio_player_volume_ptr(void);
esp_err_t audio_player_output_init(void);
esp_err_t audio_player_output_enable(void);
void audio_player_output_disable(void);
void audio_player_output_pause(void);
void audio_player_output_resume(void);
void audio_player_output_reset(void);
void audio_player_output_play_tone(int freq_hz, int duration_ms, int volume_percent);
i2s_chan_handle_t audio_player_output_channel(void);

esp_err_t audio_player_status_init(void);
audio_player_format_t audio_player_to_public_format(audio_format_t fmt);
void audio_player_status_reset(int volume);
void audio_player_status_set_play(const char *path, audio_format_t fmt, int volume);
void audio_player_status_set_runtime_state(audio_runtime_state_t state);
void audio_player_status_update_progress(size_t bytes_read, size_t total_bytes, uint32_t pos_ms, uint32_t est_total_ms);
void audio_player_status_update_bitrate(int kbps);
void audio_player_status_set_message(const char *msg);
void audio_player_status_set_volume(int volume);
void audio_player_status_get(audio_player_status_t *out);
void audio_player_status_mark_seek_play(const char *path, audio_format_t fmt);
bool audio_player_status_prepare_seek(char *path, size_t path_len, int *dur_ms);
void audio_player_status_set_seek_position(uint32_t pos_ms, int progress);

int audio_player_runtime_volume(void);
esp_err_t audio_player_runtime_init(void);
esp_err_t audio_player_runtime_start(void);
esp_err_t audio_player_runtime_enqueue(const audio_cmd_t *cmd);
audio_reader_ctx_t *audio_player_runtime_create_reader_ctx(const audio_cmd_t *cmd);
void audio_player_runtime_destroy_reader_ctx(audio_reader_ctx_t *ctx);
esp_err_t audio_player_runtime_prepare_seek(audio_cmd_t *cmd, uint32_t pos_ms);
void audio_player_runtime_reader_started(const audio_reader_ctx_t *ctx);
void audio_player_runtime_reader_finished(audio_reader_ctx_t *ctx);
bool audio_player_reader_stop_requested(const audio_reader_ctx_t *ctx);
void audio_player_reader_wait_while_paused(const audio_reader_ctx_t *ctx);
int *audio_player_reader_bitrate_ptr(const audio_reader_ctx_t *ctx);
int audio_player_reader_volume(const audio_reader_ctx_t *ctx);
void audio_player_reader_task(void *param);
