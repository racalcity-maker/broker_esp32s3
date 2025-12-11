#include "dm_profiles.h"

#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_heap_caps.h"

#include "device_manager_utils.h"

#define DM_PROFILE_STORAGE_DIR "/sdcard/.dm_profiles"
#define DM_PROFILE_STORAGE_EXT ".bin"
#define DM_PROFILE_MAGIC       0x44504647u
#define DM_PROFILE_VERSION     3u
#define DM_PROFILE_PATH_MAX    128
#define DM_PROFILE_LEGACY_MAX_TABS 12

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t device_count;
} dm_profile_file_header_t;

static const char *TAG = "dm_profiles";

// Ensure SD card directory exists for profile binaries.
static esp_err_t ensure_storage_dir(void)
{
    struct stat st = {0};
    if (stat(DM_PROFILE_STORAGE_DIR, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return ESP_OK;
        }
        ESP_LOGE(TAG, "%s exists but is not a directory", DM_PROFILE_STORAGE_DIR);
        return ESP_FAIL;
    }
    if (errno == ENOENT) {
        if (mkdir(DM_PROFILE_STORAGE_DIR, 0775) == 0) {
            return ESP_OK;
        }
        ESP_LOGE(TAG, "mkdir %s failed: %d", DM_PROFILE_STORAGE_DIR, errno);
        return ESP_FAIL;
    }
    ESP_LOGE(TAG, "stat %s failed: %d", DM_PROFILE_STORAGE_DIR, errno);
    return ESP_FAIL;
}

// Compose on-disk filename for a profile id.
static esp_err_t make_profile_path(const char *id, char *out, size_t out_size)
{
    if (!id || !id[0] || !out || out_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    int written = snprintf(out, out_size, "%s/%s%s", DM_PROFILE_STORAGE_DIR, id, DM_PROFILE_STORAGE_EXT);
    if (written <= 0 || (size_t)written >= out_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

typedef void (*dm_profile_record_converter_t)(const void *src, device_descriptor_t *dst);

// Current record matches struct layout.
static void convert_device_current(const void *src, device_descriptor_t *dst)
{
    if (!src || !dst) {
        return;
    }
    const device_descriptor_t *cur = (const device_descriptor_t *)src;
    memcpy(dst, cur, sizeof(device_descriptor_t));
}

// Generic reader that streams `raw_count` records of size `record_size`
// and invokes `convert` to expand them into current descriptors.
static esp_err_t read_device_records(FILE *fp,
                                     const char *path,
                                     uint32_t raw_count,
                                     size_t record_size,
                                     device_descriptor_t *devices,
                                     uint8_t capacity,
                                     uint8_t *out_count,
                                     dm_profile_record_converter_t convert)
{
    if (!fp || !path || !devices || !out_count || !convert || record_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t *raw_buf = malloc(record_size);
    if (!raw_buf) {
        ESP_LOGE(TAG, "profile %s no memory for temp buffer (%zu bytes)", path, record_size);
        return ESP_ERR_NO_MEM;
    }
    uint32_t stored = raw_count;
    if (stored > capacity) {
        ESP_LOGW(TAG, "profile %s truncated (%" PRIu32 " -> %u)", path, raw_count, capacity);
        stored = capacity;
    }
    for (uint32_t i = 0; i < raw_count; ++i) {
        if (fread(raw_buf, 1, record_size, fp) != record_size) {
            ESP_LOGE(TAG, "profile %s truncated body", path);
            memset(devices, 0, sizeof(device_descriptor_t) * capacity);
            free(raw_buf);
            *out_count = 0;
            return ESP_ERR_INVALID_SIZE;
        }
        if (i < capacity) {
            convert(raw_buf, &devices[i]);
        }
    }
    if (stored < capacity) {
        memset(&devices[stored], 0,
               sizeof(device_descriptor_t) * (capacity - stored));
    }
    free(raw_buf);
    *out_count = (uint8_t)stored;
    return ESP_OK;
}

static esp_err_t read_variable_records(FILE *fp,
                                       const char *path,
                                       uint32_t raw_count,
                                       size_t expected_size,
                                       device_descriptor_t *devices,
                                       uint8_t capacity,
                                       uint8_t *out_count,
                                       dm_profile_record_converter_t convert)
{
    long data_start = ftell(fp);
    if (data_start < 0) {
        ESP_LOGE(TAG, "profile %s start offset unknown", path);
        return ESP_ERR_INVALID_STATE;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        ESP_LOGE(TAG, "profile %s seek end failed", path);
        return ESP_ERR_INVALID_STATE;
    }
    long data_end = ftell(fp);
    if (data_end < 0 || data_end <= data_start) {
        ESP_LOGE(TAG, "profile %s invalid size markers", path);
        return ESP_ERR_INVALID_SIZE;
    }
    if (fseek(fp, data_start, SEEK_SET) != 0) {
        ESP_LOGE(TAG, "profile %s seek restore failed", path);
        return ESP_ERR_INVALID_STATE;
    }
    if (raw_count == 0) {
        *out_count = 0;
        memset(devices, 0, sizeof(device_descriptor_t) * capacity);
        return ESP_OK;
    }
    size_t bytes = (size_t)(data_end - data_start);
    size_t record_size = bytes / raw_count;
    if (record_size < expected_size) {
        ESP_LOGE(TAG,
                 "profile %s record size too small (%zu vs %zu)",
                 path,
                 record_size,
                 expected_size);
        return ESP_ERR_INVALID_SIZE;
    }
    if (record_size != expected_size) {
        ESP_LOGW(TAG,
                 "profile %s adjusted record size %zu (expected %zu)",
                 path,
                 record_size,
                 expected_size);
    }
    return read_device_records(fp,
                               path,
                               raw_count,
                               record_size,
                               devices,
                               capacity,
                               out_count,
                               convert);
}

// Persist `count` descriptors for profile `id` to the SD card.
static esp_err_t write_devices(const char *id, const device_descriptor_t *devices, uint8_t count)
{
    if (!id || !id[0] || !devices || count > DEVICE_MANAGER_MAX_DEVICES) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = ensure_storage_dir();
    if (err != ESP_OK) {
        return err;
    }
    char path[DM_PROFILE_PATH_MAX];
    err = make_profile_path(id, path, sizeof(path));
    if (err != ESP_OK) {
        return err;
    }
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        ESP_LOGE(TAG, "open %s for write failed: %d", path, errno);
        return ESP_FAIL;
    }
    dm_profile_file_header_t hdr = {
        .magic = DM_PROFILE_MAGIC,
        .version = DM_PROFILE_VERSION,
        .device_count = count,
    };
    if (fwrite(&hdr, 1, sizeof(hdr), fp) != sizeof(hdr)) {
        ESP_LOGE(TAG, "write header to %s failed: %d", path, errno);
        fclose(fp);
        return ESP_FAIL;
    }
    if (count > 0) {
        size_t written = fwrite(devices, sizeof(device_descriptor_t), count, fp);
        if (written != count) {
            ESP_LOGE(TAG, "write devices to %s failed: %d", path, errno);
            fclose(fp);
            return ESP_FAIL;
        }
    }
    fclose(fp);
    return ESP_OK;
}

// Load profile binary, supporting all known on-disk versions.
static esp_err_t read_devices(const char *id,
                              device_descriptor_t *devices,
                              uint8_t capacity,
                              uint8_t *out_count)
{
    if (!id || !id[0] || !devices || !out_count || capacity == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    char path[DM_PROFILE_PATH_MAX];
    esp_err_t err = make_profile_path(id, path, sizeof(path));
    if (err != ESP_OK) {
        return err;
    }
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        ESP_LOGW(TAG, "profile %s missing", path);
        *out_count = 0;
        memset(devices, 0, sizeof(device_descriptor_t) * capacity);
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t result = ESP_OK;
    dm_profile_file_header_t hdr = {0};
    if (fread(&hdr, 1, sizeof(hdr), fp) != sizeof(hdr) || hdr.magic != DM_PROFILE_MAGIC) {
        ESP_LOGE(TAG, "profile %s invalid header", path);
        result = ESP_ERR_INVALID_STATE;
    } else if (hdr.version == DM_PROFILE_VERSION) {
        result = read_device_records(fp,
                                     path,
                                     hdr.device_count,
                                     sizeof(device_descriptor_t),
                                     devices,
                                     capacity,
                                     out_count,
                                     convert_device_current);
    } else if (hdr.version == 2u) {
        result = read_variable_records(fp,
                                       path,
                                       hdr.device_count,
                                       sizeof(device_descriptor_t),
                                       devices,
                                       capacity,
                                       out_count,
                                       convert_device_current);
    } else {
        ESP_LOGE(TAG, "profile %s unsupported version %" PRIu32, path, hdr.version);
        result = ESP_ERR_INVALID_VERSION;
    }
    fclose(fp);
    if (result != ESP_OK) {
        memset(devices, 0, sizeof(device_descriptor_t) * capacity);
        *out_count = 0;
    }
    return result;
}

// Lookup helper for profile array.
device_manager_profile_t *dm_profiles_find_by_id(device_manager_config_t *cfg, const char *id)
{
    if (!cfg || !id || !id[0]) {
        return NULL;
    }
    for (uint8_t i = 0; i < cfg->profile_count && i < DEVICE_MANAGER_MAX_PROFILES; ++i) {
        device_manager_profile_t *profile = &cfg->profiles[i];
        if (profile->id[0] && strcasecmp(profile->id, id) == 0) {
            return profile;
        }
    }
    return NULL;
}

// Ensure cfg has at least one profile and returns the active one.
device_manager_profile_t *dm_profiles_ensure_active(device_manager_config_t *cfg)
{
    if (!cfg) {
        return NULL;
    }
    if (cfg->profile_count == 0 && DEVICE_MANAGER_MAX_PROFILES > 0) {
        device_manager_profile_t *profile = &cfg->profiles[0];
        memset(profile, 0, sizeof(*profile));
        dm_str_copy(profile->id, sizeof(profile->id), DM_DEFAULT_PROFILE_ID);
        dm_str_copy(profile->name, sizeof(profile->name), DM_DEFAULT_PROFILE_NAME);
        profile->device_count = cfg->device_count;
        cfg->profile_count = 1;
        dm_str_copy(cfg->active_profile, sizeof(cfg->active_profile), profile->id);
    }
    if (!cfg->active_profile[0] && cfg->profile_count > 0) {
        dm_str_copy(cfg->active_profile, sizeof(cfg->active_profile), cfg->profiles[0].id);
    }
    device_manager_profile_t *profile = dm_profiles_find_by_id(cfg, cfg->active_profile);
    if (!profile && cfg->profile_count > 0) {
        profile = &cfg->profiles[0];
        dm_str_copy(cfg->active_profile, sizeof(cfg->active_profile), profile->id);
    }
    return profile;
}

// Load active profile from disk into cfg->devices (optionally create if missing).
void dm_profiles_sync_from_active(device_manager_config_t *cfg, bool create_if_missing)
{
    device_manager_profile_t *profile = dm_profiles_ensure_active(cfg);
    if (!cfg || !profile) {
        return;
    }
    uint8_t count = 0;
    esp_err_t err = read_devices(profile->id, cfg->devices, cfg->device_capacity, &count);
    if (err != ESP_OK) {
        memset(cfg->devices, 0, sizeof(device_descriptor_t) * cfg->device_capacity);
        cfg->device_count = 0;
        profile->device_count = 0;
        if (create_if_missing && err == ESP_ERR_NOT_FOUND) {
            ESP_LOGI(TAG, "profile %s missing, creating default", profile->id);
            esp_err_t store_err = dm_profiles_store_active(cfg);
            if (store_err != ESP_OK) {
                ESP_LOGW(TAG, "failed to persist default profile %s: %s", profile->id, esp_err_to_name(store_err));
            }
        } else if (err != ESP_OK) {
            ESP_LOGW(TAG, "profile %s load failed: %s", profile->id, esp_err_to_name(err));
        }
        return;
    }
    cfg->device_count = count;
    profile->device_count = count;
}

// Copy current in-memory device list counts back into the active profile entry.
void dm_profiles_sync_to_active(device_manager_config_t *cfg)
{
    device_manager_profile_t *profile = dm_profiles_ensure_active(cfg);
    if (!cfg || !profile) {
        return;
    }
    uint8_t count = cfg->device_count;
    if (cfg->device_capacity && count > cfg->device_capacity) {
        count = cfg->device_capacity;
    }
    profile->device_count = count;
}

// Check id uses safe characters and fits buffer.
bool dm_profiles_id_valid(const char *id)
{
    if (!id || !id[0]) {
        return false;
    }
    size_t len = strlen(id);
    if (len >= DEVICE_MANAGER_ID_MAX_LEN) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)id[i];
        if (!(isalnum(c) || c == '-' || c == '_')) {
            return false;
        }
    }
    return true;
}

// Write active profile to SD using current descriptors.
esp_err_t dm_profiles_store_active(const device_manager_config_t *cfg)
{
    if (!cfg || !cfg->active_profile[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t count = cfg->device_count;
    if (count > cfg->device_capacity) {
        count = cfg->device_capacity;
    }
    return write_devices(cfg->active_profile, cfg->devices, count);
}

// Load arbitrary profile file into caller-provided buffer.
esp_err_t dm_profiles_load_profile(const char *profile_id,
                                   device_descriptor_t *devices,
                                   uint8_t capacity,
                                   uint8_t *device_count)
{
    if (!profile_id || !profile_id[0] || !devices || !device_count || capacity == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    char path[DM_PROFILE_PATH_MAX];
    esp_err_t err = make_profile_path(profile_id, path, sizeof(path));
    if (err != ESP_OK) {
        return err;
    }
    esp_err_t result = read_devices(profile_id, devices, capacity, device_count);
    return result;
}

// Remove on-disk profile, ignoring ENOENT.
esp_err_t dm_profiles_delete_profile_file(const char *profile_id)
{
    if (!profile_id || !profile_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    char path[DM_PROFILE_PATH_MAX];
    esp_err_t err = make_profile_path(profile_id, path, sizeof(path));
    if (err != ESP_OK) {
        return err;
    }
    if (unlink(path) != 0) {
        if (errno == ENOENT) {
            return ESP_OK;
        }
        ESP_LOGW(TAG, "unlink %s failed: %d", path, errno);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t dm_profiles_export_raw(const char *profile_id, uint8_t **out_data, size_t *out_size)
{
    if (!out_data || !out_size) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_data = NULL;
    *out_size = 0;
    if (!profile_id || !profile_id[0]) {
        profile_id = DM_DEFAULT_PROFILE_ID;
    }
    char path[DM_PROFILE_PATH_MAX];
    esp_err_t err = make_profile_path(profile_id, path, sizeof(path));
    if (err != ESP_OK) {
        return err;
    }
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        ESP_LOGE(TAG, "profile %s not found", path);
        return ESP_ERR_NOT_FOUND;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return ESP_ERR_INVALID_STATE;
    }
    long size = ftell(fp);
    if (size <= 0) {
        fclose(fp);
        return ESP_ERR_INVALID_SIZE;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t *buf = heap_caps_malloc((size_t)size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        fclose(fp);
        return ESP_ERR_NO_MEM;
    }
    size_t read = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    if (read != (size_t)size) {
        heap_caps_free(buf);
        return ESP_ERR_INVALID_SIZE;
    }
    *out_data = buf;
    *out_size = (size_t)size;
    return ESP_OK;
}
