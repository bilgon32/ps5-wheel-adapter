#ifndef ADAPTER_PROFILE_STORE_H
#define ADAPTER_PROFILE_STORE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t sequence;
    uint32_t pending_since;
    uint8_t profile;
    uint8_t next_slot;
    bool pending;
    bool available;
} profile_store_t;

uint8_t profile_store_init(profile_store_t* store, uint8_t fallback_profile);
void profile_store_schedule(profile_store_t* store, uint8_t profile, uint32_t now);
void profile_store_task(profile_store_t* store, uint32_t now);

#endif
