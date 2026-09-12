#ifndef ADAPTER_G27_LEDS_H
#define ADAPTER_G27_LEDS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    G27_LED_OVERLAY_NONE,
    G27_LED_OVERLAY_READY,
    G27_LED_OVERLAY_PROFILE,
} g27_led_overlay_t;

typedef struct {
    bool auth_present;
    bool brake_fault;
    bool override_active;
    bool last_mask_valid;
    uint8_t last_mask;
    uint8_t profile_mask;
    uint32_t condition_changed_at;
    uint32_t overlay_started_at;
    uint32_t last_sent_at;
    g27_led_overlay_t overlay;
} g27_leds_t;

void g27_leds_init(g27_leds_t* leds);
void g27_leds_set_conditions(g27_leds_t* leds, bool auth_present, bool brake_fault, uint32_t now);
void g27_leds_show_ready(g27_leds_t* leds, uint32_t now);
void g27_leds_show_profile(g27_leds_t* leds, uint8_t profile_number, uint32_t now);
bool g27_leds_override_is_active(g27_leds_t* leds, uint32_t now);
bool g27_leds_command_due(g27_leds_t* leds, uint32_t now, uint8_t* mask);
void g27_leds_command_sent(g27_leds_t* leds, uint8_t mask, uint32_t now);

#endif
