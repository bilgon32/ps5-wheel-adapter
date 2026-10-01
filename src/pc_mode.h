#ifndef ADAPTER_PC_MODE_H
#define ADAPTER_PC_MODE_H
#include <stdbool.h>
typedef enum { PC_REBOOT_NONE, PC_REBOOT_SETUP, PC_REBOOT_CONSOLE, PC_REBOOT_BOOTSEL } pc_reboot_t;
extern bool pc_setup_mode;
void pc_mode_init(bool configured);
void pc_mode_request(pc_reboot_t target);
void pc_mode_task(void);
#endif
