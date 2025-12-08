#include "web_ui_utils.h"

#include <ctype.h>
#include <stdlib.h>

esp_err_t web_ui_send_ok(httpd_req_t *req, const char *mime, const char *body)
{
    httpd_resp_set_type(req, mime);
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
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
