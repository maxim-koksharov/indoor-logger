#include "http_server.h"
#include "data_store.h"
#include "client_registry.h"
#include "web_ui.h"
#include <string.h>
#include <time.h>
#include <esp_log.h>
#include <esp_http_server.h>
#include <esp_system.h>
#include <cJSON.h>

static const char *TAG = "http_server";
static httpd_handle_t server = NULL;

static esp_err_t index_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, index_html, sizeof(index_html) - 1);
    return ESP_OK;
}

static esp_err_t health_get_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddNumberToObject(root, "uptime", (double)xTaskGetTickCount() * portTICK_PERIOD_MS / 1000.0);
    cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size());
    
    client_info_t clients[CLIENT_REGISTRY_MAX_CLIENTS];
    int count = client_registry_get_all(clients, CLIENT_REGISTRY_MAX_CLIENTS);
    int online = 0;
    for (int i = 0; i < count; i++) {
        if (clients[i].online) online++;
    }
    cJSON_AddNumberToObject(root, "clients_online", online);
    
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    
    cJSON_Delete(root);
    free(json);
    return ESP_OK;
}

static esp_err_t clients_get_handler(httpd_req_t *req) {
    client_info_t clients[CLIENT_REGISTRY_MAX_CLIENTS];
    int count = client_registry_get_all(clients, CLIENT_REGISTRY_MAX_CLIENTS);
    
    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON *c = cJSON_CreateObject();
        cJSON_AddStringToObject(c, "id", clients[i].id);
        cJSON_AddStringToObject(c, "name", clients[i].name);
        cJSON_AddBoolToObject(c, "online", clients[i].online);
        cJSON_AddNumberToObject(c, "last_seen", clients[i].last_seen);
        cJSON_AddStringToObject(c, "ip", clients[i].ip_str);
        
        uint32_t rec_count = data_store_get_count(clients[i].id);
        cJSON_AddNumberToObject(c, "records", rec_count);
        
        cJSON_AddItemToArray(root, c);
    }
    
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    
    cJSON_Delete(root);
    free(json);
    return ESP_OK;
}

static esp_err_t client_get_handler(httpd_req_t *req) {
    char uri[128];
    strncpy(uri, req->uri, sizeof(uri) - 1);
    uri[sizeof(uri) - 1] = '\0';
    
    char *id_start = uri + strlen("/api/clients/");
    char *id_end = strchr(id_start, '/');
    if (id_end) *id_end = '\0';
    
    client_info_t *client = client_registry_get(id_start);
    if (client == NULL) {
        httpd_resp_send_404(req);
        return ESP_OK;
    }
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "id", client->id);
    cJSON_AddStringToObject(root, "name", client->name);
    cJSON_AddBoolToObject(root, "online", client->online);
    cJSON_AddNumberToObject(root, "last_seen", client->last_seen);
    cJSON_AddStringToObject(root, "ip", client->ip_str);
    
    uint32_t rec_count = data_store_get_count(client->id);
    cJSON_AddNumberToObject(root, "records", rec_count);
    
    data_record_t records[24];
    int read_count = data_store_read_range(client->id, 
        rec_count > 24 ? rec_count - 24 : 0, 24, records, 24);
    
    cJSON *data = cJSON_CreateArray();
    for (int i = 0; i < read_count; i++) {
        cJSON *r = cJSON_CreateObject();
        cJSON_AddNumberToObject(r, "timestamp", records[i].timestamp);
        cJSON_AddNumberToObject(r, "temp", records[i].temp_x100 / 100.0);
        cJSON_AddNumberToObject(r, "hum", records[i].hum_x100 / 100.0);
        cJSON_AddNumberToObject(r, "eco2", records[i].eco2);
        cJSON_AddNumberToObject(r, "tvoc", records[i].tvoc);
        cJSON_AddNumberToObject(r, "aqi", records[i].aqi);
        cJSON_AddItemToArray(data, r);
    }
    cJSON_AddItemToObject(root, "data", data);
    
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    
    cJSON_Delete(root);
    free(json);
    return ESP_OK;
}

static esp_err_t client_upload_post_handler(httpd_req_t *req) {
    char uri[128];
    strncpy(uri, req->uri, sizeof(uri) - 1);
    uri[sizeof(uri) - 1] = '\0';
    
    char *id_start = uri + strlen("/api/clients/");
    char *upload_pos = strstr(id_start, "/upload");
    if (upload_pos) *upload_pos = '\0';
    
    char *buf = malloc(req->content_len + 1);
    if (buf == NULL) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }
    
    int received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, buf + received, req->content_len - received);
        if (ret <= 0) {
            free(buf);
            httpd_resp_send_500(req);
            return ESP_OK;
        }
        received += ret;
    }
    buf[received] = '\0';
    
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    
    if (root == NULL) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }
    
    int count = 0;
    if (cJSON_IsArray(root)) {
        cJSON *item;
        cJSON_ArrayForEach(item, root) {
            data_record_t rec = {0};
            rec.timestamp = (uint32_t)time(NULL);
            
            cJSON *temp = cJSON_GetObjectItem(item, "temp");
            if (temp) rec.temp_x100 = (int16_t)(temp->valuedouble * 100);
            
            cJSON *hum = cJSON_GetObjectItem(item, "hum");
            if (hum) rec.hum_x100 = (uint16_t)(hum->valuedouble * 100);
            
            cJSON *eco2 = cJSON_GetObjectItem(item, "eco2");
            if (eco2) rec.eco2 = (uint16_t)eco2->valuedouble;
            
            cJSON *tvoc = cJSON_GetObjectItem(item, "tvoc");
            if (tvoc) rec.tvoc = (uint16_t)tvoc->valuedouble;
            
            cJSON *aqi = cJSON_GetObjectItem(item, "aqi");
            if (aqi) rec.aqi = (uint8_t)aqi->valuedouble;
            
            if (data_store_append(id_start, &rec) == 0) {
                count++;
            }
        }
    }
    
    cJSON_Delete(root);
    
    client_registry_update(id_start, "", "");
    client_registry_save();
    
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "accepted", count);
    
    char *json = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    
    cJSON_Delete(resp);
    free(json);
    return ESP_OK;
}

static esp_err_t client_data_get_handler(httpd_req_t *req) {
    char uri[128];
    strncpy(uri, req->uri, sizeof(uri) - 1);
    uri[sizeof(uri) - 1] = '\0';
    
    char *id_start = uri + strlen("/api/clients/");
    char *data_pos = strstr(id_start, "/data");
    if (data_pos) *data_pos = '\0';
    
    uint32_t offset = 0;
    uint32_t limit = 100;
    
    char *query = strchr(req->uri, '?');
    if (query) {
        char offset_str[16] = {0};
        char limit_str[16] = {0};
        httpd_query_key_value(query + 1, "offset", offset_str, sizeof(offset_str));
        httpd_query_key_value(query + 1, "limit", limit_str, sizeof(limit_str));
        if (offset_str[0]) offset = atoi(offset_str);
        if (limit_str[0]) limit = atoi(limit_str);
    }
    
    if (limit > 1000) limit = 1000;
    
    data_record_t *records = malloc(limit * sizeof(data_record_t));
    if (records == NULL) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }
    
    int read_count = data_store_read_range(id_start, offset, limit, records, limit);
    
    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < read_count; i++) {
        cJSON *r = cJSON_CreateObject();
        cJSON_AddNumberToObject(r, "timestamp", records[i].timestamp);
        cJSON_AddNumberToObject(r, "temp", records[i].temp_x100 / 100.0);
        cJSON_AddNumberToObject(r, "hum", records[i].hum_x100 / 100.0);
        cJSON_AddNumberToObject(r, "eco2", records[i].eco2);
        cJSON_AddNumberToObject(r, "tvoc", records[i].tvoc);
        cJSON_AddNumberToObject(r, "aqi", records[i].aqi);
        cJSON_AddItemToArray(root, r);
    }
    
    free(records);
    
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    
    cJSON_Delete(root);
    free(json);
    return ESP_OK;
}

int http_server_init(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10;
    
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return -1;
    }
    
    return 0;
}

int http_server_start(void) {
    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_get_handler
    };
    httpd_register_uri_handler(server, &index_uri);
    
    httpd_uri_t health_uri = {
        .uri = "/api/health",
        .method = HTTP_GET,
        .handler = health_get_handler
    };
    httpd_register_uri_handler(server, &health_uri);
    
    httpd_uri_t clients_uri = {
        .uri = "/api/clients",
        .method = HTTP_GET,
        .handler = clients_get_handler
    };
    httpd_register_uri_handler(server, &clients_uri);
    
    httpd_uri_t client_uri = {
        .uri = "/api/clients/*",
        .method = HTTP_GET,
        .handler = client_get_handler
    };
    httpd_register_uri_handler(server, &client_uri);
    
    httpd_uri_t upload_uri = {
        .uri = "/api/clients/*/upload",
        .method = HTTP_POST,
        .handler = client_upload_post_handler
    };
    httpd_register_uri_handler(server, &upload_uri);
    
    httpd_uri_t data_uri = {
        .uri = "/api/clients/*/data",
        .method = HTTP_GET,
        .handler = client_data_get_handler
    };
    httpd_register_uri_handler(server, &data_uri);
    
    ESP_LOGI(TAG, "HTTP server started with 6 endpoints");
    return 0;
}
