#include "client_registry.h"
#include "data_store.h"
#include <string.h>
#include <time.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>

static const char *TAG = "client_registry";
static client_info_t clients[CLIENT_REGISTRY_MAX_CLIENTS];
static bool basic_modes[CLIENT_REGISTRY_MAX_CLIENTS];
static int client_count = 0;

static id_change_mapping_t id_mappings[CLIENT_REGISTRY_MAX_CLIENTS];
static int id_mapping_count = 0;

extern uint32_t server_get_timestamp(void);

int client_registry_init(void) {
    memset(clients, 0, sizeof(clients));
    memset(basic_modes, 1, sizeof(basic_modes));
    client_count = 0;
    memset(id_mappings, 0, sizeof(id_mappings));
    id_mapping_count = 0;
    return 0;
}

const char *client_registry_resolve_id(const char *id) {
    if (!id) return NULL;
    for (int i = 0; i < id_mapping_count; i++) {
        if (strncmp(id_mappings[i].old_id, id, CLIENT_REGISTRY_ID_LEN) == 0) {
            return id_mappings[i].new_id;
        }
    }
    return id;
}

int client_registry_rename_id(const char *old_id, const char *new_id) {
    if (!old_id || !old_id[0] || !new_id || !new_id[0]) return -1;
    if (strncmp(old_id, new_id, CLIENT_REGISTRY_ID_LEN) == 0) return 0;

    client_info_t *c = client_registry_get(old_id);
    if (c == NULL) {
        ESP_LOGW(TAG, "Rename: old_id '%s' not found", old_id);
        return -1;
    }
    if (client_registry_get(new_id) != NULL) {
        ESP_LOGW(TAG, "Rename: new_id '%s' already exists", new_id);
        return -1;
    }

    for (int i = 0; i < id_mapping_count; i++) {
        if (strncmp(id_mappings[i].old_id, old_id, CLIENT_REGISTRY_ID_LEN) == 0) {
            strncpy(id_mappings[i].new_id, new_id, CLIENT_REGISTRY_ID_LEN - 1);
            id_mappings[i].new_id[CLIENT_REGISTRY_ID_LEN - 1] = '\0';
            ESP_LOGI(TAG, "Rename: updated mapping %s -> %s", old_id, new_id);
            goto update_entry;
        }
    }

    if (id_mapping_count >= CLIENT_REGISTRY_MAX_CLIENTS) {
        ESP_LOGE(TAG, "Rename: mapping table full");
        return -1;
    }
    strncpy(id_mappings[id_mapping_count].old_id, old_id, CLIENT_REGISTRY_ID_LEN - 1);
    id_mappings[id_mapping_count].old_id[CLIENT_REGISTRY_ID_LEN - 1] = '\0';
    strncpy(id_mappings[id_mapping_count].new_id, new_id, CLIENT_REGISTRY_ID_LEN - 1);
    id_mappings[id_mapping_count].new_id[CLIENT_REGISTRY_ID_LEN - 1] = '\0';
    id_mapping_count++;
    ESP_LOGI(TAG, "Rename: added mapping %s -> %s", old_id, new_id);

update_entry:
    strncpy(c->id, new_id, CLIENT_REGISTRY_ID_LEN - 1);
    c->id[CLIENT_REGISTRY_ID_LEN - 1] = '\0';
    return 0;
}

void client_registry_clear_id_mapping(const char *old_id) {
    if (!old_id || !old_id[0]) return;
    for (int i = 0; i < id_mapping_count; i++) {
        if (strncmp(id_mappings[i].old_id, old_id, CLIENT_REGISTRY_ID_LEN) == 0) {
            for (int j = i; j < id_mapping_count - 1; j++) {
                id_mappings[j] = id_mappings[j + 1];
            }
            id_mapping_count--;
            memset(&id_mappings[id_mapping_count], 0, sizeof(id_mappings[id_mapping_count]));
            ESP_LOGI(TAG, "Rename: cleared mapping for old_id '%s'", old_id);
            return;
        }
    }
}

int client_registry_update(const char *id, const char *name, const char *ip, bool overwrite_name) {
    client_info_t *c = client_registry_get(id);

    if (c == NULL && ip && ip[0]) {
        for (int i = 0; i < client_count; i++) {
            if (strncmp(clients[i].ip_str, ip, CLIENT_REGISTRY_IP_LEN) == 0 &&
                strncmp(clients[i].id, id, CLIENT_REGISTRY_ID_LEN) != 0) {
                ESP_LOGI(TAG, "Dedup: IP '%s' already bound to '%s', migrating to '%s'",
                         ip, clients[i].id, id);
                data_store_rename_client(clients[i].id, id);
                client_registry_clear_id_mapping(clients[i].id);
                memset(clients[i].id, 0, CLIENT_REGISTRY_ID_LEN);
                memset(clients[i].ip_str, 0, CLIENT_REGISTRY_IP_LEN);
                basic_modes[i] = true;
                for (int j = i; j < client_count - 1; j++) {
                    clients[j] = clients[j + 1];
                    basic_modes[j] = basic_modes[j + 1];
                }
                client_count--;
                memset(&clients[client_count], 0, sizeof(clients[client_count]));
                basic_modes[client_count] = true;
                break;
            }
        }
        c = client_registry_get(id);
    }

    if (c == NULL) {
        if (client_count >= CLIENT_REGISTRY_MAX_CLIENTS) {
            ESP_LOGW(TAG, "Registry full, cannot add %s", id);
            return -1;
        }
        c = &clients[client_count++];
        strncpy(c->id, id, CLIENT_REGISTRY_ID_LEN - 1);
        int idx = (int)(c - clients);
        basic_modes[idx] = true;
    }

    if (name && name[0] && (overwrite_name || c->name[0] == '\0')) {
        strncpy(c->name, name, CLIENT_REGISTRY_NAME_LEN - 1);
    }

    if (ip && ip[0]) {
        strncpy(c->ip_str, ip, CLIENT_REGISTRY_IP_LEN - 1);
    }
    
    c->last_seen = server_get_timestamp();
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

bool client_registry_get_basic_mode(const char *id) {
    for (int i = 0; i < client_count; i++) {
        if (strncmp(clients[i].id, id, CLIENT_REGISTRY_ID_LEN) == 0) {
            return basic_modes[i];
        }
    }
    return true;
}

int client_registry_set_basic_mode(const char *id, bool value) {
    for (int i = 0; i < client_count; i++) {
        if (strncmp(clients[i].id, id, CLIENT_REGISTRY_ID_LEN) == 0) {
            if (basic_modes[i] != value) {
                basic_modes[i] = value;
                ESP_LOGI(TAG, "Client %s basic_mode = %s", id, value ? "true" : "false");
            }
            return 0;
        }
    }
    return -1;
}

int client_registry_get_all(client_info_t *out, int capacity) {
    int count = (client_count < capacity) ? client_count : capacity;
    memcpy(out, clients, count * sizeof(client_info_t));
    return count;
}

void client_registry_check_stale(uint32_t timeout_sec) {
    uint32_t now = server_get_timestamp();
    
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

    err = nvs_set_blob(handle, "basic_modes", basic_modes, sizeof(basic_modes));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save basic_modes: %s", esp_err_to_name(err));
        nvs_close(handle);
        return -1;
    }

    err = nvs_set_i32(handle, "count", client_count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save count: %s", esp_err_to_name(err));
        nvs_close(handle);
        return -1;
    }

    err = nvs_set_blob(handle, "id_mappings", id_mappings, sizeof(id_mappings));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save id_mappings: %s", esp_err_to_name(err));
        nvs_close(handle);
        return -1;
    }

    err = nvs_set_i32(handle, "id_mapping_count", id_mapping_count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save id_mapping_count: %s", esp_err_to_name(err));
        nvs_close(handle);
        return -1;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit: %s", esp_err_to_name(err));
        return -1;
    }

    ESP_LOGI(TAG, "Saved %d clients, %d id mappings to NVS",
             client_count, id_mapping_count);
    return 0;
}

int client_registry_load(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("client_reg", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved registry in NVS");
        memset(basic_modes, 1, sizeof(basic_modes));
        memset(id_mappings, 0, sizeof(id_mappings));
        id_mapping_count = 0;
        return 0;
    }

    size_t len = sizeof(clients);
    err = nvs_get_blob(handle, "clients", clients, &len);
    if (err != ESP_OK || len != sizeof(clients)) {
        ESP_LOGW(TAG, "Failed to load clients: %s", esp_err_to_name(err));
        memset(basic_modes, 1, sizeof(basic_modes));
        memset(id_mappings, 0, sizeof(id_mappings));
        id_mapping_count = 0;
        nvs_close(handle);
        return 0;
    }

    int32_t count = 0;
    err = nvs_get_i32(handle, "count", &count);
    if (err == ESP_OK && count > 0 && count <= CLIENT_REGISTRY_MAX_CLIENTS) {
        client_count = count;
    }

    len = sizeof(basic_modes);
    err = nvs_get_blob(handle, "basic_modes", basic_modes, &len);
    if (err != ESP_OK || len != sizeof(basic_modes)) {
        ESP_LOGI(TAG, "No saved basic_modes, defaulting all to true");
        memset(basic_modes, 1, sizeof(basic_modes));
    }

    len = sizeof(id_mappings);
    err = nvs_get_blob(handle, "id_mappings", id_mappings, &len);
    if (err != ESP_OK || len != sizeof(id_mappings)) {
        ESP_LOGI(TAG, "No saved id_mappings");
        memset(id_mappings, 0, sizeof(id_mappings));
        id_mapping_count = 0;
    } else {
        int32_t mc = 0;
        if (nvs_get_i32(handle, "id_mapping_count", &mc) == ESP_OK &&
            mc > 0 && mc <= CLIENT_REGISTRY_MAX_CLIENTS) {
            id_mapping_count = mc;
        } else {
            id_mapping_count = 0;
        }
    }

    nvs_close(handle);

    for (int i = 0; i < client_count; i++) {
        clients[i].online = false;
    }

    ESP_LOGI(TAG, "Loaded %d clients, %d id mappings from NVS",
             client_count, id_mapping_count);
    return client_count;
}

int client_registry_delete(const char *id) {
    if (!id || !id[0]) return -1;

    for (int i = 0; i < client_count; i++) {
        if (strncmp(clients[i].id, id, CLIENT_REGISTRY_ID_LEN) == 0) {
            ESP_LOGI(TAG, "Deleted client '%s'", id);
            for (int j = i; j < client_count - 1; j++) {
                clients[j] = clients[j + 1];
                basic_modes[j] = basic_modes[j + 1];
            }
            client_count--;
            memset(&clients[client_count], 0, sizeof(clients[client_count]));
            basic_modes[client_count] = true;
            client_registry_save();
            return 0;
        }
    }
    return -1;
}
