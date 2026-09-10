#include "ffb.h"
#include <string.h>

bool ffb_enqueue_report(ffb_queue_t* queue, uint8_t report_id, const uint8_t* data, size_t length) {
    if (!data) { queue->invalid++; return false; }
    if (report_id == 0 && length && data[0] == 5) {
        data++; length--; report_id = 5;
    }
    // The PS5 uses the legacy shape handled by the upstream adapter: metadata
    // does not identify report 5, while byte 0 precedes the seven-byte Logitech
    // command. Keep strict report-5 handling first, then use that proven layout.
    if (report_id != 5 && length > FFB_COMMAND_SIZE) {
        data++; length--; report_id = 5;
    }
    if (report_id != 5 || length < FFB_COMMAND_SIZE) {
        queue->invalid++;
        return false;
    }
    // Identity changes belong to our host-side initialization, not to the PS5.
    // Forward ranges, RPM LEDs, spring/damper setup and force samples unchanged.
    if (data[0] == 0xf8 && (data[1] == 0x01 || data[1] == 0x09 ||
        data[1] == 0x0a || data[1] == 0x10 || data[1] == 0x11)) {
        queue->blocked_mode_changes++;
        return false;
    }
    queue->received++;
    if (queue->count == FFB_QUEUE_CAPACITY) {
        queue->overflows++;
        return false;
    }
    memcpy(queue->commands[(queue->head + queue->count) % FFB_QUEUE_CAPACITY], data, FFB_COMMAND_SIZE);
    queue->count++;
    if (queue->count > queue->high_water) queue->high_water = queue->count;
    return true;
}

const uint8_t* ffb_front(const ffb_queue_t* queue) {
    return queue->count ? queue->commands[queue->head] : NULL;
}

void ffb_pop(ffb_queue_t* queue) {
    if (!queue->count) return;
    queue->head = (queue->head + 1u) % FFB_QUEUE_CAPACITY;
    queue->count--;
    queue->sent++;
}

void ffb_clear(ffb_queue_t* queue) {
    queue->head = 0;
    queue->count = 0;
}
