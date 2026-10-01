#include "settings.h"
#include <string.h>

#ifndef _MSC_VER
#include "hardware/flash.h"
#include "hardware/address_mapped.h"
#include "pico/flash.h"
#define SETTINGS_OFFSET (PICO_FLASH_SIZE_BYTES - SETTINGS_FLASH_RESERVE)
extern char __flash_binary_end;
static uint8_t write_buffer[SETTINGS_SIZE];
static uint32_t write_offset;
static void __not_in_flash_func(write_settings)(void* unused) {
    (void)unused;
    flash_range_erase(write_offset, FLASH_SECTOR_SIZE);
    flash_range_program(write_offset, write_buffer, SETTINGS_SIZE);
}
static bool storage_available(void) { return (uintptr_t)&__flash_binary_end - XIP_BASE <= SETTINGS_OFFSET; }
static const uint8_t* storage_bank(unsigned bank) {
    return (const uint8_t*)(XIP_BASE + SETTINGS_OFFSET + bank * FLASH_SECTOR_SIZE);
}
static bool storage_write(unsigned bank, const uint8_t* bytes) {
    memcpy(write_buffer, bytes, SETTINGS_SIZE);
    write_offset = SETTINGS_OFFSET + bank * FLASH_SECTOR_SIZE;
    return flash_safe_execute(write_settings, NULL, 100u) == PICO_OK;
}
#else
/* Host tests emulate flash interruption, while sharing bank selection, encoding,
 * readback validation and rollback with the firmware implementation below. */
static uint8_t host_banks[2][SETTINGS_SIZE];
static bool host_interrupt;
void settings_test_reset(void) { memset(host_banks, 255, sizeof(host_banks)); host_interrupt = false; }
void settings_test_interrupt(bool interrupt) { host_interrupt = interrupt; }
static bool storage_available(void) { return true; }
static const uint8_t* storage_bank(unsigned bank) { return host_banks[bank]; }
static bool storage_write(unsigned bank, const uint8_t* bytes) {
    memset(host_banks[bank], 255, SETTINGS_SIZE);
    memcpy(host_banks[bank], bytes, host_interrupt ? 64 : SETTINGS_SIZE);
    return true; /* Partial programming is caught by the real readback check. */
}
#endif

static bool bank_read(unsigned bank, adapter_settings_t* s, uint32_t* seq) {
    return storage_available() && settings_decode(s, seq, storage_bank(bank), SETTINGS_SIZE);
}

bool settings_load(adapter_settings_t* s) {
    settings_defaults(s);
    adapter_settings_t a, b; uint32_t sa = 0, sb = 0;
    bool va = bank_read(0, &a, &sa), vb = bank_read(1, &b, &sb);
    if (va && (!vb || (int32_t)(sa - sb) >= 0)) { *s = a; return true; }
    if (vb) { *s = b; return true; }
    return false;
}

bool settings_save(const adapter_settings_t* s) {
    if (!settings_valid(s) || !storage_available()) return false;
    adapter_settings_t old; uint32_t sa = 0, sb = 0;
    bool va = bank_read(0, &old, &sa), vb = bank_read(1, &old, &sb);
    unsigned current = va && (!vb || (int32_t)(sa - sb) >= 0) ? 0u : 1u;
    uint32_t sequence = !va && !vb ? 0u : current == 0 ? sa : sb;
    unsigned target = !va && !vb ? 0u : 1u - current;
    uint8_t bytes[SETTINGS_SIZE]; settings_encode(s, sequence + 1u, bytes);
    if (!storage_write(target, bytes)) return false;
    uint32_t saved = 0;
    return bank_read(target, &old, &saved) && saved == sequence + 1u;
}
