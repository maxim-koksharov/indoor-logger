#include "http_server.h"
#include "data_store.h"
#include "client_registry.h"
#include "web_ui.h"
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <esp_log.h>
#include <esp_http_server.h>
#include <esp_system.h>
#include <cJSON.h>

static const char *TAG = "http_server";
static httpd_handle_t server = NULL;

extern uint32_t server_get_timestamp(void);

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

static esp_err_t index_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, index_html, sizeof(index_html) - 1);
    return ESP_OK;
}

static void fmt_temp(char *buf, size_t sz, int16_t x100) {
    int sign = (x100 < 0) ? -1 : 1;
    int val = x100 * sign;
    snprintf(buf, sz, "%d.%02d", sign < 0 ? -val/100 : val/100, val % 100);
}

static void fmt_hum(char *buf, size_t sz, uint16_t x100) {
    snprintf(buf, sz, "%d.%02d", x100 / 100, x100 % 100);
}

static esp_err_t health_get_handler(httpd_req_t *req) {
    int uptime = (xTaskGetTickCount() * portTICK_PERIOD_MS) / 1000;
    int free_heap = esp_get_free_heap_size();

    int online = 0;
    client_info_t *clients = malloc(CLIENT_REGISTRY_MAX_CLIENTS * sizeof(client_info_t));
    if (clients) {
        int count = client_registry_get_all(clients, CLIENT_REGISTRY_MAX_CLIENTS);
        for (int i = 0; i < count; i++) {
            if (clients[i].online) online++;
        }
        free(clients);
    }

    char buf[256];
    int n = snprintf(buf, sizeof(buf),
        "{\"status\":\"ok\",\"uptime\":%d,\"free_heap\":%d,\"clients_online\":%d}",
        uptime, free_heap, online);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, n);
    return ESP_OK;
}

static esp_err_t clients_get_handler(httpd_req_t *req) {
    client_info_t *clients = malloc(CLIENT_REGISTRY_MAX_CLIENTS * sizeof(client_info_t));
    if (!clients) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    int count = client_registry_get_all(clients, CLIENT_REGISTRY_MAX_CLIENTS);

    size_t bufsz = 4096;
    char *buf = malloc(bufsz);
    if (!buf) {
        free(clients);
        httpd_resp_send_500(req);
        return ESP_OK;
    }
    size_t pos = 0;
    buf[pos++] = '[';

    for (int i = 0; i < count; i++) {
        if (i > 0 && pos < bufsz) buf[pos++] = ',';
        uint32_t rec_count = data_store_get_count(clients[i].id);
        int n = snprintf(buf + pos, bufsz - pos,
            "{\"id\":\"%s\",\"name\":\"%s\",\"online\":%s,"
            "\"last_seen\":%lu,\"ip\":\"%s\",\"records\":%lu}",
            clients[i].id, clients[i].name,
            clients[i].online ? "true" : "false",
            (unsigned long)clients[i].last_seen, clients[i].ip_str,
            (unsigned long)rec_count);
        if (n > 0) pos += n;
        if (pos >= bufsz - 128) break;
    }

    if (pos < bufsz) buf[pos++] = ']';
    buf[pos] = '\0';

    free(clients);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, pos);
    free(buf);
    return ESP_OK;
}

static esp_err_t client_get_handler(httpd_req_t *req) {
    char client_id[32] = {0};
    const char *query = strchr(req->uri, '?');
    get_query_val(query, "id", client_id, sizeof(client_id));
    if (client_id[0] == '\0') {
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "missing id param", 16);
        return ESP_OK;
    }

    client_info_t *client = client_registry_get(client_id);
    if (client == NULL) {
        httpd_resp_send_404(req);
        return ESP_OK;
    }

    uint32_t rec_count = data_store_get_count(client->id);
    uint32_t limit = 24;
    uint32_t offset = rec_count > limit ? rec_count - limit : 0;

    data_record_t *records = malloc(limit * sizeof(data_record_t));
    int read_count = 0;
    if (records) {
        read_count = data_store_read_range(client->id, offset, limit, records, limit);
    }

    size_t bufsz = 4096 + read_count * 128;
    char *buf = malloc(bufsz);
    if (!buf) {
        free(records);
        httpd_resp_send_500(req);
        return ESP_OK;
    }
    size_t pos = 0;

    pos += snprintf(buf + pos, bufsz - pos,
        "{\"id\":\"%s\",\"name\":\"%s\",\"online\":%s,"
        "\"last_seen\":%lu,\"ip\":\"%s\",\"records\":%lu,\"data\":[",
        client->id, client->name,
        client->online ? "true" : "false",
        (unsigned long)client->last_seen, client->ip_str,
        (unsigned long)rec_count);

    for (int i = 0; i < read_count; i++) {
        if (i > 0 && pos < bufsz) buf[pos++] = ',';
        char tbuf[16], hbuf[16];
        fmt_temp(tbuf, sizeof(tbuf), records[i].temp_x100);
        fmt_hum(hbuf, sizeof(hbuf), records[i].hum_x100);
        int n = snprintf(buf + pos, bufsz - pos,
            "{\"timestamp\":%lu,\"temp\":%s,\"hum\":%s,"
            "\"eco2\":%u,\"tvoc\":%u,\"aqi\":%u}",
            (unsigned long)records[i].timestamp,
            tbuf, hbuf, records[i].eco2, records[i].tvoc, records[i].aqi);
        if (n > 0) pos += n;
        if (pos >= bufsz - 128) break;
    }

    if (pos < bufsz) buf[pos++] = ']';
    if (pos < bufsz) buf[pos++] = '}';
    buf[pos] = '\0';

    free(records);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, pos);
    free(buf);
    return ESP_OK;
}

static esp_err_t upload_post_handler(httpd_req_t *req) {
    char client_id[32] = {0};
    const char *query = strchr(req->uri, '?');
    get_query_val(query, "id", client_id, sizeof(client_id));
    if (client_id[0] == '\0') {
        ESP_LOGW(TAG, "Upload: missing client id");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "missing id", 10);
        return ESP_OK;
    }

    if (req->content_len <= 0) {
        ESP_LOGW(TAG, "Upload: empty body from %s", client_id);
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "empty body", 10);
        return ESP_OK;
    }

    if (req->content_len > 8192) {
        ESP_LOGW(TAG, "Upload: body too large (%d) from %s", req->content_len, client_id);
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "body too large", 14);
        return ESP_OK;
    }

    char *buf = malloc(req->content_len + 1);
    if (buf == NULL) {
        ESP_LOGE(TAG, "Upload: OOM (%d bytes)", req->content_len + 1);
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    int received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, buf + received, req->content_len - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                ESP_LOGW(TAG, "Upload: recv timeout from %s", client_id);
            } else {
                ESP_LOGE(TAG, "Upload: recv error %d from %s", ret, client_id);
            }
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
        ESP_LOGW(TAG, "Upload: invalid JSON from %s", client_id);
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "invalid json", 12);
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
            if (!cJSON_IsObject(item)) continue;
            data_record_t rec = {0};
            rec.timestamp = server_get_timestamp();

            cJSON *t = cJSON_GetObjectItem(item, "t");
            if (!t) t = cJSON_GetObjectItem(item, "temp");
            if (t && cJSON_IsNumber(t)) rec.temp_x100 = (int16_t)(t->valuedouble * 100);

            cJSON *h = cJSON_GetObjectItem(item, "h");
            if (!h) h = cJSON_GetObjectItem(item, "hum");
            if (h && cJSON_IsNumber(h)) rec.hum_x100 = (uint16_t)(h->valuedouble * 100);

            cJSON *c = cJSON_GetObjectItem(item, "c");
            if (!c) c = cJSON_GetObjectItem(item, "eco2");
            if (c && cJSON_IsNumber(c)) rec.eco2 = (uint16_t)c->valuedouble;

            cJSON *v = cJSON_GetObjectItem(item, "v");
            if (!v) v = cJSON_GetObjectItem(item, "tvoc");
            if (v && cJSON_IsNumber(v)) rec.tvoc = (uint16_t)v->valuedouble;

            cJSON *a = cJSON_GetObjectItem(item, "a");
            if (!a) a = cJSON_GetObjectItem(item, "aqi");
            if (a && cJSON_IsNumber(a)) rec.aqi = (uint8_t)a->valuedouble;

            if (data_store_append(client_id, &rec) != 0) {
                ESP_LOGW(TAG, "Upload: append failed for record %d", count);
                continue;
            }
            count++;
        }
    } else {
        ESP_LOGW(TAG, "Upload: no readings array in JSON from %s", client_id);
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
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "missing id", 10);
        return ESP_OK;
    }

    uint32_t offset = 0;
    uint32_t limit = 100;

    if (query) {
        char tmp[16] = {0};
        get_query_val(query, "offset", tmp, sizeof(tmp));
        if (tmp[0]) {
            int val = atoi(tmp);
            offset = (val >= 0) ? (uint32_t)val : 0;
        }
        tmp[0] = '\0';
        get_query_val(query, "limit", tmp, sizeof(tmp));
        if (tmp[0]) {
            int val = atoi(tmp);
            limit = (val > 0) ? (uint32_t)val : 100;
        }
    }
    if (limit > 200) limit = 200;

    uint32_t available = data_store_get_count(client_id);
    if (offset >= available || limit == 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "[]", 2);
        return ESP_OK;
    }
    if (limit > available - offset) {
        limit = available - offset;
    }

    data_record_t *records = malloc(limit * sizeof(data_record_t));
    if (records == NULL) {
        ESP_LOGE(TAG, "data_get: OOM (%u records)", limit);
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    int read_count = data_store_read_range(client_id, offset, limit, records, limit);

    size_t bufsz = 256 + read_count * 128;
    char *buf = malloc(bufsz);
    if (!buf) {
        free(records);
        httpd_resp_send_500(req);
        return ESP_OK;
    }
    size_t pos = 0;
    buf[pos++] = '[';

    for (int i = 0; i < read_count; i++) {
        if (i > 0 && pos < bufsz) buf[pos++] = ',';
        char tbuf[16], hbuf[16];
        fmt_temp(tbuf, sizeof(tbuf), records[i].temp_x100);
        fmt_hum(hbuf, sizeof(hbuf), records[i].hum_x100);
        int n = snprintf(buf + pos, bufsz - pos,
            "{\"timestamp\":%lu,\"temp\":%s,\"hum\":%s,"
            "\"eco2\":%u,\"tvoc\":%u,\"aqi\":%u}",
            (unsigned long)records[i].timestamp,
            tbuf, hbuf, records[i].eco2, records[i].tvoc, records[i].aqi);
        if (n > 0) pos += n;
        if (pos >= bufsz - 128) break;
    }

    if (pos < bufsz) buf[pos++] = ']';
    buf[pos] = '\0';

    free(records);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, pos);
    free(buf);
    return ESP_OK;
}

int http_server_init(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 6144;
    config.max_uri_handlers = 10;
    config.max_open_sockets = 8;
    config.lru_purge_enable = true;

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
