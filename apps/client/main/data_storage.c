#include "data_storage.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifndef CONFIG_CLIENT_ID
#define CONFIG_CLIENT_ID CONFIG_CLIENT_ID
#endif

static const char *TAG = "data_storage";
static const char *DATA_FILE = "/spiffs/client_data.dat";

#define MAGIC 0x434C4944
#define VERSION 1

/* Reserve this many bytes of SPIFFS for metadata, GC, and look-up tables.
 * SPIFFS uses ~0.5-1.5% of the partition for its own needs depending on
 * page size and object count. 32 KB is a safe upper bound for our 256-byte
 * page / 32-char object name setup on partitions up to 16 MB. */
#define SPIFFS_RESERVE_BYTES (32 * 1024)

typedef struct {
    uint32_t magic;
    uint8_t version;
    char client_id[32];
    uint32_t max_records;
    uint32_t write_idx;
    uint32_t total_written;
    uint32_t unsynced_count;
} file_header_t;

static file_header_t s_header;
static FILE *s_fp = NULL;

static int read_header(void) {
    fseek(s_fp, 0, SEEK_SET);
    if (fread(&s_header, sizeof(s_header), 1, s_fp) != 1) {
        return -1;
    }
    return 0;
}

static int write_header(void) {
    fseek(s_fp, 0, SEEK_SET);
    if (fwrite(&s_header, sizeof(s_header), 1, s_fp) != 1) {
        return -1;
    }
    fflush(s_fp);
    return 0;
}

static long record_offset(uint32_t idx) {
    return (long)sizeof(file_header_t) + (long)idx * (long)sizeof(client_record_t);
}

/* Compute the maximum number of records that fit into the SPIFFS partition.
 * The partition has spiffs_total bytes; we reserve SPIFFS_RESERVE_BYTES for
 * SPIFFS metadata, GC, and one file header. The rest is divided by record
 * size to get the ring buffer capacity. */
static uint32_t compute_max_records(size_t spiffs_total) {
    if (spiffs_total <= SPIFFS_RESERVE_BYTES + sizeof(file_header_t)) {
        return 0;
    }
    size_t usable = spiffs_total - SPIFFS_RESERVE_BYTES - sizeof(file_header_t);
    return (uint32_t)(usable / sizeof(client_record_t));
}

int data_storage_init(void) {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SPIFFS: %s", esp_err_to_name(ret));
        return -1;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info("storage", &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS total: %u, used: %u", (unsigned)total, (unsigned)used);
    }

    uint32_t max_records = compute_max_records(total);
    if (max_records == 0) {
        ESP_LOGE(TAG, "SPIFFS partition too small for any records (total=%u)", (unsigned)total);
        return -1;
    }
    ESP_LOGI(TAG, "Max records in ring buffer: %u (record size %u)",
             (unsigned)max_records, (unsigned)sizeof(client_record_t));

    s_fp = fopen(DATA_FILE, "r+b");
    bool need_init = (s_fp == NULL);
    if (s_fp == NULL) {
        ESP_LOGI(TAG, "Creating new data file");
        s_fp = fopen(DATA_FILE, "w+b");
        if (s_fp == NULL) {
            ESP_LOGE(TAG, "Failed to create data file");
            return -1;
        }
    }

    bool header_valid = false;
    if (!need_init) {
        if (read_header() == 0 && s_header.magic == MAGIC) {
            header_valid = true;
        }
    }

    if (!header_valid) {
        if (!need_init) {
            ESP_LOGW(TAG, "Invalid header, reinitializing data file");
            fclose(s_fp);
            remove(DATA_FILE);
            s_fp = fopen(DATA_FILE, "w+b");
            if (s_fp == NULL) {
                ESP_LOGE(TAG, "Failed to create data file after reinit");
                return -1;
            }
        }
        memset(&s_header, 0, sizeof(s_header));
        s_header.magic = MAGIC;
        s_header.version = VERSION;
        strncpy(s_header.client_id, CONFIG_CLIENT_ID, sizeof(s_header.client_id) - 1);
        s_header.max_records = max_records;
        s_header.write_idx = 0;
        s_header.total_written = 0;
        s_header.unsynced_count = 0;

        if (write_header() != 0) {
            ESP_LOGE(TAG, "Failed to write header");
            fclose(s_fp);
            s_fp = NULL;
            return -1;
        }
        ESP_LOGI(TAG, "Initialized new data file (max_records=%u)", (unsigned)max_records);
    } else {
        if (s_header.max_records != max_records) {
            ESP_LOGW(TAG, "Header max_records=%u differs from partition capacity=%u, "
                          "keeping existing value (use erase_flash to expand)",
                     (unsigned)s_header.max_records, (unsigned)max_records);
        }
        if (s_header.write_idx >= s_header.max_records) {
            s_header.write_idx %= s_header.max_records;
            write_header();
        }
        ESP_LOGI(TAG, "Opened existing data file: %u records, %u unsynced (capacity %u)",
                 (unsigned)s_header.total_written, (unsigned)s_header.unsynced_count,
                 (unsigned)s_header.max_records);
    }

    return 0;
}

int data_storage_append(const client_record_t *record) {
    if (s_fp == NULL) return -1;

    client_record_t rec = *record;
    rec.synced = false;

    fseek(s_fp, record_offset(s_header.write_idx), SEEK_SET);
    if (fwrite(&rec, sizeof(rec), 1, s_fp) != 1) {
        ESP_LOGE(TAG, "Failed to write record");
        return -1;
    }

    s_header.write_idx = (s_header.write_idx + 1) % s_header.max_records;
    s_header.total_written++;
    s_header.unsynced_count++;

    if (write_header() != 0) {
        ESP_LOGE(TAG, "Failed to update header");
        return -1;
    }

    return 0;
}

int data_storage_read_unsynced(client_record_t *records, int max_records) {
    if (s_fp == NULL || s_header.unsynced_count == 0) return 0;

    int count = 0;
    uint32_t start_idx = (s_header.write_idx + s_header.max_records - s_header.unsynced_count) % s_header.max_records;

    for (uint32_t i = 0; i < s_header.unsynced_count && count < max_records; i++) {
        uint32_t idx = (start_idx + i) % s_header.max_records;
        fseek(s_fp, record_offset(idx), SEEK_SET);

        client_record_t rec;
        if (fread(&rec, sizeof(rec), 1, s_fp) == 1 && !rec.synced) {
            records[count++] = rec;
        }
    }

    return count;
}

int data_storage_mark_synced(int count) {
    if (s_fp == NULL || count <= 0) return 0;

    int marked = 0;
    uint32_t start_idx = (s_header.write_idx + s_header.max_records - s_header.unsynced_count) % s_header.max_records;

    for (int i = 0; i < count && i < (int)s_header.unsynced_count; i++) {
        uint32_t idx = (start_idx + i) % s_header.max_records;
        fseek(s_fp, record_offset(idx), SEEK_SET);

        client_record_t rec;
        if (fread(&rec, sizeof(rec), 1, s_fp) == 1 && !rec.synced) {
            rec.synced = true;
            fseek(s_fp, record_offset(idx), SEEK_SET);
            if (fwrite(&rec, sizeof(rec), 1, s_fp) == 1) {
                marked++;
            }
        }
    }

    if (marked > 0) {
        s_header.unsynced_count -= marked;
        write_header();
    }

    return marked;
}

int data_storage_get_count(void) {
    return s_fp ? (int)s_header.total_written : 0;
}

void data_storage_clear_all(void) {
    if (s_fp == NULL) return;

    fclose(s_fp);
    remove(DATA_FILE);
    s_fp = NULL;

    data_storage_init();
}
