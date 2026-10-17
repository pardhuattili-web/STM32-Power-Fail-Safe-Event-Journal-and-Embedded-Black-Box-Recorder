#ifndef FAKE_NOR_FLASH_H
#define FAKE_NOR_FLASH_H
#include "event_journal.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *bytes;
    size_t capacity;
    size_t erase_block_size;
    size_t program_unit;
    int64_t operations_before_failure;
    size_t bytes_before_failure;
    bool failure_enabled;
    uint32_t read_operations;
    uint32_t program_operations;
    uint32_t erase_operations;
} fake_nor_flash_t;

int fake_nor_init(fake_nor_flash_t *flash, uint8_t *storage, size_t capacity,
                  size_t erase_block_size, size_t program_unit);
void fake_nor_set_power_cut(fake_nor_flash_t *flash, int64_t operations_before_failure,
                            size_t bytes_before_failure);
void fake_nor_clear_power_cut(fake_nor_flash_t *flash);
ej_flash_t fake_nor_as_device(fake_nor_flash_t *flash);

#endif
