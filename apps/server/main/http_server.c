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
#include <esp_spiffs.h>
#include <cJSON.h>
#include "lwip/inet.h"
#include "lwip/sockets.h"

static const char *TAG = "http_server";
static httpd_handle_t server = NULL;

extern uint32_t server_get_timestamp(void);

/* Minimal compatible accessors for the private httpd_req/socket structures
 * in ESP-IDF v3.4, used only to retrieve the client's IP address. */
struct sock_db_compat {
    int fd;
};

struct httpd_req_aux_compat {
    struct sock_db_compat *sd;
};

static void get_client_ip(httpd_req_t *req, char *ip_str, size_t ip_sz) {
    ip_str[0] = '\0';
    if (!req || !req->aux) return;
    struct sock_db_compat *sd = ((struct httpd_req_aux_compat *)req->aux)->sd;
    if (!sd) return;
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    if (getpeername(sd->fd, (struct sockaddr *)&addr, &len) == 0) {
        strncpy(ip_str, inet_ntoa(addr.sin_addr), ip_sz - 1);
        ip_str[ip_sz - 1] = '\0';
    }
}

static void url_decode(char *dst, const char *src) {
    while (*src) {
        if (*src == '%' && src[1] && src[2]) {
            int hi = src[1] >= 'a' ? src[1] - 'a' + 10 : (src[1] >= 'A' ? src[1] - 'A' + 10 : src[1] - '0');
            int lo = src[2] >= 'a' ? src[2] - 'a' + 10 : (src[2] >= 'A' ? src[2] - 'A' + 10 : src[2] - '0');
            *dst++ = (char)((hi << 4) | lo);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' '; src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

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
    url_decode(out, out);
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
        "\"last_seen\":%lu,\"ip\":\"%s\",\"records\":%lu,\"basic_mode\":%s,\"data\":[",
        client->id, client->name,
        client->online ? "true" : "false",
        (unsigned long)client->last_seen, client->ip_str,
        (unsigned long)rec_count,
        client_registry_get_basic_mode(client->id) ? "true" : "false");

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
    cJSON *meta_obj = cJSON_IsObject(root) ? root : NULL;
    if (cJSON_IsArray(root)) {
        records_array = root;
    } else {
        records_array = cJSON_GetObjectItem(root, "readings");
    }

    char client_name[CLIENT_REGISTRY_NAME_LEN] = {0};
    if (meta_obj) {
        cJSON *name_item = cJSON_GetObjectItem(meta_obj, "name");
        if (name_item && cJSON_IsString(name_item) && name_item->valuestring) {
            strncpy(client_name, name_item->valuestring, sizeof(client_name) - 1);
        }
    }

    int count = 0;
    if (records_array && cJSON_IsArray(records_array)) {
        cJSON *item;
        cJSON_ArrayForEach(item, records_array) {
            if (!cJSON_IsObject(item)) continue;
            data_record_t rec = {0};

            cJSON *ts_item = cJSON_GetObjectItem(item, "ts");
            uint32_t client_ts = 0;
            if (ts_item && cJSON_IsNumber(ts_item)) {
                client_ts = (uint32_t)ts_item->valuedouble;
            }
            if (client_ts > 1577836800U) { /* 2020-01-01 */
                rec.timestamp = client_ts;
            } else {
                rec.timestamp = server_get_timestamp();
            }

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

    char client_ip[16] = {0};
    get_client_ip(req, client_ip, sizeof(client_ip));
    client_registry_update(client_id,
                           client_name[0] ? client_name : NULL,
                           client_ip);
    client_registry_save();

    const char *resolved_id = client_registry_resolve_id(client_id);
    bool id_changed = (strncmp(resolved_id, client_id, CLIENT_REGISTRY_ID_LEN) != 0);
    if (id_changed) {
        ESP_LOGI(TAG, "Upload: routed from old_id '%s' to '%s' via pending mapping",
                 client_id, resolved_id);
    }

    client_info_t *client = client_registry_get(resolved_id);
    const char *resp_name = (client && client->name[0]) ? client->name : "";
    uint32_t sync_interval = config_get_sync_interval();
    uint32_t ts = server_get_timestamp();
    const char *tz = config_get_timezone();
    bool basic_mode = client_registry_get_basic_mode(resolved_id);

    char resp_buf[384];
    int resp_len = snprintf(resp_buf, sizeof(resp_buf),
        "{\"status\":\"ok\",\"accepted\":%d,\"timestamp\":%lu,\"sync_interval\":%lu,\"name\":\"%s\",\"timezone\":\"%s\",\"basic_mode\":%s,\"client_id\":\"%s\"}",
        count, (unsigned long)ts, (unsigned long)sync_interval, resp_name, tz,
        basic_mode ? "true" : "false", resolved_id);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp_buf, resp_len);
    ESP_LOGI(TAG, "Upload: accepted %d records from %s (resolved=%s, name='%s', sync=%lu, ts=%lu, basic_mode=%s)",
             count, client_id, resolved_id, resp_name,
             (unsigned long)sync_interval, (unsigned long)ts,
             basic_mode ? "true" : "false");
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
    uint32_t limit = 500;

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
            limit = (val > 0) ? (uint32_t)val : 500;
        }
    }
    if (limit > 2000) limit = 2000;

    uint32_t available = data_store_get_count(client_id);
    data_record_t *records = malloc(limit * sizeof(data_record_t));
    if (records == NULL) {
        ESP_LOGE(TAG, "data_get: OOM (%u records)", limit);
        httpd_resp_send_500(req);
        return ESP_OK;
    }
    int read_count = 0;

    if (query) {
        char since_str[16] = {0};
        get_query_val(query, "since", since_str, sizeof(since_str));
        if (since_str[0]) {
            uint32_t since_ts = (uint32_t)atol(since_str);
            read_count = data_store_read_since(client_id, since_ts, records, limit);
        }
    }

    if (read_count == 0) {
        if (offset >= available) {
            free(records);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, "[]", 2);
            return ESP_OK;
        }
        if (limit > available - offset) limit = available - offset;
        read_count = data_store_read_range(client_id, offset, limit, records, limit);
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send_chunk(req, "[", 1);

    char chunk[256];
    for (int i = 0; i < read_count; i++) {
        char tbuf[16], hbuf[16];
        fmt_temp(tbuf, sizeof(tbuf), records[i].temp_x100);
        fmt_hum(hbuf, sizeof(hbuf), records[i].hum_x100);
        int n = snprintf(chunk, sizeof(chunk),
            "%s{\"timestamp\":%lu,\"temp\":%s,\"hum\":%s,"
            "\"eco2\":%u,\"tvoc\":%u,\"aqi\":%u}",
            i > 0 ? "," : "",
            (unsigned long)records[i].timestamp,
            tbuf, hbuf, records[i].eco2, records[i].tvoc, records[i].aqi);
        if (n > 0) {
            httpd_resp_send_chunk(req, chunk, n);
        }
    }

    httpd_resp_send_chunk(req, "]", 1);
    httpd_resp_send_chunk(req, NULL, 0);

    free(records);
    return ESP_OK;
}

static esp_err_t time_get_handler(httpd_req_t *req) {
    char buf[64];
    uint32_t ts = server_get_timestamp();
    int n = snprintf(buf, sizeof(buf), "{\"timestamp\":%lu}", (unsigned long)ts);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, n);
    return ESP_OK;
}

static esp_err_t sync_interval_get_handler(httpd_req_t *req) {
    char buf[64];
    uint32_t interval = config_get_sync_interval();
    int n = snprintf(buf, sizeof(buf), "{\"sync_interval\":%lu}", (unsigned long)interval);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, n);
    return ESP_OK;
}

static esp_err_t sync_interval_post_handler(httpd_req_t *req) {
    char value_str[16] = {0};
    const char *query = strchr(req->uri, '?');
    get_query_val(query, "value", value_str, sizeof(value_str));
    if (value_str[0] == '\0') {
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "missing value param", 19);
        return ESP_OK;
    }
    int value = atoi(value_str);
    if (value <= 0) {
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "invalid value", 13);
        return ESP_OK;
    }
    config_save_sync_interval((uint32_t)value);
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "{\"ok\":true,\"sync_interval\":%d}", value);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, n);
    return ESP_OK;
}

static esp_err_t timezone_get_handler(httpd_req_t *req) {
    const char *tz = config_get_timezone();
    char buf[128];
    int n = snprintf(buf, sizeof(buf), "{\"timezone\":\"%s\"}", tz);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, n);
    return ESP_OK;
}

static esp_err_t timezone_post_handler(httpd_req_t *req) {
    char value_str[80] = {0};
    const char *query = strchr(req->uri, '?');
    get_query_val(query, "value", value_str, sizeof(value_str));
    if (value_str[0] == '\0') {
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "missing value param", 19);
        return ESP_OK;
    }
    config_save_timezone(value_str);
    char buf[128];
    int n = snprintf(buf, sizeof(buf), "{\"ok\":true,\"timezone\":\"%s\"}", config_get_timezone());
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, n);
    return ESP_OK;
}

static esp_err_t client_name_get_handler(httpd_req_t *req) {
    char client_id[32] = {0};
    char name[CLIENT_REGISTRY_NAME_LEN] = {0};
    const char *query = strchr(req->uri, '?');
    get_query_val(query, "id", client_id, sizeof(client_id));
    get_query_val(query, "name", name, sizeof(name));

    if (client_id[0] == '\0' || name[0] == '\0') {
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "missing id or name", 18);
        return ESP_OK;
    }

    const char *resolved = client_registry_resolve_id(client_id);
    if (client_registry_update(resolved, name, NULL) != 0) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }
    client_registry_save();
    ESP_LOGI(TAG, "Client %s renamed to: %s", resolved, name);

    char buf[96];
    int n = snprintf(buf, sizeof(buf), "{\"id\":\"%s\",\"name\":\"%s\"}", resolved, name);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, n);
    return ESP_OK;
}

static bool is_valid_digit_id(const char *s) {
    if (!s || !*s) return false;
    size_t len = strlen(s);
    if (len == 0 || len >= CLIENT_REGISTRY_ID_LEN) return false;
    for (size_t i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
    }
    return true;
}

static esp_err_t client_id_post_handler(httpd_req_t *req) {
    char client_id[32] = {0};
    char new_id[32] = {0};
    const char *query = strchr(req->uri, '?');
    get_query_val(query, "id", client_id, sizeof(client_id));
    get_query_val(query, "new_id", new_id, sizeof(new_id));

    if (client_id[0] == '\0' || new_id[0] == '\0') {
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "missing id or new_id", 20);
        return ESP_OK;
    }

    if (!is_valid_digit_id(new_id)) {
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "invalid new_id (digits only, 1-31 chars)", 41);
        return ESP_OK;
    }

    const char *resolved_old = client_registry_resolve_id(client_id);
    if (client_registry_get(resolved_old) == NULL) {
        httpd_resp_send_404(req);
        return ESP_OK;
    }
    if (client_registry_get(new_id) != NULL) {
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "new_id already exists", 21);
        return ESP_OK;
    }

    if (client_registry_rename_id(resolved_old, new_id) != 0) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }
    if (data_store_rename_client(resolved_old, new_id) != 0) {
        ESP_LOGW(TAG, "Rename: data migration failed for %s -> %s",
                 resolved_old, new_id);
    }
    client_registry_save();

    ESP_LOGI(TAG, "Client renamed: %s -> %s", resolved_old, new_id);

    char buf[96];
    int n = snprintf(buf, sizeof(buf), "{\"id\":\"%s\",\"new_id\":\"%s\"}", resolved_old, new_id);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, n);
    return ESP_OK;
}

static esp_err_t storage_get_handler(httpd_req_t *req) {
    size_t total = 0, used = 0;
    esp_err_t ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }
    size_t free_bytes = (used <= total) ? (total - used) : 0;
    char buf[96];
    int n = snprintf(buf, sizeof(buf),
        "{\"total\":%u,\"used\":%u,\"free\":%u}",
        (unsigned)total, (unsigned)used, (unsigned)free_bytes);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, n);
    return ESP_OK;
}

static esp_err_t client_delete_handler(httpd_req_t *req) {
    char client_id[32] = {0};
    const char *query = strchr(req->uri, '?');
    get_query_val(query, "id", client_id, sizeof(client_id));

    if (client_id[0] == '\0') {
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "missing id param", 15);
        return ESP_OK;
    }

    if (client_registry_delete(client_id) != 0) {
        httpd_resp_send_404(req);
        return ESP_OK;
    }

    data_store_delete_client(client_id);

    char buf[48];
    int n = snprintf(buf, sizeof(buf), "{\"deleted\":\"%s\"}", client_id);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, n);
    return ESP_OK;
}

static esp_err_t client_basic_mode_get_handler(httpd_req_t *req) {
    char client_id[32] = {0};
    const char *query = strchr(req->uri, '?');
    get_query_val(query, "id", client_id, sizeof(client_id));
    if (client_id[0] == '\0') {
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "missing id param", 16);
        return ESP_OK;
    }

    client_info_t *c = client_registry_get(client_id);
    if (c == NULL) {
        httpd_resp_send_404(req);
        return ESP_OK;
    }

    bool basic_mode = client_registry_get_basic_mode(client_id);
    char buf[96];
    int n = snprintf(buf, sizeof(buf),
        "{\"id\":\"%s\",\"basic_mode\":%s}", client_id,
        basic_mode ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, n);
    return ESP_OK;
}

static esp_err_t client_basic_mode_post_handler(httpd_req_t *req) {
    char client_id[32] = {0};
    char value_str[8] = {0};
    const char *query = strchr(req->uri, '?');
    get_query_val(query, "id", client_id, sizeof(client_id));
    get_query_val(query, "value", value_str, sizeof(value_str));

    if (client_id[0] == '\0' || value_str[0] == '\0') {
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "missing id or value", 19);
        return ESP_OK;
    }

    int v = atoi(value_str);
    if (v != 0 && v != 1) {
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "invalid value", 13);
        return ESP_OK;
    }

    if (client_registry_set_basic_mode(client_id, v == 1) != 0) {
        client_registry_update(client_id, NULL, NULL);
    }
    if (client_registry_set_basic_mode(client_id, v == 1) != 0) {
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "client not found", 16);
        return ESP_OK;
    }
    client_registry_save();

    char buf[96];
    int n = snprintf(buf, sizeof(buf), "{\"ok\":true,\"id\":\"%s\",\"basic_mode\":%s}",
                     client_id, v ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, n);
    return ESP_OK;
}

static esp_err_t data_aggregated_get_handler(httpd_req_t *req) {
    char client_id[32] = {0};
    const char *query = strchr(req->uri, '?');
    get_query_val(query, "id", client_id, sizeof(client_id));
    if (client_id[0] == '\0') {
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "missing id", 10);
        return ESP_OK;
    }

    uint32_t since_ts = 0;
    uint32_t bucket_sec = 300;
    if (query) {
        char tmp[16] = {0};
        get_query_val(query, "since", tmp, sizeof(tmp));
        if (tmp[0]) since_ts = (uint32_t)atol(tmp);
        tmp[0] = '\0';
        get_query_val(query, "bucket", tmp, sizeof(tmp));
        if (tmp[0]) {
            int val = atoi(tmp);
            if (val > 0) bucket_sec = (uint32_t)val;
        }
    }

    uint32_t capacity = 480;
    data_aggregated_t *buckets = malloc(capacity * sizeof(data_aggregated_t));
    if (!buckets) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    int count = data_store_read_aggregated(client_id, since_ts, bucket_sec, buckets, capacity);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send_chunk(req, "[", 1);

    char chunk[256];
    for (int i = 0; i < count; i++) {
        char tbuf[16], hbuf[16];
        fmt_temp(tbuf, sizeof(tbuf), buckets[i].temp_avg_x100);
        fmt_hum(hbuf, sizeof(hbuf), buckets[i].hum_avg_x100);
        int n = snprintf(chunk, sizeof(chunk),
            "%s{\"ts\":%lu,\"c\":%u,\"temp\":%s,\"hum\":%s,"
            "\"eco2\":%u,\"tvoc\":%u,\"aqi\":%u}",
            i > 0 ? "," : "",
            (unsigned long)buckets[i].timestamp,
            buckets[i].count,
            tbuf, hbuf,
            buckets[i].eco2_avg, buckets[i].tvoc_avg, buckets[i].aqi_avg);
        if (n > 0) {
            httpd_resp_send_chunk(req, chunk, n);
        }
    }

    httpd_resp_send_chunk(req, "]", 1);
    httpd_resp_send_chunk(req, NULL, 0);

    free(buckets);
    return ESP_OK;
}

int http_server_init(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 6144;
    config.max_uri_handlers = 20;
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

    httpd_uri_t agg_uri = {
        .uri = "/api/data/aggregated",
        .method = HTTP_GET,
        .handler = data_aggregated_get_handler
    };
    httpd_register_uri_handler(server, &agg_uri);

    httpd_uri_t name_uri = {
        .uri = "/api/client/name",
        .method = HTTP_GET,
        .handler = client_name_get_handler
    };
    httpd_register_uri_handler(server, &name_uri);

    httpd_uri_t id_post_uri = {
        .uri = "/api/client/id",
        .method = HTTP_POST,
        .handler = client_id_post_handler
    };
    httpd_register_uri_handler(server, &id_post_uri);

    httpd_uri_t storage_uri = {
        .uri = "/api/storage",
        .method = HTTP_GET,
        .handler = storage_get_handler
    };
    httpd_register_uri_handler(server, &storage_uri);

    httpd_uri_t client_delete_uri = {
        .uri = "/api/client",
        .method = HTTP_DELETE,
        .handler = client_delete_handler
    };
    httpd_register_uri_handler(server, &client_delete_uri);

    httpd_uri_t basic_mode_uri = {
        .uri = "/api/client/basic-mode",
        .method = HTTP_GET,
        .handler = client_basic_mode_get_handler
    };
    httpd_register_uri_handler(server, &basic_mode_uri);

    httpd_uri_t basic_mode_post_uri = {
        .uri = "/api/client/basic-mode",
        .method = HTTP_POST,
        .handler = client_basic_mode_post_handler
    };
    httpd_register_uri_handler(server, &basic_mode_post_uri);

    httpd_uri_t time_uri = {
        .uri = "/api/time",
        .method = HTTP_GET,
        .handler = time_get_handler
    };
    httpd_register_uri_handler(server, &time_uri);

    httpd_uri_t sync_interval_uri = {
        .uri = "/api/sync-interval",
        .method = HTTP_GET,
        .handler = sync_interval_get_handler
    };
    httpd_register_uri_handler(server, &sync_interval_uri);

    httpd_uri_t sync_interval_post_uri = {
        .uri = "/api/sync-interval",
        .method = HTTP_POST,
        .handler = sync_interval_post_handler
    };
    httpd_register_uri_handler(server, &sync_interval_post_uri);

    httpd_uri_t timezone_uri = {
        .uri = "/api/timezone",
        .method = HTTP_GET,
        .handler = timezone_get_handler
    };
    httpd_register_uri_handler(server, &timezone_uri);

    httpd_uri_t timezone_post_uri = {
        .uri = "/api/timezone",
        .method = HTTP_POST,
        .handler = timezone_post_handler
    };
    httpd_register_uri_handler(server, &timezone_post_uri);

    ESP_LOGI(TAG, "HTTP server started with 14 endpoints");
    return 0;
}
