#include "sd_storage.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "esp_check.h"
#include "esp_log.h"
#include "event_bus.h"

// SD card (SPI mode) pins.
#define SD_PIN_MISO CONFIG_BROKER_SD_MISO_PIN
#define SD_PIN_MOSI CONFIG_BROKER_SD_MOSI_PIN
#define SD_PIN_CLK  CONFIG_BROKER_SD_CLK_PIN
#define SD_PIN_CS   CONFIG_BROKER_SD_CS_PIN

static const char *TAG = "sd_storage";

static sdmmc_card_t *s_card = NULL;
static bool s_spi_init = false;
static SemaphoreHandle_t s_sd_mutex = NULL;

static void sd_storage_post_state(event_bus_type_t type)
{
    event_bus_message_t msg = {
        .type = type,
    };
    esp_err_t err = event_bus_post(&msg, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to post sd state event %d: %s", (int)type, esp_err_to_name(err));
    }
}

static void sd_lock(void)
{
    if (s_sd_mutex) {
        xSemaphoreTake(s_sd_mutex, portMAX_DELAY);
    }
}

static void sd_unlock(void)
{
    if (s_sd_mutex) {
        xSemaphoreGive(s_sd_mutex);
    }
}

static esp_err_t ensure_mutex(void)
{
    if (s_sd_mutex) {
        return ESP_OK;
    }
    s_sd_mutex = xSemaphoreCreateMutex();
    return s_sd_mutex ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t sd_storage_init(void)
{
    return ensure_mutex();
}

esp_err_t sd_storage_mount(void)
{
    ESP_RETURN_ON_ERROR(ensure_mutex(), TAG, "sd mutex init failed");

    sd_lock();
    if (s_card) {
        sd_unlock();
        return ESP_OK;
    }

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    if (!s_spi_init) {
        spi_bus_config_t bus_cfg = {
            .mosi_io_num = SD_PIN_MOSI,
            .miso_io_num = SD_PIN_MISO,
            .sclk_io_num = SD_PIN_CLK,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 4000,
        };
        esp_err_t err = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
        if (err != ESP_OK) {
            sd_unlock();
            ESP_RETURN_ON_ERROR(err, TAG, "spi init failed");
        }
        s_spi_init = true;
        ESP_LOGI(TAG, "SPI bus init for SD: MOSI=%d MISO=%d CLK=%d CS=%d freq=%dkHz",
                 SD_PIN_MOSI, SD_PIN_MISO, SD_PIN_CLK, SD_PIN_CS, host.max_freq_khz);
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_PIN_CS;
    slot_config.host_id = host.slot;

    esp_err_t ret = esp_vfs_fat_sdspi_mount(SD_STORAGE_ROOT_PATH, &host, &slot_config, &mount_config, &s_card);
    if (ret != ESP_OK) {
        s_card = NULL;
        ESP_LOGE(TAG, "failed to mount SD card: %s", esp_err_to_name(ret));
        sd_storage_post_state(EVENT_CARD_BAD);
    } else {
        ESP_LOGI(TAG, "SD mounted at %s", SD_STORAGE_ROOT_PATH);
        sd_storage_post_state(EVENT_CARD_OK);
    }
    sd_unlock();
    return ret;
}

bool sd_storage_available(void)
{
    bool present = false;
    if (ensure_mutex() != ESP_OK) {
        return false;
    }
    sd_lock();
    present = (s_card != NULL);
    sd_unlock();
    return present;
}

esp_err_t sd_storage_info(uint64_t *kb_total, uint64_t *kb_free)
{
    uint64_t total = 0;
    uint64_t free = 0;
    esp_err_t err = esp_vfs_fat_info(SD_STORAGE_ROOT_PATH, &total, &free);
    if (kb_total) {
        *kb_total = (err == ESP_OK) ? total : 0;
    }
    if (kb_free) {
        *kb_free = (err == ESP_OK) ? free : 0;
    }
    return err;
}

const char *sd_storage_root_path(void)
{
    return SD_STORAGE_ROOT_PATH;
}
