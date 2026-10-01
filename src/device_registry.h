#ifndef ADAPTER_DEVICE_REGISTRY_H
#define ADAPTER_DEVICE_REGISTRY_H
#include "hid_axes.h"
#include "settings.h"
#define ADAPTER_DEVICE_MAX 16u
typedef struct {
    bool connected, auth_capable, descriptor_valid, receive_pending, has_report;
    uint8_t address, instance, axis_count, report_length;
    uint16_t vid, pid;
    uint32_t descriptor_hash, updated_ms;
    hid_axis_t axes[HID_AXIS_MAX];
    uint8_t report[64];
    int32_t values[HID_AXIS_MAX];
    uint16_t valid_axes;
} adapter_device_t;
typedef struct { adapter_device_t items[ADAPTER_DEVICE_MAX]; uint32_t generation; } device_registry_t;

adapter_device_t* devices_find(device_registry_t* r, uint8_t address, uint8_t instance);
adapter_device_t* devices_mount(device_registry_t* r, uint8_t address, uint8_t instance,
    uint16_t vid, uint16_t pid, const uint8_t* descriptor, size_t length);
void devices_unmount(device_registry_t* r, uint8_t address, uint8_t instance);
void devices_report(device_registry_t* r, uint8_t address, uint8_t instance,
    const uint8_t* report, size_t length, uint32_t now);
bool device_is_brake(const adapter_device_t* d, const adapter_settings_t* s);
bool device_is_auth(const adapter_device_t* d, const adapter_settings_t* s);
#endif
