#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "settings.h"
#include "hid_axes.h"
#include "device_registry.h"
#include "pc_protocol.h"
#include "pc_mode.h"
#include "brake.h"

static adapter_settings_t settings;
static device_registry_t devices;
static g29_report_t report;
static ffb_queue_t queue;
static uint8_t wheel;
static bool apply_ok = true;
static unsigned apply_calls;
static bool apply(bool save) { (void)save; apply_calls++; return apply_ok; }
static bool force(int8_t level) { return level == 0; }

static void request(pc_protocol_t* p, uint8_t op, const uint8_t* data, uint8_t n) {
    uint8_t bytes[63] = {'W', 'A', 1, op, 17, n};
    if (n) memcpy(bytes + 6, data, n);
    pc_protocol_request(p, bytes, sizeof(bytes));
    assert(p->reply[3] == op && p->reply[4] == 17);
}

int main(void) {
    adapter_settings_t decoded;
    uint8_t bytes[SETTINGS_SIZE], original[SETTINGS_SIZE]; uint32_t seq;
    settings_defaults(&settings); assert(settings_valid(&settings));
    settings.external_brake = true;
    settings_encode(&settings, 123, bytes);
    assert(settings_decode(&decoded, &seq, bytes, sizeof(bytes)) && seq == 123);
    assert(decoded.brake_released == -32767 && decoded.brake_pressed == 32767);
    assert(decoded.brake_bit_offset == 120 && decoded.brake_report_id == 3);
    memcpy(original, bytes, sizeof(bytes));
    for (unsigned i = 0; i < sizeof(bytes); i++) {
        bytes[i] ^= 0x80;
        assert(!settings_decode(&decoded, NULL, bytes, sizeof(bytes)));
        bytes[i] ^= 0x80;
    }
    assert(!settings_decode(&decoded, NULL, bytes, sizeof(bytes) - 1));
    decoded = settings; decoded.brake_width = 0; assert(!settings_valid(&decoded));
    decoded = settings; decoded.brake_width = 32; assert(!settings_valid(&decoded));
    decoded = settings; decoded.brake_released = 40000; assert(!settings_valid(&decoded));
    decoded = settings; decoded.brake_pressed = decoded.brake_released; assert(!settings_valid(&decoded));
    decoded = settings; decoded.lut[4] = 127; assert(!settings_valid(&decoded));
    decoded = settings; decoded.auth_vid = decoded.brake_vid; decoded.auth_pid = decoded.brake_pid;
    assert(!settings_valid(&decoded));

    /* Bank rollback uses the firmware's selection and checksum implementation. */
    settings_test_reset();
    assert(!settings_load(&decoded));
    assert(settings_save(&settings));
    assert(settings_load(&decoded) && decoded.default_profile == settings.default_profile);
    adapter_settings_t changed = settings; changed.default_profile = 3;
    settings_test_interrupt(true);
    assert(!settings_save(&changed));
    assert(settings_load(&decoded) && decoded.default_profile == settings.default_profile);
    settings_test_interrupt(false);
    assert(settings_save(&changed));
    assert(settings_load(&decoded) && decoded.default_profile == 3);
    changed.default_profile = 4;
    assert(settings_save(&changed));
    assert(settings_load(&decoded) && decoded.default_profile == 4);

    /* A report-ID axis after five button bits: signed, unaligned, twelve bits. */
    const uint8_t descriptor[] = {0x05,1,0x09,4,0xa1,1,0x85,3,
        0x05,9,0x19,1,0x29,5,0x15,0,0x25,1,0x75,1,0x95,5,0x81,2,
        0x05,1,0x09,0x35,0x16,0,0xf8,0x26,0xff,7,0x75,12,0x95,1,0x81,2,0xc0};
    hid_axis_t axes[HID_AXIS_MAX]; uint8_t count = 0; int32_t value;
    assert(hid_axes_parse(descriptor, sizeof(descriptor), axes, &count) && count == 1);
    assert(axes[0].bit_offset == 5 && axes[0].minimum == -2048 && axes[0].maximum == 2047);
    uint8_t raw[] = {3,0,0,1};
    assert(hid_axis_read(axes, raw, sizeof(raw), &value) && value == -2048);
    raw[1] = 0xe0; raw[2] = 0xff; raw[3] = 0;
    assert(hid_axis_read(axes, raw, sizeof(raw), &value) && value == 2047);
    raw[0] = 4; assert(!hid_axis_read(axes, raw, sizeof(raw), &value));
    raw[0] = 3; assert(!hid_axis_read(axes, raw, 3, &value));
    const uint8_t truncated[] = {0x16,0};
    assert(!hid_axes_parse(truncated, sizeof(truncated), axes, &count));
    const uint8_t bad_pop[] = {0xb4};
    assert(!hid_axes_parse(bad_pop, sizeof(bad_pop), axes, &count));
    const uint8_t oversized[] = {0x75,32,0x96,0,2,0x81,2};
    assert(!hid_axes_parse(oversized, sizeof(oversized), axes, &count));
    assert(hid_axis_pedal(-2048,-2048,2047) == 65535);
    assert(hid_axis_pedal(2047,-2048,2047) == 0);
    assert(hid_axis_pedal(0,2047,-2048) >= 32760);
    assert(hid_axis_pedal(-100,0,100) == 65535 && hid_axis_pedal(500,0,100) == 0);

    adapter_settings_t pedal_settings = settings;
    pedal_settings.brake_width = 12; pedal_settings.brake_bit_offset = 5;
    pedal_settings.brake_released = -2048; pedal_settings.brake_pressed = 2047;
    pedal_settings.brake_timeout_ms = 0;
    external_brake_t pedal = {0}; external_brake_configure(&pedal, &pedal_settings);
    assert(external_brake_fault(&pedal,0)); external_brake_connect(&pedal);
    assert(external_brake_receive(&pedal,raw,sizeof(raw),0));
    assert(external_brake_value(&pedal,65535,100000) == 0); /* change-only */
    pedal_settings.brake_timeout_ms = 500;
    assert(external_brake_value(&pedal,65535,500) == 65535);
    external_brake_disconnect(&pedal); assert(external_brake_value(&pedal,0,1) == 65535);

    /* Unknown gamepads never become authentication; pedals reserve all interfaces. */
    adapter_device_t* d = devices_mount(&devices, 2, 0, 0x1234, 0x5678, descriptor, sizeof(descriptor));
    assert(d && !device_is_auth(d, &settings));
    settings.brake_vid = d->vid; settings.brake_pid = d->pid;
    settings.brake_width = 12; settings.brake_signed = true; settings.brake_bit_offset = 5;
    settings.brake_released = -2048; settings.brake_pressed = 2047;
    assert(device_is_brake(d, &settings) && !device_is_auth(d, &settings));
    devices_report(&devices,2,0,raw,sizeof(raw),91);
    assert(d->has_report && d->values[0] == 2047 && d->valid_axes == 1);
    devices_unmount(&devices,2,0); assert(!devices_find(&devices,2,0));
    const uint8_t auth_desc[] = {0x75,8,0x85,0xf0,0x95,63,0xb1,2,0x85,0xf1,0xb1,2,
        0x85,0xf2,0x95,15,0xb1,2,0x85,0xf3,0x95,7,0xb1,2};
    assert(hid_auth_descriptor(auth_desc,sizeof(auth_desc)));
    d = devices_mount(&devices,3,1,0x0f0d,0x005e,auth_desc,sizeof(auth_desc));
    assert(device_is_auth(d,&settings));
    settings.auth_vid = d->vid; settings.auth_pid = d->pid; settings.auth_instance = 1;
    assert(device_is_auth(d,&settings));
    d->instance = 2; assert(!device_is_auth(d,&settings)); d->instance = 1;

    pc_protocol_t pc = {0};
    pc.settings = &settings; pc.devices = &devices; pc.input = &report; pc.ffb = &queue;
    pc.wheel_address = &wheel; pc.apply = apply; pc.force = force; pc.flash_bytes = 2097152;
    pc_protocol_init(&pc); pc_setup_mode = true;
    request(&pc,PC_INFO,NULL,0); assert(pc.reply[5] == PC_OK && pc.reply[6] == 16);
    uint8_t save = 1; request(&pc,PC_APPLY_SETTINGS,&save,1);
    assert(pc.reply[5] == PC_INCOMPLETE && apply_calls == 0);
    uint8_t chunk[57];
    for (unsigned offset = 0; offset < SETTINGS_SIZE; offset += 55) {
        unsigned n = SETTINGS_SIZE - offset < 55 ? SETTINGS_SIZE - offset : 55;
        chunk[0] = (uint8_t)offset; chunk[1] = (uint8_t)(offset >> 8);
        memcpy(chunk+2,original+offset,n); request(&pc,PC_WRITE_SETTINGS,chunk,(uint8_t)(n+2));
        assert(pc.reply[5] == PC_OK);
    }
    adapter_settings_t before = settings;
    apply_ok = false; request(&pc,PC_APPLY_SETTINGS,&save,1);
    assert(pc.reply[5] == PC_SAVE_FAILED && settings.brake_vid == before.brake_vid);
    apply_ok = true; request(&pc,PC_APPLY_SETTINGS,&save,1);
    assert(pc.reply[5] == PC_OK && settings.brake_vid == 0x2341);
    request(&pc,PC_APPLY_SETTINGS,&save,1); assert(pc.reply[5] == PC_INCOMPLETE);
    chunk[0] = 0xff; chunk[1] = 1; chunk[2] = 0; chunk[3] = 0;
    request(&pc,PC_WRITE_SETTINGS,chunk,4); assert(pc.reply[5] == PC_BAD_REQUEST);
    uint8_t slot = 16; request(&pc,PC_DEVICE,&slot,1); assert(pc.reply[5] == PC_BAD_REQUEST);
    uint8_t level = 16; request(&pc,PC_FORCE,&level,1); assert(pc.reply[5] == PC_BAD_REQUEST);
    level = 1; request(&pc,PC_FORCE,&level,1); assert(pc.reply[5] == PC_BUSY);
    pc_setup_mode = false; request(&pc,PC_INFO,NULL,0); assert(pc.reply[5] == PC_BAD_REQUEST);
    puts("Settings integrity, descriptor parsing, device roles and PC protocol passed.");
    return 0;
}
