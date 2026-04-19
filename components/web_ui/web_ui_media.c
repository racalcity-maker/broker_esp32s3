#include "web_ui_handlers.h"

#include <stdbool.h>
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "audio_player.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "sd_storage.h"

#include "web_ui_utils.h"

static esp_err_t web_http_check(esp_err_t err, const char *context)
{
    if (err != ESP_OK) {
        web_ui_report_httpd_error(err, context);
    }
    return err;
}

#define WEB_HTTP_CHECK(call) web_http_check((call), __func__)

static bool path_allowed(const char *path)
{
    if (!path) {
        return false;
    }
    const char *root = sd_storage_root_path();
    size_t root_len = strlen(root);
    return strncmp(path, root, root_len) == 0;
}

esp_err_t files_handler(httpd_req_t *req)
{
    char q[320];
    char path_enc[256] = {0};
    char dir_path[256] = SD_STORAGE_ROOT_PATH;
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        httpd_query_key_value(q, "path", path_enc, sizeof(path_enc));
    }
    if (path_enc[0]) {
        web_ui_url_decode(dir_path, sizeof(dir_path), path_enc);
        if (!path_allowed(dir_path)) {
            return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad path"));
        }
    }

    esp_err_t sd_err = sd_storage_mount();
    if (sd_err != ESP_OK) {
        char msg[64];
        snprintf(msg, sizeof(msg), "sd not mounted: %s", esp_err_to_name(sd_err));
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, msg));
    }
    DIR *d = opendir(dir_path);
    if (!d) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sd not mounted"));
    }
    httpd_resp_set_hdr(req, "Connection", "close");
    cJSON *root = cJSON_CreateArray();
    if (!root) {
        closedir(d);
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem"));
    }
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        const char *n = ent->d_name;
        if (strcmp(n, ".") == 0 || strcmp(n, "..") == 0) {
            continue;
        }
        if (strcasecmp(n, "System Volume Information") == 0 || strcasecmp(n, "SYSTEM~1") == 0) {
            continue;
        }
        char full[200];
        int written = snprintf(full, sizeof(full), "%s/%s", dir_path, n);
        if (written <= 0 || written >= (int)sizeof(full)) {
            continue;
        }
        struct stat st;
        bool stat_ok = (stat(full, &st) == 0);
        bool is_dir = stat_ok ? S_ISDIR(st.st_mode) : false;
        bool ext_audio = false;
        {
            size_t nlen = strlen(n);
            if (nlen >= 4) {
                const char *ext = n + nlen - 4;
                ext_audio = (strcasecmp(ext, ".wav") == 0 ||
                             strcasecmp(ext, ".mp3") == 0 ||
                             strcasecmp(ext, ".ogg") == 0);
            }
        }
        if (!stat_ok && !ext_audio) {
            is_dir = true;
        }
        if (!is_dir) {
            size_t nlen = strlen(n);
            if (nlen < 4) {
                continue;
            }
            const char *ext = n + nlen - 4;
            if (strcasecmp(ext, ".wav") != 0 &&
                strcasecmp(ext, ".mp3") != 0 &&
                strcasecmp(ext, ".ogg") != 0) {
                continue;
            }
        }
        long size_bytes = (is_dir || !stat_ok) ? 0 : st.st_size;
        int dur = 0;
        if (!is_dir) {
            size_t nlen = strlen(n);
            const char *ext = n + nlen - 4;
            if (strcasecmp(ext, ".wav") == 0) {
                FILE *wf = fopen(full, "rb");
                if (wf) {
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
                    if (fread(&hdr, 1, sizeof(hdr), wf) == sizeof(hdr) &&
                        strncmp(hdr.riff, "RIFF", 4) == 0 &&
                        strncmp(hdr.wave, "WAVE", 4) == 0 &&
                        hdr.audio_format == 1 &&
                        hdr.byte_rate > 0) {
                        dur = hdr.data_size / (int)hdr.byte_rate;
                    }
                    fclose(wf);
                }
            }
        }
        cJSON *obj = cJSON_CreateObject();
        if (!obj) {
            cJSON_Delete(root);
            closedir(d);
            return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem"));
        }
        cJSON_AddStringToObject(obj, "path", full);
        cJSON_AddNumberToObject(obj, "size", (double)size_bytes);
        cJSON_AddNumberToObject(obj, "dur", dur);
        cJSON_AddBoolToObject(obj, "dir", is_dir);
        cJSON_AddItemToArray(root, obj);
    }
    closedir(d);
    return WEB_HTTP_CHECK(web_ui_send_json(req, root));
}

esp_err_t audio_play_handler(httpd_req_t *req)
{
    char query[520];
    char path_enc[512] = {0};
    char path[512] = {0};
    char vol[8] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "path", path_enc, sizeof(path_enc));
        httpd_query_key_value(query, "volume", vol, sizeof(vol));
    }
    web_ui_url_decode(path, sizeof(path), path_enc);
    esp_err_t sd_err = sd_storage_mount();
    if (sd_err != ESP_OK) {
        char msg[64];
        snprintf(msg, sizeof(msg), "sd not mounted: %s", esp_err_to_name(sd_err));
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, msg));
    }
    if (path[0] == '\0') {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "path required"));
    }
    if (vol[0]) {
        audio_player_set_volume(atoi(vol));
    }
    esp_err_t err = audio_player_play(path);
    if (err != ESP_OK) {
        char msg[64];
        snprintf(msg, sizeof(msg), "play failed: %s", esp_err_to_name(err));
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, msg));
    }
    return web_ui_send_ok(req, "text/plain", "play");
}

esp_err_t audio_stop_handler(httpd_req_t *req)
{
    audio_player_stop();
    return web_ui_send_ok(req, "text/plain", "stop");
}

esp_err_t audio_pause_handler(httpd_req_t *req)
{
    audio_player_pause();
    return web_ui_send_ok(req, "text/plain", "pause");
}

esp_err_t audio_resume_handler(httpd_req_t *req)
{
    audio_player_resume();
    return web_ui_send_ok(req, "text/plain", "resume");
}

esp_err_t audio_volume_handler(httpd_req_t *req)
{
    char q[32];
    char val[8] = {0};
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        httpd_query_key_value(q, "val", val, sizeof(val));
    }
    int v = atoi(val);
    audio_player_set_volume(v);
    return web_ui_send_ok(req, "text/plain", "vol set");
}

esp_err_t audio_seek_handler(httpd_req_t *req)
{
    char q[64];
    char pos[16] = {0};
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "pos required"));
    }
    httpd_query_key_value(q, "pos", pos, sizeof(pos));
    int ms = atoi(pos);
    if (ms < 0) {
        ms = 0;
    }
    esp_err_t err = audio_player_seek((uint32_t)ms);
    if (err != ESP_OK) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "seek failed"));
    }
    return web_ui_send_ok(req, "text/plain", "seek");
}
