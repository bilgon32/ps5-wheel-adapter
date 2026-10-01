#ifndef ADAPTER_HID_AXES_H
#define ADAPTER_HID_AXES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define HID_AXIS_MAX 16u
typedef struct {
    uint16_t usage_page, usage, bit_offset;
    uint8_t report_id, width;
    bool is_signed;
    int32_t minimum, maximum;
} hid_axis_t;
/* False means malformed/unsupported/truncated, never silently use partial axes. */
bool hid_axes_parse(const uint8_t* descriptor, size_t length, hid_axis_t axes[HID_AXIS_MAX], uint8_t* count);
bool hid_axis_read(const hid_axis_t* axis, const uint8_t* report, size_t length, int32_t* value);
bool hid_auth_descriptor(const uint8_t* descriptor, size_t length);
uint16_t hid_axis_pedal(int32_t raw, int32_t released, int32_t pressed);
#endif
