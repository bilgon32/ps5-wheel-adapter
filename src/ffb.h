#ifndef ADAPTER_FFB_H
#define ADAPTER_FFB_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FFB_COMMAND_SIZE 7u
#define FFB_QUEUE_CAPACITY 128u
typedef struct {
    uint8_t commands[FFB_QUEUE_CAPACITY][FFB_COMMAND_SIZE];
    uint16_t head, count, high_water;
    uint32_t received, sent, overflows, blocked_mode_changes, invalid;
} ffb_queue_t;

// Report 5 over control excludes its ID; interrupt OUT includes the ID. Unknown
// metadata falls back to the upstream adapter's byte-1 command layout.
bool ffb_enqueue_report(ffb_queue_t* queue, uint8_t report_id, const uint8_t* data, size_t length);
const uint8_t* ffb_front(const ffb_queue_t* queue);
void ffb_pop(ffb_queue_t* queue);
void ffb_clear(ffb_queue_t* queue);
#endif
