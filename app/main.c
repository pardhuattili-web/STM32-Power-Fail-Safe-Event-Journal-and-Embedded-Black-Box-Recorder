#include "event_journal.h"
#include "fake_nor_flash.h"

#include <stdio.h>
#include <string.h>

#define FLASH_CAPACITY 4096u
#define FLASH_ERASE_BLOCK 256u

static uint8_t flash_bytes[FLASH_CAPACITY];

int main(void)
{
    fake_nor_flash_t sim;
    ej_flash_t device;
    ej_journal_t journal;
    ej_event_t latest;
    const char message[] = "simulated watchdog reset";
    uint32_t sequence = 0u;

    if (fake_nor_init(&sim, flash_bytes, sizeof(flash_bytes),
                      FLASH_ERASE_BLOCK, 1u) != 0) {
        fputs("flash simulator init failed\n", stderr);
        return 1;
    }
    device = fake_nor_as_device(&sim);
    if (ej_journal_format(&journal, &device) != EJ_OK) return 1;
    if (ej_journal_append(&journal, EJ_EVENT_WATCHDOG, 123456u, message,
                          (uint16_t)(sizeof(message) - 1u), &sequence) != EJ_OK) return 1;

    if (ej_journal_open(&journal, &device) != EJ_OK) return 1;
    if (ej_journal_read_latest(&journal, &latest) != EJ_OK) return 1;

    printf("Power-fail-safe journal demo (simulated NOR; not hardware data)\n");
    printf("sequence=%lu type=%u timestamp_ms=%lu payload=",
           (unsigned long)latest.sequence, (unsigned)latest.event_type,
           (unsigned long)latest.timestamp_ms);
    (void)fwrite(latest.payload, 1u, latest.payload_length, stdout);
    printf("\nprogram_ops=%lu erase_ops=%lu\n",
           (unsigned long)sim.program_operations,
           (unsigned long)sim.erase_operations);
    return (latest.sequence == sequence) ? 0 : 2;
}
