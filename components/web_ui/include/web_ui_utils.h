#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t web_ui_send_ok(httpd_req_t *req, const char *mime, const char *body);
void web_ui_url_decode(char *out, size_t out_len, const char *in);
void web_ui_report_httpd_error(esp_err_t err, const char *context);
