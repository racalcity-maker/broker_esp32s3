#include "ota_manager.h"

#include <string.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "ota_manager";
static const TickType_t OTA_CONFIRM_DELAY_TICKS = pdMS_TO_TICKS(30000);
static const TickType_t OTA_REBOOT_DELAY_TICKS = pdMS_TO_TICKS(1200);

typedef struct {
    SemaphoreHandle_t mutex;
    bool initialized;
    bool rollback_supported;
    bool pending_verify;
    bool in_progress;
    bool system_ready;
    bool reboot_required;
    bool reboot_task_started;
    bool last_success;
    bool confirm_task_started;
    size_t bytes_written;
    size_t total_bytes;
    esp_ota_handle_t handle;
    const esp_partition_t *target_partition;
    char last_error[OTA_MANAGER_ERROR_MAX];
} ota_manager_state_t;

static ota_manager_state_t s_ota = {0};

static bool ota_rollback_enabled(void)
{
#if defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) && defined(CONFIG_APP_ROLLBACK_ENABLE)
    return true;
#else
    return false;
#endif
}

static bool ota_lock(TickType_t ticks)
{
    return s_ota.mutex && xSemaphoreTake(s_ota.mutex, ticks) == pdTRUE;
}

static void ota_unlock(void)
{
    if (s_ota.mutex) {
        xSemaphoreGive(s_ota.mutex);
    }
}

static void copy_label(char *dst, size_t dst_len, const esp_partition_t *part)
{
    if (!dst || dst_len == 0) {
        return;
    }
    dst[0] = '\0';
    if (!part || !part->label[0]) {
        return;
    }
    strncpy(dst, part->label, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

static const char *ota_phase_name(bool in_progress,
                                  bool reboot_required,
                                  bool reboot_task_started,
                                  bool pending_verify,
                                  bool system_ready)
{
    if (in_progress) {
        return OTA_MANAGER_PHASE_UPLOADING;
    }
    if (reboot_task_started) {
        return OTA_MANAGER_PHASE_REBOOTING;
    }
    if (reboot_required) {
        return OTA_MANAGER_PHASE_REBOOT_REQUIRED;
    }
    if (pending_verify) {
        return system_ready ? OTA_MANAGER_PHASE_VERIFY_PENDING : OTA_MANAGER_PHASE_VERIFY_WAIT_READY;
    }
    return OTA_MANAGER_PHASE_IDLE;
}

static void set_last_error_locked(const char *msg)
{
    if (!msg) {
        s_ota.last_error[0] = '\0';
        return;
    }
    strncpy(s_ota.last_error, msg, sizeof(s_ota.last_error) - 1);
    s_ota.last_error[sizeof(s_ota.last_error) - 1] = '\0';
}

static void reset_upload_state_locked(void)
{
    s_ota.in_progress = false;
    s_ota.reboot_required = false;
    s_ota.bytes_written = 0;
    s_ota.total_bytes = 0;
    s_ota.handle = 0;
    s_ota.target_partition = NULL;
}

static void ota_reboot_task(void *arg)
{
    (void)arg;
    vTaskDelay(OTA_REBOOT_DELAY_TICKS);
    ESP_LOGW(TAG, "restarting after OTA install");
    esp_restart();
}

static void ota_confirm_task_done(void)
{
    if (ota_lock(portMAX_DELAY)) {
        s_ota.confirm_task_started = false;
        ota_unlock();
    }
}

static void ota_confirm_task(void *arg)
{
    (void)arg;
    vTaskDelay(OTA_CONFIRM_DELAY_TICKS);

    bool should_confirm = false;
    bool pending_verify = false;
    bool system_ready = false;
    if (ota_lock(portMAX_DELAY)) {
        pending_verify = s_ota.pending_verify;
        system_ready = s_ota.system_ready;
        should_confirm = pending_verify && system_ready;
        ota_unlock();
    }

    if (!should_confirm) {
        ESP_LOGW(TAG, "skip OTA confirm: pending=%d ready=%d",
                 pending_verify, system_ready);
        ota_confirm_task_done();
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (ota_lock(portMAX_DELAY)) {
        s_ota.confirm_task_started = false;
        if (err == ESP_OK) {
            s_ota.pending_verify = false;
            s_ota.last_success = true;
            set_last_error_locked(NULL);
            ESP_LOGI(TAG, "OTA image confirmed");
        } else {
            s_ota.last_success = false;
            set_last_error_locked("confirm failed");
            ESP_LOGE(TAG, "failed to confirm OTA image: %s", esp_err_to_name(err));
        }
        ota_unlock();
    } else {
        ota_confirm_task_done();
    }
    vTaskDelete(NULL);
}

esp_err_t ota_manager_init(void)
{
    if (s_ota.initialized) {
        return ESP_OK;
    }
    s_ota.mutex = xSemaphoreCreateMutex();
    if (!s_ota.mutex) {
        return ESP_ERR_NO_MEM;
    }
    s_ota.rollback_supported = ota_rollback_enabled();
    s_ota.last_success = true;
    s_ota.initialized = true;
    ESP_LOGI(TAG, "initialized (rollback=%s)", s_ota.rollback_supported ? "on" : "off");
    return ESP_OK;
}

esp_err_t ota_manager_notify_boot(void)
{
    if (!s_ota.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running) {
        return ESP_FAIL;
    }

    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    esp_err_t err = esp_ota_get_state_partition(running, &state);
    bool pending_verify = (err == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY);

    if (ota_lock(portMAX_DELAY)) {
        s_ota.pending_verify = pending_verify;
        s_ota.system_ready = false;
        s_ota.reboot_required = false;
        s_ota.reboot_task_started = false;
        s_ota.confirm_task_started = false;
        set_last_error_locked(NULL);
        ota_unlock();
    }

    ESP_LOGI(TAG, "boot partition=%s state=%s",
             running->label,
             pending_verify ? "pending_verify" : (err == ESP_OK ? "stable" : esp_err_to_name(err)));

    if (pending_verify) {
        if (ota_lock(portMAX_DELAY)) {
            if (!s_ota.confirm_task_started) {
                s_ota.confirm_task_started = true;
                if (xTaskCreate(ota_confirm_task, "ota_confirm", 4096, NULL, 4, NULL) != pdPASS) {
                    s_ota.confirm_task_started = false;
                    set_last_error_locked("confirm task create failed");
                    ota_unlock();
                    return ESP_ERR_NO_MEM;
                }
            }
            ota_unlock();
        }
    }

    return ESP_OK;
}

void ota_manager_notify_system_ready(void)
{
    if (!s_ota.initialized) {
        return;
    }
    if (ota_lock(portMAX_DELAY)) {
        s_ota.system_ready = true;
        ota_unlock();
    }
    ESP_LOGI(TAG, "system marked ready for OTA confirm");
}

void ota_manager_get_status(ota_manager_status_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    strncpy(out->phase, OTA_MANAGER_PHASE_IDLE, sizeof(out->phase) - 1);
    out->phase[sizeof(out->phase) - 1] = '\0';

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    const esp_app_desc_t *desc = esp_app_get_description();

    if (ota_lock(portMAX_DELAY)) {
        out->rollback_supported = s_ota.rollback_supported;
        out->pending_verify = s_ota.pending_verify;
        out->in_progress = s_ota.in_progress;
        out->system_ready = s_ota.system_ready;
        out->reboot_required = s_ota.reboot_required;
        out->last_success = s_ota.last_success;
        out->bytes_written = s_ota.bytes_written;
        out->total_bytes = s_ota.total_bytes;
        strncpy(out->phase,
                ota_phase_name(s_ota.in_progress,
                               s_ota.reboot_required,
                               s_ota.reboot_task_started,
                               s_ota.pending_verify,
                               s_ota.system_ready),
                sizeof(out->phase) - 1);
        out->phase[sizeof(out->phase) - 1] = '\0';
        strncpy(out->last_error, s_ota.last_error, sizeof(out->last_error) - 1);
        out->last_error[sizeof(out->last_error) - 1] = '\0';
        ota_unlock();
    }

    copy_label(out->running_partition, sizeof(out->running_partition), running);
    copy_label(out->boot_partition, sizeof(out->boot_partition), boot);
    if (desc && desc->version[0]) {
        strncpy(out->app_version, desc->version, sizeof(out->app_version) - 1);
        out->app_version[sizeof(out->app_version) - 1] = '\0';
    }
}

bool ota_manager_is_busy(void)
{
    bool busy = false;
    if (ota_lock(portMAX_DELAY)) {
        busy = s_ota.in_progress;
        ota_unlock();
    }
    return busy;
}

esp_err_t ota_manager_begin_upload(size_t image_size)
{
    if (!s_ota.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!target) {
        if (ota_lock(portMAX_DELAY)) {
            s_ota.last_success = false;
            set_last_error_locked("no OTA slot available");
            ota_unlock();
        }
        return ESP_ERR_NOT_FOUND;
    }

    if (image_size == 0 || image_size > target->size) {
        if (ota_lock(portMAX_DELAY)) {
            s_ota.last_success = false;
            set_last_error_locked("image too large");
            ota_unlock();
        }
        return ESP_ERR_INVALID_SIZE;
    }

    if (!ota_lock(portMAX_DELAY)) {
        return ESP_ERR_TIMEOUT;
    }
    if (s_ota.in_progress) {
        ota_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    ota_unlock();

    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(target, image_size, &handle);
    if (err != ESP_OK) {
        if (ota_lock(portMAX_DELAY)) {
            s_ota.last_success = false;
            set_last_error_locked(err == ESP_ERR_OTA_ROLLBACK_INVALID_STATE
                                      ? "current image not confirmed"
                                      : "ota begin failed");
            ota_unlock();
        }
        return err;
    }

    if (!ota_lock(portMAX_DELAY)) {
        esp_ota_abort(handle);
        return ESP_ERR_TIMEOUT;
    }
    s_ota.handle = handle;
    s_ota.target_partition = target;
    s_ota.in_progress = true;
    s_ota.reboot_required = false;
    s_ota.bytes_written = 0;
    s_ota.total_bytes = image_size;
    s_ota.last_success = false;
    set_last_error_locked(NULL);
    ota_unlock();

    ESP_LOGI(TAG, "OTA upload started: target=%s size=%u", target->label, (unsigned)image_size);
    return ESP_OK;
}

esp_err_t ota_manager_write_chunk(const void *data, size_t len)
{
    if (!data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ota_lock(portMAX_DELAY)) {
        return ESP_ERR_TIMEOUT;
    }
    if (!s_ota.in_progress) {
        ota_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    esp_ota_handle_t handle = s_ota.handle;
    ota_unlock();

    esp_err_t err = esp_ota_write(handle, data, len);
    if (!ota_lock(portMAX_DELAY)) {
        return ESP_ERR_TIMEOUT;
    }
    if (err == ESP_OK) {
        s_ota.bytes_written += len;
    } else {
        s_ota.last_success = false;
        set_last_error_locked("ota write failed");
    }
    ota_unlock();
    return err;
}

esp_err_t ota_manager_finish_upload(void)
{
    if (!ota_lock(portMAX_DELAY)) {
        return ESP_ERR_TIMEOUT;
    }
    if (!s_ota.in_progress || !s_ota.target_partition) {
        ota_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    esp_ota_handle_t handle = s_ota.handle;
    const esp_partition_t *target = s_ota.target_partition;
    ota_unlock();

    esp_err_t err = esp_ota_end(handle);
    if (err != ESP_OK) {
        if (ota_lock(portMAX_DELAY)) {
            s_ota.last_success = false;
            set_last_error_locked(err == ESP_ERR_OTA_VALIDATE_FAILED ? "invalid image" : "ota end failed");
            reset_upload_state_locked();
            ota_unlock();
        }
        return err;
    }

    err = esp_ota_set_boot_partition(target);
    if (err != ESP_OK) {
        if (ota_lock(portMAX_DELAY)) {
            s_ota.last_success = false;
            set_last_error_locked("set boot partition failed");
            reset_upload_state_locked();
            ota_unlock();
        }
        return err;
    }

    if (!ota_lock(portMAX_DELAY)) {
        return ESP_ERR_TIMEOUT;
    }
    s_ota.in_progress = false;
    s_ota.reboot_required = true;
    s_ota.last_success = true;
    s_ota.handle = 0;
    s_ota.target_partition = NULL;
    ota_unlock();

    ESP_LOGI(TAG, "OTA upload finished, boot partition set to %s; reboot required", target->label);
    return ESP_OK;
}

esp_err_t ota_manager_request_reboot(void)
{
    if (!s_ota.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!ota_lock(portMAX_DELAY)) {
        return ESP_ERR_TIMEOUT;
    }
    bool allowed = s_ota.reboot_required && !s_ota.in_progress && !s_ota.reboot_task_started;
    if (!allowed) {
        ota_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    s_ota.reboot_task_started = true;
    ota_unlock();

    if (xTaskCreate(ota_reboot_task, "ota_reboot", 3072, NULL, 4, NULL) != pdPASS) {
        if (ota_lock(portMAX_DELAY)) {
            s_ota.reboot_task_started = false;
            s_ota.last_success = false;
            set_last_error_locked("reboot task create failed");
            ota_unlock();
        }
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "OTA reboot requested");
    return ESP_OK;
}

void ota_manager_abort_upload(void)
{
    if (!ota_lock(portMAX_DELAY)) {
        return;
    }
    if (!s_ota.in_progress) {
        ota_unlock();
        return;
    }
    esp_ota_handle_t handle = s_ota.handle;
    s_ota.last_success = false;
    set_last_error_locked("ota aborted");
    reset_upload_state_locked();
    ota_unlock();

    esp_ota_abort(handle);
    ESP_LOGW(TAG, "OTA upload aborted");
}
