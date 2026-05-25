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

/* --- Helper: extract query param by key --- */
static const char *get_query_val(const char *query, const char *key, char *out, size_t out_sz) {
    if (!query || !*query) return NULL;
    const char *k = strstr(query, key);
    if (!k) return NULL;
    k += strlen(key);
    if (*k != '=') return NULL;
    k++;
    const char *e = strchr(k, '&');
    size_t len = e ? (size_t)(e - k) : strlen(k);
    if (len >= out_sz) len = out_sz - 1;
    memcpy(out, k, len);
    out[len] = '\0';
    return out;
}

/* --- Handlers --- */

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
    char client_id[32] = {0};
    const char *query = strchr(req->uri, '?');
    get_query_val(query, "id", client_id, sizeof(client_id));
    if (client_id[0] == '\0') {
        httpd_resp_send_404(req);
        return ESP_OK;
    }
    
    client_info_t *client = client_registry_get(client_id);
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

static esp_err_t upload_post_handler(httpd_req_t *req) {
    char client_id[32] = {0};
    const char *query = strchr(req->uri, '?');
    get_query_val(query, "id", client_id, sizeof(client_id));
    if (client_id[0] == '\0') {
        ESP_LOGW(TAG, "Upload: missing client id");
        httpd_resp_send_404(req);
        return ESP_OK;
    }
    
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
    
    cJSON *records_array = NULL;
    if (cJSON_IsArray(root)) {
        records_array = root;
    } else {
        records_array = cJSON_GetObjectItem(root, "readings");
    }
    
    int count = 0;
    if (records_array && cJSON_IsArray(records_array)) {
        cJSON *item;
        cJSON_ArrayForEach(item, records_array) {
            data_record_t rec = {0};
            rec.timestamp = (uint32_t)time(NULL);
            
            cJSON *t = cJSON_GetObjectItem(item, "t");
            if (!t) t = cJSON_GetObjectItem(item, "temp");
            if (t) rec.temp_x100 = (int16_t)(t->valuedouble * 100);
            
            cJSON *h = cJSON_GetObjectItem(item, "h");
            if (!h) h = cJSON_GetObjectItem(item, "hum");
            if (h) rec.hum_x100 = (uint16_t)(h->valuedouble * 100);
            
            cJSON *c = cJSON_GetObjectItem(item, "c");
            if (!c) c = cJSON_GetObjectItem(item, "eco2");
            if (c) rec.eco2 = (uint16_t)c->valuedouble;
            
            cJSON *v = cJSON_GetObjectItem(item, "v");
            if (!v) v = cJSON_GetObjectItem(item, "tvoc");
            if (v) rec.tvoc = (uint16_t)v->valuedouble;
            
            cJSON *a = cJSON_GetObjectItem(item, "a");
            if (!a) a = cJSON_GetObjectItem(item, "aqi");
            if (a) rec.aqi = (uint8_t)a->valuedouble;
            
            data_store_append(client_id, &rec);
            count++;
        }
    }
    
    cJSON_Delete(root);
    
    client_registry_update(client_id, "", "");
    client_registry_save();
    
    char resp_buf[64];
    int resp_len = snprintf(resp_buf, sizeof(resp_buf), "{\"accepted\":%d}", count);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp_buf, resp_len);
    ESP_LOGI(TAG, "Upload: accepted %d records from %s", count, client_id);
    return ESP_OK;
}

static esp_err_t data_get_handler(httpd_req_t *req) {
    char client_id[32] = {0};
    const char *query = strchr(req->uri, '?');
    get_query_val(query, "id", client_id, sizeof(client_id));
    if (client_id[0] == '\0') {
        httpd_resp_send_404(req);
        return ESP_OK;
    }
    
    uint32_t offset = 0;
    uint32_t limit = 100;
    
    if (query) {
        char tmp[16] = {0};
        get_query_val(query, "offset", tmp, sizeof(tmp));
        if (tmp[0]) offset = atoi(tmp);
        tmp[0] = '\0';
        get_query_val(query, "limit", tmp, sizeof(tmp));
        if (tmp[0]) limit = atoi(tmp);
    }
    if (limit > 1000) limit = 1000;
    
    data_record_t *records = malloc(limit * sizeof(data_record_t));
    if (records == NULL) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }
    
    int read_count = data_store_read_range(client_id, offset, limit, records, limit);
    
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
        .uri = "/api/client",
        .method = HTTP_GET,
        .handler = client_get_handler
    };
    httpd_register_uri_handler(server, &client_uri);
    
    httpd_uri_t upload_uri = {
        .uri = "/api/upload",
        .method = HTTP_POST,
        .handler = upload_post_handler
    };
    httpd_register_uri_handler(server, &upload_uri);
    
    httpd_uri_t data_uri = {
        .uri = "/api/data",
        .method = HTTP_GET,
        .handler = data_get_handler
    };
    httpd_register_uri_handler(server, &data_uri);
    
    ESP_LOGI(TAG, "HTTP server started with 6 endpoints");
    return 0;
}
