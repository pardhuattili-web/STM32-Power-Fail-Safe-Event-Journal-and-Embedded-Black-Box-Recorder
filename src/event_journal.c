#include "event_journal.h"

#include <stddef.h>
#include <string.h>

#define EJ_COMMIT_SIZE 1u
#define EJ_EMPTY_BYTE 0xFFu

static size_t align_up(size_t value, size_t alignment)
{
    if (alignment == 0u) return value;
    return ((value + alignment - 1u) / alignment) * alignment;
}

static bool flash_is_valid(const ej_flash_t *f)
{
    return f != NULL && f->context != NULL && f->capacity > 0u &&
           f->erase_block_size > 0u && f->program_unit > 0u &&
           f->capacity % f->erase_block_size == 0u &&
           f->read != NULL && f->program != NULL && f->erase != NULL;
}

uint32_t ej_crc32(const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;
    unsigned bit;

    if (bytes == NULL && length != 0u) return 0u;
    for (i = 0u; i < length; ++i) {
        crc ^= bytes[i];
        for (bit = 0u; bit < 8u; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static size_t record_size(const ej_flash_t *f, uint16_t payload_length)
{
    const size_t raw = sizeof(ej_record_header_t) + (size_t)payload_length + EJ_COMMIT_SIZE;
    return align_up(raw, f->program_unit);
}

static int is_erased(ej_journal_t *j, size_t offset, size_t length, bool *erased)
{
    uint8_t buffer[64];
    size_t position = 0u;

    if (erased == NULL) return -1;
    *erased = true;
    while (position < length) {
        size_t amount = length - position;
        size_t i;
        if (amount > sizeof(buffer)) amount = sizeof(buffer);
        if (j->flash.read(j->flash.context, offset + position, buffer, amount) != 0) return -1;
        for (i = 0u; i < amount; ++i) {
            if (buffer[i] != EJ_EMPTY_BYTE) {
                *erased = false;
                return 0;
            }
        }
        position += amount;
    }
    return 0;
}

static bool header_is_plausible(const ej_record_header_t *h)
{
    return h->magic == EJ_MAGIC && h->version == EJ_FORMAT_VERSION &&
           h->payload_length <= EJ_MAX_PAYLOAD;
}

static uint32_t record_crc(const ej_record_header_t *header, const uint8_t *payload)
{
    uint8_t buffer[sizeof(ej_record_header_t) - sizeof(uint32_t) + EJ_MAX_PAYLOAD];
    const size_t prefix = offsetof(ej_record_header_t, crc32);
    memcpy(buffer, header, prefix);
    if (header->payload_length > 0u) {
        memcpy(buffer + prefix, payload, header->payload_length);
    }
    return ej_crc32(buffer, prefix + header->payload_length);
}

static ej_result_t validate_record(ej_journal_t *j, size_t offset,
                                   ej_record_header_t *header_out,
                                   uint8_t payload_out[EJ_MAX_PAYLOAD],
                                   size_t *record_size_out)
{
    ej_record_header_t header;
    uint8_t payload[EJ_MAX_PAYLOAD];
    uint8_t marker;
    size_t size;

    if (j->flash.read(j->flash.context, offset, &header, sizeof(header)) != 0) return EJ_ERR_IO;
    if (!header_is_plausible(&header)) return EJ_ERR_CORRUPT;
    size = record_size(&j->flash, header.payload_length);
    if (offset > j->flash.capacity || size > j->flash.capacity - offset) return EJ_ERR_CORRUPT;
    if (j->flash.read(j->flash.context, offset + sizeof(header), payload,
                      header.payload_length) != 0) return EJ_ERR_IO;
    if (j->flash.read(j->flash.context,
                      offset + sizeof(header) + header.payload_length,
                      &marker, 1u) != 0) return EJ_ERR_IO;
    if (marker != EJ_RECORD_COMMIT || record_crc(&header, payload) != header.crc32) {
        return EJ_ERR_CORRUPT;
    }
    if (header_out != NULL) *header_out = header;
    if (payload_out != NULL && header.payload_length > 0u) {
        memcpy(payload_out, payload, header.payload_length);
    }
    if (record_size_out != NULL) *record_size_out = size;
    return EJ_OK;
}

ej_result_t ej_journal_format(ej_journal_t *journal, const ej_flash_t *flash)
{
    size_t offset;

    if (journal == NULL || !flash_is_valid(flash)) return EJ_ERR_ARGUMENT;
    memset(journal, 0, sizeof(*journal));
    journal->flash = *flash;
    for (offset = 0u; offset < flash->capacity; offset += flash->erase_block_size) {
        if (flash->erase(flash->context, offset, flash->erase_block_size) != 0) return EJ_ERR_IO;
    }
    journal->write_offset = 0u;
    journal->next_sequence = 1u;
    return EJ_OK;
}

ej_result_t ej_journal_open(ej_journal_t *journal, const ej_flash_t *flash)
{
    size_t offset = 0u;
    uint32_t newest_sequence = 0u;
    size_t newest_end = 0u;
    size_t append_candidate = 0u;

    if (journal == NULL || !flash_is_valid(flash)) return EJ_ERR_ARGUMENT;
    memset(journal, 0, sizeof(*journal));
    journal->flash = *flash;

    /*
     * Scan in erase-block-sized recovery regions. On a torn record, skip to the
     * next block and never append over bytes that may have been programmed.
     * This format intentionally does not recycle blocks; it reports full.
     */
    while (offset + sizeof(ej_record_header_t) <= flash->capacity) {
        bool erased = false;
        ej_record_header_t header;
        size_t size = 0u;
        ej_result_t status;

        if (is_erased(journal, offset, sizeof(header), &erased) != 0) return EJ_ERR_IO;
        if (erased) {
            append_candidate = offset;
            offset += flash->program_unit;
            continue;
        }

        status = validate_record(journal, offset, &header, NULL, &size);
        if (status == EJ_ERR_IO) return status;
        if (status == EJ_OK) {
            ++journal->recovered_records;
            if (header.sequence >= newest_sequence) {
                newest_sequence = header.sequence;
                newest_end = offset + size;
            }
            offset += size;
        } else {
            ++journal->rejected_records;
            offset = align_up(offset + 1u, flash->erase_block_size);
            append_candidate = offset;
        }
    }

    if (newest_sequence == 0u) {
        journal->write_offset = append_candidate;
    } else {
        /*
         * If the newest record has a later torn record in the same region,
         * append_candidate is advanced when scan finds the damaged region.
         * Otherwise the end of the newest valid record is the append point.
         */
        journal->write_offset = append_candidate > newest_end ? append_candidate : newest_end;
    }
    if (journal->write_offset > flash->capacity) return EJ_ERR_FULL;
    journal->next_sequence = newest_sequence + 1u;
    if (journal->next_sequence == 0u) journal->next_sequence = 1u;
    return EJ_OK;
}

ej_result_t ej_journal_append(ej_journal_t *journal, uint8_t event_type,
                              uint32_t timestamp_ms, const void *payload,
                              uint16_t payload_length, uint32_t *sequence_out)
{
    ej_record_header_t header;
    uint8_t marker = EJ_RECORD_COMMIT;
    uint8_t payload_buffer[EJ_MAX_PAYLOAD];
    size_t size;
    size_t payload_offset;
    size_t marker_offset;

    if (journal == NULL || !flash_is_valid(&journal->flash) ||
        (payload == NULL && payload_length != 0u) || payload_length > EJ_MAX_PAYLOAD) {
        return EJ_ERR_ARGUMENT;
    }

    size = record_size(&journal->flash, payload_length);
    if (size == 0u || journal->write_offset > journal->flash.capacity ||
        size > journal->flash.capacity - journal->write_offset) return EJ_ERR_FULL;

    memset(&header, 0xFF, sizeof(header));
    header.magic = EJ_MAGIC;
    header.version = EJ_FORMAT_VERSION;
    header.event_type = event_type;
    header.payload_length = payload_length;
    header.sequence = journal->next_sequence;
    header.timestamp_ms = timestamp_ms;
    memset(payload_buffer, 0xFF, sizeof(payload_buffer));
    if (payload_length > 0u) memcpy(payload_buffer, payload, payload_length);
    header.crc32 = record_crc(&header, payload_buffer);

    payload_offset = journal->write_offset + sizeof(header);
    marker_offset = payload_offset + payload_length;

    if (journal->flash.program(journal->flash.context, journal->write_offset,
                               &header, sizeof(header)) != 0) return EJ_ERR_IO;
    if (payload_length > 0u &&
        journal->flash.program(journal->flash.context, payload_offset,
                               payload_buffer, payload_length) != 0) return EJ_ERR_IO;
    /* Commit last. If this fails, recovery rejects the uncommitted record. */
    if (journal->flash.program(journal->flash.context, marker_offset, &marker, 1u) != 0)
        return EJ_ERR_IO;

    journal->write_offset += size;
    ++journal->next_sequence;
    if (sequence_out != NULL) *sequence_out = header.sequence;
    return EJ_OK;
}

ej_result_t ej_journal_read_latest(ej_journal_t *journal, ej_event_t *event_out)
{
    size_t offset = 0u;
    bool found = false;
    uint32_t newest_sequence = 0u;
    ej_event_t newest;

    if (journal == NULL || event_out == NULL || !flash_is_valid(&journal->flash))
        return EJ_ERR_ARGUMENT;

    while (offset + sizeof(ej_record_header_t) <= journal->flash.capacity) {
        ej_record_header_t header;
        uint8_t payload[EJ_MAX_PAYLOAD];
        size_t size = 0u;
        ej_result_t status;
        bool erased = false;

        if (is_erased(journal, offset, sizeof(header), &erased) != 0) return EJ_ERR_IO;
        if (erased) {
            offset += journal->flash.program_unit;
            continue;
        }
        status = validate_record(journal, offset, &header, payload, &size);
        if (status == EJ_ERR_IO) return status;
        if (status == EJ_OK) {
            if (!found || header.sequence > newest_sequence) {
                memset(&newest, 0, sizeof(newest));
                newest.event_type = header.event_type;
                newest.payload_length = header.payload_length;
                newest.sequence = header.sequence;
                newest.timestamp_ms = header.timestamp_ms;
                if (header.payload_length > 0u) {
                    memcpy(newest.payload, payload, header.payload_length);
                }
                newest_sequence = header.sequence;
                found = true;
            }
            offset += size;
        } else {
            offset = align_up(offset + 1u, journal->flash.erase_block_size);
        }
    }

    if (!found) return EJ_ERR_NOT_FOUND;
    *event_out = newest;
    return EJ_OK;
}
