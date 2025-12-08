#include "web_ui_robot.h"

#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include "nvs.h"
#include "mqtt_core.h"

#include "web_ui_utils.h"

#define ROBOT_PRESET_COUNT 8
#define ROBOT_NAME_LEN 48

typedef struct {
    char laser_dir[96];
    char sarcastic_dir[96];
    char broken_dir[96];
} robot_cfg_t;

typedef struct {
    char names[ROBOT_PRESET_COUNT][ROBOT_NAME_LEN];
} robot_presets_t;

static robot_cfg_t s_robot_cfg;
static robot_presets_t s_robot_presets;

static void robot_load_defaults(robot_cfg_t *cfg)
{
    if (!cfg) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->laser_dir, "/sdcard/laser", sizeof(cfg->laser_dir) - 1);
}

static esp_err_t robot_load(robot_cfg_t *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open("robotcfg", NVS_READONLY, &h);
    if (err != ESP_OK) {
        robot_load_defaults(cfg);
        return err;
    }
    size_t sz = sizeof(*cfg);
    err = nvs_get_blob(h, "cfg", cfg, &sz);
    nvs_close(h);
    if (err != ESP_OK || sz != sizeof(*cfg)) {
        robot_load_defaults(cfg);
        return err;
    }
    return ESP_OK;
}

static esp_err_t robot_save(const robot_cfg_t *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open("robotcfg", NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(h, "cfg", cfg, sizeof(*cfg));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

static void robot_presets_default(robot_presets_t *p)
{
    if (!p) {
        return;
    }
    memset(p, 0, sizeof(*p));
}

static esp_err_t robot_presets_load(robot_presets_t *p)
{
    if (!p) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open("robotpreset", NVS_READONLY, &h);
    if (err != ESP_OK) {
        robot_presets_default(p);
        return err;
    }
    size_t sz = sizeof(*p);
    err = nvs_get_blob(h, "slots", p, &sz);
    nvs_close(h);
    if (err != ESP_OK || sz != sizeof(*p)) {
        robot_presets_default(p);
        return err;
    }
    return ESP_OK;
}

static esp_err_t robot_presets_save(const robot_presets_t *p)
{
    if (!p) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open("robotpreset", NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(h, "slots", p, sizeof(*p));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

static void sanitize_name(const char *src, char *dst, size_t dst_sz)
{
    if (!dst || dst_sz == 0) {
        return;
    }
    size_t w = 0;
    if (src) {
        for (size_t i = 0; src[i] && w + 1 < dst_sz; ++i) {
            char c = src[i];
            if (isalnum((int)c) || c == '_' || c == '-') {
                dst[w++] = c;
            }
        }
    }
    dst[w] = 0;
}

static esp_err_t robot_config_handler(httpd_req_t *req)
{
    char resp[320];
    int w = snprintf(resp, sizeof(resp),
                     "{\"laser\":\"%s\",\"sarcastic\":\"%s\",\"broken\":\"%s\"}",
                     s_robot_cfg.laser_dir, s_robot_cfg.sarcastic_dir, s_robot_cfg.broken_dir);
    if (w < 0 || w >= (int)sizeof(resp)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "resp too long");
    }
    return web_ui_send_ok(req, "application/json", resp);
}

static esp_err_t robot_save_handler(httpd_req_t *req)
{
    char q[320];
    char laser_enc[96] = {0}, sarcasm_enc[96] = {0}, broken_enc[96] = {0};
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        httpd_query_key_value(q, "laser", laser_enc, sizeof(laser_enc));
        httpd_query_key_value(q, "sarcastic", sarcasm_enc, sizeof(sarcasm_enc));
        httpd_query_key_value(q, "broken", broken_enc, sizeof(broken_enc));
    }
    char laser[96] = {0}, sarcasm[96] = {0}, broken[96] = {0};
    web_ui_url_decode(laser, sizeof(laser), laser_enc);
    web_ui_url_decode(sarcasm, sizeof(sarcasm), sarcasm_enc);
    web_ui_url_decode(broken, sizeof(broken), broken_enc);
    if (laser[0]) {
        strncpy(s_robot_cfg.laser_dir, laser, sizeof(s_robot_cfg.laser_dir) - 1);
    }
    if (sarcasm[0]) {
        strncpy(s_robot_cfg.sarcastic_dir, sarcasm, sizeof(s_robot_cfg.sarcastic_dir) - 1);
    }
    if (broken[0]) {
        strncpy(s_robot_cfg.broken_dir, broken, sizeof(s_robot_cfg.broken_dir) - 1);
    }
    esp_err_t err = robot_save(&s_robot_cfg);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "nvs save failed");
    }
    return web_ui_send_ok(req, "text/plain", "robot cfg saved");
}

static esp_err_t robot_presets_get_handler(httpd_req_t *req)
{
    char resp[ROBOT_PRESET_COUNT * (ROBOT_NAME_LEN + 4) + 16];
    size_t len = 0;
    len += snprintf(resp + len, sizeof(resp) - len, "[");
    for (int i = 0; i < ROBOT_PRESET_COUNT; ++i) {
        len += snprintf(resp + len, sizeof(resp) - len, "%s\"%s\"", i == 0 ? "" : ",", s_robot_presets.names[i]);
    }
    len += snprintf(resp + len, sizeof(resp) - len, "]");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, resp, len);
}

static esp_err_t robot_presets_set_handler(httpd_req_t *req)
{
    char q[128];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "query required");
    }
    char idxs[8] = {0};
    char name_raw[ROBOT_NAME_LEN] = {0};
    httpd_query_key_value(q, "idx", idxs, sizeof(idxs));
    httpd_query_key_value(q, "name", name_raw, sizeof(name_raw));
    int idx = atoi(idxs);
    if (idx < 0 || idx >= ROBOT_PRESET_COUNT) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad idx");
    }
    char clean[ROBOT_NAME_LEN] = {0};
    sanitize_name(name_raw, clean, sizeof(clean));
    strncpy(s_robot_presets.names[idx], clean, sizeof(s_robot_presets.names[idx]) - 1);
    robot_presets_save(&s_robot_presets);
    return web_ui_send_ok(req, "text/plain", "saved");
}

static esp_err_t robot_presets_play_handler(httpd_req_t *req)
{
    char q[64];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "query required");
    }
    char idxs[8] = {0};
    httpd_query_key_value(q, "idx", idxs, sizeof(idxs));
    int idx = atoi(idxs);
    if (idx < 0 || idx >= ROBOT_PRESET_COUNT) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad idx");
    }
    const char *name = s_robot_presets.names[idx];
    if (!name || strlen(name) == 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty slot");
    }
    char path[128];
    snprintf(path, sizeof(path), "/sdcard/audio/%s.mp3", name);
    mqtt_core_publish("audio/play", path);
    return web_ui_send_ok(req, "text/plain", "play");
}

esp_err_t web_ui_robot_register(httpd_handle_t server)
{
    if (!server) {
        return ESP_ERR_INVALID_ARG;
    }
    httpd_uri_t robot_cfg = {.uri = "/api/robot/config", .method = HTTP_GET, .handler = robot_config_handler};
    httpd_uri_t robot_save_uri = {.uri = "/api/robot/save", .method = HTTP_GET, .handler = robot_save_handler};
    httpd_uri_t robot_presets = {.uri = "/api/robot/presets", .method = HTTP_GET, .handler = robot_presets_get_handler};
    httpd_uri_t robot_preset_set = {.uri = "/api/robot/preset_set", .method = HTTP_GET, .handler = robot_presets_set_handler};
    httpd_uri_t robot_preset_play = {.uri = "/api/robot/preset_play", .method = HTTP_GET, .handler = robot_presets_play_handler};
    esp_err_t err = httpd_register_uri_handler(server, &robot_cfg);
    if (err != ESP_OK) {
        return err;
    }
    err = httpd_register_uri_handler(server, &robot_save_uri);
    if (err != ESP_OK) {
        return err;
    }
    err = httpd_register_uri_handler(server, &robot_presets);
    if (err != ESP_OK) {
        return err;
    }
    err = httpd_register_uri_handler(server, &robot_preset_set);
    if (err != ESP_OK) {
        return err;
    }
    err = httpd_register_uri_handler(server, &robot_preset_play);
    if (err != ESP_OK) {
        return err;
    }
    return ESP_OK;
}

esp_err_t web_ui_robot_init(void)
{
    robot_load(&s_robot_cfg);
    robot_presets_load(&s_robot_presets);
    return ESP_OK;
}
