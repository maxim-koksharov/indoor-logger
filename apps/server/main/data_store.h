#ifndef DATA_STORE_H
#define DATA_STORE_H

#include <stdint.h>
#include <stdbool.h>

#define DATA_STORE_MAGIC 0x44415441
#define DATA_STORE_VERSION 1
#define DATA_STORE_MAX_CLIENTS 16
#define DATA_STORE_MAX_RECORDS_PER_CLIENT 10000

typedef struct __attribute__((packed)) {
    uint32_t timestamp;
    int16_t temp_x100;
    uint16_t hum_x100;
    uint16_t eco2;
    uint16_t tvoc;
    uint8_t aqi;
    uint8_t _pad;
} data_record_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    char client_id[32];
    uint32_t max_records;
    uint32_t write_idx;
    uint32_t count;
} client_file_header_t;

int data_store_init(void);
int data_store_append(const char *client_id, const data_record_t *rec);
int data_store_read_range(const char *client_id, uint32_t offset, uint32_t limit,
                          data_record_t *out, uint32_t capacity);
uint32_t data_store_get_count(const char *client_id);
int data_store_delete_client(const char *client_id);

#endif
