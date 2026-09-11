#ifndef ADAPTER_FFB_H
#define ADAPTER_FFB_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FFB_COMMAND_SIZE 7u
#define FFB_QUEUE_CAPACITY 128u

typedef enum {
    FFB_PROFILE_LINEAR = 0,
    FFB_PROFILE_MINIMUM_12,
    FFB_PROFILE_MINIMUM_18,
    FFB_PROFILE_PROGRESSIVE,
    FFB_PROFILE_G27_MEASURED,
    FFB_PROFILE_COUNT
} ffb_profile_t;

typedef struct {
    uint8_t commands[FFB_QUEUE_CAPACITY][FFB_COMMAND_SIZE];
    uint16_t head, count, high_water;
    uint32_t received, sent, transformed, overflows, blocked_mode_changes, invalid;
    ffb_profile_t profile;
    bool profiles_enabled;
} ffb_queue_t;

// Report 5 over control excludes its ID; interrupt OUT includes the ID. Unknown
// metadata falls back to the upstream adapter's byte-1 command layout.
bool ffb_enqueue_report(ffb_queue_t* queue, uint8_t report_id, const uint8_t* data, size_t length);
const uint8_t* ffb_front(const ffb_queue_t* queue);
void ffb_pop(ffb_queue_t* queue);
void ffb_clear(ffb_queue_t* queue);
bool ffb_profile_valid(uint8_t profile);
void ffb_set_profile(ffb_queue_t* queue, ffb_profile_t profile);
void ffb_enable_profiles(ffb_queue_t* queue, bool enabled);
ffb_profile_t ffb_next_profile(ffb_profile_t profile);
const char* ffb_profile_name(ffb_profile_t profile);
#endif
