#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define SD_STORAGE_ROOT_PATH "/sdcard"

esp_err_t sd_storage_init(void);
esp_err_t sd_storage_mount(void);
bool sd_storage_available(void);
esp_err_t sd_storage_info(uint64_t *kb_total, uint64_t *kb_free);
const char *sd_storage_root_path(void);
