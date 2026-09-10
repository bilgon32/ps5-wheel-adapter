#ifndef _REPORTS_H_
#define _REPORTS_H_

#include <stdint.h>
#include <stddef.h>

#ifdef _MSC_VER
#pragma pack(push, 1)
#define REPORT_PACKED
#else
#define REPORT_PACKED __attribute__((packed))
#endif

// G29 HID report
typedef struct REPORT_PACKED {
    uint8_t lx;
    uint8_t ly;
    uint8_t rx;
    uint8_t ry;
    uint8_t dpad : 4;
    uint8_t square : 1;
    uint8_t cross : 1;
    uint8_t circle : 1;
    uint8_t triangle : 1;
    uint8_t L1 : 1;
    uint8_t R1 : 1;
    uint8_t L2 : 1;
    uint8_t R2 : 1;
    uint8_t select : 1;
    uint8_t start : 1;
    uint8_t L3 : 1;
    uint8_t R3 : 1;
    uint8_t PS : 1;
    uint8_t touchpad : 1;
    uint8_t counter : 6;
    uint8_t whatever[35];
    uint16_t wheel;
    uint16_t throttle;
    uint16_t brake;
    uint16_t clutch;
    uint8_t gears; // bits 0..5 = gears 1..6, bit 7 = reverse; zero = neutral
    uint8_t pedal_reserved[2]; // 0xff, 0xff
    uint8_t extra_buttons; // enter, dial down, dial up, minus, plus (bits 0..4)
    uint8_t whatever2[9];
} g29_report_t;

// Driving Force HID report
typedef struct REPORT_PACKED {
    uint32_t wheel : 10;
    uint32_t cross : 1;
    uint32_t square : 1;
    uint32_t circle : 1;
    uint32_t triangle : 1;
    uint32_t R1 : 1;
    uint32_t L1 : 1;
    uint32_t R2 : 1;
    uint32_t L2 : 1;
    uint32_t select : 1;
    uint32_t start : 1;
    uint32_t R3 : 1;
    uint32_t L3 : 1;
    uint32_t whatever : 2;
    uint8_t y;
    uint32_t hat : 4;
    uint32_t whatever2 : 4;
    uint8_t throttle;
    uint8_t brake;
} df_report_t;

#ifdef _MSC_VER
#pragma pack(pop)
#endif
#undef REPORT_PACKED

_Static_assert(sizeof(g29_report_t) == 63, "G29 payload must be 63 bytes");
_Static_assert(offsetof(g29_report_t, wheel) == 42, "G29 steering offset");
_Static_assert(offsetof(g29_report_t, clutch) == 48, "G29 clutch offset");
_Static_assert(offsetof(g29_report_t, gears) == 50, "G29 shifter offset");
_Static_assert(offsetof(g29_report_t, extra_buttons) == 53, "G29 extra button offset");

#endif
