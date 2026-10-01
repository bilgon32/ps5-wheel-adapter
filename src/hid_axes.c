#include "hid_axes.h"
#include <string.h>

typedef struct {
    uint32_t page, size, count;
    int32_t minimum, maximum;
    uint8_t report;
} globals_t;
static uint32_t unsigned_value(const uint8_t* p, unsigned n) {
    uint32_t v = 0;
    for (unsigned i = 0; i < n; i++) v |= (uint32_t)p[i] << (i * 8u);
    return v;
}
static int32_t signed_value(uint32_t v, unsigned n) {
    if (n && n < 4 && (v & (1u << (n * 8u - 1u)))) v |= UINT32_MAX << (n * 8u);
    return (int32_t)((int64_t)v - ((v & 0x80000000u) ? INT64_C(4294967296) : 0));
}

bool hid_axes_parse(const uint8_t* d, size_t length, hid_axis_t axes[HID_AXIS_MAX], uint8_t* count) {
    if (!count || !axes || !d || !length) return false;
    *count = 0;
    hid_axis_t found[HID_AXIS_MAX]; uint8_t used = 0;
    globals_t g = {0}, stack[4]; unsigned depth = 0;
    uint32_t usages[32], usage_min = 0, usage_max = 0;
    unsigned usage_count = 0; bool has_range = false;
    uint16_t offsets[256] = {0};
    for (size_t p = 0; p < length;) {
        uint8_t tag = d[p++];
        if (tag == 0xfe) { /* long item */
            if (p + 2 > length || p + 2u + d[p] > length) return false;
            p += 2u + d[p]; continue;
        }
        unsigned n = tag & 3u; if (n == 3) n = 4;
        if (p + n > length) return false;
        uint32_t v = unsigned_value(d + p, n); p += n;
        uint8_t type = (tag >> 2) & 3u, item = tag >> 4;
        if (type == 1) {
            switch (item) {
                case 0: g.page = v; break;
                case 1: g.minimum = signed_value(v, n); break;
                case 2: g.maximum = g.minimum < 0 ? signed_value(v, n) : (int32_t)v; break;
                case 7: g.size = v; break;
                case 8: if (!v || v > 255) return false; g.report = (uint8_t)v; break;
                case 9: g.count = v; break;
                case 10: if (depth == 4) return false; stack[depth++] = g; break;
                case 11: if (!depth) return false; g = stack[--depth]; break;
                default: break;
            }
        } else if (type == 2) {
            uint32_t usage = n == 4 ? v : (g.page << 16) | v;
            if (item == 0) { if (usage_count >= 32) return false; usages[usage_count++] = usage; }
            else if (item == 1) { usage_min = usage; has_range = true; }
            else if (item == 2) usage_max = usage;
        } else if (type == 0) {
            if (item == 8) { /* Input; feature/output fields do not advance input offsets. */
                if (!g.size || !g.count || g.count > 512 || g.size > 512 ||
                    g.size * g.count + offsets[g.report] > 512u) return false;
                if ((v & 7u) == 2u) { /* data, variable, absolute */
                    for (uint32_t i = 0; i < g.count; i++) {
                        uint32_t usage = i < usage_count ? usages[i] :
                            has_range && usage_min <= usage_max && i <= usage_max - usage_min ? usage_min + i : 0;
                        uint16_t page = (uint16_t)(usage >> 16), id = (uint16_t)usage;
                        bool axis = (page == 1 && id >= 0x30 && id <= 0x38) ||
                            (page == 2 && (id == 0xc4 || id == 0xc5));
                        if (!axis) continue;
                        if (used == HID_AXIS_MAX || g.size > 31 || g.maximum <= g.minimum) return false;
                        int64_t low = g.minimum < 0 ? -(INT64_C(1) << (g.size - 1)) : 0;
                        int64_t high = (INT64_C(1) << (g.size - (g.minimum < 0))) - 1;
                        if (g.minimum < low || g.maximum > high) return false;
                        found[used++] = (hid_axis_t){page, id, (uint16_t)(offsets[g.report] + i * g.size),
                            g.report, (uint8_t)g.size, g.minimum < 0, g.minimum, g.maximum};
                    }
                }
                offsets[g.report] += (uint16_t)(g.size * g.count);
            }
            usage_count = 0; has_range = false; usage_min = usage_max = 0;
        }
    }
    if (depth) return false;
    memcpy(axes, found, used * sizeof(found[0])); *count = used;
    return true;
}

bool hid_axis_read(const hid_axis_t* a, const uint8_t* data, size_t length, int32_t* value) {
    if (!a || !data || !value || !a->width || a->width > 31) return false;
    if (a->report_id) { if (!length || data[0] != a->report_id) return false; data++; length--; }
    if (a->bit_offset + a->width > length * 8u) return false;
    uint32_t v = 0;
    for (unsigned i = 0; i < a->width; i++) {
        unsigned bit = a->bit_offset + i;
        v |= (uint32_t)((data[bit / 8u] >> (bit % 8u)) & 1u) << i;
    }
    int64_t result = v;
    if (a->is_signed && (v & (1u << (a->width - 1u)))) result -= INT64_C(1) << a->width;
    *value = (int32_t)result; return true;
}

bool hid_auth_descriptor(const uint8_t* d, size_t length) {
    if (!d) return false;
    uint8_t report = 0, mask = 0; uint32_t size = 0, count = 0;
    for (size_t p = 0; p < length;) {
        uint8_t tag = d[p++];
        if (tag == 0xfe) { if (p + 2 > length || p + 2u + d[p] > length) return false; p += 2u + d[p]; continue; }
        unsigned n = tag & 3u; if (n == 3) n = 4;
        if (p + n > length) return false;
        uint32_t v = unsigned_value(d + p, n); p += n;
        if (tag == 0x85) report = (uint8_t)v;
        else if (tag == 0x75) size = v;
        else if (tag == 0x95) count = v;
        else if ((tag & 0xfcu) == 0xb0 && report >= 0xf0 && report <= 0xf3) {
            uint32_t required = report <= 0xf1 ? 63u : report == 0xf2 ? 15u : 7u;
            if (size == 8 && count >= required && count <= 64) mask |= (uint8_t)(1u << (report - 0xf0));
        }
    }
    return mask == 15;
}

uint16_t hid_axis_pedal(int32_t raw, int32_t released, int32_t pressed) {
    int64_t position = (int64_t)raw - released, span = (int64_t)pressed - released;
    if (!span) return UINT16_MAX;
    if (span < 0) { span = -span; position = -position; }
    if (position < 0) position = 0;
    if (position > span) position = span;
    return (uint16_t)(UINT16_MAX - (position * UINT16_MAX + span / 2) / span);
}
