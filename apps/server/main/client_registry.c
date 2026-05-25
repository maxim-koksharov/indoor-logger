#include "client_registry.h"
#include <string.h>
#include <time.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>

static const char *TAG = "client_registry";
static client_info_t clients[CLIENT_REGISTRY_MAX_CLIENTS];
static int client_count = 0;

int client_registry_init(void) {
    memset(clients, 0, sizeof(clients));
    client_count = 0;
    return 0;
}

int client_registry_update(const char *id, const char *name, const char *ip) {
    client_info_t *c = client_registry_get(id);
    
    if (c == NULL) {
        if (client_count >= CLIENT_REGISTRY_MAX_CLIENTS) {
            ESP_LOGW(TAG, "Registry full, cannot add %s", id);
            return -1;
        }
        c = &clients[client_count++];
        strncpy(c->id, id, CLIENT_REGISTRY_ID_LEN - 1);
    }
    
    if (name && name[0]) {
        strncpy(c->name, name, CLIENT_REGISTRY_NAME_LEN - 1);
    }
    
    if (ip && ip[0]) {
        strncpy(c->ip_str, ip, CLIENT_REGISTRY_IP_LEN - 1);
    }
    
    c->last_seen = (uint32_t)time(NULL);
    c->online = true;
    
    return 0;
}

client_info_t *client_registry_get(const char *id) {
    for (int i = 0; i < client_count; i++) {
        if (strncmp(clients[i].id, id, CLIENT_REGISTRY_ID_LEN) == 0) {
            return &clients[i];
        }
    }
    return NULL;
}

int client_registry_get_all(client_info_t *out, int capacity) {
    int count = (client_count < capacity) ? client_count : capacity;
    memcpy(out, clients, count * sizeof(client_info_t));
    return count;
}

void client_registry_check_stale(uint32_t timeout_sec) {
    uint32_t now = (uint32_t)time(NULL);
    
    for (int i = 0; i < client_count; i++) {
        if (clients[i].online && (now - clients[i].last_seen) > timeout_sec) {
            clients[i].online = false;
            ESP_LOGI(TAG, "Client %s marked offline (last seen %d sec ago)",
                     clients[i].id, now - clients[i].last_seen);
        }
    }
}

int client_registry_save(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("client_reg", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return -1;
    }
    
    err = nvs_set_blob(handle, "clients", clients, sizeof(clients));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save clients: %s", esp_err_to_name(err));
        nvs_close(handle);
        return -1;
    }
    
    err = nvs_set_i32(handle, "count", client_count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save count: %s", esp_err_to_name(err));
        nvs_close(handle);
        return -1;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit: %s", esp_err_to_name(err));
        return -1;
    }
    
    ESP_LOGI(TAG, "Saved %d clients to NVS", client_count);
    return 0;
}

int client_registry_load(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("client_reg", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved registry in NVS");
        return 0;
    }
    
    size_t len = sizeof(clients);
    err = nvs_get_blob(handle, "clients", clients, &len);
    if (err != ESP_OK || len != sizeof(clients)) {
        ESP_LOGW(TAG, "Failed to load clients: %s", esp_err_to_name(err));
        nvs_close(handle);
        return 0;
    }
    
    int32_t count = 0;
    err = nvs_get_i32(handle, "count", &count);
    if (err == ESP_OK && count > 0 && count <= CLIENT_REGISTRY_MAX_CLIENTS) {
        client_count = count;
    }
    
    nvs_close(handle);
    
    for (int i = 0; i < client_count; i++) {
        clients[i].online = false;
    }
    
    ESP_LOGI(TAG, "Loaded %d clients from NVS", client_count);
    return client_count;
}
