#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/board_api.h"
#include "tusb.h"
#ifndef _MSC_VER
#include "pio_usb.h"
#endif

#include "pico/stdio.h"
#ifndef _MSC_VER
#include "pico/time.h"
#endif

#include "reports.h"
#include "brake.h"
#include "g27.h"
#include "ffb.h"
#include "profile_store.h"

uint8_t nonce_id;
uint8_t nonce[280];
uint8_t nonce_part = 0;
uint8_t signature[1064];
uint8_t signature_part = 0;
uint8_t signature_ready = 0;
uint8_t nonce_ready = 0;

uint8_t expected_part = 0;

uint8_t wheel_device = 0;
uint8_t wheel_instance = 0;
uint8_t auth_device = 0;
uint8_t auth_instance = 0;
uint8_t brake_device = 0;
uint8_t brake_instance = 0;
bool brake_receive_pending = false;
external_brake_t external_brake = { 0 };
uint16_t wheel_brake = BRAKE_G29_RELEASED;

#define PS_SHORTCUT_CHORD_MS 75u
bool wheel_select_raw, wheel_start_raw;
bool ps_shortcut_latched;
uint8_t ps_shortcut_pending, ps_shortcut_passthrough;
uint32_t ps_shortcut_pending_at;

#define PROFILE_SHORTCUT_CHORD_MS 75u
#define PROFILE_SHORTCUT_HOLD_MS 1000u
bool wheel_l3_raw, wheel_r3_raw;
bool profile_shortcut_holding, profile_shortcut_latched;
uint8_t profile_shortcut_pending, profile_shortcut_passthrough;
uint32_t profile_shortcut_pending_at, profile_shortcut_hold_at;
profile_store_t profile_store;

bool auth_led_on;
bool profile_led_active, profile_led_on;
uint8_t profile_led_pulses;
uint32_t profile_led_deadline;

bool busy = false;

enum {
    IDLE = 0,
    SENDING_RESET = 1,
    SENDING_NONCE = 2,
    WAITING_FOR_SIG = 3,
    RECEIVING_SIG = 4,
};

uint8_t state = IDLE;

typedef enum {
    WHEEL_ABSENT, WHEEL_PREPARE_NATIVE, WHEEL_REVERT_ON_RESET, WHEEL_SWITCH_NATIVE,
    WHEEL_WAIT_NATIVE, WHEEL_STOP_FORCES, WHEEL_AUTOCENTER_OFF,
    WHEEL_RANGE, WHEEL_LEDS_OFF, WHEEL_READY
} wheel_phase_t;
wheel_phase_t wheel_phase = WHEEL_ABSENT;
uint16_t wheel_pid;
bool wheel_receive_pending, wheel_tx_pending, wheel_tx_ffb;
bool expecting_native;
uint8_t wheel_mode_failures;
uint8_t wheel_init_failures;
uint32_t wheel_wait_started, wheel_tx_retries;
uint32_t wheel_tx_retry_at, wheel_receive_retry_at;
uint32_t wheel_last_diagnostic;
ffb_queue_t ffb_queue = { 0 };

static uint32_t adapter_millis(void) {
#ifdef _MSC_VER
    return board_millis();
#else
    return to_ms_since_boot(get_absolute_time());
#endif
}

static void adapter_led_set_auth(bool on) {
    auth_led_on = on;
    if (!profile_led_active) board_led_write(on);
}

static void adapter_led_show_profile(ffb_profile_t profile, uint32_t now) {
    profile_led_active = true;
    profile_led_on = true;
    profile_led_pulses = (uint8_t) profile + 1u;
    profile_led_deadline = now + 150u;
    board_led_write(true);
}

static void adapter_led_task(uint32_t now) {
    if (!profile_led_active || (int32_t) (now - profile_led_deadline) < 0) return;
    if (profile_led_on) {
        profile_led_on = false;
        profile_led_pulses--;
        board_led_write(false);
        profile_led_deadline = now + 150u;
    } else if (profile_led_pulses) {
        profile_led_on = true;
        board_led_write(true);
        profile_led_deadline = now + 150u;
    } else {
        profile_led_active = false;
        board_led_write(auth_led_on);
    }
}

uint8_t get_buffer[64];
uint8_t set_buffer[64];

g29_report_t report;
g29_report_t prev_report;

// G29
const uint8_t output_0x03[] = {
    0x21, 0x27, 0x03, 0x11, 0x06, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0D, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x0D, 0x84, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

const uint8_t output_0xf3[] = { 0x0, 0x38, 0x38, 0, 0, 0, 0 };

void wheel_controls_reset() {
    memset(&report, 0, sizeof(report));
    report.lx = 0x80;
    report.ly = 0x80;
    report.rx = 0x80;
    report.ry = 0x80;
    report.clutch = 0xFFFF;
    report.brake = BRAKE_G29_RELEASED;
    report.throttle = UINT16_MAX;
    report.wheel = 0x8000;
    report.dpad = 8;
    report.pedal_reserved[0] = report.pedal_reserved[1] = 0xff;
    wheel_brake = BRAKE_G29_RELEASED;
    wheel_select_raw = false;
    wheel_start_raw = false;
    wheel_l3_raw = false;
    wheel_r3_raw = false;
    ps_shortcut_latched = false;
    ps_shortcut_pending = 0;
    ps_shortcut_passthrough = 0;
    ps_shortcut_pending_at = 0;
    profile_shortcut_holding = false;
    profile_shortcut_latched = false;
    profile_shortcut_pending = 0;
    profile_shortcut_passthrough = 0;
    profile_shortcut_pending_at = 0;
    profile_shortcut_hold_at = 0;
}

void report_init() {
    wheel_controls_reset();
    memcpy(&prev_report, &report, sizeof(report));
}

static void ps_shortcut_update(uint32_t now) {
    uint8_t raw = (wheel_select_raw ? 1u : 0u) | (wheel_start_raw ? 2u : 0u);
    report.select = false;
    report.start = false;
    report.PS = false;

    // Once the chord is recognized, consume both buttons until both are up.
    // This prevents a slightly later Select release from opening Share after PS.
    if (ps_shortcut_latched) {
        report.PS = raw == 3u;
        if (!raw) ps_shortcut_latched = false;
        return;
    }

    if (raw == 3u) {
        // An individual button already exposed to the console remains a normal
        // Start+Select combination. The PS chord must begin inside the window.
        if (ps_shortcut_passthrough) {
            report.select = true;
            report.start = true;
            return;
        }
        ps_shortcut_latched = true;
        ps_shortcut_pending = 0;
        report.PS = true;
        return;
    }

    if (!raw) {
        ps_shortcut_pending = 0;
        ps_shortcut_passthrough = 0;
        return;
    }

    if (ps_shortcut_passthrough == raw) {
        report.select = (raw & 1u) != 0;
        report.start = (raw & 2u) != 0;
        return;
    }

    // Delay a lone button briefly so a near-simultaneous second press can turn
    // it into the PS chord without ever exposing Select or Start to the PS5.
    if (ps_shortcut_pending != raw) {
        ps_shortcut_pending = raw;
        ps_shortcut_pending_at = now;
        ps_shortcut_passthrough = 0;
        return;
    }
    if ((uint32_t) (now - ps_shortcut_pending_at) >= PS_SHORTCUT_CHORD_MS) {
        ps_shortcut_passthrough = raw;
        ps_shortcut_pending = 0;
        report.select = (raw & 1u) != 0;
        report.start = (raw & 2u) != 0;
    }
}

static void profile_shortcut_update(uint32_t now) {
    uint8_t raw = (wheel_l3_raw ? 1u : 0u) | (wheel_r3_raw ? 2u : 0u);
    report.L3 = false;
    report.R3 = false;

    if (wheel_pid != G27_PID) {
        profile_shortcut_holding = false;
        profile_shortcut_latched = false;
        profile_shortcut_pending = 0;
        profile_shortcut_passthrough = 0;
        report.L3 = wheel_l3_raw;
        report.R3 = wheel_r3_raw;
        return;
    }

    if (profile_shortcut_latched) {
        if (!raw) profile_shortcut_latched = false;
        return;
    }

    if (profile_shortcut_holding) {
        if (raw == 3u) {
            if ((uint32_t) (now - profile_shortcut_hold_at) >= PROFILE_SHORTCUT_HOLD_MS) {
                ffb_profile_t next = ffb_next_profile(ffb_queue.profile);
                ffb_set_profile(&ffb_queue, next);
                profile_store_schedule(&profile_store, (uint8_t) next, now);
                adapter_led_show_profile(next, now);
                printf("FFB profile: %s\n", ffb_profile_name(next));
                profile_shortcut_holding = false;
                profile_shortcut_latched = true;
            }
            return;
        }
        profile_shortcut_holding = false;
        profile_shortcut_pending = 0;
        profile_shortcut_passthrough = 0;
    }

    if (raw == 3u) {
        // If a lone button was already exposed, retain the ordinary L3+R3
        // combination. A profile change begins only inside the chord window.
        if (profile_shortcut_passthrough) {
            report.L3 = true;
            report.R3 = true;
            return;
        }
        profile_shortcut_holding = true;
        profile_shortcut_hold_at = now;
        profile_shortcut_pending = 0;
        return;
    }

    if (!raw) {
        profile_shortcut_pending = 0;
        profile_shortcut_passthrough = 0;
        return;
    }

    if (profile_shortcut_passthrough == raw) {
        report.L3 = (raw & 1u) != 0;
        report.R3 = (raw & 2u) != 0;
        return;
    }

    if (profile_shortcut_pending != raw) {
        profile_shortcut_pending = raw;
        profile_shortcut_pending_at = now;
        profile_shortcut_passthrough = 0;
        return;
    }
    if ((uint32_t) (now - profile_shortcut_pending_at) >= PROFILE_SHORTCUT_CHORD_MS) {
        profile_shortcut_passthrough = raw;
        profile_shortcut_pending = 0;
        report.L3 = (raw & 1u) != 0;
        report.R3 = (raw & 2u) != 0;
    }
}

void hid_task() {
    if (!tud_hid_ready()) {
        return;
    }

    uint32_t now = adapter_millis();
    // On a G27, Select and Start are the second and third red shifter buttons.
    ps_shortcut_update(now);
    // Hold the two outer red shifter buttons to cycle force-feedback profiles.
    profile_shortcut_update(now);
    report.brake = external_brake_value(&external_brake, wheel_brake, now);

    if (memcmp(&prev_report, &report, sizeof(report))) {
        if (tud_hid_report(1, &report, sizeof(report))) {
            memcpy(&prev_report, &report, sizeof(report));
        }
    }

}

void brake_task() {
    if (brake_device && !brake_receive_pending) {
        // Retry on subsequent iterations if the host could not queue a receive.
        brake_receive_pending = tuh_hid_receive_report(brake_device, brake_instance);
    }
}

static bool deadline_reached(uint32_t now, uint32_t deadline) {
    return (int32_t) (now - deadline) >= 0;
}

static void wheel_command_succeeded() {
    wheel_init_failures = 0;
    switch (wheel_phase) {
        case WHEEL_PREPARE_NATIVE: wheel_phase = WHEEL_REVERT_ON_RESET; break;
        case WHEEL_REVERT_ON_RESET: wheel_phase = WHEEL_SWITCH_NATIVE; break;
        case WHEEL_SWITCH_NATIVE: wheel_phase = WHEEL_WAIT_NATIVE; break;
        case WHEEL_STOP_FORCES: wheel_phase = WHEEL_AUTOCENTER_OFF; break;
        case WHEEL_AUTOCENTER_OFF: wheel_phase = wheel_pid == G27_PID ? WHEEL_RANGE : WHEEL_READY; break;
        case WHEEL_RANGE: wheel_phase = WHEEL_LEDS_OFF; break;
        case WHEEL_LEDS_OFF:
            wheel_phase = WHEEL_READY;
            printf("G27 native inputs and FFB ready\n");
            break;
        default: break;
    }
}

static void wheel_setup_command_failed(uint32_t now) {
    wheel_tx_retries++;
    wheel_tx_retry_at = now + 20u;
    if (wheel_phase == WHEEL_PREPARE_NATIVE || wheel_phase == WHEEL_REVERT_ON_RESET ||
        wheel_phase == WHEEL_SWITCH_NATIVE) {
        expecting_native = false;
        if (++wheel_mode_failures >= 3) {
            wheel_mode_failures = 0;
            if (wheel_phase == WHEEL_SWITCH_NATIVE) {
                wheel_phase = WHEEL_READY;
                printf("Native-mode switch failed; retaining compatibility mode\n");
            } else {
                printf("Native-mode preparation command failed; continuing sequence\n");
                wheel_command_succeeded();
            }
        }
        return;
    }
    // Initialization commands improve wheel state but must never prevent input.
    if (++wheel_init_failures >= 3) {
        printf("Wheel initialization command %u failed; continuing\n", (unsigned) wheel_phase);
        wheel_command_succeeded();
    }
}

void wheel_init_task() {
    uint32_t now = adapter_millis();
    if (!wheel_device) {
        if (expecting_native && (uint32_t) (now - wheel_wait_started) >= 5000u) {
            expecting_native = false;
            ffb_clear(&ffb_queue);
            printf("G27 did not re-enumerate; reconnect the wheel\n");
        }
        return;
    }
    if (wheel_phase == WHEEL_WAIT_NATIVE) {
        if ((uint32_t) (now - wheel_wait_started) >= 3000u) {
            expecting_native = false;
            wheel_phase = WHEEL_READY;
            printf("Native-mode switch timed out; retaining compatibility mode\n");
        }
        return;
    }
    static const uint8_t prepare[] = { 0xf5, 0, 0, 0, 0, 0, 0 };
    static const uint8_t revert[] = { 0xf8, 0x0a, 0, 0, 0, 0, 0 };
    static const uint8_t native[] = { 0xf8, 0x09, 0x04, 0x01, 0, 0, 0 };
    static const uint8_t stop[] = { 0xf3, 0, 0, 0, 0, 0, 0 };
    static const uint8_t autocenter[] = { 0xf5, 0, 0, 0, 0, 0, 0 };
    static const uint8_t range[] = { 0xf8, 0x81, 0x84, 0x03, 0, 0, 0 }; // 900 degrees
    static const uint8_t leds[] = { 0xf8, 0x12, 0, 0, 0, 0, 0 };
    const uint8_t* command = NULL;
    switch (wheel_phase) {
        case WHEEL_PREPARE_NATIVE: command = prepare; break;
        case WHEEL_REVERT_ON_RESET: command = revert; break;
        case WHEEL_SWITCH_NATIVE: command = native; break;
        case WHEEL_STOP_FORCES: command = stop; break;
        case WHEEL_AUTOCENTER_OFF: command = autocenter; break;
        case WHEEL_RANGE: command = range; break;
        case WHEEL_LEDS_OFF: command = leds; break;
        default: break;
    }
    if (command) {
        if (wheel_tx_pending) return;
        if (busy || !deadline_reached(now, wheel_tx_retry_at)) return;
        wheel_tx_ffb = false;
        if (tuh_hid_send_report(wheel_device, wheel_instance, 0, command, FFB_COMMAND_SIZE)) {
            wheel_tx_pending = true;
            if (wheel_phase == WHEEL_SWITCH_NATIVE) {
                expecting_native = true;
                wheel_wait_started = now;
            }
        } else {
            wheel_setup_command_failed(now);
        }
        return;
    }

    // Once initialized, keep interrupt IN armed while game FFB uses interrupt
    // OUT. Native reports may complete only when a control changes, so making
    // output wait for input would stall force feedback while driving straight.
    if (!wheel_receive_pending && deadline_reached(now, wheel_receive_retry_at)) {
        wheel_receive_pending = tuh_hid_receive_report(wheel_device, wheel_instance);
        if (!wheel_receive_pending) wheel_receive_retry_at = now + 10u;
    }
    command = ffb_front(&ffb_queue);
    if (command && !wheel_tx_pending && !busy && deadline_reached(now, wheel_tx_retry_at)) {
        wheel_tx_ffb = true;
        if (tuh_hid_send_report(wheel_device, wheel_instance, 0, command, FFB_COMMAND_SIZE)) {
            wheel_tx_pending = true;
            return;
        }
        wheel_tx_ffb = false;
        wheel_tx_retries++;
        wheel_tx_retry_at = now + 10u;
    }
    // UART diagnostics only; no USB identity/interface changes.
    if ((uint32_t) (now - wheel_last_diagnostic) >= 10000u) {
        wheel_last_diagnostic = now;
        printf("FFB profile=%s rx=%lu shaped=%lu sent=%lu queued=%u peak=%u retries=%lu overflow=%lu blocked_modes=%lu\n",
            ffb_profile_name(ffb_queue.profile),
            (unsigned long) ffb_queue.received, (unsigned long) ffb_queue.transformed,
            (unsigned long) ffb_queue.sent,
            ffb_queue.count, ffb_queue.high_water, (unsigned long) wheel_tx_retries,
            (unsigned long) ffb_queue.overflows, (unsigned long) ffb_queue.blocked_mode_changes);
    }
}

void tuh_hid_report_sent_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* data, uint16_t len) {
    (void) data;
    if (dev_addr != wheel_device || instance != wheel_instance || !wheel_tx_pending) return;
    wheel_tx_pending = false;
    if (len != FFB_COMMAND_SIZE) {
        if (wheel_tx_ffb) {
            wheel_tx_retries++;
            wheel_tx_retry_at = adapter_millis() + 10u;
        } else {
            wheel_setup_command_failed(adapter_millis());
        }
        return;
    }
    if (wheel_tx_ffb) {
        ffb_pop(&ffb_queue);
        return;
    }
    wheel_mode_failures = 0;
    wheel_tx_retry_at = adapter_millis() + 20u;
    wheel_command_succeeded();
}

void auth_task() {
    if (!busy && !wheel_tx_pending && auth_device) {
        switch (state) {
            case IDLE:
                break;
            case SENDING_RESET:
                busy = tuh_hid_get_report(auth_device, auth_instance, 0xF3, HID_REPORT_TYPE_FEATURE, get_buffer, 7 + 1);
                break;
            case SENDING_NONCE:
                set_buffer[0] = 0xF0;
                set_buffer[1] = nonce_id;
                set_buffer[2] = nonce_part;
                set_buffer[3] = 0;
                memcpy(set_buffer + 4, nonce + (nonce_part * 56), 56);
                printf(".");
                busy = tuh_hid_set_report(auth_device, auth_instance, 0xF0, HID_REPORT_TYPE_FEATURE, set_buffer, 64);
                if (busy) nonce_part++;
                break;
            case WAITING_FOR_SIG:
                busy = tuh_hid_get_report(auth_device, auth_instance, 0xF2, HID_REPORT_TYPE_FEATURE, get_buffer, 15 + 1);
                break;
            case RECEIVING_SIG:
                busy = tuh_hid_get_report(auth_device, auth_instance, 0xF1, HID_REPORT_TYPE_FEATURE, get_buffer, 63 + 1);
                break;
        }
    }
}

int main() {
    board_init();
    report_init();
    ffb_set_profile(&ffb_queue, (ffb_profile_t) profile_store_init(&profile_store, FFB_PROFILE_MINIMUM_12));
#ifndef _MSC_VER
    // Preserve the two-PIO layout used by the repository's proven 2023 host
    // stack. New Pico-PIO-USB defaults both engines to PIO0, which prevents the
    // existing downstream hub from enumerating on this RP2040 adapter.
    pio_usb_configuration_t pio_config = PIO_USB_DEFAULT_CONFIG;
    pio_config.pio_tx_num = 0;
    pio_config.sm_tx = 0;
    pio_config.pio_rx_num = 1;
    pio_config.sm_rx = 0;
    pio_config.sm_eop = 1;
    tuh_configure(BOARD_TUH_RHPORT, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_config);
#endif
    tusb_init();
    stdio_init_all();
    printf("FFB profile: %s\n", ffb_profile_name(ffb_queue.profile));
    adapter_led_show_profile(ffb_queue.profile, adapter_millis());

    while (1) {
        tuh_task();
        tud_task();
        brake_task();
        hid_task();
        auth_task();
        wheel_init_task();
        profile_store_task(&profile_store, adapter_millis());
        adapter_led_task(adapter_millis());
    }

    return 0;
}

void tuh_hid_get_report_complete_cb(uint8_t dev_addr, uint8_t idx, uint8_t report_id, uint8_t report_type, uint16_t len) {
    if (dev_addr == auth_device) {
        busy = false;
        switch (report_id) {
            case 0xF3:
                printf("Sending nonce to auth controller");
                state = SENDING_NONCE;
                break;
            case 0xF2:
                // printf(".");
                if (get_buffer[2] == 0) {
                    signature_part = 0;
                    state = RECEIVING_SIG;
                    printf("\n");
                    printf("Receiving signature from auth controller");
                }
                break;
            case 0xF1:
                memcpy(signature + (signature_part * 56), get_buffer + 4, 56);
                signature_part++;
                printf(".");
                if (signature_part == 19) {
                    state = IDLE;
                    expected_part = 0;
                    signature_ready = true;
                    signature_part = 0;
                    printf("\n");
                }
                break;
        }
    }
}

void tuh_hid_set_report_complete_cb(uint8_t dev_addr, uint8_t idx, uint8_t report_id, uint8_t report_type, uint16_t len) {
    if ((dev_addr == auth_device) && (report_id == 0xF0)) {
        busy = false;
        if (nonce_part == 5) {
            printf("\n");
            printf("Waiting for auth controller to sign...\n");
            state = WAITING_FOR_SIG;
        }
    }
}

uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen) {
    switch (report_id) {
        case 0x03:
            memcpy(buffer, output_0x03, reqlen);
            adapter_led_set_auth(false);
            return reqlen;
        case 0xF3:
            memcpy(buffer, output_0xf3, reqlen);
            signature_ready = false;
            return reqlen;
        case 0xF1: {  // GET_SIGNATURE_NONCE
            buffer[0] = nonce_id;
            buffer[1] = signature_part;
            buffer[2] = 0;
            if (signature_part == 0) {
                printf("Sending signature to PS5");
            }
            printf(".");
            memcpy(&buffer[3], &signature[signature_part * 56], 56);
            signature_part++;
            if (signature_part == 19) {
                signature_part = 0;
                printf("\n");
                adapter_led_set_auth(true);
            }
            return reqlen;
        }
        case 0xF2: {  // GET_SIGNING_STATE
            printf("PS5 asks if signature ready (%s).\n", signature_ready ? "yes" : "no");
            buffer[0] = nonce_id;
            buffer[1] = signature_ready ? 0 : 16;
            memset(&buffer[2], 0, 9);
            return reqlen;
        }
    }
    return reqlen;
}

void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize) {
    if (report_id == 0xF0) {  // SET_AUTH_PAYLOAD
        uint8_t part = expected_part;
        if (bufsize == 63) {
            nonce_id = buffer[0];
            part = buffer[1];
        }
        if (part == 0) {
            printf("Getting nonce from PS5");
        }
        printf(".");
        if (part > 4) {
            return;
        }
        expected_part = part + 1;
        memcpy(&nonce[part * 56], &buffer[3], 56);
        if (part == 4) {
            nonce_ready = 1;
            printf("\n");
            printf("Sending reset to auth controller...\n");
            state = SENDING_RESET;
            nonce_part = 0;
        }
    } else {
        // The original adapter accepted every non-auth SET_REPORT here. Keep
        // that behavior: the PS5 may label G29 report 5 differently from a
        // Windows interrupt-OUT write even though its payload is wheel output.
        uint32_t overflows = ffb_queue.overflows;
        ffb_enqueue_report(&ffb_queue, report_id, buffer, bufsize);
        if (ffb_queue.overflows != overflows) printf("FFB queue overflow: command dropped\n");
    }
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len) {
    uint16_t vid;
    uint16_t pid;
    tuh_vid_pid_get(dev_addr, &vid, &pid);

    printf("tuh_hid_mount_cb %04x:%04x %d %d\n", vid, pid, dev_addr, instance);

    if ((vid == LOGITECH_VID) && (pid == DRIVING_FORCE_PID || pid == G27_PID)) {
        if (wheel_device) return;
        wheel_device = dev_addr;
        wheel_instance = instance;
        wheel_pid = pid;
        ffb_enable_profiles(&ffb_queue, pid == G27_PID);
        // Logitech mode and setup commands use interrupt OUT. Keep interrupt IN
        // unarmed until setup finishes because this PIO host serializes wheel I/O.
        wheel_receive_pending = false;
        wheel_tx_pending = false;
        wheel_tx_ffb = false;
        wheel_mode_failures = 0;
        wheel_init_failures = 0;
        wheel_tx_retry_at = 0;
        wheel_receive_retry_at = 0;
        expecting_native = false;
        // Logitech hides several wheels behind the c294 compatibility identity.
        // Re-reading the device descriptor disconnects a G27 on the PIO USB
        // host, so attempt the reversible G27 native-mode sequence directly.
        // Unsupported wheels time out and continue in compatibility mode.
        wheel_phase = pid == G27_PID ? WHEEL_STOP_FORCES : WHEEL_PREPARE_NATIVE;
        wheel_controls_reset();
    } else if ((vid == BRAKE_USB_VID) && (pid == BRAKE_USB_PID)) {
        // Reserve every HID interface of this device so it can never replace auth.
        // The measured Micro has one HID interface (USB interface 2).
        if (!brake_device) {
            brake_device = dev_addr;
            brake_instance = instance;
            brake_receive_pending = false;
            external_brake_connect(&external_brake);
            printf("External Arduino brake connected\n");
        }
    } else {  // retain the original auth-controller convention for other devices
        auth_device = dev_addr;
        auth_instance = instance;
    }
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    printf("tuh_hid_umount_cb\n");
    if ((dev_addr == wheel_device) && (instance == wheel_instance)) {
        wheel_device = 0;
        wheel_instance = 0;
        wheel_phase = WHEEL_ABSENT;
        wheel_receive_pending = false;
        wheel_tx_pending = false;
        wheel_tx_ffb = false;
        ffb_enable_profiles(&ffb_queue, false);
        wheel_controls_reset();
        if (!expecting_native) ffb_clear(&ffb_queue);
    }
    if ((dev_addr == brake_device) && (instance == brake_instance)) {
        brake_device = 0;
        brake_instance = 0;
        brake_receive_pending = false;
        external_brake_disconnect(&external_brake);
        printf("External brake disconnected; brake released until reconnect\n");
    }
    if ((dev_addr == auth_device) && (instance == auth_instance)) {
        auth_device = 0;
        auth_instance = 0;
    }
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report_, uint16_t len) {
    if ((dev_addr == brake_device) && (instance == brake_instance)) {
        brake_receive_pending = false;
        external_brake_receive(&external_brake, report_, len, adapter_millis());
        return;
    }
    if ((dev_addr == wheel_device) && (instance == wheel_instance)) {
        wheel_receive_pending = false;
        if (!len) {
            wheel_receive_retry_at = adapter_millis() + 10u;
            return;
        }
        if (wheel_pid == G27_PID) {
            if (g27_decode(report_, len, &report, &wheel_brake)) {
                wheel_select_raw = report.select;
                wheel_start_raw = report.start;
                wheel_l3_raw = report.L3;
                wheel_r3_raw = report.R3;
            }
            return;
        }
        if (len >= sizeof(df_report_t)) {
            const df_report_t* df = (const df_report_t*) report_;
            report.wheel = df->wheel << 6;
            report.throttle = df->throttle << 8;
            wheel_brake = df->brake << 8;
            report.dpad = df->hat;
            report.cross = df->cross;
            report.square = df->square;
            report.circle = df->circle;
            report.triangle = df->triangle;
            report.L2 = df->L2;
            report.L1 = df->L1;
            report.R2 = df->R2;
            report.R1 = df->R1;
            report.select = df->select;
            report.start = df->start;
            wheel_select_raw = report.select;
            wheel_start_raw = report.start;
            report.R3 = df->R3;
            report.L3 = df->L3;
            wheel_l3_raw = report.L3;
            wheel_r3_raw = report.R3;
        }
    }
}
