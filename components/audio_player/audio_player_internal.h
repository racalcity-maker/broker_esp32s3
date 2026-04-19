#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>

#include "audio_player.h"
#include "driver/i2s_std.h"
#include "esp_err.h"

typedef enum {
    AUDIO_FMT_UNKNOWN = 0,
    AUDIO_FMT_WAV,
    AUDIO_FMT_MP3,
    AUDIO_FMT_OGG,
} audio_format_t;

typedef struct {
    char path[256];
    int volume;
    float seek_ratio;
} audio_cmd_t;

typedef struct {
    audio_format_t fmt;
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t bits_per_sample;
} audio_info_t;

esp_err_t audio_player_volume_init(void);
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
void audio_player_status_set_paused(bool paused);
void audio_player_status_set_playing(bool playing);
void audio_player_status_update_progress(size_t bytes_read, size_t total_bytes, uint32_t pos_ms, uint32_t est_total_ms);
void audio_player_status_update_bitrate(int kbps);
void audio_player_status_set_message(const char *msg);
void audio_player_status_set_volume(int volume);
void audio_player_status_get(audio_player_status_t *out);
void audio_player_status_mark_seek_play(const char *path, audio_format_t fmt);
bool audio_player_status_prepare_seek(char *path, size_t path_len, int *dur_ms);
void audio_player_status_set_seek_position(uint32_t pos_ms, int progress);

bool audio_player_stop_requested(void);
void audio_player_wait_while_paused(void);
int audio_player_runtime_volume(void);
int *audio_player_runtime_bitrate_ptr(void);
void audio_player_reader_begin(void);
void audio_player_reader_finished(void);
void audio_player_reader_task(void *param);
