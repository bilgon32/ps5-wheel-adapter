#include <assert.h>
#include <stdio.h>

#include "g27_leds.h"

static uint8_t due(g27_leds_t* leds, uint32_t now) {
    uint8_t mask = 0xff;
    assert(g27_leds_command_due(leds, now, &mask));
    g27_leds_command_sent(leds, mask, now);
    return mask;
}

int main(void) {
    g27_leds_t leds;
    g27_leds_init(&leds);

    // Missing authentication alternates the two outer LED segments.
    g27_leds_set_conditions(&leds, false, false, 0);
    assert(due(&leds, 0) == 0x11);
    assert(!g27_leds_command_due(&leds, 99, &(uint8_t){0}));
    assert(due(&leds, 100) == 0x11); // Refresh resists game RPM overrides.
    assert(due(&leds, 500) == 0);

    // Ready is quiet after clearing the persistent warning.
    g27_leds_set_conditions(&leds, true, false, 600);
    assert(due(&leds, 600) == 0);
    assert(!g27_leds_command_due(&leds, 601, &(uint8_t){0}));

    // A ready sweep fills all five segments and then clears them.
    g27_leds_show_ready(&leds, 1000);
    assert(due(&leds, 1000) == 0x01);
    assert(due(&leds, 1140) == 0x03);
    assert(due(&leds, 1280) == 0x07);
    assert(due(&leds, 1420) == 0x0f);
    assert(due(&leds, 1560) == 0x1f);
    assert(due(&leds, 1700) == 0);
    assert(due(&leds, 1840) == 0); // Final clear after the overlay expires.
    assert(!g27_leds_command_due(&leds, 1841, &(uint8_t){0}));

    // Profile 3 displays a three-segment bar with two deliberate flashes.
    g27_leds_show_profile(&leds, 3, 2000);
    assert(due(&leds, 2000) == 0x07);
    assert(due(&leds, 2500) == 0);
    assert(due(&leds, 2700) == 0x07);
    assert(due(&leds, 3200) == 0);
    assert(due(&leds, 3400) == 0x07);
    assert(due(&leds, 4400) == 0);

    // A brake fault has priority over temporary profile feedback.
    g27_leds_show_profile(&leds, 5, 5000);
    g27_leds_set_conditions(&leds, true, true, 5050);
    assert(due(&leds, 5050) == 0x1f);
    assert(due(&leds, 5200) == 0);
    g27_leds_set_conditions(&leds, true, false, 5300);
    assert(due(&leds, 5300) == 0x1f); // Profile display resumes.

    puts("G27 LED status priority, timing, refresh and clearing passed.");
    return 0;
}
