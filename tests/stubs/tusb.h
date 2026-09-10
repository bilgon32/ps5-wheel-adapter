#ifndef TEST_TUSB_H
#define TEST_TUSB_H
#include <stdbool.h>
#include <stdint.h>
typedef enum { HID_REPORT_TYPE_INVALID, HID_REPORT_TYPE_INPUT, HID_REPORT_TYPE_OUTPUT, HID_REPORT_TYPE_FEATURE } hid_report_type_t;
typedef struct { uint8_t prefix[12]; uint16_t bcdDevice; uint8_t suffix[4]; } tusb_desc_device_t;
enum { XFER_RESULT_SUCCESS, XFER_RESULT_FAILED };
typedef struct { uint8_t daddr; uint8_t result; uint32_t actual_len; uintptr_t user_data; } tuh_xfer_t;
typedef void (*tuh_xfer_cb_t)(tuh_xfer_t*);
bool tuh_descriptor_get_device(uint8_t device, void* buffer, uint16_t length, tuh_xfer_cb_t callback, uintptr_t user_data);
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
