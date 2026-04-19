#include "audio_player_internal.h"

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "event_bus.h"
#include "helix_mp3_wrapper.h"
#include "error_monitor.h"

#define AUDIO_FINISHED_POST_RETRIES 5
#define AUDIO_FINISHED_POST_WAIT_MS 20

#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_CHANNELS    2

static const char *TAG = "audio_player";

static void post_audio_finished(const char *path)
{
    if (!path || !path[0]) {
        return;
    }
    event_bus_message_t msg = {0};
    msg.type = EVENT_AUDIO_FINISHED;
    strncpy(msg.payload, path, sizeof(msg.payload) - 1);
    msg.payload[sizeof(msg.payload) - 1] = 0;
    for (int attempt = 0; attempt < AUDIO_FINISHED_POST_RETRIES; ++attempt) {
        esp_err_t err = event_bus_post(&msg, pdMS_TO_TICKS(AUDIO_FINISHED_POST_WAIT_MS));
        if (err == ESP_OK) {
            return;
        }
        if (err != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "audio finished event post failed: %s", esp_err_to_name(err));
            return;
        }
    }
    ESP_LOGW(TAG, "audio finished event dropped for %s", path);
}

static size_t mp3_i2s_write_cb(const uint8_t *data, size_t len, void *user)
{
    if (audio_player_stop_requested()) {
        return 0;
    }
    audio_player_wait_while_paused();

    i2s_chan_handle_t chan = (i2s_chan_handle_t)user;
    size_t written = 0;
    if (!chan) {
        return 0;
    }

    int vol = audio_player_runtime_volume();
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;

    const bool unity = (vol == 100);
    const int16_t *in = (const int16_t *)data;
    size_t samples = len / sizeof(int16_t);
    uint8_t *tmp_buf = NULL;
    const void *out_ptr = data;
    if (!unity) {
        tmp_buf = heap_caps_malloc(len, MALLOC_CAP_DEFAULT);
        if (tmp_buf) {
            int16_t *out = (int16_t *)tmp_buf;
            float gain = vol / 100.0f;
            for (size_t i = 0; i < samples; ++i) {
                int32_t v = (int32_t)((float)in[i] * gain);
                if (v > 32767) v = 32767;
                if (v < -32768) v = -32768;
                out[i] = (int16_t)v;
            }
            out_ptr = tmp_buf;
        }
    }

    if (i2s_channel_write(chan, out_ptr, len, &written, portMAX_DELAY) != ESP_OK) {
        written = 0;
    }
    if (tmp_buf) {
        heap_caps_free(tmp_buf);
    }
    return written;
}

static void mp3_progress_cb(size_t bytes_read, size_t total_bytes, uint32_t elapsed_ms, uint32_t est_total_ms, void *user)
{
    int kbps = user ? *((int *)user) : 0;
    audio_player_status_update_progress(bytes_read, total_bytes, elapsed_ms, est_total_ms);
    if (kbps > 0) {
        audio_player_status_update_bitrate(kbps);
    }
}

static audio_format_t detect_format(const uint8_t *hdr, size_t len)
{
    if (len >= 12 && memcmp(hdr, "RIFF", 4) == 0 && memcmp(hdr + 8, "WAVE", 4) == 0) {
        return AUDIO_FMT_WAV;
    }
    if (len >= 3 && memcmp(hdr, "ID3", 3) == 0) {
        return AUDIO_FMT_MP3;
    }
    if (len >= 2 && hdr[0] == 0xFF && (hdr[1] & 0xE0) == 0xE0) {
        return AUDIO_FMT_MP3;
    }
    if (len >= 4 && memcmp(hdr, "OggS", 4) == 0) {
        return AUDIO_FMT_OGG;
    }
    return AUDIO_FMT_UNKNOWN;
}

static esp_err_t parse_wav_header(FILE *f, audio_info_t *info, long *data_offset, uint32_t *data_size)
{
    struct __attribute__((packed)) wav_header {
        char riff[4];
        uint32_t size;
        char wave[4];
        char fmt[4];
        uint32_t fmt_size;
        uint16_t audio_format;
        uint16_t num_channels;
        uint32_t sample_rate;
        uint32_t byte_rate;
        uint16_t block_align;
        uint16_t bits_per_sample;
        char data_id[4];
        uint32_t data_size;
    } hdr;

    if (fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
        return ESP_FAIL;
    }
    if (strncmp(hdr.riff, "RIFF", 4) != 0 || strncmp(hdr.wave, "WAVE", 4) != 0) {
        return ESP_FAIL;
    }
    if (hdr.audio_format != 1 || hdr.bits_per_sample != 16) {
        return ESP_FAIL;
    }

    info->fmt = AUDIO_FMT_WAV;
    info->sample_rate = hdr.sample_rate;
    info->channels = hdr.num_channels;
    info->bits_per_sample = hdr.bits_per_sample;
    *data_offset = ftell(f);
    *data_size = hdr.data_size;
    return ESP_OK;
}

static size_t convert_pcm_to_output(const int16_t *in, size_t frames_in, const audio_info_t *info, int16_t *out, float gain)
{
    if (info->sample_rate == AUDIO_SAMPLE_RATE && info->channels == AUDIO_CHANNELS) {
        size_t samples = frames_in * AUDIO_CHANNELS;
        for (size_t i = 0; i < samples; ++i) {
            int32_t v = (int32_t)((float)in[i] * gain);
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            out[i] = (int16_t)v;
        }
        return frames_in;
    }

    float ratio = (float)info->sample_rate / (float)AUDIO_SAMPLE_RATE;
    size_t out_frames = (size_t)((float)frames_in / ratio);
    for (size_t o = 0; o < out_frames; ++o) {
        size_t src_idx = (size_t)(o * ratio);
        int32_t l = 0, r = 0;
        if (info->channels == 1) {
            int16_t s = in[src_idx];
            l = r = s;
        } else {
            l = in[src_idx * 2];
            r = in[src_idx * 2 + 1];
        }
        l = (int32_t)((float)l * gain);
        r = (int32_t)((float)r * gain);
        if (l > 32767) {
            l = 32767;
        } else if (l < -32768) {
            l = -32768;
        }
        if (r > 32767) {
            r = 32767;
        } else if (r < -32768) {
            r = -32768;
        }
        out[o * 2] = (int16_t)l;
        out[o * 2 + 1] = (int16_t)r;
    }
    return out_frames;
}

static bool decode_wav_to_output(FILE *f, const audio_info_t *info, uint32_t data_size, size_t initial_bytes_done)
{
    const size_t in_buf_frames = 512;
    int16_t *in_buf = heap_caps_malloc(in_buf_frames * info->channels * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int16_t *out_buf = heap_caps_malloc(in_buf_frames * AUDIO_CHANNELS * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!in_buf || !out_buf) {
        ESP_LOGE(TAG, "no mem for wav decode");
        if (in_buf) heap_caps_free(in_buf);
        if (out_buf) heap_caps_free(out_buf);
        error_monitor_report_audio_fault();
        return false;
    }

    size_t bytes_read = 0;
    size_t bytes_done = initial_bytes_done;
    const uint32_t byte_rate = info->sample_rate * info->channels * (info->bits_per_sample / 8);
    while ((bytes_read = fread(in_buf, 1, in_buf_frames * info->channels * sizeof(int16_t), f)) > 0) {
        if (audio_player_stop_requested()) {
            break;
        }
        audio_player_wait_while_paused();

        int vol = audio_player_runtime_volume();
        if (vol < 0) vol = 0;
        if (vol > 100) vol = 100;
        float gain = vol / 100.0f;

        size_t frames = bytes_read / (info->channels * sizeof(int16_t));
        size_t out_frames = convert_pcm_to_output(in_buf, frames, info, out_buf, gain);
        size_t out_bytes = out_frames * AUDIO_CHANNELS * sizeof(int16_t);
        i2s_chan_handle_t chan = audio_player_output_channel();
        if (!chan) {
            ESP_LOGW(TAG, "i2s channel unavailable");
            break;
        }
        size_t written = 0;
        if (i2s_channel_write(chan, out_buf, out_bytes, &written, portMAX_DELAY) != ESP_OK) {
            ESP_LOGW(TAG, "i2s write failed");
            break;
        }

        bytes_done += bytes_read;
        if (byte_rate > 0) {
            uint32_t pos_ms = (uint32_t)((bytes_done * 1000ULL) / byte_rate);
            uint32_t dur_ms = data_size > 0 ? (uint32_t)((data_size * 1000ULL) / byte_rate) : 0;
            audio_player_status_update_progress(bytes_done, data_size, pos_ms, dur_ms);
        } else {
            audio_player_status_update_progress(bytes_done, data_size, 0, 0);
        }
    }

    heap_caps_free(in_buf);
    heap_caps_free(out_buf);
    return true;
}

void audio_player_reader_task(void *param)
{
    audio_cmd_t cmd = *(audio_cmd_t *)param;
    free(param);
    audio_player_reader_begin();

    FILE *f = fopen(cmd.path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "cannot open %s", cmd.path);
        audio_player_status_set_message("Cannot open file");
        error_monitor_report_audio_fault();
        if (!audio_player_stop_requested()) {
            post_audio_finished(cmd.path);
        }
        audio_player_reader_finished();
        vTaskDelete(NULL);
        return;
    }

    fseek(f, 0, SEEK_END);
#if AUDIO_PLAYER_DEBUG
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t head[32] = {0};
    fread(head, 1, sizeof(head), f);
    fseek(f, 0, SEEK_SET);
    ESP_LOGI(TAG, "file %s size=%ld head=%02X %02X %02X %02X %02X %02X %02X %02X",
             cmd.path,
             fsize,
             head[0],
             head[1],
             head[2],
             head[3],
             head[4],
             head[5],
             head[6],
             head[7]);
#else
    fseek(f, 0, SEEK_END);
    fseek(f, 0, SEEK_SET);
#endif

    uint8_t hdr[16] = {0};
    size_t hdr_len = fread(hdr, 1, sizeof(hdr), f);
    fseek(f, 0, SEEK_SET);
    audio_format_t fmt = detect_format(hdr, hdr_len);
    audio_info_t info = {0};
    info.fmt = fmt;

    if (cmd.seek_ratio < 0.0f) {
        audio_player_status_set_play(cmd.path, fmt, audio_player_runtime_volume());
    } else {
        audio_player_status_mark_seek_play(cmd.path, fmt);
    }

    if (fmt == AUDIO_FMT_WAV) {
        long data_off = 0;
        uint32_t data_size = 0;
        if (parse_wav_header(f, &info, &data_off, &data_size) == ESP_OK) {
            uint32_t byte_rate = info.sample_rate * info.channels * (info.bits_per_sample / 8);
            size_t skip_bytes = 0;
            if (cmd.seek_ratio >= 0.0f && cmd.seek_ratio <= 1.0f) {
                skip_bytes = (size_t)((float)data_size * cmd.seek_ratio);
                if (skip_bytes >= data_size) {
                    skip_bytes = data_size > 0 ? data_size - 1 : 0;
                }
            }
            fseek(f, data_off + (long)skip_bytes, SEEK_SET);
            if (skip_bytes > 0 && byte_rate > 0) {
                uint32_t est_ms = (uint32_t)((skip_bytes * 1000ULL) / byte_rate);
                uint32_t dur_ms = (uint32_t)((data_size * 1000ULL) / byte_rate);
                audio_player_status_update_progress(skip_bytes, data_size, est_ms, dur_ms);
            }
            decode_wav_to_output(f, &info, data_size, skip_bytes);
        } else {
            ESP_LOGE(TAG, "bad wav header");
            audio_player_status_set_message("Bad WAV header");
            error_monitor_report_audio_fault();
        }
    } else if (fmt == AUDIO_FMT_MP3) {
        fclose(f);
        f = NULL;
        int *bitrate_kbps = audio_player_runtime_bitrate_ptr();
        *bitrate_kbps = 0;
        helix_mp3_decode_file(cmd.path,
                              audio_player_runtime_volume(),
                              mp3_i2s_write_cb,
                              audio_player_output_channel(),
                              mp3_progress_cb,
                              bitrate_kbps,
                              cmd.seek_ratio);
    } else if (fmt == AUDIO_FMT_OGG) {
        ESP_LOGW(TAG, "OGG decode not implemented yet");
        audio_player_status_set_message("OGG not supported");
        error_monitor_report_audio_fault();
    } else {
        ESP_LOGW(TAG, "unknown audio format");
        audio_player_status_set_message("Unknown format");
        error_monitor_report_audio_fault();
    }

    if (f) {
        fclose(f);
    }
    if (!audio_player_stop_requested() && audio_player_output_channel()) {
        audio_player_output_disable();
    }
    audio_player_status_set_playing(false);
    if (!audio_player_stop_requested()) {
        post_audio_finished(cmd.path);
    }
    audio_player_reader_finished();
    vTaskDelete(NULL);
}
