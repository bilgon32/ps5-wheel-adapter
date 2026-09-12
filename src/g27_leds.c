#include "g27_leds.h"

#include <string.h>

#define G27_LED_ALL 0x1fu
#define G27_LED_OUTER 0x11u
#define G27_LED_REFRESH_MS 100u
#define G27_LED_READY_STEP_MS 140u
#define G27_LED_READY_STEPS 6u
#define G27_LED_PROFILE_MS 2400u

static bool desired_mask(g27_leds_t* leds, uint32_t now, uint8_t* mask) {
    if (leds->brake_fault) {
        *mask = ((uint32_t) (now - leds->condition_changed_at) / 150u) & 1u ? 0 : G27_LED_ALL;
        return true;
    }

    uint32_t elapsed = (uint32_t) (now - leds->overlay_started_at);
    if (leds->overlay == G27_LED_OVERLAY_READY) {
        static const uint8_t sweep[G27_LED_READY_STEPS] = { 0x01u, 0x03u, 0x07u, 0x0fu, 0x1fu, 0 };
        uint32_t step = elapsed / G27_LED_READY_STEP_MS;
        if (step < G27_LED_READY_STEPS) {
            *mask = sweep[step];
            return true;
        }
        leds->overlay = G27_LED_OVERLAY_NONE;
    } else if (leds->overlay == G27_LED_OVERLAY_PROFILE) {
        if (elapsed < G27_LED_PROFILE_MS) {
            // Two clear flashes followed by a solid bar. The number of lit
            // segments is the selected profile number.
            if ((elapsed >= 500u && elapsed < 700u) || (elapsed >= 1200u && elapsed < 1400u)) {
                *mask = 0;
            } else {
                *mask = leds->profile_mask;
            }
            return true;
        }
        leds->overlay = G27_LED_OVERLAY_NONE;
    }

    if (!leds->auth_present) {
        *mask = ((uint32_t) (now - leds->condition_changed_at) / 500u) & 1u ? 0 : G27_LED_OUTER;
        return true;
    }
    return false;
}

void g27_leds_init(g27_leds_t* leds) {
    memset(leds, 0, sizeof(*leds));
}

void g27_leds_set_conditions(g27_leds_t* leds, bool auth_present, bool brake_fault, uint32_t now) {
    if (leds->auth_present != auth_present || leds->brake_fault != brake_fault) {
        leds->condition_changed_at = now;
    }
    leds->auth_present = auth_present;
    leds->brake_fault = brake_fault;
}

void g27_leds_show_ready(g27_leds_t* leds, uint32_t now) {
    leds->overlay = G27_LED_OVERLAY_READY;
    leds->overlay_started_at = now;
    leds->last_mask_valid = false;
}

void g27_leds_show_profile(g27_leds_t* leds, uint8_t profile_number, uint32_t now) {
    if (profile_number < 1u) profile_number = 1u;
    if (profile_number > 5u) profile_number = 5u;
    leds->profile_mask = (uint8_t) ((1u << profile_number) - 1u);
    leds->overlay = G27_LED_OVERLAY_PROFILE;
    leds->overlay_started_at = now;
    leds->last_mask_valid = false;
}

bool g27_leds_override_is_active(g27_leds_t* leds, uint32_t now) {
    uint8_t ignored;
    return desired_mask(leds, now, &ignored);
}

bool g27_leds_command_due(g27_leds_t* leds, uint32_t now, uint8_t* mask) {
    if (!leds || !mask) return false;
    uint8_t desired = 0;
    bool active = desired_mask(leds, now, &desired);
    if (active) {
        leds->override_active = true;
        *mask = desired;
        return !leds->last_mask_valid || desired != leds->last_mask ||
            (uint32_t) (now - leds->last_sent_at) >= G27_LED_REFRESH_MS;
    }
    if (leds->override_active) {
        *mask = 0;
        return true;
    }
    return false;
}

void g27_leds_command_sent(g27_leds_t* leds, uint8_t mask, uint32_t now) {
    uint8_t ignored;
    if (!desired_mask(leds, now, &ignored) && mask == 0) {
        leds->override_active = false;
        leds->last_mask_valid = false;
        return;
    }
    leds->last_mask = mask;
    leds->last_mask_valid = true;
    leds->last_sent_at = now;
}
