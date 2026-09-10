#ifndef ADAPTER_G27_H
#define ADAPTER_G27_H
#include <stdbool.h>
#include <stddef.h>
#include "reports.h"

#define LOGITECH_VID 0x046d
#define DRIVING_FORCE_PID 0xc294
#define G27_PID 0xc29b

bool g27_revision(uint16_t bcd_device);
bool g27_decode(const uint8_t* data, size_t length, g29_report_t* output, uint16_t* wheel_brake);
#endif
