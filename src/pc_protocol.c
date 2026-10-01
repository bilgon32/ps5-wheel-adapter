#include "pc_protocol.h"
#include "pc_mode.h"
#include <string.h>

static uint16_t read16(const uint8_t* p) { return (uint16_t)(p[0] | (uint16_t)p[1] << 8); }
static void write16(uint8_t* p, uint16_t n) { p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8); }
static void write32(uint8_t* p, uint32_t n) { write16(p, (uint16_t)n); write16(p + 2, (uint16_t)(n >> 16)); }

void pc_protocol_init(pc_protocol_t* p) {
    memset(p->written, 0, sizeof(p->written)); memset(p->reply, 0, sizeof(p->reply));
}

void pc_protocol_request(pc_protocol_t* p, const uint8_t* req, size_t length) {
    memset(p->reply, 0, sizeof(p->reply));
    uint8_t* r = p->reply;
    r[0] = 'W'; r[1] = 'A'; r[2] = PC_PROTOCOL_VERSION; r[5] = PC_BAD_REQUEST;
    if (!req || length < 6u || req[0] != 'W' || req[1] != 'A' || req[2] != PC_PROTOCOL_VERSION) return;
    r[3] = req[3]; r[4] = req[4];
    uint8_t n = req[5]; const uint8_t* data = req + 6; uint8_t* out = r + 7;
    if (n > PC_PACKET_SIZE - 6u || (size_t)n + 6 > length || !pc_setup_mode) return;
    r[5] = PC_OK;
    switch (req[3]) {
        case PC_INFO:
            if (n) break;
            write16(out, SETTINGS_VERSION); write16(out + 2, SETTINGS_SIZE);
            write32(out + 4, p->flash_bytes); write32(out + 8, p->devices->generation);
            out[12] = 1; out[13] = 0; /* firmware version 1.0 */
            out[14] = p->ffb->profile; out[15] = p->wheel_status ? p->wheel_status() : (*p->wheel_address ? 2 : 0);
            r[6] = 16; return;
        case PC_READ_SETTINGS: {
            if (n != 3) break;
            uint16_t offset = read16(data); uint8_t count = data[2];
            if (!count || count > PC_PACKET_SIZE - 7u || offset + count > SETTINGS_SIZE) break;
            uint8_t bytes[SETTINGS_SIZE]; settings_encode(p->settings, 0, bytes);
            memcpy(out, bytes + offset, count); r[6] = count; return;
        }
        case PC_WRITE_SETTINGS: {
            if (n < 3) break;
            uint16_t offset = read16(data); unsigned count = n - 2u;
            if (offset + count > SETTINGS_SIZE) break;
            /* Offset zero starts a fresh transaction, preventing stale chunks. */
            if (!offset) memset(p->written, 0, sizeof(p->written));
            memcpy(p->staged + offset, data + 2, count);
            for (unsigned i = offset; i < offset + count; i++) p->written[i / 8] |= (uint8_t)(1u << (i % 8));
            return;
        }
        case PC_APPLY_SETTINGS: {
            if (n != 1 || data[0] > 1) break;
            for (unsigned i = 0; i < sizeof(p->written); i++)
                if (p->written[i] != 255) { r[5] = PC_INCOMPLETE; return; }
            adapter_settings_t next;
            if (!settings_decode(&next, NULL, p->staged, SETTINGS_SIZE)) { r[5] = PC_BAD_SETTINGS; return; }
            adapter_settings_t previous = *p->settings; *p->settings = next;
            if (!p->apply(data[0] != 0)) {
                *p->settings = previous; r[5] = PC_SAVE_FAILED; return;
            }
            memset(p->written, 0, sizeof(p->written)); return;
        }
        case PC_DEVICE: {
            if (n != 1 || data[0] >= ADAPTER_DEVICE_MAX) break;
            const adapter_device_t* d = &p->devices->items[data[0]];
            if (!d->connected) { r[5] = PC_NOT_FOUND; return; }
            out[0] = d->address; out[1] = d->instance; write16(out + 2, d->vid); write16(out + 4, d->pid);
            out[6] = d->axis_count; out[7] = d->auth_capable; out[8] = d->descriptor_valid;
            write32(out + 9, d->descriptor_hash); write32(out + 13, d->updated_ms);
            out[17] = d->has_report; r[6] = 18; return;
        }
        case PC_AXIS: {
            if (n != 2 || data[0] >= ADAPTER_DEVICE_MAX) break;
            const adapter_device_t* d = &p->devices->items[data[0]];
            if (!d->connected || data[1] >= d->axis_count) { r[5] = PC_NOT_FOUND; return; }
            const hid_axis_t* a = &d->axes[data[1]];
            write16(out, a->usage_page); write16(out + 2, a->usage); write16(out + 4, a->bit_offset);
            out[6] = a->report_id; out[7] = a->width; out[8] = a->is_signed;
            write32(out + 9, (uint32_t)a->minimum); write32(out + 13, (uint32_t)a->maximum);
            write32(out + 17, (uint32_t)d->values[data[1]]); out[21] = (d->valid_axes & (1u << data[1])) != 0;
            r[6] = 22; return;
        }
        case PC_TELEMETRY:
            if (n) break;
            write16(out, p->input->wheel); write16(out + 2, p->input->throttle);
            write16(out + 4, p->input->brake); write16(out + 6, p->input->clutch);
            out[8] = p->input->gears; out[9] = p->ffb->profile;
            memcpy(out + 10, (uint8_t*)p->input + 4, 3);
            write32(out + 13, p->ffb->received); write32(out + 17, p->ffb->sent);
            write32(out + 21, p->ffb->overflows); write16(out + 25, p->ffb->count);
            out[27] = p->wheel_status ? p->wheel_status() : (*p->wheel_address ? 2 : 0);
            out[28] = p->input->extra_buttons; out[29] = p->input->dpad;
            r[6] = 30; return;
        case PC_REBOOT:
            if (n != 1 || data[0] < PC_REBOOT_SETUP || data[0] > PC_REBOOT_BOOTSEL) break;
            pc_mode_request((pc_reboot_t)data[0]); return;
        case PC_SET_PROFILE:
            if (n != 1 || !ffb_profile_valid(data[0])) break;
            ffb_set_profile(p->ffb, (ffb_profile_t)data[0]); return;
        case PC_FORCE:
            if (n != 1 || (int8_t)data[0] < -15 || (int8_t)data[0] > 15) break;
            if (!p->force((int8_t)data[0])) r[5] = PC_BUSY;
            return;
        default: break;
    }
    r[5] = PC_BAD_REQUEST;
}
