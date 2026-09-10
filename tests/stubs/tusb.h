#ifndef TEST_TUSB_H
#define TEST_TUSB_H
#include <stdbool.h>
#include <stdint.h>
typedef enum { HID_REPORT_TYPE_FEATURE = 3 } hid_report_type_t;
bool tud_hid_ready(void);
bool tud_hid_report(uint8_t id, const void* report, uint16_t length);
bool tuh_hid_send_report(uint8_t device, uint8_t instance, uint8_t id, const void* report, uint16_t length);
bool tuh_hid_receive_report(uint8_t device, uint8_t instance);
bool tuh_hid_get_report(uint8_t device, uint8_t instance, uint8_t id, uint8_t type, void* report, uint16_t length);
bool tuh_hid_set_report(uint8_t device, uint8_t instance, uint8_t id, uint8_t type, const void* report, uint16_t length);
void tuh_vid_pid_get(uint8_t device, uint16_t* vid, uint16_t* pid);
void tusb_init(void);
void tuh_task(void);
void tud_task(void);
#endif
