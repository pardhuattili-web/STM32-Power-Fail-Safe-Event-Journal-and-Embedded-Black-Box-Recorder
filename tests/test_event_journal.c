#include "event_journal.h"
#include "fake_nor_flash.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define FLASH_SIZE 4096u
#define ERASE_SIZE 256u
#define PROGRAM_UNIT 1u

static uint8_t storage[FLASH_SIZE];
static fake_nor_flash_t fake;
static ej_flash_t device;
static ej_journal_t journal;

static void reset_fixture(void)
{
    assert(fake_nor_init(&fake, storage, sizeof(storage), ERASE_SIZE, PROGRAM_UNIT) == 0);
    device = fake_nor_as_device(&fake);
    assert(ej_journal_format(&journal, &device) == EJ_OK);
    fake_nor_clear_power_cut(&fake);
}

static void test_crc32_reference(void)
{
    static const char sample[] = "123456789";
    assert(ej_crc32(sample, sizeof(sample) - 1u) == 0xCBF43926u);
}

static void test_append_reopen_latest(void)
{
    const char first[] = "boot";
    const char second[] = "watchdog";
    ej_event_t latest;
    uint32_t seq1, seq2;

    reset_fixture();
    assert(ej_journal_append(&journal, EJ_EVENT_BOOT, 100u, first,
                            (uint16_t)(sizeof(first) - 1u), &seq1) == EJ_OK);
    assert(ej_journal_append(&journal, EJ_EVENT_WATCHDOG, 250u, second,
                            (uint16_t)(sizeof(second) - 1u), &seq2) == EJ_OK);
    assert(seq1 == 1u && seq2 == 2u);

    assert(ej_journal_open(&journal, &device) == EJ_OK);
    assert(journal.recovered_records == 2u);
    assert(journal.next_sequence == 3u);
    assert(ej_journal_read_latest(&journal, &latest) == EJ_OK);
    assert(latest.sequence == 2u && latest.event_type == EJ_EVENT_WATCHDOG);
    assert(latest.timestamp_ms == 250u);
    assert(latest.payload_length == sizeof(second) - 1u);
    assert(memcmp(latest.payload, second, sizeof(second) - 1u) == 0);
}

static void test_payload_validation(void)
{
    uint8_t payload[EJ_MAX_PAYLOAD + 1u] = {0u};
    reset_fixture();
    assert(ej_journal_append(&journal, EJ_EVENT_USER, 0u, payload,
                             (uint16_t)sizeof(payload), NULL) == EJ_ERR_ARGUMENT);
    assert(ej_journal_append(&journal, EJ_EVENT_USER, 0u, NULL, 1u, NULL) == EJ_ERR_ARGUMENT);
}

static void test_crc_corruption_rejected(void)
{
    const char payload[] = "integrity";
    ej_event_t latest;
    reset_fixture();
    assert(ej_journal_append(&journal, EJ_EVENT_USER, 10u, payload,
                             (uint16_t)(sizeof(payload) - 1u), NULL) == EJ_OK);
    storage[sizeof(ej_record_header_t)] ^= 0x01u;
    assert(ej_journal_open(&journal, &device) == EJ_OK);
    assert(journal.rejected_records >= 1u);
    assert(ej_journal_read_latest(&journal, &latest) == EJ_ERR_NOT_FOUND);
}

static void test_power_cut_before_commit(void)
{
    const char payload[] = "interrupted";
    ej_event_t latest;

    reset_fixture();
    /* Allow the header operation, then cut the power before payload programming. */
    fake_nor_set_power_cut(&fake, 1, 0u);
    assert(ej_journal_append(&journal, EJ_EVENT_APP_FAULT, 300u, payload,
                             (uint16_t)(sizeof(payload) - 1u), NULL) == EJ_ERR_IO);
    fake_nor_clear_power_cut(&fake);

    assert(ej_journal_open(&journal, &device) == EJ_OK);
    assert(ej_journal_read_latest(&journal, &latest) == EJ_ERR_NOT_FOUND);
    assert(ej_journal_append(&journal, EJ_EVENT_BOOT, 400u, "ok", 2u, NULL) == EJ_OK);
    assert(ej_journal_open(&journal, &device) == EJ_OK);
    assert(ej_journal_read_latest(&journal, &latest) == EJ_OK);
    assert(latest.event_type == EJ_EVENT_BOOT);
}

static void test_power_cut_mid_program(void)
{
    const char payload[] = "abcdefgh";
    ej_event_t latest;
    reset_fixture();
    /* Header is 20 bytes with program_unit=1. Next operation writes payload. */
    fake_nor_set_power_cut(&fake, 1, 3u);
    assert(ej_journal_append(&journal, EJ_EVENT_SENSOR_FAULT, 500u, payload,
                             (uint16_t)(sizeof(payload) - 1u), NULL) == EJ_ERR_IO);
    fake_nor_clear_power_cut(&fake);
    assert(ej_journal_open(&journal, &device) == EJ_OK);
    assert(ej_journal_read_latest(&journal, &latest) == EJ_ERR_NOT_FOUND);
}

int main(void)
{
    test_crc32_reference();
    test_append_reopen_latest();
    test_payload_validation();
    test_crc_corruption_rejected();
    test_power_cut_before_commit();
    test_power_cut_mid_program();
    puts("All event journal tests passed.");
    return 0;
}
