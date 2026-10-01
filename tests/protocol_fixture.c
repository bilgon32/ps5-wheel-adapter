/* Interactive host of the actual firmware protocol for desktop integration tests. */
#include "pc_protocol.h"
#include "pc_mode.h"
#include <stdio.h>
#include <string.h>
static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
static bool apply(bool save) { (void)save; return true; }
static bool force(int8_t level) { (void)level; return true; }
int main(void) {
    adapter_settings_t settings; settings_defaults(&settings);
    device_registry_t devices = {0}; g29_report_t input = {0}; ffb_queue_t ffb = {0}; uint8_t wheel = 0;
    pc_protocol_t p = {0}; p.settings = &settings; p.devices = &devices; p.input = &input;
    p.ffb = &ffb; p.wheel_address = &wheel; p.apply = apply; p.force = force; p.flash_bytes = 2097152;
    pc_setup_mode = true; pc_protocol_init(&p);
    char line[130];
    while (fgets(line, sizeof(line), stdin)) {
        uint8_t packet[63];
        if (strlen(line) < 126) return 2;
        for (unsigned i = 0; i < 63; i++) {
            int hi = hex_digit(line[2*i]), lo = hex_digit(line[2*i + 1]);
            if (hi < 0 || lo < 0) return 3;
            packet[i] = (uint8_t)((hi << 4) | lo);
        }
        pc_protocol_request(&p, packet, sizeof(packet));
        for (unsigned i = 0; i < 63; i++) printf("%02x", p.reply[i]);
        puts(""); fflush(stdout);
    }
    return 0;
}
