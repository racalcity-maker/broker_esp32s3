#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_timer.h"

#include "config_store.h"
#include "event_bus.h"

#define MQTT_MAX_CLIENTS       CONFIG_BROKER_MQTT_MAX_CLIENTS
#define MQTT_MAX_SUBS          8
#define MQTT_MAX_TOPIC         96
#define MQTT_MAX_PAYLOAD       512
#define MQTT_MAX_PACKET        1024
#define MQTT_RETAIN_MAX        32
#define MQTT_CLIENT_STACK      6144
#define MQTT_ACCEPT_STACK      4096

typedef struct {
    bool in_use;
    char topic[MQTT_MAX_TOPIC];
    char *payload;
    size_t payload_len;
    uint8_t qos;
} retain_entry_t;

typedef struct {
    char topic[MQTT_MAX_TOPIC];
    uint8_t qos;
} mqtt_subscription_t;

typedef struct {
    bool has;
    char topic[MQTT_MAX_TOPIC];
    char payload[MQTT_MAX_PAYLOAD];
    uint8_t qos;
    bool retain;
} will_t;

typedef struct {
    int sock;
    TaskHandle_t task;
    bool active;
    bool closing;
    bool suppress_will;
    char client_id[CONFIG_STORE_CLIENT_ID_MAX];
    uint16_t keepalive;
    int64_t last_rx_ms;
    mqtt_subscription_t subs[MQTT_MAX_SUBS];
    size_t sub_count;
    will_t will;
} mqtt_session_t;

extern mqtt_session_t *s_sessions;
extern StackType_t *s_session_stacks[MQTT_MAX_CLIENTS];
extern StaticTask_t *s_session_tcbs[MQTT_MAX_CLIENTS];
extern uint8_t *s_session_tx_bufs[MQTT_MAX_CLIENTS];
extern retain_entry_t *s_retain;
extern SemaphoreHandle_t s_lock;
extern uint8_t s_client_count;
extern int s_listen_sock;
extern TaskHandle_t s_accept_task;
extern StackType_t *s_accept_stack;
extern StaticTask_t *s_accept_tcb;
extern esp_timer_handle_t s_sweep_timer;
extern bool s_event_handler_registered;

void lock(void);
void unlock(void);
size_t session_index(const mqtt_session_t *sess);
mqtt_session_t *alloc_session(void);
mqtt_session_t *find_session_by_client_id(const char *client_id);
void free_session(mqtt_session_t *s);
void sweep_idle_sessions(void);
bool ensure_session_task_storage(size_t idx);
uint8_t *ensure_session_tx_buffer(size_t idx);
bool ensure_accept_task_storage(void);
int64_t now_ms(void);
void request_session_close(mqtt_session_t *sess, const char *reason, int err);
void send_will_if_needed(mqtt_session_t *sess);
esp_err_t mqtt_core_start_server(int port);

bool acl_can_publish(const char *client_id, const char *topic);
bool acl_can_subscribe(const char *client_id, const char *topic);
bool topic_matches_filter(const char *filter, const char *topic);

const char *find_topic_by_type(event_bus_type_t type);
event_bus_type_t find_type_by_topic(const char *topic);
void on_event_bus_message(const event_bus_message_t *msg);

void retain_store(const char *topic, const char *payload, uint8_t qos);
void deliver_retain(mqtt_session_t *sess, const char *filter);
void publish_to_subscribers(const char *topic,
                            const char *payload,
                            uint8_t qos,
                            bool retain_flag,
                            mqtt_session_t *exclude);

int recv_all(int sock, uint8_t *buf, size_t len);
int send_all(int sock, const uint8_t *buf, size_t len);
int read_remaining_length(int sock, int *out_rem);
int send_connack(int sock, uint8_t rc);
int send_suback(mqtt_session_t *sess, uint16_t pid, uint8_t *qos, size_t count);
int send_unsuback(int sock, uint16_t pid);
int send_puback(int sock, uint16_t pid);
int send_pingresp(int sock);
int send_publish_packet(mqtt_session_t *sess,
                        const char *topic,
                        const char *payload,
                        uint8_t qos,
                        bool retain,
                        uint16_t pid);
int handle_connect(mqtt_session_t *sess, const uint8_t *buf, size_t len);
int handle_subscribe(mqtt_session_t *sess, const uint8_t *buf, size_t len);
int handle_unsubscribe(mqtt_session_t *sess, const uint8_t *buf, size_t len);
int handle_publish(mqtt_session_t *sess, uint8_t header, const uint8_t *buf, size_t len);
