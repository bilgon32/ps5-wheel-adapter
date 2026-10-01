#include "pc_mode.h"
#include <stdint.h>
bool pc_setup_mode;
#ifndef _MSC_VER
#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/time.h"
#define SETUP_SCRATCH 0x57414346u
static pc_reboot_t pending;
static uint32_t requested_at;
void pc_mode_init(bool configured) {
    pc_setup_mode = !configured || watchdog_hw->scratch[0] == SETUP_SCRATCH;
    watchdog_hw->scratch[0] = 0;
}
void pc_mode_request(pc_reboot_t target) {
    if (target == PC_REBOOT_NONE) return;
    pending = target; requested_at = to_ms_since_boot(get_absolute_time());
}
void pc_mode_task(void) {
    if (!pending || (uint32_t)(to_ms_since_boot(get_absolute_time()) - requested_at) < 100u) return;
    if (pending == PC_REBOOT_BOOTSEL) reset_usb_boot(0, 0);
    watchdog_hw->scratch[0] = pending == PC_REBOOT_SETUP ? SETUP_SCRATCH : 0;
    watchdog_reboot(0, 0, 10);
    pending = PC_REBOOT_NONE;
}
#else
void pc_mode_init(bool configured) { pc_setup_mode = !configured; }
void pc_mode_request(pc_reboot_t target) { (void)target; }
void pc_mode_task(void) {}
#endif
