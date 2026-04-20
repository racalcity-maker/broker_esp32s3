#include "audio_player_internal.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static audio_player_status_t s_status = {0};
static SemaphoreHandle_t s_status_mutex = NULL;

static void status_lock(void)
{
    if (s_status_mutex) {
        xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    }
}

static void status_unlock(void)
{
    if (s_status_mutex) {
        xSemaphoreGive(s_status_mutex);
    }
}

esp_err_t audio_player_status_init(void)
{
    if (!s_status_mutex) {
        s_status_mutex = xSemaphoreCreateMutex();
    }
    return s_status_mutex ? ESP_OK : ESP_ERR_NO_MEM;
}

audio_player_format_t audio_player_to_public_format(audio_format_t fmt)
{
    switch (fmt) {
    case AUDIO_FMT_WAV: return AUDIO_PLAYER_FMT_WAV;
    case AUDIO_FMT_MP3: return AUDIO_PLAYER_FMT_MP3;
    case AUDIO_FMT_OGG: return AUDIO_PLAYER_FMT_OGG;
    default: return AUDIO_PLAYER_FMT_UNKNOWN;
    }
}

void audio_player_status_reset(int volume)
{
    status_lock();
    memset(&s_status, 0, sizeof(s_status));
    s_status.volume = volume;
    s_status.bitrate_kbps = 0;
    s_status.message[0] = 0;
    status_unlock();
}

void audio_player_status_set_play(const char *path, audio_format_t fmt, int volume)
{
    status_lock();
    s_status.playing = true;
    s_status.paused = false;
    s_status.progress = 0;
    s_status.pos_ms = 0;
    s_status.dur_ms = 0;
    s_status.bitrate_kbps = 0;
    s_status.volume = volume;
    s_status.fmt = audio_player_to_public_format(fmt);
    s_status.message[0] = 0;
    s_status.path[0] = 0;
    if (path) {
        strncpy(s_status.path, path, sizeof(s_status.path) - 1);
        s_status.path[sizeof(s_status.path) - 1] = 0;
    }
    status_unlock();
}

void audio_player_status_set_runtime_state(audio_runtime_state_t state)
{
    status_lock();
    switch (state) {
    case AUDIO_RUNTIME_STARTING:
    case AUDIO_RUNTIME_PLAYING:
        s_status.playing = true;
        s_status.paused = false;
        s_status.bitrate_kbps = 0;
        break;
    case AUDIO_RUNTIME_PAUSED:
        s_status.playing = true;
        s_status.paused = true;
        s_status.bitrate_kbps = 0;
        break;
    case AUDIO_RUNTIME_IDLE:
        s_status.playing = false;
        s_status.paused = false;
        s_status.progress = 0;
        s_status.pos_ms = 0;
        s_status.dur_ms = 0;
        s_status.bitrate_kbps = 0;
        s_status.fmt = AUDIO_PLAYER_FMT_UNKNOWN;
        s_status.path[0] = 0;
        break;
    case AUDIO_RUNTIME_STOPPING:
        s_status.playing = false;
        s_status.paused = false;
        s_status.progress = 0;
        s_status.pos_ms = 0;
        s_status.dur_ms = 0;
        s_status.bitrate_kbps = 0;
        break;
    case AUDIO_RUNTIME_ERROR:
    default:
        s_status.playing = false;
        s_status.paused = false;
        s_status.progress = 0;
        s_status.pos_ms = 0;
        s_status.dur_ms = 0;
        s_status.bitrate_kbps = 0;
        s_status.fmt = AUDIO_PLAYER_FMT_UNKNOWN;
        s_status.path[0] = 0;
        break;
    }
    status_unlock();
}

void audio_player_status_update_progress(size_t bytes_read, size_t total_bytes, uint32_t pos_ms, uint32_t est_total_ms)
{
    status_lock();
    if (total_bytes > 0) {
        s_status.progress = (int)((bytes_read * 100U) / total_bytes);
        if (s_status.progress > 100) {
            s_status.progress = 100;
        }
    }
    if (pos_ms > 0) {
        s_status.pos_ms = (int)pos_ms;
    }
    if (est_total_ms > 0) {
        s_status.dur_ms = (int)est_total_ms;
    }
    status_unlock();
}

void audio_player_status_update_bitrate(int kbps)
{
    status_lock();
    s_status.bitrate_kbps = kbps;
    status_unlock();
}

void audio_player_status_set_message(const char *msg)
{
    status_lock();
    if (msg) {
        strncpy(s_status.message, msg, sizeof(s_status.message) - 1);
        s_status.message[sizeof(s_status.message) - 1] = 0;
    } else {
        s_status.message[0] = 0;
    }
    status_unlock();
}

void audio_player_status_set_volume(int volume)
{
    status_lock();
    s_status.volume = volume;
    status_unlock();
}

void audio_player_status_get(audio_player_status_t *out)
{
    if (!out) {
        return;
    }
    status_lock();
    *out = s_status;
    status_unlock();
}

void audio_player_status_mark_seek_play(const char *path, audio_format_t fmt)
{
    status_lock();
    s_status.playing = true;
    s_status.paused = false;
    s_status.fmt = audio_player_to_public_format(fmt);
    s_status.message[0] = 0;
    if (path && path[0]) {
        strncpy(s_status.path, path, sizeof(s_status.path) - 1);
        s_status.path[sizeof(s_status.path) - 1] = 0;
    }
    status_unlock();
}

bool audio_player_status_prepare_seek(char *path, size_t path_len, int *dur_ms)
{
    bool can_seek = false;
    status_lock();
    if ((s_status.playing || s_status.paused) && s_status.path[0] != 0) {
        can_seek = true;
        if (path && path_len > 0) {
            strncpy(path, s_status.path, path_len - 1);
            path[path_len - 1] = 0;
        }
        if (dur_ms) {
            *dur_ms = s_status.dur_ms;
        }
    }
    status_unlock();
    return can_seek;
}

void audio_player_status_set_seek_position(uint32_t pos_ms, int progress)
{
    status_lock();
    s_status.pos_ms = (int)pos_ms;
    s_status.progress = progress;
    status_unlock();
}
