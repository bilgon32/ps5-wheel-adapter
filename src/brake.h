#ifndef ADAPTER_BRAKE_H
#define ADAPTER_BRAKE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Profile measured from the user's Arduino Micro. See tools/arduino-micro-hid.json.
#define BRAKE_USB_VID 0x2341
#define BRAKE_USB_PID 0x8037
#define BRAKE_REPORT_ID 3
#define BRAKE_REPORT_SIZE 28
#define BRAKE_RZ_OFFSET 16 // Includes the report-ID byte.

// Signed raw endpoints; reversing these also reverses the pedal direction.
#ifndef BRAKE_RELEASED_RAW
#define BRAKE_RELEASED_RAW (-32767)
#endif
#ifndef BRAKE_PRESSED_RAW
#define BRAKE_PRESSED_RAW 32767
#endif

// This Arduino was observed sending reports continuously, including at rest.
#define BRAKE_TIMEOUT_MS 500u
#define BRAKE_G29_RELEASED UINT16_MAX

typedef struct {
    bool selected;
    bool connected;
    bool has_sample;
    uint16_t value;
    uint32_t updated_ms;
} external_brake_t;

void external_brake_connect(external_brake_t* brake);
void external_brake_disconnect(external_brake_t* brake);
bool external_brake_receive(external_brake_t* brake, const uint8_t* data, size_t length, uint32_t now_ms);
uint16_t external_brake_value(const external_brake_t* brake, uint16_t wheel_brake, uint32_t now_ms);

#endif
