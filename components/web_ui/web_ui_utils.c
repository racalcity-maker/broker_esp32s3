#include "web_ui_utils.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

esp_err_t web_ui_send_json(httpd_req_t *req, cJSON *root)
{
    if (!req || !root) {
        if (root) {
            cJSON_Delete(root);
        }
        return ESP_ERR_INVALID_ARG;
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json encode failed");
    }
    esp_err_t err = web_ui_send_ok(req, "application/json", json);
    free(json);
    return err;
}

void web_ui_url_decode(char *out, size_t out_len, const char *in)
{
    if (!out || out_len == 0 || !in) {
        return;
    }
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 1 < out_len; ++i) {
        if (in[i] == '%' && isxdigit((unsigned char)in[i + 1]) && isxdigit((unsigned char)in[i + 2])) {
            char hex[3] = {in[i + 1], in[i + 2], 0};
            out[o++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else if (in[i] == '+') {
            out[o++] = ' ';
        } else {
            out[o++] = in[i];
        }
    }
    out[o] = 0;
}

void web_ui_sanitize_filename_token(char *out, size_t out_len, const char *in, const char *fallback)
{
    if (!out || out_len == 0) {
        return;
    }
    size_t o = 0;
    const char *src = (in && in[0]) ? in : fallback;
    if (!src) {
        src = "file";
    }
    for (size_t i = 0; src[i] && o + 1 < out_len; ++i) {
        unsigned char c = (unsigned char)src[i];
        if (isalnum(c) || c == '-' || c == '_') {
            out[o++] = (char)c;
        } else {
            out[o++] = '_';
        }
    }
    if (o == 0) {
        const char *def = (fallback && fallback[0]) ? fallback : "file";
        for (size_t i = 0; def[i] && o + 1 < out_len; ++i) {
            unsigned char c = (unsigned char)def[i];
            if (isalnum(c) || c == '-' || c == '_') {
                out[o++] = (char)c;
            }
        }
    }
    if (o == 0 && out_len > 1) {
        out[o++] = 'f';
    }
    out[o] = 0;
}

static bool web_ui_origin_matches_host(const char *origin, const char *host)
{
    if (!origin || !origin[0] || !host || !host[0]) {
        return false;
    }
    const char *http_prefix = "http://";
    const char *https_prefix = "https://";
    size_t host_len = strlen(host);
    if (strncmp(origin, http_prefix, strlen(http_prefix)) == 0) {
        const char *p = origin + strlen(http_prefix);
        return strncmp(p, host, host_len) == 0 && (p[host_len] == '\0' || p[host_len] == '/');
    }
    if (strncmp(origin, https_prefix, strlen(https_prefix)) == 0) {
        const char *p = origin + strlen(https_prefix);
        return strncmp(p, host, host_len) == 0 && (p[host_len] == '\0' || p[host_len] == '/');
    }
    return false;
}

bool web_ui_is_same_origin_request(httpd_req_t *req)
{
    if (!req) {
        return false;
    }
    size_t host_len = httpd_req_get_hdr_value_len(req, "Host");
    if (host_len == 0 || host_len >= 128) {
        return false;
    }
    char host[128];
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK || !host[0]) {
        return false;
    }

    size_t origin_len = httpd_req_get_hdr_value_len(req, "Origin");
    if (origin_len > 0 && origin_len < 256) {
        char origin[256];
        if (httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) == ESP_OK) {
            return web_ui_origin_matches_host(origin, host);
        }
        return false;
    }

    size_t referer_len = httpd_req_get_hdr_value_len(req, "Referer");
    if (referer_len > 0 && referer_len < 384) {
        char referer[384];
        if (httpd_req_get_hdr_value_str(req, "Referer", referer, sizeof(referer)) == ESP_OK) {
            return web_ui_origin_matches_host(referer, host);
        }
        return false;
    }

    return false;
}
