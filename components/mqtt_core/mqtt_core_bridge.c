#include "mqtt_core.h"
#include "mqtt_core_internal.h"

#include <string.h>

typedef struct {
    event_bus_type_t type;
    const char *topic;
} event_topic_map_t;

static const event_topic_map_t k_outgoing_map[] = {
    {EVENT_AUDIO_PLAY, "audio/play"},
    {EVENT_SYSTEM_STATUS, "sys/broker/metrics"},
    {EVENT_CARD_OK, "access/card/ok"},
    {EVENT_CARD_BAD, "access/card/bad"},
    {EVENT_RELAY_CMD, "relay/cmd"},
    {EVENT_WEB_COMMAND, "web/cmd"},
};

static const event_topic_map_t k_incoming_map[] = {
    {EVENT_AUDIO_PLAY, "audio/play"},
    {EVENT_RELAY_CMD, "relay/"},
    {EVENT_WEB_COMMAND, "web/cmd"},
};

const char *find_topic_by_type(event_bus_type_t type)
{
    for (size_t i = 0; i < sizeof(k_outgoing_map) / sizeof(k_outgoing_map[0]); ++i) {
        if (k_outgoing_map[i].type == type) {
            return k_outgoing_map[i].topic;
        }
    }
    return NULL;
}

const char *mqtt_core_topic_for_event(event_bus_type_t type)
{
    return find_topic_by_type(type);
}

event_bus_type_t find_type_by_topic(const char *topic)
{
    if (!topic) {
        return EVENT_NONE;
    }
    for (size_t i = 0; i < sizeof(k_incoming_map) / sizeof(k_incoming_map[0]); ++i) {
        const char *t = k_incoming_map[i].topic;
        size_t len = strlen(t);
        if (strncmp(topic, t, len) == 0) {
            return k_incoming_map[i].type;
        }
    }
    return EVENT_NONE;
}

void on_event_bus_message(const event_bus_message_t *msg)
{
    if (!msg) {
        return;
    }
    const char *topic = msg->topic[0] ? msg->topic : find_topic_by_type(msg->type);
    if (!topic) {
        return;
    }
    mqtt_core_publish(topic, msg->payload);
}
