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
#include "g27_leds.h"
#include "ffb.h"
#include "profile_store.h"
#include "settings.h"
#include "device_registry.h"
#include "pc_mode.h"
#include "pc_protocol.h"
#include "auth_bridge.h"

adapter_settings_t settings;
device_registry_t devices;
pc_protocol_t pc_protocol;
static bool pc_force_active;
static uint32_t pc_force_until;
static bool adapter_apply_settings(bool save);
static bool adapter_pc_force(int8_t level);
static uint8_t adapter_pc_wheel_status(void);

uint8_t wheel_device = 0;
uint8_t wheel_instance = 0;
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
g27_leds_t g27_leds;

bool auth_led_on;
bool profile_led_active, profile_led_on;
uint8_t profile_led_pulses;
uint32_t profile_led_deadline;

#define AUTH_FFB_YIELD_MAX_MS 25u
uint32_t auth_ffb_yields;

typedef enum {
    WHEEL_ABSENT, WHEEL_PREPARE_NATIVE, WHEEL_REVERT_ON_RESET, WHEEL_SWITCH_NATIVE,
    WHEEL_WAIT_NATIVE, WHEEL_STOP_FORCES, WHEEL_AUTOCENTER_OFF,
    WHEEL_RANGE, WHEEL_LEDS_OFF, WHEEL_READY
} wheel_phase_t;
wheel_phase_t wheel_phase = WHEEL_ABSENT;
uint16_t wheel_pid;
bool wheel_receive_pending, wheel_tx_pending, wheel_tx_ffb, wheel_tx_led;
uint8_t wheel_tx_led_mask;
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
    g27_leds_init(&g27_leds);
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
                g27_leds_show_profile(&g27_leds, (uint8_t) next + 1u, now);
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
    uint32_t now = adapter_millis();
    // On a G27, Select and Start are the second and third red shifter buttons.
    ps_shortcut_update(now);
    // Hold the two outer red shifter buttons to cycle force-feedback profiles.
    profile_shortcut_update(now);
    report.brake = external_brake_value(&external_brake, wheel_brake, now);

    if (pc_setup_mode || !tud_hid_ready()) return;

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
            g27_leds_show_ready(&g27_leds, adapter_millis());
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
        wheel_tx_led = false;
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
    bool brake_fault = external_brake_fault(&external_brake, now);
    g27_leds_set_conditions(&g27_leds, auth_device != 0, brake_fault, now);

    // Status indications own the decorative RPM LEDs while active. Discard
    // game LED updates during the overlay so they cannot erase a fault or
    // profile indication; all force and setup commands retain their order.
    while (g27_leds_override_is_active(&g27_leds, now) &&
           ffb_command_is_rpm_led(ffb_front(&ffb_queue))) {
        ffb_suppress_front_led(&ffb_queue);
    }

    command = ffb_front(&ffb_queue);
    // After every authentication packet, give one queued force command a
    // chance to reach the wheel. Without this turn, the complete signing
    // exchange monopolizes the serialized PIO USB host and the wheel goes numb.
    bool ffb_has_auth_priority = auth_yield_for_ffb && command;
    uint8_t led_mask;
    bool led_due = !ffb_has_auth_priority && g27_leds_command_due(&g27_leds, now, &led_mask);
    static uint8_t led_command[FFB_COMMAND_SIZE] = { 0xf8, 0x12, 0, 0, 0, 0, 0 };
    if (led_due && !wheel_tx_pending && !busy && deadline_reached(now, wheel_tx_retry_at)) {
        led_command[2] = led_mask;
        wheel_tx_ffb = false;
        wheel_tx_led = true;
        wheel_tx_led_mask = led_mask;
        if (tuh_hid_send_report(wheel_device, wheel_instance, 0, led_command, FFB_COMMAND_SIZE)) {
            wheel_tx_pending = true;
            return;
        }
        wheel_tx_led = false;
        wheel_tx_retries++;
        wheel_tx_retry_at = now + 10u;
    }
    if (command && !wheel_tx_pending && !busy && deadline_reached(now, wheel_tx_retry_at)) {
        wheel_tx_ffb = true;
        wheel_tx_led = false;
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
        printf("FFB profile=%s rx=%lu shaped=%lu sent=%lu queued=%u peak=%u retries=%lu overflow=%lu auth_yields=%lu led_suppressed=%lu blocked_modes=%lu\n",
            ffb_profile_name(ffb_queue.profile),
            (unsigned long) ffb_queue.received, (unsigned long) ffb_queue.transformed,
            (unsigned long) ffb_queue.sent,
            ffb_queue.count, ffb_queue.high_water, (unsigned long) wheel_tx_retries,
            (unsigned long) ffb_queue.overflows, (unsigned long) auth_ffb_yields,
            (unsigned long) ffb_queue.suppressed_leds,
            (unsigned long) ffb_queue.blocked_mode_changes);
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
            auth_yield_for_ffb = false;
        } else if (wheel_tx_led) {
            wheel_tx_retries++;
            wheel_tx_retry_at = adapter_millis() + 10u;
            wheel_tx_led = false;
        } else {
            wheel_setup_command_failed(adapter_millis());
        }
        return;
    }
    if (wheel_tx_ffb) {
        ffb_pop(&ffb_queue);
        if (auth_yield_for_ffb) auth_ffb_yields++;
        auth_yield_for_ffb = false;
        return;
    }
    if (wheel_tx_led) {
        wheel_tx_led = false;
        g27_leds_command_sent(&g27_leds, wheel_tx_led_mask, adapter_millis());
        return;
    }
    wheel_mode_failures = 0;
    wheel_tx_retry_at = adapter_millis() + 20u;
    wheel_command_succeeded();
}

uint32_t adapter_auth_millis(void) { return adapter_millis(); }
bool adapter_auth_waiting_for_ffb(void) {
    return wheel_device && wheel_phase == WHEEL_READY && ffb_front(&ffb_queue);
}
bool adapter_wheel_transfer_pending(void) { return wheel_tx_pending; }
void adapter_auth_completed(void) {
    adapter_led_set_auth(true);
    g27_leds_show_ready(&g27_leds, adapter_millis());
}

static bool adapter_apply_settings(bool save) {
    if (save && !settings_save(&settings)) return false;
    brake_device = 0; brake_instance = 0; brake_receive_pending = false;
    external_brake_configure(&external_brake, &settings);
    auth_device = 0; auth_instance = 0; busy = false;
    state = IDLE; nonce_ready = false; signature_ready = false;
    auth_yield_for_ffb = false;
    ffb_clear(&ffb_queue);
    ffb_set_custom_lut(&ffb_queue, settings.custom_lut ? settings.lut : NULL);
    ffb_set_profile(&ffb_queue, (ffb_profile_t)settings.default_profile);
    uint8_t bytes[SETTINGS_SIZE]; settings_encode(&settings, 0, bytes);
    profile_store_init_scoped(&profile_store, settings.default_profile,
        settings_checksum(bytes + 16, SETTINGS_SIZE - 16));
    for (unsigned i = 0; i < ADAPTER_DEVICE_MAX; i++) {
        adapter_device_t* d = &devices.items[i];
        if (!brake_device && device_is_brake(d, &settings)) {
            brake_device = d->address; brake_instance = d->instance;
            external_brake_connect(&external_brake);
            if (d->has_report) external_brake_receive(&external_brake, d->report, d->report_length, d->updated_ms);
        } else if (!auth_device && device_is_auth(d, &settings)) {
            auth_device = d->address; auth_instance = d->instance;
        }
    }
    return true;
}

static bool adapter_pc_force(int8_t level) {
    if (!pc_setup_mode || !wheel_device || wheel_phase != WHEEL_READY || ffb_queue.count > 8) return false;
    uint8_t force[] = {0x11, 0, (uint8_t)((level < 0 ? 127 : 128) + level), 128, 128, 128, 0};
    if (!level) force[0] = 0xf3;
    if (!ffb_enqueue_report(&ffb_queue, 5, force, sizeof(force))) return false;
    /* A bench pulse stays below 25%, even with an aggressive imported LUT. */
    unsigned tail = (ffb_queue.head + ffb_queue.count - 1u) % FFB_QUEUE_CAPACITY;
    uint8_t* shaped = &ffb_queue.commands[tail][2];
    if (*shaped > 159) *shaped = 159;
    if (*shaped < 96) *shaped = 96;
    pc_force_active = level != 0;
    pc_force_until = adapter_millis() + 300u;
    return true;
}

static uint8_t adapter_pc_wheel_status(void) {
    if (!wheel_device) return 0;
    if (wheel_phase != WHEEL_READY) return 1;
    return wheel_pid == G27_PID ? 2 : 3;
}

int main() {
    board_init();
    bool configured = settings_load(&settings);
    pc_mode_init(configured);
    report_init();
    external_brake_configure(&external_brake, &settings);
    ffb_set_custom_lut(&ffb_queue, settings.custom_lut ? settings.lut : NULL);
    uint8_t settings_bytes[SETTINGS_SIZE]; settings_encode(&settings, 0, settings_bytes);
    ffb_set_profile(&ffb_queue, (ffb_profile_t) profile_store_init_scoped(&profile_store,
        settings.default_profile, settings_checksum(settings_bytes + 16, SETTINGS_SIZE - 16)));
    pc_protocol.settings = &settings; pc_protocol.devices = &devices;
    pc_protocol.input = &report; pc_protocol.ffb = &ffb_queue;
    pc_protocol.wheel_address = &wheel_device; pc_protocol.apply = adapter_apply_settings;
    pc_protocol.force = adapter_pc_force;
    pc_protocol.wheel_status = adapter_pc_wheel_status;
#ifndef _MSC_VER
    pc_protocol.flash_bytes = PICO_FLASH_SIZE_BYTES;
#else
    pc_protocol.flash_bytes = 2097152u;
#endif
    pc_protocol_init(&pc_protocol);
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
        if (pc_setup_mode) {
            for (unsigned i = 0; i < ADAPTER_DEVICE_MAX; i++) {
                adapter_device_t* d = &devices.items[i];
                if (d->connected && d->address != wheel_device &&
                    !(d->address == brake_device && d->instance == brake_instance) && !d->receive_pending)
                    d->receive_pending = tuh_hid_receive_report(d->address, d->instance);
            }
            if (pc_force_active && (int32_t)(adapter_millis() - pc_force_until) >= 0)
                adapter_pc_force(0);
        }
        profile_store_task(&profile_store, adapter_millis());
        adapter_led_task(adapter_millis());
        pc_mode_task();
    }

    return 0;
}

uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen) {
    (void) itf;
    (void) report_type;
    if (!buffer) return 0;
    if (pc_setup_mode) {
        if (report_id != 1 || report_type != HID_REPORT_TYPE_FEATURE) return 0;
        uint16_t length = reqlen < PC_PACKET_SIZE ? reqlen : PC_PACKET_SIZE;
        memcpy(buffer, pc_protocol.reply, length); return length;
    }
    switch (report_id) {
        case 0x03: {
            uint16_t length = reqlen < sizeof(output_0x03) ? reqlen : sizeof(output_0x03);
            memcpy(buffer, output_0x03, length);
            adapter_led_set_auth(false);
            return length;
        }
        default: return auth_bridge_get_report(report_id, buffer, reqlen);
    }
    return reqlen;
}

void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize) {
    (void) itf;
    (void) report_type;
    if (pc_setup_mode) {
        if (report_id == 1 && report_type == HID_REPORT_TYPE_FEATURE)
            pc_protocol_request(&pc_protocol, buffer, bufsize);
        return;
    }
    static const uint8_t setup_request[7] = {'W', 'A', 'C', 'F', 'G', 1, 0};
    if (report_id == 0xf3 && report_type == HID_REPORT_TYPE_FEATURE && bufsize >= sizeof(setup_request) &&
        buffer && !memcmp(buffer, setup_request, sizeof(setup_request))) {
        pc_mode_request(PC_REBOOT_SETUP); return;
    }
    if (report_id == 0xF0) {  // SET_AUTH_PAYLOAD
        auth_bridge_set_report(buffer, bufsize);
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
    adapter_device_t* device = devices_mount(&devices, dev_addr, instance, vid, pid, desc_report, desc_len);

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
        wheel_tx_led = false;
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
    } else if (device_is_brake(device, &settings)) {
        // Reserve every HID interface of this device so it can never replace auth.
        // The measured Micro has one HID interface (USB interface 2).
        if (!brake_device) {
            brake_device = dev_addr;
            brake_instance = instance;
            brake_receive_pending = false;
            external_brake_connect(&external_brake);
            printf("External Arduino brake connected\n");
        }
    } else if (!auth_device && device_is_auth(device, &settings)) {
        auth_device = dev_addr; auth_instance = instance;
    }
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    devices_unmount(&devices, dev_addr, instance);
    printf("tuh_hid_umount_cb\n");
    if ((dev_addr == wheel_device) && (instance == wheel_instance)) {
        wheel_device = 0;
        wheel_instance = 0;
        wheel_phase = WHEEL_ABSENT;
        wheel_receive_pending = false;
        wheel_tx_pending = false;
        wheel_tx_ffb = false;
        wheel_tx_led = false;
        auth_yield_for_ffb = false;
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
        bool transaction_pending = state != IDLE;
        auth_device = 0;
        auth_instance = 0;
        busy = false;
        auth_yield_for_ffb = false;
        nonce_part = 0;
        signature_part = 0;
        signature_ready = false;
        state = transaction_pending && nonce_ready ? SENDING_RESET : IDLE;
        printf("Authentication controller disconnected\n");
    }
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report_, uint16_t len) {
    devices_report(&devices, dev_addr, instance, report_, len, adapter_millis());
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
