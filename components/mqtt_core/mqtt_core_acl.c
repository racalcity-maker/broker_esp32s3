#include "mqtt_core_internal.h"

#include <string.h>

typedef struct {
    const char *client_id;
    const char *pub_prefix;
    const char *sub_prefix;
} acl_entry_t;

static const acl_entry_t k_acl[] = {
    {"pn532",  "access/", "access/"},
    {"laser",  "laser/",  "laser/"},
    {"relay",  "relay/",  "relay/"},
    {"puppet", "puppet/", "puppet/"},
    {"webui",  "web/",    "web/"},
    {"*",      "*",       "*"},
};

static bool prefix_match(const char *prefix, const char *topic)
{
    if (!prefix || !topic) {
        return false;
    }
    if (strcmp(prefix, "*") == 0) {
        return true;
    }
    size_t len = strlen(prefix);
    return strncmp(topic, prefix, len) == 0;
}

bool acl_can_publish(const char *client_id, const char *topic)
{
    for (size_t i = 0; i < sizeof(k_acl) / sizeof(k_acl[0]); ++i) {
        if (prefix_match(k_acl[i].client_id, client_id)) {
            return prefix_match(k_acl[i].pub_prefix, topic);
        }
    }
    return false;
}

bool acl_can_subscribe(const char *client_id, const char *topic)
{
    for (size_t i = 0; i < sizeof(k_acl) / sizeof(k_acl[0]); ++i) {
        if (prefix_match(k_acl[i].client_id, client_id)) {
            return prefix_match(k_acl[i].sub_prefix, topic);
        }
    }
    return false;
}

bool topic_matches_filter(const char *filter, const char *topic)
{
    if (!filter || !topic) {
        return false;
    }

    const char *f = filter;
    const char *t = topic;

    while (true) {
        size_t flen = 0;
        size_t tlen = 0;

        while (f[flen] && f[flen] != '/') {
            flen++;
        }
        while (t[tlen] && t[tlen] != '/') {
            tlen++;
        }

        if (flen == 1 && f[0] == '#') {
            return f[1] == '\0';
        }

        if (!(flen == 1 && f[0] == '+')) {
            if (flen != tlen || memcmp(f, t, flen) != 0) {
                return false;
            }
        }

        f += flen;
        t += tlen;

        if (*f == '\0' && *t == '\0') {
            return true;
        }
        if (*f == '/' && *t == '/') {
            ++f;
            ++t;
            continue;
        }
        if (*f == '/' && *t == '\0') {
            return (f[1] == '#' && f[2] == '\0');
        }
        return false;
    }
}
