#include <assert.h>
#ifdef _MSC_VER
// Host tests exercise callbacks; the ARM build checks actual packed wire layouts.
#define __attribute__(x)
#endif
#define main firmware_main
#include "../src/adapter.c"
#undef main

static uint32_t now;
static bool accept_receive = true, accept_send = true;
static unsigned receive_calls, send_calls;
static uint8_t receive_device, receive_instance;
void board_init(void) {}
void stdio_init_all(void) {}
void tusb_init(void) {}
void tuh_task(void) {}
void tud_task(void) {}
uint32_t board_millis(void) { return now; }
void board_led_write(bool on) { (void) on; }
bool tud_hid_ready(void) { return true; }
bool tud_hid_report(uint8_t id, const void* data, uint16_t length) { assert(id == 1); (void) data; (void) length; send_calls++; return accept_send; }
bool tuh_hid_send_report(uint8_t d, uint8_t i, uint8_t id, const void* p, uint16_t l) { (void)d; (void)i; (void)id; (void)p; (void)l; return true; }
bool tuh_hid_get_report(uint8_t d, uint8_t i, uint8_t id, uint8_t t, void* p, uint16_t l) { (void)d; (void)i; (void)id; (void)t; (void)p; (void)l; return true; }
bool tuh_hid_set_report(uint8_t d, uint8_t i, uint8_t id, uint8_t t, const void* p, uint16_t l) { (void)d; (void)i; (void)id; (void)t; (void)p; (void)l; return true; }
bool tuh_hid_receive_report(uint8_t device, uint8_t instance) {
    receive_calls++; receive_device = device; receive_instance = instance; return accept_receive;
}
void tuh_vid_pid_get(uint8_t device, uint16_t* vid, uint16_t* pid) {
    *vid = device == 1 ? 0x046d : device == 2 ? BRAKE_USB_VID : 0x1532;
    *pid = device == 1 ? 0xc294 : device == 2 ? BRAKE_USB_PID : 1;
}

int main(void) {
    report_init();
    tuh_hid_mount_cb(1, 0, NULL, 0);
    df_report_t wheel = { 0 };
    wheel.brake = 123;
    tuh_hid_report_received_cb(1, 0, (const uint8_t*) &wheel, sizeof(wheel));
    hid_task();
    assert(report.brake == 123u << 8);
    // Arduino before auth, and additional Arduino interfaces, must not claim auth.
    tuh_hid_mount_cb(2, 1, NULL, 0);
    tuh_hid_mount_cb(2, 2, NULL, 0);
    assert(auth_device == 0 && brake_device == 2 && brake_instance == 1);
    tuh_hid_mount_cb(3, 0, NULL, 0);
    assert(auth_device == 3);
    hid_task();
    assert(report.brake == UINT16_MAX);
    accept_receive = false;
    brake_task();
    assert(!brake_receive_pending);
    accept_receive = true;
    brake_task();
    assert(brake_receive_pending && receive_device == 2 && receive_instance == 1);
    unsigned queued = receive_calls;
    brake_task();
    assert(receive_calls == queued);

    uint8_t pedal[28] = { 3 };
    pedal[16] = 0xff; pedal[17] = 0x7f;
    tuh_hid_report_received_cb(2, 2, pedal, sizeof(pedal));
    assert(!external_brake.has_sample);
    tuh_hid_report_received_cb(2, 1, pedal, sizeof(pedal));
    assert(!brake_receive_pending);
    wheel.brake = 255;
    tuh_hid_report_received_cb(1, 0, (const uint8_t*) &wheel, sizeof(wheel));
    hid_task();
    assert(report.brake == 0); // Wheel reports cannot overwrite external brake.
    now = BRAKE_TIMEOUT_MS;
    accept_send = false;
    unsigned sent = send_calls;
    hid_task();
    assert(report.brake == UINT16_MAX && prev_report.brake == 0);
    accept_send = true;
    hid_task();
    assert(send_calls == sent + 2 && prev_report.brake == UINT16_MAX);
    tuh_hid_report_received_cb(2, 1, pedal, sizeof(pedal));
    hid_task();
    assert(report.brake == 0);
    tuh_hid_umount_cb(2, 2); // An unrelated interface must not disconnect the pedal.
    assert(brake_device == 2);
    tuh_hid_umount_cb(2, 1);
    hid_task();
    assert(report.brake == UINT16_MAX && auth_device == 3);
    tuh_hid_mount_cb(2, 1, NULL, 0); // Auth before Arduino also preserves auth.
    assert(auth_device == 3);
    hid_task();
    assert(report.brake == UINT16_MAX);
    tuh_hid_report_received_cb(2, 1, pedal, sizeof(pedal));
    hid_task();
    assert(report.brake == 0);
    tuh_hid_report_received_cb(1, 0, (const uint8_t*) &wheel, 1);
    assert(wheel_brake == 255u << 8);
    puts("Adapter device routing, report composition, receive retry and disconnect tests passed.");
    return 0;
}
