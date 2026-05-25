#ifndef CLIENT_REGISTRY_H
#define CLIENT_REGISTRY_H

#include <stdint.h>
#include <stdbool.h>

#define CLIENT_REGISTRY_MAX_CLIENTS 16
#define CLIENT_REGISTRY_ID_LEN 32
#define CLIENT_REGISTRY_NAME_LEN 32
#define CLIENT_REGISTRY_IP_LEN 16

typedef struct {
    char id[CLIENT_REGISTRY_ID_LEN];
    char name[CLIENT_REGISTRY_NAME_LEN];
    uint32_t last_seen;
    bool online;
    char ip_str[CLIENT_REGISTRY_IP_LEN];
} client_info_t;

int client_registry_init(void);
int client_registry_update(const char *id, const char *name, const char *ip);
client_info_t *client_registry_get(const char *id);
int client_registry_get_all(client_info_t *out, int capacity);
void client_registry_check_stale(uint32_t timeout_sec);
int client_registry_save(void);
int client_registry_load(void);

#endif
