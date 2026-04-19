#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "esp_http_server.h"
#include "cJSON.h"

esp_err_t web_ui_send_ok(httpd_req_t *req, const char *mime, const char *body);
esp_err_t web_ui_send_json(httpd_req_t *req, cJSON *root);
void web_ui_url_decode(char *out, size_t out_len, const char *in);
void web_ui_sanitize_filename_token(char *out, size_t out_len, const char *in, const char *fallback);
bool web_ui_is_same_origin_request(httpd_req_t *req);
void web_ui_report_httpd_error(esp_err_t err, const char *context);
