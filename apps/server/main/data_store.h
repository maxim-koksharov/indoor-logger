#ifndef DATA_STORE_H
#define DATA_STORE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DATA_STORE_MAGIC 0x44415441
#define DATA_STORE_VERSION 1
#define DATA_STORE_MAX_CLIENTS 16

/* Max records per client is computed at runtime from the SPIFFS partition
 * size. There is no compile-time limit. */

/** @brief A single sensor data record (packed to 14 bytes). */
typedef struct __attribute__((packed)) {
    uint32_t timestamp;  /**< Unix timestamp or uptime seconds */
    int16_t  temp_x100;  /**< Temperature in Celsius * 100 (signed) */
    uint16_t hum_x100;   /**< Humidity in % RH * 100 (unsigned) */
    uint16_t eco2;       /**< Equivalent CO2 in ppm */
    uint16_t tvoc;       /**< Total VOC in ppb */
    uint8_t  aqi;        /**< Air Quality Index 1-5 */
    uint8_t  _pad;       /**< Padding to align to 14 bytes */
} data_record_t;

/** @brief Per-client file header at the start of each /spiffs/<id>.dat file. */
typedef struct __attribute__((packed)) {
    uint32_t magic;               /**< DATA_STORE_MAGIC identifier */
    uint8_t  version;             /**< DATA_STORE_VERSION */
    char     client_id[32];       /**< Client identifier string */
    uint32_t max_records;         /**< Ring buffer capacity */
    uint32_t write_idx;           /**< Current write position in ring buffer */
    uint32_t count;               /**< Total records written (capped at max_records) */
} client_file_header_t;

/** @brief Mount SPIFFS and prepare the data store.
 *  @return 0 on success, -1 on failure (SPIFFS mount failed).
 */
int data_store_init(void);

/** @brief Get the per-client ring buffer capacity computed from the SPIFFS
 *         partition size at init time. Returns 0 if data_store_init() has
 *         not been called or failed. */
uint32_t data_store_get_max_records_per_client(void);

/** @brief Append a record to a client's ring buffer file.
 *  @param client_id  Client identifier (used as filename: /spiffs/<id>.dat).
 *  @param rec        Pointer to the record to append.
 *  @return 0 on success, -1 on failure (I/O error, SPIFFS full).
 */
int data_store_append(const char *client_id, const data_record_t *rec);

/** @brief Read a range of records from a client's file.
 *  @param client_id Client identifier.
 *  @param offset    Record offset from the oldest record.
 *  @param limit     Maximum number of records to read.
 *  @param out       Output buffer (must be at least @p capacity records).
 *  @param capacity  Size of the output buffer in records.
 *  @return Number of records actually read (0 if none or file missing).
 */
int data_store_read_range(const char *client_id, uint32_t offset, uint32_t limit,
                          data_record_t *out, uint32_t capacity);

/** @brief Read records with timestamp >= since_ts (newest-first).
 *  @param client_id Client identifier.
 *  @param since_ts  Minimum Unix timestamp.
 *  @param out       Output buffer.
 *  @param capacity  Max records to return.
 *  @return Number of records read, or 0.
 */
int data_store_read_since(const char *client_id, uint32_t since_ts,
                          data_record_t *out, uint32_t capacity);

/** @brief Get the total number of stored records for a client.
 *  @param client_id Client identifier.
 *  @return Record count, or 0 if the client has no file.
 */
uint32_t data_store_get_count(const char *client_id);

/** @brief Delete a client's data file from SPIFFS.
 *  @param client_id Client identifier.
 *  @return 0 on success, -1 if the file could not be deleted.
 */
int data_store_delete_client(const char *client_id);

/** @brief Rename a client's data file from old_id to new_id.
 *  @param old_id Existing client identifier.
 *  @param new_id New client identifier.
 *  @return 0 on success, -1 on I/O error. Returns 0 if the old file does not exist.
 */
int data_store_rename_client(const char *old_id, const char *new_id);

/** @brief A single aggregated data bucket (result of grouping raw records by time). */
typedef struct {
    uint32_t timestamp;  /**< Bucket start timestamp (Unix seconds) */
    uint16_t count;      /**< Number of raw records in this bucket */
    int16_t  temp_avg_x100;
    uint16_t hum_avg_x100;
    uint16_t eco2_avg;
    uint16_t tvoc_avg;
    uint8_t  aqi_avg;
} data_aggregated_t;

/** @brief Read aggregated (averaged) records grouped into time buckets.
 *  @param client_id  Client identifier.
 *  @param since_ts   Minimum Unix timestamp.
 *  @param bucket_sec Bucket size in seconds (e.g. 300 for 5 min).
 *  @param out        Output buffer for aggregated records.
 *  @param capacity   Max buckets to return.
 *  @return Number of buckets returned, or 0.
 */
int data_store_read_aggregated(const char *client_id, uint32_t since_ts, uint32_t bucket_sec,
                               data_aggregated_t *out, uint32_t capacity);

#ifdef __cplusplus
}
#endif

#endif
