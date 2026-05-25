#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    time_t timestamp;
    uint32_t uptime_sec;
    float temperature;
    float humidity;
    uint16_t eco2;
    uint16_t tvoc;
    uint8_t aqi;
    bool synced;
} __attribute__((packed)) client_record_t;

int data_storage_init(void);
int data_storage_append(const client_record_t *record);
int data_storage_read_unsynced(client_record_t *records, int max_records);
int data_storage_mark_synced(int count);
int data_storage_get_count(void);
void data_storage_clear_all(void);

#ifdef __cplusplus
}
#endif
