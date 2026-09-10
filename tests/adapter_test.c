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
static bool accept_host=true, console_ready=true;
static uint16_t mock_wheel_pid=DRIVING_FORCE_PID;
static uint8_t last_command[7];
static unsigned host_sends, control_sends;
static tuh_xfer_cb_t descriptor_callback;
static uintptr_t descriptor_generation;
bool tuh_descriptor_get_device(uint8_t d, void* p, uint16_t l, tuh_xfer_cb_t cb, uintptr_t user) {
    assert(d==1 && l==18); ((tusb_desc_device_t*)p)->bcdDevice=0x1238;
    descriptor_callback=cb; descriptor_generation=user; return accept_host;
}
void board_init(void) {}
void stdio_init_all(void) {}
void tusb_init(void) {}
void tuh_task(void) {}
void tud_task(void) {}
uint32_t board_millis(void) { return now; }
void board_led_write(bool on) { (void) on; }
bool tud_hid_ready(void) { return console_ready; }
bool tud_hid_report(uint8_t id, const void* data, uint16_t length) { assert(id == 1); (void) data; (void) length; send_calls++; return accept_send; }
bool tuh_hid_send_report(uint8_t d, uint8_t i, uint8_t id, const void* p, uint16_t l) { assert(d==1 && i==0 && id==0 && l==7); memcpy(last_command,p,7); host_sends++; return accept_host; }
bool tuh_hid_get_report(uint8_t d, uint8_t i, uint8_t id, uint8_t t, void* p, uint16_t l) { (void)d; (void)i; (void)id; (void)t; (void)p; (void)l; return true; }
bool tuh_hid_set_report(uint8_t d, uint8_t i, uint8_t id, uint8_t t, const void* p, uint16_t l) { (void)i; if(d==1) { assert(id==0 && t==HID_REPORT_TYPE_OUTPUT && l==7); memcpy(last_command,p,7); control_sends++; } return accept_host; }
bool tuh_hid_receive_report(uint8_t device, uint8_t instance) {
    receive_calls++; receive_device = device; receive_instance = instance; return accept_receive;
}
void tuh_vid_pid_get(uint8_t device, uint16_t* vid, uint16_t* pid) {
    *vid = device == 1 ? 0x046d : device == 2 ? BRAKE_USB_VID : 0x1532;
    *pid = device == 1 ? mock_wheel_pid : device == 2 ? BRAKE_USB_PID : 1;
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
    // Compatibility-mode Logitech wheels send the proven interrupt-OUT sequence
    // before input polling: autocenter off, revert-on-reset, then native G27.
    assert(wheel_phase==WHEEL_PREPARE_NATIVE && !wheel_receive_pending);
    state=IDLE;
    accept_host=false; wheel_init_task(); assert(!wheel_tx_pending);
    now += 20;
    accept_host=true; wheel_init_task();
    assert(wheel_tx_pending && last_command[0]==0xf5);
    tuh_hid_report_sent_cb(1,0,last_command,0);
    assert(wheel_phase==WHEEL_PREPARE_NATIVE); // Failed transfer retries same command.
    now += 20;
    wheel_init_task(); tuh_hid_report_sent_cb(1,0,last_command,7);
    assert(wheel_phase==WHEEL_REVERT_ON_RESET);
    now += 20;
    wheel_init_task(); assert(last_command[0]==0xf8 && last_command[1]==0x0a);
    tuh_hid_report_sent_cb(1,0,last_command,7);
    now += 20;
    wheel_init_task(); assert(last_command[1]==9 && last_command[2]==4 && expecting_native);
    // Re-enumeration may precede the mode-switch completion callback.
    uint8_t ffb[]={5,0x21,0x0b,1,2,3,4,5};
    // Preserve upstream behavior when a host labels wheel report 5 as a
    // feature SET_REPORT instead of an output transfer.
    tud_hid_set_report_cb(0,0,HID_REPORT_TYPE_FEATURE,ffb,sizeof(ffb));
    tuh_hid_umount_cb(1,0); assert(ffb_queue.count==1 && !wheel_tx_pending);
    mock_wheel_pid=G27_PID; tuh_hid_mount_cb(1,0,NULL,0);
    assert(auth_device==3 && wheel_phase==WHEEL_STOP_FORCES);
    const uint8_t prime_native[11]={8,0,0,0,0x80,255,255,255,128,128,0x9c};
    tuh_hid_report_received_cb(1,0,prime_native,sizeof(prime_native));
    const uint8_t init0[]={0xf3,0xf5,0xf8,0xf8};
    const uint8_t init1[]={0,0,0x81,0x12};
    for(unsigned i=0;i<4;i++) {
        wheel_init_task(); assert(last_command[0]==init0[i] && last_command[1]==init1[i]);
        assert(wheel_tx_pending && !wheel_receive_pending);
        unsigned calls=host_sends; wheel_init_task(); assert(host_sends==calls);
        tuh_hid_report_sent_cb(1,0,last_command,7);
        now += 20;
    }
    assert(wheel_phase==WHEEL_READY);
    console_ready=false; accept_host=false; accept_receive=false; wheel_init_task();
    assert(ffb_queue.count==1 && !wheel_tx_pending && !wheel_receive_pending);
    now += 10;
    accept_host=true; wheel_init_task(); assert(!memcmp(last_command,ffb+1,7));
    tuh_hid_report_sent_cb(1,0,last_command,0); assert(ffb_queue.count==1);
    now += 10;
    // When the retry is due, input and FFB are queued on their separate endpoints.
    accept_receive=true; wheel_init_task();
    assert(wheel_receive_pending && wheel_tx_pending);
    tuh_hid_report_received_cb(1,0,(const uint8_t*) &wheel,sizeof(wheel));
    tuh_hid_report_sent_cb(1,0,last_command,7); assert(!ffb_queue.count);
    wheel_init_task();
    assert(wheel_receive_pending && !wheel_tx_pending);
    // Clutch and gears coexist with the independent external brake.
    const uint8_t native[11]={8,0,4,0,0x80,255,255,0,128,255,0x9c};
    tuh_hid_report_received_cb(1,0,native,11); console_ready=true; hid_task();
    assert(report.clutch==0 && report.gears==4 && report.brake==0);
    uint8_t native_ps[11]={8,0x30,0,0,0x80,255,255,255,128,128,0x9c};
    tuh_hid_report_received_cb(1,0,native_ps,11); hid_task();
    assert(report.select && report.start && report.PS);
    native_ps[1]=0x10; // Only the second red button (Select).
    tuh_hid_report_received_cb(1,0,native_ps,11); hid_task();
    assert(report.select && !report.start && !report.PS);
    native_ps[1]=0x30;
    tuh_hid_report_received_cb(1,0,native_ps,11); hid_task();
    assert(report.select && report.start && report.PS);
    tuh_hid_report_received_cb(1,0,native_ps,0);
    assert(!wheel_receive_pending);
    state=SENDING_NONCE; nonce_part=0;
    accept_host=false; auth_task(); assert(nonce_part==0 && !busy);
    accept_host=true; auth_task(); assert(nonce_part==1 && busy);
    tuh_hid_umount_cb(1,0); assert(report.clutch==65535 && report.gears==0);
    puts("Adapter brake routing, native switch, authentication arbitration and FFB retries passed.");
    return 0;
}
