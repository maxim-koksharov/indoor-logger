#include "data_store.h"
#include <stdio.h>
#include <string.h>
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
