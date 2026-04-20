#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t web_ui_system_init(void);
esp_err_t status_handler(httpd_req_t *req);
esp_err_t ota_status_handler(httpd_req_t *req);
esp_err_t ota_upload_handler(httpd_req_t *req);
esp_err_t ota_reboot_handler(httpd_req_t *req);
esp_err_t files_handler(httpd_req_t *req);
esp_err_t audio_play_handler(httpd_req_t *req);
esp_err_t audio_stop_handler(httpd_req_t *req);
esp_err_t audio_pause_handler(httpd_req_t *req);
esp_err_t audio_resume_handler(httpd_req_t *req);
esp_err_t audio_volume_handler(httpd_req_t *req);
esp_err_t audio_seek_handler(httpd_req_t *req);
esp_err_t ping_handler(httpd_req_t *req);
esp_err_t wifi_scan_handler(httpd_req_t *req);
esp_err_t wifi_config_handler(httpd_req_t *req);
esp_err_t mqtt_config_handler(httpd_req_t *req);
esp_err_t logging_config_handler(httpd_req_t *req);
esp_err_t mqtt_users_handler(httpd_req_t *req);
esp_err_t publish_handler(httpd_req_t *req);
esp_err_t ap_stop_handler(httpd_req_t *req);
esp_err_t root_get_handler(httpd_req_t *req);
esp_err_t devices_config_handler(httpd_req_t *req);
esp_err_t devices_apply_handler(httpd_req_t *req);
esp_err_t devices_run_handler(httpd_req_t *req);
esp_err_t devices_signal_reset_handler(httpd_req_t *req);
esp_err_t devices_sequence_reset_handler(httpd_req_t *req);
esp_err_t devices_profile_create_handler(httpd_req_t *req);
esp_err_t devices_profile_delete_handler(httpd_req_t *req);
esp_err_t devices_profile_rename_handler(httpd_req_t *req);
esp_err_t devices_profile_activate_handler(httpd_req_t *req);
esp_err_t devices_profile_download_handler(httpd_req_t *req);
esp_err_t devices_variables_handler(httpd_req_t *req);
esp_err_t devices_templates_handler(httpd_req_t *req);
