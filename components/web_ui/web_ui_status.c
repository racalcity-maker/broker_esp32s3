#include "web_ui_utils.h"
#include "web_ui_handlers.h"

#include <stdio.h>
#include <string.h>

#include "audio_player.h"
#include "cJSON.h"
#include "config_store.h"
#include "device_manager.h"
#include "dm_template_runtime.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/ip4_addr.h"
#include "mqtt_core.h"
#include "network.h"
#include "ota_manager.h"
#include "sd_storage.h"
#include "service_status.h"

static esp_err_t web_http_check(esp_err_t err, const char *context)
{
    if (err != ESP_OK) {
        web_ui_report_httpd_error(err, context);
    }
    return err;
}

#define WEB_HTTP_CHECK(call) web_http_check((call), __func__)

static cJSON *empty_json_array(void)
{
    return cJSON_CreateArray();
}

static cJSON *build_uid_monitor_json(void)
{
    const device_manager_config_t *cfg = device_manager_lock_config();
    if (!cfg) {
        device_manager_unlock_config();
        return empty_json_array();
    }
    cJSON *root = cJSON_CreateArray();
    if (!root) {
        device_manager_unlock_config();
        return empty_json_array();
    }
    uint8_t limit = cfg->device_capacity ? cfg->device_capacity : DEVICE_MANAGER_MAX_DEVICES;
    for (uint8_t i = 0; i < cfg->device_count && i < limit; ++i) {
        const device_descriptor_t *dev = &cfg->devices[i];
        if (!dev->template_assigned || dev->template_config.type != DM_TEMPLATE_TYPE_UID) {
            continue;
        }
        cJSON *dev_obj = cJSON_CreateObject();
        if (!dev_obj) {
            cJSON_Delete(root);
            device_manager_unlock_config();
            return empty_json_array();
        }
        cJSON_AddStringToObject(dev_obj, "id", dev->id);
        cJSON_AddStringToObject(dev_obj, "name", dev->display_name[0] ? dev->display_name : dev->id);
        cJSON *slot_arr = cJSON_AddArrayToObject(dev_obj, "slots");
        if (!slot_arr) {
            cJSON_Delete(root);
            device_manager_unlock_config();
            return empty_json_array();
        }
        dm_uid_runtime_snapshot_t snapshot;
        bool have_snapshot = (dm_template_runtime_get_uid_snapshot(dev->id, &snapshot) == ESP_OK);
        const dm_uid_template_t *tpl = &dev->template_config.data.uid;
        uint8_t slot_count = tpl->slot_count;
        if (slot_count > DM_UID_TEMPLATE_MAX_SLOTS) {
            slot_count = DM_UID_TEMPLATE_MAX_SLOTS;
        }
        for (uint8_t s = 0; s < slot_count; ++s) {
            const dm_uid_slot_t *slot_cfg = &tpl->slots[s];
            if (!slot_cfg->source_id[0]) {
                continue;
            }
            cJSON *slot_obj = cJSON_CreateObject();
            if (!slot_obj) {
                cJSON_Delete(root);
                device_manager_unlock_config();
                return empty_json_array();
            }
            cJSON_AddNumberToObject(slot_obj, "index", s);
            cJSON_AddStringToObject(slot_obj, "source", slot_cfg->source_id);
            if (slot_cfg->label[0]) {
                cJSON_AddStringToObject(slot_obj, "label", slot_cfg->label);
            }
            if (have_snapshot && s < snapshot.slot_count && snapshot.slots[s].has_value &&
                snapshot.slots[s].last_value[0]) {
                cJSON_AddStringToObject(slot_obj, "last_value", snapshot.slots[s].last_value);
            }
            cJSON_AddItemToArray(slot_arr, slot_obj);
        }
        cJSON_AddItemToArray(root, dev_obj);
    }
    device_manager_unlock_config();
    return root;
}

static cJSON *build_mqtt_users_json(const app_mqtt_config_t *mqtt_cfg)
{
    cJSON *root = cJSON_CreateArray();
    if (!root) {
        return empty_json_array();
    }
    if (mqtt_cfg) {
        for (uint8_t i = 0; i < mqtt_cfg->user_count && i < CONFIG_STORE_MAX_MQTT_USERS; ++i) {
            const app_mqtt_user_t *user = &mqtt_cfg->users[i];
            if (!user->client_id[0]) {
                continue;
            }
            cJSON *obj = cJSON_CreateObject();
            if (!obj) {
                cJSON_Delete(root);
                return empty_json_array();
            }
            cJSON_AddStringToObject(obj, "client_id", user->client_id);
            cJSON_AddStringToObject(obj, "username", user->username);
            cJSON_AddStringToObject(obj, "password", user->password);
            cJSON_AddItemToArray(root, obj);
        }
    }
    return root;
}

esp_err_t status_handler(httpd_req_t *req)
{
    const app_config_t *cfg = config_store_get();
    ota_manager_status_t ota = {0};
    ota_manager_get_status(&ota);
    char ip_buf[32] = "";
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip) == ESP_OK && ip.ip.addr != 0) {
        snprintf(ip_buf, sizeof(ip_buf), IPSTR, IP2STR(&ip.ip));
    }
    mqtt_client_stats_t stats;
    mqtt_core_get_client_stats(&stats);
    audio_player_status_t a_status;
    audio_player_get_status(&a_status);
    uint64_t kb_total = 0, kb_free = 0;
    if (!sd_storage_available()) {
        (void)sd_storage_mount();
    }
    bool sd_ok = (sd_storage_info(&kb_total, &kb_free) == ESP_OK);
    uint64_t sd_total = sd_ok ? kb_total : 0;
    uint64_t sd_free = sd_ok ? kb_free : 0;
    uint32_t dram_free_kb = (uint32_t)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024);
    uint32_t dram_total_kb = (uint32_t)(heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024);
    uint32_t psram_free_kb = (uint32_t)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) / 1024);
    uint32_t psram_total_kb = (uint32_t)(heap_caps_get_total_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) / 1024);
    service_status_entry_t network_status = {0};
    service_status_entry_t mqtt_status = {0};
    service_status_entry_t audio_service_status = {0};
    service_status_entry_t web_service_status = {0};
    (void)service_status_get(SERVICE_STATUS_NETWORK, &network_status);
    (void)service_status_get(SERVICE_STATUS_MQTT, &mqtt_status);
    (void)service_status_get(SERVICE_STATUS_AUDIO, &audio_service_status);
    (void)service_status_get(SERVICE_STATUS_WEB_UI, &web_service_status);
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem"));
    }

    cJSON *wifi = cJSON_AddObjectToObject(root, "wifi");
    cJSON *mqtt = cJSON_AddObjectToObject(root, "mqtt");
    cJSON *audio = cJSON_AddObjectToObject(root, "audio");
    cJSON *web = cJSON_AddObjectToObject(root, "web");
    cJSON *web_operator = web ? cJSON_AddObjectToObject(web, "operator") : NULL;
    cJSON *sd = cJSON_AddObjectToObject(root, "sd");
    cJSON *diag = cJSON_AddObjectToObject(root, "diag");
    cJSON *mem = cJSON_AddObjectToObject(root, "mem");
    cJSON *dram = mem ? cJSON_AddObjectToObject(mem, "dram") : NULL;
    cJSON *psram = mem ? cJSON_AddObjectToObject(mem, "psram") : NULL;
    cJSON *clients = cJSON_AddObjectToObject(root, "clients");
    cJSON *ota_obj = cJSON_AddObjectToObject(root, "ota");
    cJSON *services = cJSON_AddObjectToObject(root, "services");
    cJSON *uid_monitor = build_uid_monitor_json();
    cJSON *mqtt_users = build_mqtt_users_json(&cfg->mqtt);

    if (!wifi || !mqtt || !audio || !web || !web_operator || !sd || !diag || !mem ||
        !dram || !psram || !clients || !ota_obj || !services || !uid_monitor || !mqtt_users) {
        if (uid_monitor) {
            cJSON_Delete(uid_monitor);
        }
        if (mqtt_users) {
            cJSON_Delete(mqtt_users);
        }
        cJSON_Delete(root);
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem"));
    }

    cJSON_AddStringToObject(wifi, "ssid", cfg->wifi.ssid);
    cJSON_AddStringToObject(wifi, "host", cfg->wifi.hostname);
    cJSON_AddStringToObject(wifi, "sta_ip", ip_buf);
    cJSON_AddBoolToObject(wifi, "ap", network_is_ap_mode());

    cJSON_AddStringToObject(mqtt, "id", cfg->mqtt.broker_id);
    cJSON_AddNumberToObject(mqtt, "port", cfg->mqtt.port);
    cJSON_AddNumberToObject(mqtt, "keepalive", cfg->mqtt.keepalive_seconds);
    cJSON_AddItemToObject(mqtt, "users", mqtt_users);

    cJSON_AddNumberToObject(audio, "volume", audio_player_get_volume());
    cJSON_AddBoolToObject(audio, "playing", a_status.playing);
    cJSON_AddBoolToObject(audio, "paused", a_status.paused);
    cJSON_AddNumberToObject(audio, "progress", a_status.progress);
    cJSON_AddNumberToObject(audio, "pos_ms", a_status.pos_ms);
    cJSON_AddNumberToObject(audio, "dur_ms", a_status.dur_ms);
    cJSON_AddNumberToObject(audio, "bitrate", a_status.bitrate_kbps);
    cJSON_AddStringToObject(audio, "path", a_status.path);
    cJSON_AddStringToObject(audio, "message", a_status.message);
    cJSON_AddNumberToObject(audio, "fmt", a_status.fmt);

    cJSON_AddStringToObject(web, "username", cfg->web.username);
    cJSON_AddBoolToObject(web_operator, "enabled", cfg->web_user_enabled);
    cJSON_AddStringToObject(web_operator, "username", cfg->web_user.username);

    cJSON_AddBoolToObject(sd, "ok", sd_ok);
    cJSON_AddNumberToObject(sd, "total", (double)sd_total);
    cJSON_AddNumberToObject(sd, "free", (double)sd_free);

    cJSON_AddBoolToObject(diag, "verbose_logging", cfg->verbose_logging);

    cJSON_AddNumberToObject(dram, "free_kb", dram_free_kb);
    cJSON_AddNumberToObject(dram, "total_kb", dram_total_kb);
    cJSON_AddNumberToObject(psram, "free_kb", psram_free_kb);
    cJSON_AddNumberToObject(psram, "total_kb", psram_total_kb);

    cJSON_AddNumberToObject(clients, "total", stats.total);

    cJSON_AddStringToObject(ota_obj, "version", ota.app_version);
    cJSON_AddStringToObject(ota_obj, "running_partition", ota.running_partition);
    cJSON_AddStringToObject(ota_obj, "boot_partition", ota.boot_partition);
    cJSON_AddStringToObject(ota_obj, "phase", ota.phase);
    cJSON_AddBoolToObject(ota_obj, "rollback_supported", ota.rollback_supported);
    cJSON_AddBoolToObject(ota_obj, "pending_verify", ota.pending_verify);
    cJSON_AddBoolToObject(ota_obj, "in_progress", ota.in_progress);
    cJSON_AddBoolToObject(ota_obj, "system_ready", ota.system_ready);
    cJSON_AddBoolToObject(ota_obj, "reboot_required", ota.reboot_required);
    cJSON_AddBoolToObject(ota_obj, "last_success", ota.last_success);
    cJSON_AddNumberToObject(ota_obj, "bytes_written", (double)ota.bytes_written);
    cJSON_AddNumberToObject(ota_obj, "total_bytes", (double)ota.total_bytes);
    cJSON_AddStringToObject(ota_obj, "last_error", ota.last_error);

    cJSON *svc_network = cJSON_AddObjectToObject(services, "network");
    cJSON *svc_mqtt = cJSON_AddObjectToObject(services, "mqtt");
    cJSON *svc_audio = cJSON_AddObjectToObject(services, "audio");
    cJSON *svc_web = cJSON_AddObjectToObject(services, "web_ui");
    if (!svc_network || !svc_mqtt || !svc_audio || !svc_web) {
        cJSON_Delete(uid_monitor);
        cJSON_Delete(root);
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem"));
    }
    cJSON_AddBoolToObject(svc_network, "init_attempted", network_status.init_attempted);
    cJSON_AddBoolToObject(svc_network, "init_ok", network_status.init_ok);
    cJSON_AddBoolToObject(svc_network, "start_attempted", network_status.start_attempted);
    cJSON_AddBoolToObject(svc_network, "start_ok", network_status.start_ok);

    cJSON_AddBoolToObject(svc_mqtt, "init_attempted", mqtt_status.init_attempted);
    cJSON_AddBoolToObject(svc_mqtt, "init_ok", mqtt_status.init_ok);
    cJSON_AddBoolToObject(svc_mqtt, "start_attempted", mqtt_status.start_attempted);
    cJSON_AddBoolToObject(svc_mqtt, "start_ok", mqtt_status.start_ok);

    cJSON_AddBoolToObject(svc_audio, "init_attempted", audio_service_status.init_attempted);
    cJSON_AddBoolToObject(svc_audio, "init_ok", audio_service_status.init_ok);
    cJSON_AddBoolToObject(svc_audio, "start_attempted", audio_service_status.start_attempted);
    cJSON_AddBoolToObject(svc_audio, "start_ok", audio_service_status.start_ok);

    cJSON_AddBoolToObject(svc_web, "init_attempted", web_service_status.init_attempted);
    cJSON_AddBoolToObject(svc_web, "init_ok", web_service_status.init_ok);
    cJSON_AddBoolToObject(svc_web, "start_attempted", web_service_status.start_attempted);
    cJSON_AddBoolToObject(svc_web, "start_ok", web_service_status.start_ok);

    cJSON_AddItemToObject(root, "uid_monitor", uid_monitor);

    return WEB_HTTP_CHECK(web_ui_send_json(req, root));
}
