#include "g27.h"

// Protocol facts and their sources are listed in docs/g27-protocol.md.
bool g27_revision(uint16_t bcd_device) {
    return (bcd_device & 0xfff0u) == 0x1230u;
}

bool g27_decode(const uint8_t* data, size_t length, g29_report_t* output, uint16_t* wheel_brake) {
    // Native USB reports have no report-ID prefix. Windows-filtered reports
    // have a different length/layout and must not be passed to this decoder.
    // The native payload is 11 bytes. Some host controllers report the full
    // interrupt transfer with trailing endpoint padding, so ignore bytes after
    // the payload instead of rejecting an otherwise valid input report.
    if (!data || !output || !wheel_brake || length < 11) return false;

    uint16_t steering = ((uint16_t) data[3] | ((uint16_t) data[4] << 8)) >> 2;
    output->wheel = (uint16_t) (((uint32_t) steering * 65535u + 8191u) / 16383u);
    output->throttle = (uint16_t) data[5] * 257u;
    *wheel_brake = (uint16_t) data[6] * 257u;
    output->clutch = (uint16_t) data[7] * 257u;

    output->dpad = (data[0] & 0x0fu) < 8 ? data[0] & 0x0fu : 8;
    output->cross = (data[0] >> 4) & 1u;
    output->square = (data[0] >> 5) & 1u;
    output->circle = (data[0] >> 6) & 1u;
    output->triangle = (data[0] >> 7) & 1u;
    output->R1 = data[1] & 1u;
    output->L1 = (data[1] >> 1) & 1u;
    output->R2 = (data[1] >> 2) & 1u;
    output->L2 = (data[1] >> 3) & 1u;
    output->select = (data[1] >> 4) & 1u;
    output->start = (data[1] >> 5) & 1u;
    output->R3 = (data[1] >> 6) & 1u;
    output->L3 = (data[1] >> 7) & 1u;

    // Four additional wheel buttons use real G29 controls, without duplicating
    // the shifter buttons: L4=minus, L5=dial down, R4=plus, R5=dial up.
    output->extra_buttons = ((data[3] & 1u) ? 0x08u : 0) |
                            ((data[3] & 2u) ? 0x02u : 0) |
                            ((data[2] & 0x40u) ? 0x10u : 0) |
                            ((data[2] & 0x80u) ? 0x04u : 0);

    uint8_t gears = data[2] & 0x3fu;
    // The push-down switch can also be held in neutral. Require the right/down
    // gate as well, so pressing the stick vertically never selects reverse.
    if ((data[10] & 0x40u) && data[8] > 0xa0u && data[9] < 0x40u) gears = 0x80u;
    // Reject impossible simultaneous gears; neutral must actively clear bits.
    if (gears && (gears & (gears - 1u))) gears = 0;
    output->gears = gears;
    return true;
}
