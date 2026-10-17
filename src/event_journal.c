#include "event_journal.h"

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
    size_t raw = sizeof(ej_record_header_t) + (size_t)payload_length + EJ_COMMIT_SIZE;
    return align_up(raw, f->program_unit);
}

static int is_erased(ej_journal_t *j, size_t offset, size_t length, bool *erased)
{
    uint8_t buf[64];
    size_t pos = 0u;
    if (erased == NULL) return -1;
    *erased = true;
    while (pos < length) {
        size_t n = length - pos;
        size_t i;
        if (n > sizeof(buf)) n = sizeof(buf);
        if (j->flash.read(j->flash.context, offset + pos, buf, n) != 0) return -1;
        for (i = 0u; i < n; ++i) {
            if (buf[i] != EJ_EMPTY_BYTE) {
                *erased = false;
                return 0;
            }
        }
        pos += n;
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
    size_t prefix = offsetof(ej_record_header_t, crc32);
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
    ej_record_header_t h;
    uint8_t payload[EJ_MAX_PAYLOAD];
    uint8_t marker;
    uint32_t expected;
    size_t size;

    if (j->flash.read(j->flash.context, offset, &h, sizeof(h)) != 0) return EJ_ERR_IO;
    if (!header_is_plausible(&h)) return EJ_ERR_CORRUPT;
    size = record_size(&j->flash, h.payload_length);
    if (offset > j->flash.capacity || size > j->flash.capacity - offset) return EJ_ERR_CORRUPT;
    if (j->flash.read(j->flash.context, offset + sizeof(h), payload, h.payload_length) != 0)
        return EJ_ERR_IO;
    if (j->flash.read(j->flash.context, offset + sizeof(h) + h.payload_length,
                      &marker, 1u) != 0) return EJ_ERR_IO;
    if (marker != EJ_RECORD_COMMIT) return EJ_ERR_CORRUPT;
    expected = record_crc(&h, payload);
    if (expected != h.crc32) return EJ_ERR_CORRUPT;
    if (header_out != NULL) *header_out = h;
    if (payload_out != NULL && h.payload_length > 0u)
        memcpy(payload_out, payload, h.payload_length);
    if (record_size_out != NULL) *record_size_out = size;
    return EJ_OK;
}

ej_result_t ej_journal_format(ej_journal_t *j, const ej_flash_t *f)
{
    size_t offset;
    if (j == NULL || !flash_is_valid(f)) return EJ_ERR_ARGUMENT;
    memset(j, 0, sizeof(*j));
    j->flash = *f;
    for (offset = 0u; offset < f->capacity; offset += f->erase_block_size) {
        if (f->erase(f->context, offset, f->erase_block_size) != 0) return EJ_ERR_IO;
    }
    j->write_offset = 0u;
    j->next_sequence = 1u;
    return EJ_OK;
}

ej_result_t ej_journal_open(ej_journal_t *j, const ej_flash_t *f)
{
    size_t offset = 0u;
    uint32_t newest_sequence = 0u;
    size_t newest_end = 0u;

    if (j == NULL || !flash_is_valid(f)) return EJ_ERR_ARGUMENT;
    memset(j, 0, sizeof(*j));
    j->flash = *f;

    while (offset + sizeof(ej_record_header_t) <= f->capacity) {
        bool erased = false;
        ej_record_header_t h;
        size_t size = 0u;
        ej_result_t status;
        if (is_erased(j, offset, sizeof(ej_record_header_t), &erased) != 0) return EJ_ERR_IO;
        if (erased) {
            /* Preserve the first erased region as the next append location. */
            if (newest_end <= offset) newest_end = offset;
            offset += f->program_unit;
            continue;
        }
        status = validate_record(j, offset, &h, NULL, &size);
        if (status == EJ_ERR_IO) return status;
        if (status == EJ_OK) {
            ++j->recovered_records;
            if (h.sequence >= newest_sequence) {
                newest_sequence = h.sequence;
                newest_end = offset + size;
            }
            offset += size;
        } else {
            ++j->rejected_records;
            /*
             * A torn record may leave programmed bytes. Skip to the next erase
             * boundary; never overwrite a possibly-programmed flash region.
             */
            offset = align_up(offset + 1u, f->erase_block_size);
        }
    }
    if (newest_end > f->capacity) return EJ_ERR_CORRUPT;
    j->write_offset = newest_end;
    j->next_sequence = newest_sequence + 1u;
    if (j->next_sequence == 0u) j->next_sequence = 1u;
    return EJ_OK;
}

ej_result_t ej_journal_append(ej_journal_t *j, uint8_t type, uint32_t timestamp,
                              const void *payload, uint16_t payload_len,
                              uint32_t *sequence_out)
{
    ej_record_header_t h;
    uint8_t marker = EJ_RECORD_COMMIT;
    uint8_t payload_buf[EJ_MAX_PAYLOAD];
    size_t size, marker_offset, payload_offset;
    size_t padded;
    uint8_t padding[64];

    if (j == NULL || !flash_is_valid(&j->flash) ||
        (payload == NULL && payload_len != 0u) || payload_len > EJ_MAX_PAYLOAD)
        return EJ_ERR_ARGUMENT;
    size = record_size(&j->flash, payload_len);
    if (size == 0u || j->write_offset > j->flash.capacity ||
        size > j->flash.capacity - j->write_offset) return EJ_ERR_FULL;

    memset(&h, 0xFF, sizeof(h));
    h.magic = EJ_MAGIC;
    h.version = EJ_FORMAT_VERSION;
    h.event_type = type;
    h.payload_length = payload_len;
    h.sequence = j->next_sequence;
    h.timestamp_ms = timestamp;
    memset(payload_buf, 0xFF, sizeof(payload_buf));
    if (payload_len > 0u) memcpy(payload_buf, payload, payload_len);
    h.crc32 = record_crc(&h, payload_buf);

    payload_offset = j->write_offset + sizeof(h);
    marker_offset = payload_offset + payload_len;

    /* Write header and payload first. A record is visible only after the marker. */
    if (j->flash.program(j->flash.context, j->write_offset, &h, sizeof(h)) != 0)
        return EJ_ERR_IO;
    if (payload_len > 0u &&
        j->flash.program(j->flash.context, payload_offset, payload_buf, payload_len) != 0)
        return EJ_ERR_IO;
    if (j->flash.program(j->flash.context, marker_offset, &marker, 1u) != 0)
        return EJ_ERR_IO;

    padded = size - (sizeof(h) + payload_len + 1u);
    if (padded > 0u) {
        size_t remaining = padded;
        size_t pos = marker_offset + 1u;
        memset(padding, 0xFF, sizeof(padding));
        while (remaining > 0u) {
            size_t n = remaining > sizeof(padding) ? sizeof(padding) : remaining;
            /* Padding remains erased; program API is intentionally not called. */
            pos += n;
            remaining -= n;
        }
        (void)pos;
    }

    j->write_offset += size;
    ++j->next_sequence;
    if (sequence_out != NULL) *sequence_out = h.sequence;
    return EJ_OK;
}

ej_result_t ej_journal_read_latest(ej_journal_t *j, ej_event_t *out)
{
    size_t offset = 0u;
    bool found = false;
    uint32_t latest_sequence = 0u;
    ej_event_t latest;
    if (j == NULL || out == NULL || !flash_is_valid(&j->flash)) return EJ_ERR_ARGUMENT;

    while (offset + sizeof(ej_record_header_t) <= j->flash.capacity) {
        ej_record_header_t h;
        uint8_t payload[EJ_MAX_PAYLOAD];
        size_t size;
        ej_result_t status;
        bool erased = false;
        if (is_erased(j, offset, sizeof(h), &erased) != 0) return EJ_ERR_IO;
        if (erased) {
            offset += j->flash.program_unit;
            continue;
        }
        status = validate_record(j, offset, &h, payload, &size);
        if (status == EJ_ERR_IO) return status;
        if (status == EJ_OK) {
            if (!found || h.sequence > latest_sequence) {
                size_t copy_len = h.payload_length;
                memset(&latest, 0, sizeof(latest));
                latest.event_type = h.event_type;
                latest.payload_length = h.payload_length;
                latest.sequence = h.sequence;
                latest.timestamp_ms = h.timestamp_ms;
                if (copy_len > 0u) memcpy(latest.payload, payload, copy_len);
                latest_sequence = h.sequence;
                found = true;
            }
            offset += size;
        } else {
            offset = align_up(offset + 1u, j->flash.erase_block_size);
        }
    }
    if (!found) return EJ_ERR_NOT_FOUND;
    *out = latest;
    return EJ_OK;
}
