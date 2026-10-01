#ifndef ADAPTER_PC_PROTOCOL_H
#define ADAPTER_PC_PROTOCOL_H
#include "device_registry.h"
#include "reports.h"
#include "ffb.h"

#define PC_USB_VID 0xcafeu
#define PC_USB_PID 0x4029u
#define PC_PROTOCOL_VERSION 1u
#define PC_PACKET_SIZE 63u
enum { PC_INFO = 1, PC_READ_SETTINGS, PC_WRITE_SETTINGS, PC_APPLY_SETTINGS,
    PC_DEVICE, PC_AXIS, PC_TELEMETRY, PC_REBOOT, PC_SET_PROFILE, PC_FORCE };
enum { PC_OK = 0, PC_BAD_REQUEST, PC_BAD_SETTINGS, PC_INCOMPLETE, PC_SAVE_FAILED, PC_NOT_FOUND, PC_BUSY };

typedef struct {
    adapter_settings_t* settings;
    device_registry_t* devices;
    g29_report_t* input;
    ffb_queue_t* ffb;
    uint8_t* wheel_address;
    bool (*apply)(bool save);
    bool (*force)(int8_t level);
    uint8_t (*wheel_status)(void); /* 0 absent, 1 initializing, 2 native, 3 compatibility */
    uint32_t flash_bytes;
    uint8_t staged[SETTINGS_SIZE], written[SETTINGS_SIZE / 8u], reply[PC_PACKET_SIZE];
} pc_protocol_t;
void pc_protocol_init(pc_protocol_t* p);
void pc_protocol_request(pc_protocol_t* p, const uint8_t* request, size_t length);
#endif
