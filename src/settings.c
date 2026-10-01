#include "settings.h"
#include "brake.h"
#include "ffb.h"
#include "g27.h"
#include <string.h>

static uint16_t get16(const uint8_t* p) { return (uint16_t)(p[0] | (uint16_t)p[1] << 8); }
static uint32_t get32(const uint8_t* p) { return (uint32_t)get16(p) | (uint32_t)get16(p + 2) << 16; }
static void put16(uint8_t* p, uint16_t n) { p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8); }
static void put32(uint8_t* p, uint32_t n) { put16(p, (uint16_t)n); put16(p + 2, (uint16_t)(n >> 16)); }

uint32_t settings_checksum(const uint8_t* bytes, size_t length) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < length; i++) hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

void settings_defaults(adapter_settings_t* s) {
    memset(s, 0, sizeof(*s));
    s->external_brake = false;
    s->brake_vid = BRAKE_USB_VID; s->brake_pid = BRAKE_USB_PID;
    s->brake_instance = 255; s->brake_report_id = BRAKE_REPORT_ID;
    s->brake_width = 16; s->brake_signed = true;
    s->brake_bit_offset = (BRAKE_RZ_OFFSET - 1u) * 8u;
    s->brake_released = BRAKE_RELEASED_RAW; s->brake_pressed = BRAKE_PRESSED_RAW;
    s->brake_timeout_ms = BRAKE_TIMEOUT_MS;
    s->auth_instance = 255;
    s->default_profile = FFB_PROFILE_MINIMUM_12;
    for (unsigned i = 0; i < 128; i++) s->lut[i] = (uint8_t)i;
}

bool settings_valid(const adapter_settings_t* s) {
    if (!s || !ffb_profile_valid(s->default_profile)) return false;
    if ((s->auth_vid == 0) != (s->auth_pid == 0)) return false;
    if (s->external_brake) {
        if (!s->brake_vid || !s->brake_pid || s->brake_width < 1 || s->brake_width > 31 ||
            (uint32_t)s->brake_bit_offset + s->brake_width > (64u - (s->brake_report_id != 0)) * 8u ||
            s->brake_released == s->brake_pressed) return false;
        int64_t low = s->brake_signed ? -(INT64_C(1) << (s->brake_width - 1)) : 0;
        int64_t high = (INT64_C(1) << (s->brake_width - (s->brake_signed ? 1 : 0))) - 1;
        if (s->brake_released < low || s->brake_released > high ||
            s->brake_pressed < low || s->brake_pressed > high) return false;
        if (s->brake_vid == LOGITECH_VID &&
            (s->brake_pid == G27_PID || s->brake_pid == DRIVING_FORCE_PID)) return false;
        if (s->brake_vid == s->auth_vid && s->brake_pid == s->auth_pid) return false;
    }
    if (s->lut[0] != 0 || s->lut[127] != 127) return false;
    for (unsigned i = 1; i < 128; i++) if (s->lut[i] > 127 || s->lut[i] < s->lut[i-1]) return false;
    return true;
}

void settings_encode(const adapter_settings_t* s, uint32_t sequence, uint8_t b[SETTINGS_SIZE]) {
    memset(b, 0, SETTINGS_SIZE);
    put32(b, SETTINGS_MAGIC); put16(b + 4, SETTINGS_VERSION); put16(b + 6, SETTINGS_SIZE);
    put32(b + 8, sequence);
    b[16] = s->external_brake; b[17] = s->brake_instance;
    put16(b + 18, s->brake_vid); put16(b + 20, s->brake_pid);
    b[22] = s->brake_report_id; b[23] = s->brake_width;
    put16(b + 24, s->brake_bit_offset); b[26] = s->brake_signed;
    put32(b + 28, (uint32_t)s->brake_released); put32(b + 32, (uint32_t)s->brake_pressed);
    put16(b + 36, s->brake_timeout_ms); put16(b + 38, s->auth_vid);
    put16(b + 40, s->auth_pid); b[42] = s->auth_instance;
    b[43] = s->default_profile; b[44] = s->custom_lut;
    memcpy(b + 48, s->lut, 128);
    put32(b + 12, settings_checksum(b + 16, SETTINGS_SIZE - 16) ^ settings_checksum(b, 12));
}

bool settings_decode(adapter_settings_t* s, uint32_t* sequence, const uint8_t* b, size_t length) {
    if (!s || !b || length != SETTINGS_SIZE || get32(b) != SETTINGS_MAGIC ||
        get16(b + 4) != SETTINGS_VERSION || get16(b + 6) != SETTINGS_SIZE ||
        get32(b + 12) != (settings_checksum(b + 16, SETTINGS_SIZE - 16) ^ settings_checksum(b, 12)) ||
        b[16] > 1 || b[26] > 1 || b[44] > 1) return false;
    adapter_settings_t result = {0};
    result.external_brake = b[16] != 0; result.brake_instance = b[17];
    result.brake_vid = get16(b + 18); result.brake_pid = get16(b + 20);
    result.brake_report_id = b[22]; result.brake_width = b[23];
    result.brake_bit_offset = get16(b + 24); result.brake_signed = b[26] != 0;
    /* Converting uint32_t through int64_t avoids implementation-defined sign extension. */
    uint32_t raw = get32(b + 28);
    result.brake_released = (int32_t)((int64_t)raw - ((raw & 0x80000000u) ? INT64_C(4294967296) : 0));
    raw = get32(b + 32);
    result.brake_pressed = (int32_t)((int64_t)raw - ((raw & 0x80000000u) ? INT64_C(4294967296) : 0));
    result.brake_timeout_ms = get16(b + 36); result.auth_vid = get16(b + 38);
    result.auth_pid = get16(b + 40); result.auth_instance = b[42];
    result.default_profile = b[43]; result.custom_lut = b[44] != 0;
    memcpy(result.lut, b + 48, 128);
    if (!settings_valid(&result)) return false;
    *s = result;
    if (sequence) *sequence = get32(b + 8);
    return true;
}
