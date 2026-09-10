#include "brake.h"

_Static_assert(BRAKE_RELEASED_RAW >= -32768 && BRAKE_RELEASED_RAW <= 32767, "Release endpoint must fit signed Rz");
_Static_assert(BRAKE_PRESSED_RAW >= -32768 && BRAKE_PRESSED_RAW <= 32767, "Pressed endpoint must fit signed Rz");
_Static_assert(BRAKE_RELEASED_RAW != BRAKE_PRESSED_RAW, "Brake endpoints must differ");

void external_brake_connect(external_brake_t* brake) {
    brake->selected = true;
    brake->connected = true;
    brake->has_sample = false;
    brake->value = BRAKE_G29_RELEASED;
}

void external_brake_disconnect(external_brake_t* brake) {
    brake->connected = false;
    brake->has_sample = false;
    brake->value = BRAKE_G29_RELEASED;
    // Keep external selection until reboot: do not unexpectedly switch pedals.
}

bool external_brake_receive(external_brake_t* brake, const uint8_t* data, size_t length, uint32_t now_ms) {
    if (!brake->connected || !data || length != BRAKE_REPORT_SIZE || data[0] != BRAKE_REPORT_ID) {
        return false;
    }

    uint32_t bits = (uint32_t) data[BRAKE_RZ_OFFSET] | ((uint32_t) data[BRAKE_RZ_OFFSET + 1] << 8);
    int32_t raw = bits & 0x8000u ? (int32_t) bits - 65536 : (int32_t) bits;
    int64_t position = (int64_t) raw - BRAKE_RELEASED_RAW;
    int64_t span = (int64_t) BRAKE_PRESSED_RAW - BRAKE_RELEASED_RAW;
    if (span < 0) {
        span = -span;
        position = -position;
    }
    if (position < 0) position = 0;
    if (position > span) position = span;
    // G29 pedals use 65535 for released and 0 for fully pressed.
    brake->value = (uint16_t) (UINT16_MAX - (position * UINT16_MAX + span / 2) / span);
    brake->updated_ms = now_ms;
    brake->has_sample = true;
    return true;
}

uint16_t external_brake_value(const external_brake_t* brake, uint16_t wheel_brake, uint32_t now_ms) {
    if (!brake->selected) return wheel_brake;
    if (!brake->connected || !brake->has_sample || (uint32_t) (now_ms - brake->updated_ms) >= BRAKE_TIMEOUT_MS) {
        return BRAKE_G29_RELEASED;
    }
    return brake->value;
}
