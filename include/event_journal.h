#ifndef EVENT_JOURNAL_H
#define EVENT_JOURNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EJ_MAGIC 0x454A4E4Cu /* "EJNL" */
#define EJ_FORMAT_VERSION 1u
#define EJ_MAX_PAYLOAD 32u
#define EJ_RECORD_HEADER_SIZE 20u
#define EJ_RECORD_COMMIT 0xA5u

typedef enum {
    EJ_OK = 0,
    EJ_ERR_ARGUMENT,
    EJ_ERR_IO,
    EJ_ERR_FULL,
    EJ_ERR_CORRUPT,
    EJ_ERR_NOT_FOUND
} ej_result_t;

typedef enum {
    EJ_EVENT_BOOT = 1,
    EJ_EVENT_RESET = 2,
    EJ_EVENT_WATCHDOG = 3,
    EJ_EVENT_COMM_FAULT = 4,
    EJ_EVENT_SENSOR_FAULT = 5,
    EJ_EVENT_APP_FAULT = 6,
    EJ_EVENT_USER = 128
} ej_event_type_t;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t event_type;
    uint16_t payload_length;
    uint32_t sequence;
    uint32_t timestamp_ms;
    uint32_t crc32;
} ej_record_header_t;

typedef struct {
    uint8_t event_type;
    uint8_t payload[EJ_MAX_PAYLOAD];
    uint16_t payload_length;
    uint32_t sequence;
    uint32_t timestamp_ms;
} ej_event_t;

typedef struct {
    void *context;
    size_t capacity;
    size_t erase_block_size;
    size_t program_unit;
    int (*read)(void *context, size_t offset, void *destination, size_t length);
    int (*program)(void *context, size_t offset, const void *source, size_t length);
    int (*erase)(void *context, size_t offset, size_t length);
} ej_flash_t;

typedef struct {
    ej_flash_t flash;
    size_t write_offset;
    uint32_t next_sequence;
    uint32_t recovered_records;
    uint32_t rejected_records;
} ej_journal_t;

uint32_t ej_crc32(const void *data, size_t length);
ej_result_t ej_journal_format(ej_journal_t *journal, const ej_flash_t *flash);
ej_result_t ej_journal_open(ej_journal_t *journal, const ej_flash_t *flash);
ej_result_t ej_journal_append(ej_journal_t *journal, uint8_t event_type,
                              uint32_t timestamp_ms, const void *payload,
                              uint16_t payload_length, uint32_t *sequence_out);
ej_result_t ej_journal_read_latest(ej_journal_t *journal, ej_event_t *event_out);

#ifdef __cplusplus
}
#endif

#endif
