#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "esp_http_server.h"

typedef esp_err_t (*web_handler_fn)(httpd_req_t *req);

typedef enum {
    WEB_USER_ROLE_ADMIN = 0,
    WEB_USER_ROLE_USER = 1,
} web_user_role_t;

typedef struct {
    web_handler_fn fn;
    bool redirect_on_fail;
    web_user_role_t min_role;
} web_route_t;

esp_err_t auth_gate_handler(httpd_req_t *req);
esp_err_t login_page_handler(httpd_req_t *req);
esp_err_t auth_login_handler(httpd_req_t *req);
esp_err_t auth_logout_handler(httpd_req_t *req);
esp_err_t auth_password_handler(httpd_req_t *req);
esp_err_t session_info_handler(httpd_req_t *req);

void web_sessions_init(void);
void web_sessions_clear(void);
void web_auth_start_reset_monitor(void);
