#ifndef ADAPTER_SETTINGS_H
#define ADAPTER_SETTINGS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SETTINGS_SIZE 512u
#define SETTINGS_VERSION 1u
#define SETTINGS_MAGIC 0x31534157u /* WAS1 */
#define SETTINGS_FLASH_RESERVE 12288u /* two settings banks + profile journal */

typedef struct {
    bool external_brake;
    uint16_t brake_vid, brake_pid;
    uint8_t brake_instance, brake_report_id, brake_width;
    bool brake_signed;
    uint16_t brake_bit_offset; /* payload bits, excluding report ID */
    int32_t brake_released, brake_pressed;
    uint16_t brake_timeout_ms; /* zero for devices that report only on changes */
    uint16_t auth_vid, auth_pid;
    uint8_t auth_instance; /* 255: choose matching authentication interface */
    uint8_t default_profile;
    bool custom_lut;
    uint8_t lut[128]; /* replaces measured profile; values 0..127 */
} adapter_settings_t;

uint32_t settings_checksum(const uint8_t* bytes, size_t length);
void settings_defaults(adapter_settings_t* settings);
bool settings_valid(const adapter_settings_t* settings);
void settings_encode(const adapter_settings_t* settings, uint32_t sequence, uint8_t bytes[SETTINGS_SIZE]);
bool settings_decode(adapter_settings_t* settings, uint32_t* sequence, const uint8_t* bytes, size_t length);

bool settings_load(adapter_settings_t* settings);
bool settings_save(const adapter_settings_t* settings);
#ifdef _MSC_VER
void settings_test_reset(void);
void settings_test_interrupt(bool interrupt);
#endif
#endif
