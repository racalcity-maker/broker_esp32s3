#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifndef AUDIO_PLAYER_DEBUG
#define AUDIO_PLAYER_DEBUG 0
#endif

typedef enum {
    AUDIO_PLAYER_FMT_UNKNOWN = 0,
    AUDIO_PLAYER_FMT_WAV,
    AUDIO_PLAYER_FMT_MP3,
    AUDIO_PLAYER_FMT_OGG,
} audio_player_format_t;

typedef struct {
    bool playing;
    bool paused;
    int volume;
    int progress;   // 0-100, bytes-based estimate
    int pos_ms;     // elapsed, ms (best effort)
    int dur_ms;     // estimated duration, ms (0 if unknown)
    int bitrate_kbps;
    char path[256];
    char message[64];
    audio_player_format_t fmt;
} audio_player_status_t;

esp_err_t audio_player_init(void);
esp_err_t audio_player_start(void);
esp_err_t audio_player_play(const char *path);
esp_err_t audio_player_seek(uint32_t pos_ms);
esp_err_t audio_player_set_volume(int percent);
esp_err_t audio_player_mount_sd(void);
void audio_player_stop(void);
void audio_player_pause(void);
void audio_player_resume(void);
int audio_player_get_volume(void);
void audio_player_get_status(audio_player_status_t *out);
