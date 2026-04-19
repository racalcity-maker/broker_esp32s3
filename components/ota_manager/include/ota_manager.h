#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_MANAGER_VERSION_MAX         32
#define OTA_MANAGER_PARTITION_LABEL_MAX 16
#define OTA_MANAGER_ERROR_MAX           96
#define OTA_MANAGER_PHASE_MAX           24

#define OTA_MANAGER_PHASE_IDLE              "idle"
#define OTA_MANAGER_PHASE_UPLOADING         "uploading"
#define OTA_MANAGER_PHASE_REBOOT_REQUIRED   "reboot_required"
#define OTA_MANAGER_PHASE_REBOOTING         "rebooting"
#define OTA_MANAGER_PHASE_VERIFY_WAIT_READY "verify_wait_ready"
#define OTA_MANAGER_PHASE_VERIFY_PENDING    "verify_pending"

typedef struct {
    bool rollback_supported;
    bool pending_verify;
    bool in_progress;
    bool system_ready;
    bool reboot_required;
    bool last_success;
    size_t bytes_written;
    size_t total_bytes;
    char running_partition[OTA_MANAGER_PARTITION_LABEL_MAX];
    char boot_partition[OTA_MANAGER_PARTITION_LABEL_MAX];
    char app_version[OTA_MANAGER_VERSION_MAX];
    char phase[OTA_MANAGER_PHASE_MAX];
    char last_error[OTA_MANAGER_ERROR_MAX];
} ota_manager_status_t;

esp_err_t ota_manager_init(void);
esp_err_t ota_manager_notify_boot(void);
void ota_manager_notify_system_ready(void);
void ota_manager_get_status(ota_manager_status_t *out);
bool ota_manager_is_busy(void);
esp_err_t ota_manager_begin_upload(size_t image_size);
esp_err_t ota_manager_write_chunk(const void *data, size_t len);
esp_err_t ota_manager_finish_upload(void);
esp_err_t ota_manager_request_reboot(void);
void ota_manager_abort_upload(void);

#ifdef __cplusplus
}
#endif
