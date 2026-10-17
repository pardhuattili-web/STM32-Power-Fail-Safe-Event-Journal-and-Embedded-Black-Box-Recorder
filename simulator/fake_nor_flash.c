#include "fake_nor_flash.h"

#include <string.h>

static bool in_bounds(const fake_nor_flash_t *f, size_t offset, size_t length)
{
    return f != NULL && offset <= f->capacity && length <= f->capacity - offset;
}

int fake_nor_init(fake_nor_flash_t *f, uint8_t *storage, size_t capacity,
                  size_t erase_block_size, size_t program_unit)
{
    if (f == NULL || storage == NULL || capacity == 0u || erase_block_size == 0u ||
        program_unit == 0u || capacity % erase_block_size != 0u ||
        erase_block_size % program_unit != 0u) return -1;
    memset(f, 0, sizeof(*f));
    f->bytes = storage;
    f->capacity = capacity;
    f->erase_block_size = erase_block_size;
    f->program_unit = program_unit;
    fake_nor_clear_power_cut(f);
    memset(storage, 0xFF, capacity);
    return 0;
}

void fake_nor_set_power_cut(fake_nor_flash_t *f, int64_t operations_before_failure,
                            size_t bytes_before_failure)
{
    if (f == NULL) return;
    f->failure_enabled = true;
    f->operations_before_failure = operations_before_failure;
    f->bytes_before_failure = bytes_before_failure;
}

void fake_nor_clear_power_cut(fake_nor_flash_t *f)
{
    if (f == NULL) return;
    f->failure_enabled = false;
    f->operations_before_failure = -1;
    f->bytes_before_failure = 0u;
}

static bool operation_fails(fake_nor_flash_t *f)
{
    if (!f->failure_enabled) return false;
    if (f->operations_before_failure == 0) return true;
    if (f->operations_before_failure > 0) --f->operations_before_failure;
    return false;
}

static int nor_read(void *context, size_t offset, void *destination, size_t length)
{
    fake_nor_flash_t *f = (fake_nor_flash_t *)context;
    if (f == NULL || destination == NULL || !in_bounds(f, offset, length)) return -1;
    ++f->read_operations;
    if (operation_fails(f)) return -1;
    memcpy(destination, f->bytes + offset, length);
    return 0;
}

static int nor_program(void *context, size_t offset, const void *source, size_t length)
{
    fake_nor_flash_t *f = (fake_nor_flash_t *)context;
    const uint8_t *src = (const uint8_t *)source;
    size_t actual = length;
    size_t i;
    if (f == NULL || source == NULL || !in_bounds(f, offset, length) ||
        offset % f->program_unit != 0u) return -1;
    ++f->program_operations;
    if (operation_fails(f)) return -1;
    if (f->failure_enabled && f->bytes_before_failure < actual) {
        actual = f->bytes_before_failure;
    }
    for (i = 0u; i < actual; ++i) {
        if ((uint8_t)(f->bytes[offset + i] & src[i]) != src[i]) return -1;
    }
    for (i = 0u; i < actual; ++i) f->bytes[offset + i] &= src[i];
    if (actual != length) {
        f->bytes_before_failure = 0u;
        return -1;
    }
    if (f->failure_enabled && f->bytes_before_failure >= length) {
        f->bytes_before_failure -= length;
    }
    return 0;
}

static int nor_erase(void *context, size_t offset, size_t length)
{
    fake_nor_flash_t *f = (fake_nor_flash_t *)context;
    if (f == NULL || !in_bounds(f, offset, length) ||
        offset % f->erase_block_size != 0u || length == 0u ||
        length % f->erase_block_size != 0u) return -1;
    ++f->erase_operations;
    if (operation_fails(f)) return -1;
    memset(f->bytes + offset, 0xFF, length);
    return 0;
}

ej_flash_t fake_nor_as_device(fake_nor_flash_t *f)
{
    ej_flash_t d;
    d.context = f;
    d.capacity = f != NULL ? f->capacity : 0u;
    d.erase_block_size = f != NULL ? f->erase_block_size : 0u;
    d.program_unit = f != NULL ? f->program_unit : 0u;
    d.read = nor_read;
    d.program = nor_program;
    d.erase = nor_erase;
    return d;
}
