#include "device_registry.h"
#include "g27.h"
#include <string.h>

adapter_device_t* devices_find(device_registry_t* r, uint8_t address, uint8_t instance) {
    for (unsigned i = 0; i < ADAPTER_DEVICE_MAX; i++)
        if (r->items[i].connected && r->items[i].address == address && r->items[i].instance == instance) return &r->items[i];
    return NULL;
}
adapter_device_t* devices_mount(device_registry_t* r, uint8_t address, uint8_t instance,
    uint16_t vid, uint16_t pid, const uint8_t* descriptor, size_t length) {
    adapter_device_t* d = devices_find(r, address, instance);
    if (!d) for (unsigned i = 0; i < ADAPTER_DEVICE_MAX; i++) if (!r->items[i].connected) { d = &r->items[i]; break; }
    if (!d) return NULL;
    memset(d, 0, sizeof(*d)); d->connected = true;
    d->address = address; d->instance = instance; d->vid = vid; d->pid = pid;
    if (descriptor && length) {
        d->descriptor_hash = settings_checksum(descriptor, length);
        d->descriptor_valid = hid_axes_parse(descriptor, length, d->axes, &d->axis_count);
        d->auth_capable = hid_auth_descriptor(descriptor, length);
    }
    r->generation++; return d;
}
void devices_unmount(device_registry_t* r, uint8_t address, uint8_t instance) {
    adapter_device_t* d = devices_find(r, address, instance);
    if (d) { memset(d, 0, sizeof(*d)); r->generation++; }
}
void devices_report(device_registry_t* r, uint8_t address, uint8_t instance,
    const uint8_t* report, size_t length, uint32_t now) {
    adapter_device_t* d = devices_find(r, address, instance);
    if (!d) return;
    d->receive_pending = false;
    if (!report || !length || length > sizeof(d->report)) return;
    memcpy(d->report, report, length); d->report_length = (uint8_t)length;
    d->has_report = true; d->updated_ms = now;
    for (unsigned i = 0; i < d->axis_count; i++) {
        if (hid_axis_read(&d->axes[i], report, length, &d->values[i])) d->valid_axes |= (uint16_t)(1u << i);
    }
}
bool device_is_brake(const adapter_device_t* d, const adapter_settings_t* s) {
    if (!d || !d->connected || !s->external_brake || d->vid != s->brake_vid || d->pid != s->brake_pid) return false;
    if (s->brake_instance != 255) return d->instance == s->brake_instance;
    for (unsigned i = 0; i < d->axis_count; i++) {
        const hid_axis_t* a = &d->axes[i];
        if (a->report_id == s->brake_report_id && a->bit_offset == s->brake_bit_offset &&
            a->width == s->brake_width && a->is_signed == s->brake_signed) return true;
    }
    /* Legacy measured Arduino; permits an older HID host without a descriptor. */
    return !d->descriptor_hash && d->vid == 0x2341 && d->pid == 0x8037;
}
bool device_is_auth(const adapter_device_t* d, const adapter_settings_t* s) {
    if (!d || !d->connected || (d->vid == LOGITECH_VID &&
        (d->pid == G27_PID || d->pid == DRIVING_FORCE_PID))) return false;
    /* Reserve every interface of the configured pedal device. */
    if (s->external_brake && d->vid == s->brake_vid && d->pid == s->brake_pid) return false;
    if (s->auth_vid) return d->vid == s->auth_vid && d->pid == s->auth_pid &&
        (s->auth_instance == 255 ? d->auth_capable : d->instance == s->auth_instance);
    return d->auth_capable;
}
