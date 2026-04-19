#include "automation_engine.h"
#include "automation_engine_internal.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#define AUTOMATION_CONTEXT_MAX_VARS 32
#define AUTOMATION_CONTEXT_KEY_MAX 48
#define AUTOMATION_CONTEXT_VALUE_MAX 192

typedef struct {
    bool in_use;
    char key[AUTOMATION_CONTEXT_KEY_MAX];
    char value[AUTOMATION_CONTEXT_VALUE_MAX];
} automation_context_var_t;

static const char *TAG = "automation_ctx";
static SemaphoreHandle_t s_context_mutex = NULL;
static automation_context_var_t s_context_vars[AUTOMATION_CONTEXT_MAX_VARS];

static void ctx_str_copy(char *dst, size_t dst_len, const char *src)
{
    if (!dst || dst_len == 0) {
        return;
    }
    if (!src) {
        dst[0] = 0;
        return;
    }
    size_t i = 0;
    for (; i < dst_len - 1 && src[i]; ++i) {
        dst[i] = src[i];
    }
    dst[i] = 0;
}

esp_err_t automation_engine_context_init(void)
{
    if (!s_context_mutex) {
        s_context_mutex = xSemaphoreCreateMutex();
    }
    return s_context_mutex ? ESP_OK : ESP_ERR_NO_MEM;
}

void automation_engine_context_set(const char *key, const char *value)
{
    if (!key || !key[0] || !s_context_mutex) {
        return;
    }
    if (!value || !value[0]) {
        automation_engine_context_clear(key);
        return;
    }
    xSemaphoreTake(s_context_mutex, portMAX_DELAY);
    automation_context_var_t *slot = NULL;
    for (size_t i = 0; i < AUTOMATION_CONTEXT_MAX_VARS; ++i) {
        automation_context_var_t *var = &s_context_vars[i];
        if (var->in_use && strcasecmp(var->key, key) == 0) {
            slot = var;
            break;
        }
        if (!var->in_use && !slot) {
            slot = var;
        }
    }
    if (slot) {
        slot->in_use = true;
        ctx_str_copy(slot->key, sizeof(slot->key), key);
        ctx_str_copy(slot->value, sizeof(slot->value), value);
        ESP_LOGI(TAG, "context %s='%s'", slot->key, slot->value);
    } else {
        ESP_LOGW(TAG, "context full, cannot set %s", key);
    }
    xSemaphoreGive(s_context_mutex);
}

void automation_engine_context_clear(const char *key)
{
    if (!key || !key[0] || !s_context_mutex) {
        return;
    }
    xSemaphoreTake(s_context_mutex, portMAX_DELAY);
    for (size_t i = 0; i < AUTOMATION_CONTEXT_MAX_VARS; ++i) {
        automation_context_var_t *var = &s_context_vars[i];
        if (var->in_use && strcasecmp(var->key, key) == 0) {
            var->in_use = false;
            var->key[0] = 0;
            var->value[0] = 0;
            ESP_LOGI(TAG, "context %s cleared", key);
            break;
        }
    }
    xSemaphoreGive(s_context_mutex);
}

size_t automation_engine_context_lookup(const char *key, char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return 0;
    }
    out[0] = 0;
    if (!key || !key[0] || !s_context_mutex) {
        return 0;
    }
    size_t len = 0;
    xSemaphoreTake(s_context_mutex, portMAX_DELAY);
    for (size_t i = 0; i < AUTOMATION_CONTEXT_MAX_VARS; ++i) {
        automation_context_var_t *var = &s_context_vars[i];
        if (var->in_use && strcasecmp(var->key, key) == 0) {
            ctx_str_copy(out, out_len, var->value);
            len = strlen(out);
            break;
        }
    }
    xSemaphoreGive(s_context_mutex);
    return len;
}

void automation_engine_render_template(const char *src, char *dst, size_t dst_len)
{
    if (!dst || dst_len == 0) {
        return;
    }
    dst[0] = 0;
    if (!src) {
        return;
    }
    size_t out_len = 0;
    size_t i = 0;
    while (src[i] && out_len < dst_len - 1) {
        if (src[i] == '{' && src[i + 1] == '{') {
            size_t j = i + 2;
            while (src[j] && !(src[j] == '}' && src[j + 1] == '}')) {
                j++;
            }
            if (!src[j]) {
                while (src[i] && out_len < dst_len - 1) {
                    dst[out_len++] = src[i++];
                }
                break;
            }
            size_t start = i + 2;
            size_t end = j;
            while (start < end && isspace((unsigned char)src[start])) {
                start++;
            }
            while (end > start && isspace((unsigned char)src[end - 1])) {
                end--;
            }
            size_t key_len = end > start ? (end - start) : 0;
            char key[AUTOMATION_CONTEXT_KEY_MAX];
            if (key_len >= sizeof(key)) {
                key_len = sizeof(key) - 1;
            }
            memcpy(key, src + start, key_len);
            key[key_len] = 0;
            char value_buf[AUTOMATION_CONTEXT_VALUE_MAX];
            size_t val_len = automation_engine_context_lookup(key, value_buf, sizeof(value_buf));
            if (val_len == 0) {
                size_t placeholder_len = j + 2 - i;
                if (placeholder_len >= dst_len - out_len) {
                    placeholder_len = dst_len - out_len - 1;
                }
                memcpy(dst + out_len, src + i, placeholder_len);
                out_len += placeholder_len;
            } else {
                if (val_len >= dst_len - out_len) {
                    val_len = dst_len - out_len - 1;
                }
                memcpy(dst + out_len, value_buf, val_len);
                out_len += val_len;
            }
            i = j + 2;
        } else {
            dst[out_len++] = src[i++];
        }
    }
    dst[out_len] = 0;
}
