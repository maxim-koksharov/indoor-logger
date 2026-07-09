#include "data_store.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <esp_log.h>
#include <esp_spiffs.h>

static const char *TAG = "data_store";

static void get_filepath(const char *client_id, char *buf, size_t len) {
    snprintf(buf, len, "/spiffs/%s.dat", client_id);
}

int data_store_init(void) {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SPIFFS: %s", esp_err_to_name(ret));
        return -1;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS total: %d, used: %d", total, used);
    }

    return 0;
}

int data_store_append(const char *client_id, const data_record_t *rec) {
    char filepath[64];
    get_filepath(client_id, filepath, sizeof(filepath));

    FILE *f = fopen(filepath, "r+");
    bool is_new = (f == NULL);
    
    if (is_new) {
        f = fopen(filepath, "w+");
    }
    
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open %s (errno=%d)", filepath, errno);
        return -1;
    }

    client_file_header_t header;
    size_t n = fread(&header, 1, sizeof(header), f);

    if (is_new || n != sizeof(header) || header.magic != DATA_STORE_MAGIC) {
        memset(&header, 0, sizeof(header));
        header.magic = DATA_STORE_MAGIC;
        header.version = DATA_STORE_VERSION;
        strncpy(header.client_id, client_id, sizeof(header.client_id) - 1);
        header.max_records = DATA_STORE_MAX_RECORDS_PER_CLIENT;
        header.write_idx = 0;
        header.count = 0;

        fseek(f, 0, SEEK_SET);
        if (fwrite(&header, 1, sizeof(header), f) != sizeof(header)) {
            ESP_LOGE(TAG, "Failed to write header");
            fclose(f);
            return -1;
        }
    }

    long offset = sizeof(header) + (header.write_idx * sizeof(data_record_t));
    fseek(f, offset, SEEK_SET);

    if (fwrite(rec, 1, sizeof(data_record_t), f) != sizeof(data_record_t)) {
        ESP_LOGE(TAG, "Failed to write record");
        fclose(f);
        return -1;
    }

    header.write_idx = (header.write_idx + 1) % header.max_records;
    if (header.count < header.max_records) {
        header.count++;
    }

    fseek(f, 0, SEEK_SET);
    if (fwrite(&header, 1, sizeof(header), f) != sizeof(header)) {
        ESP_LOGE(TAG, "Failed to update header");
        fclose(f);
        return -1;
    }

    fclose(f);
    return 0;
}

int data_store_read_range(const char *client_id, uint32_t offset, uint32_t limit,
                          data_record_t *out, uint32_t capacity) {
    if (limit > capacity) {
        limit = capacity;
    }

    char filepath[64];
    get_filepath(client_id, filepath, sizeof(filepath));

    FILE *f = fopen(filepath, "r");
    if (f == NULL) {
        return 0;
    }

    client_file_header_t header;
    if (fread(&header, 1, sizeof(header), f) != sizeof(header)) {
        fclose(f);
        return 0;
    }

    if (header.magic != DATA_STORE_MAGIC) {
        fclose(f);
        return 0;
    }

    if (offset >= header.count) {
        fclose(f);
        return 0;
    }

    uint32_t available = header.count - offset;
    if (limit > available) {
        limit = available;
    }

    uint32_t start_idx;
    if (header.count < header.max_records) {
        start_idx = offset;
    } else {
        start_idx = (header.write_idx + offset) % header.max_records;
    }

    uint32_t read_count = 0;
    for (uint32_t i = 0; i < limit; i++) {
        uint32_t idx = (start_idx + i) % header.max_records;
        long file_offset = sizeof(header) + (idx * sizeof(data_record_t));
        fseek(f, file_offset, SEEK_SET);

        if (fread(&out[read_count], 1, sizeof(data_record_t), f) == sizeof(data_record_t)) {
            read_count++;
        }
    }

    fclose(f);
    return read_count;
}

int data_store_read_since(const char *client_id, uint32_t since_ts,
                          data_record_t *out, uint32_t capacity) {
    uint32_t count = data_store_get_count(client_id);
    if (count == 0 || capacity == 0) return 0;

    uint32_t lo = 0, hi = count;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        data_record_t rec;
        if (data_store_read_range(client_id, mid, 1, &rec, 1) != 1) break;
        if (rec.timestamp < since_ts) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    if (lo >= count) return 0;
    uint32_t available = count - lo;
    if (available > capacity) available = capacity;
    return data_store_read_range(client_id, lo, available, out, capacity);
}

uint32_t data_store_get_count(const char *client_id) {
    char filepath[64];
    get_filepath(client_id, filepath, sizeof(filepath));

    FILE *f = fopen(filepath, "r");
    if (f == NULL) {
        return 0;
    }

    client_file_header_t header;
    if (fread(&header, 1, sizeof(header), f) != sizeof(header)) {
        fclose(f);
        return 0;
    }

    fclose(f);

    if (header.magic != DATA_STORE_MAGIC) {
        return 0;
    }

    return header.count;
}

int data_store_delete_client(const char *client_id) {
    char filepath[64];
    get_filepath(client_id, filepath, sizeof(filepath));

    if (unlink(filepath) != 0) {
        ESP_LOGE(TAG, "Failed to delete %s", filepath);
        return -1;
    }

    ESP_LOGI(TAG, "Deleted client data: %s", client_id);
    return 0;
}

int data_store_read_aggregated(const char *client_id, uint32_t since_ts, uint32_t bucket_sec,
                               data_aggregated_t *out, uint32_t capacity) {
    if (capacity == 0 || bucket_sec == 0) return 0;

    uint32_t max_raw = 2000;
    data_record_t *raw = malloc(max_raw * sizeof(data_record_t));
    if (!raw) {
        ESP_LOGE(TAG, "OOM reading raw records for aggregation");
        return 0;
    }

    int raw_count = data_store_read_since(client_id, since_ts, raw, max_raw);
    if (raw_count <= 0) {
        free(raw);
        return 0;
    }

    int bucket_count = 0;
    int i = 0;
    while (i < raw_count && bucket_count < (int)capacity) {
        uint32_t bucket_ts = (raw[i].timestamp / bucket_sec) * bucket_sec;

        int64_t sum_temp = 0;
        int64_t sum_hum = 0;
        uint32_t sum_eco2 = 0;
        uint32_t sum_tvoc = 0;
        uint32_t sum_aqi = 0;
        uint32_t cnt = 0;

        while (i < raw_count) {
            uint32_t ts = (raw[i].timestamp / bucket_sec) * bucket_sec;
            if (ts != bucket_ts) break;

            sum_temp += raw[i].temp_x100;
            sum_hum += raw[i].hum_x100;
            sum_eco2 += raw[i].eco2;
            sum_tvoc += raw[i].tvoc;
            sum_aqi += raw[i].aqi;
            cnt++;
            i++;
        }

        if (cnt > 0) {
            out[bucket_count].timestamp = bucket_ts;
            out[bucket_count].count = (uint16_t)cnt;
            out[bucket_count].temp_avg_x100 = (int16_t)(sum_temp / (int64_t)cnt);
            out[bucket_count].hum_avg_x100 = (uint16_t)(sum_hum / (int64_t)cnt);
            out[bucket_count].eco2_avg = (uint16_t)(sum_eco2 / cnt);
            out[bucket_count].tvoc_avg = (uint16_t)(sum_tvoc / cnt);
            out[bucket_count].aqi_avg = (uint8_t)(sum_aqi / cnt);
            bucket_count++;
        }
    }

    free(raw);
    return bucket_count;
}
