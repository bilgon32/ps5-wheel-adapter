#include "profile_store.h"

#include <stddef.h>
#include <string.h>

#include "ffb.h"

#define PROFILE_MAGIC 0x50424646u /* "FFBP" in little endian */
#define PROFILE_VERSION 1u
#define PROFILE_SAVE_DELAY_MS 2000u

typedef struct {
    uint32_t magic;
    uint32_t sequence;
    uint8_t version;
    uint8_t profile;
    uint8_t profile_inverse;
    uint8_t reserved;
    uint32_t checksum;
    uint8_t padding[240];
} profile_record_t;

_Static_assert(sizeof(profile_record_t) == 256, "profile record must fill one flash page");

static uint32_t profile_checksum(const profile_record_t* record) {
    const uint8_t* bytes = (const uint8_t*) record;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < offsetof(profile_record_t, checksum); i++) {
        hash = (hash ^ bytes[i]) * 16777619u;
    }
    return hash;
}

static bool profile_record_valid(const profile_record_t* record) {
    return record->magic == PROFILE_MAGIC && record->version == PROFILE_VERSION &&
        record->profile_inverse == (uint8_t) ~record->profile &&
        ffb_profile_valid(record->profile) && record->checksum == profile_checksum(record);
}

#ifndef _MSC_VER
#include "hardware/address_mapped.h"
#include "hardware/flash.h"
#include "pico/flash.h"

#define PROFILE_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define PROFILE_RECORD_COUNT (FLASH_SECTOR_SIZE / FLASH_PAGE_SIZE)

extern char __flash_binary_end;

typedef struct {
    profile_record_t record;
    uint8_t slot;
    bool erase;
} profile_flash_operation_t;

static void __not_in_flash_func(profile_flash_write)(void* argument) {
    profile_flash_operation_t* operation = (profile_flash_operation_t*) argument;
    if (operation->erase) flash_range_erase(PROFILE_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(PROFILE_FLASH_OFFSET + operation->slot * FLASH_PAGE_SIZE,
        (const uint8_t*) &operation->record, FLASH_PAGE_SIZE);
}
#endif

uint8_t profile_store_init(profile_store_t* store, uint8_t fallback_profile) {
    memset(store, 0, sizeof(*store));
    store->profile = ffb_profile_valid(fallback_profile) ? fallback_profile : FFB_PROFILE_MINIMUM_12;
#ifndef _MSC_VER
    // Protect the journal if a future firmware ever grows into the last sector.
    if ((uintptr_t) &__flash_binary_end - XIP_BASE > PROFILE_FLASH_OFFSET) return store->profile;
    store->available = true;

    const profile_record_t* records = (const profile_record_t*) (XIP_BASE + PROFILE_FLASH_OFFSET);
    bool found = false;
    for (uint8_t slot = 0; slot < PROFILE_RECORD_COUNT; slot++) {
        if (records[slot].magic == UINT32_MAX) {
            store->next_slot = slot;
            break;
        }
        store->next_slot = slot + 1u;
        if (profile_record_valid(&records[slot]) &&
            (!found || (int32_t) (records[slot].sequence - store->sequence) > 0)) {
            found = true;
            store->sequence = records[slot].sequence;
            store->profile = records[slot].profile;
        }
    }
#else
    store->available = true;
#endif
    return store->profile;
}

void profile_store_schedule(profile_store_t* store, uint8_t profile, uint32_t now) {
    if (!store || !store->available || !ffb_profile_valid(profile)) return;
    store->profile = profile;
    store->pending_since = now;
    store->pending = true;
}

void profile_store_task(profile_store_t* store, uint32_t now) {
    if (!store || !store->pending || (uint32_t) (now - store->pending_since) < PROFILE_SAVE_DELAY_MS) return;
#ifndef _MSC_VER
    profile_flash_operation_t operation;
    memset(&operation, 0xff, sizeof(operation));
    operation.slot = store->next_slot < PROFILE_RECORD_COUNT ? store->next_slot : 0;
    operation.erase = store->next_slot >= PROFILE_RECORD_COUNT;
    operation.record.magic = PROFILE_MAGIC;
    operation.record.sequence = store->sequence + 1u;
    operation.record.version = PROFILE_VERSION;
    operation.record.profile = store->profile;
    operation.record.profile_inverse = (uint8_t) ~store->profile;
    operation.record.reserved = 0;
    operation.record.checksum = profile_checksum(&operation.record);

    if (flash_safe_execute(profile_flash_write, &operation, 100u) != PICO_OK) return;
    const profile_record_t* written = (const profile_record_t*)
        (XIP_BASE + PROFILE_FLASH_OFFSET + operation.slot * FLASH_PAGE_SIZE);
    if (!profile_record_valid(written) || written->sequence != operation.record.sequence ||
        written->profile != operation.record.profile) return;
    store->sequence = operation.record.sequence;
    store->next_slot = operation.slot + 1u;
#endif
    store->pending = false;
}
