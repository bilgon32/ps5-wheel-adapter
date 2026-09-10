#include <assert.h>
#include <stdio.h>
#include "brake.h"

static void sample(external_brake_t* brake, int32_t raw, uint32_t now) {
    uint8_t data[BRAKE_REPORT_SIZE] = { BRAKE_REPORT_ID };
    data[BRAKE_RZ_OFFSET] = (uint8_t) raw;
    data[BRAKE_RZ_OFFSET + 1] = (uint8_t) ((uint16_t) raw >> 8);
    assert(external_brake_receive(brake, data, sizeof(data), now));
}

int main(void) {
    external_brake_t brake = { 0 };
    assert(external_brake_value(&brake, 1234, 0) == 1234);
    external_brake_connect(&brake);
    assert(external_brake_value(&brake, 0, 0) == UINT16_MAX);
    sample(&brake, BRAKE_RELEASED_RAW, 10);
    assert(external_brake_value(&brake, 0, 10) == UINT16_MAX);
    sample(&brake, BRAKE_PRESSED_RAW, 20);
    assert(external_brake_value(&brake, UINT16_MAX, 20) == 0);
    sample(&brake, (BRAKE_RELEASED_RAW + BRAKE_PRESSED_RAW) / 2, 30);
    assert(external_brake_value(&brake, 0, 30) == 32767);

    // A pressure sweep must be monotonic even for reversed calibration.
    uint16_t previous = UINT16_MAX;
    for (int i = 0; i <= 1000; ++i) {
        int32_t raw = BRAKE_RELEASED_RAW + (int32_t) (((int64_t) BRAKE_PRESSED_RAW - BRAKE_RELEASED_RAW) * i / 1000);
        sample(&brake, raw, 40);
        uint16_t current = external_brake_value(&brake, 42, 40);
        assert(current <= previous);
        previous = current;
    }
    assert(previous == 0);
    sample(&brake, BRAKE_PRESSED_RAW > BRAKE_RELEASED_RAW ? -32768 : 32767, 50);
    assert(external_brake_value(&brake, 0, 50) == UINT16_MAX);
    sample(&brake, BRAKE_PRESSED_RAW > BRAKE_RELEASED_RAW ? 32767 : -32768, 50);
    assert(external_brake_value(&brake, UINT16_MAX, 50) == 0);

    // Invalid reports neither change the value nor extend its lifetime.
    uint8_t invalid[BRAKE_REPORT_SIZE + 1] = { BRAKE_REPORT_ID };
    assert(!external_brake_receive(&brake, invalid, 17, 100));
    assert(!external_brake_receive(&brake, invalid, sizeof(invalid), 100));
    invalid[0] = 4;
    assert(!external_brake_receive(&brake, invalid, BRAKE_REPORT_SIZE, 100));
    assert(!external_brake_receive(&brake, NULL, BRAKE_REPORT_SIZE, 100));
    assert(external_brake_value(&brake, UINT16_MAX, 50 + BRAKE_TIMEOUT_MS - 1) == 0);
    assert(external_brake_value(&brake, 0, 50 + BRAKE_TIMEOUT_MS) == UINT16_MAX);

    // Millisecond counter rollover must not invalidate a fresh sample.
    sample(&brake, BRAKE_PRESSED_RAW, UINT32_MAX - 10);
    assert(external_brake_value(&brake, UINT16_MAX, 5) == 0);
    assert(external_brake_value(&brake, 0, 500) == UINT16_MAX);
    external_brake_disconnect(&brake);
    assert(external_brake_value(&brake, 0, 0) == UINT16_MAX);
    invalid[0] = BRAKE_REPORT_ID;
    assert(!external_brake_receive(&brake, invalid, BRAKE_REPORT_SIZE, 0));
    external_brake_connect(&brake);
    assert(external_brake_value(&brake, 0, 0) == UINT16_MAX);
    sample(&brake, BRAKE_PRESSED_RAW, 0);
    assert(external_brake_value(&brake, UINT16_MAX, 0) == 0);
    puts("Brake decoding, calibration, precedence, invalid packets, timeout and reconnect passed.");
    return 0;
}
