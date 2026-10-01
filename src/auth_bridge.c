#include "auth_bridge.h"
#include "tusb.h"
#include <stdio.h>
#include <string.h>

uint8_t nonce_id;
uint8_t nonce[280];
uint8_t nonce_part = 0;
uint8_t signature[1064];
uint8_t signature_part = 0;
uint8_t signature_ready = 0;
uint8_t nonce_ready = 0;

uint8_t expected_part = 0;

uint8_t auth_device, auth_instance, get_buffer[64], set_buffer[64];
uint8_t state = IDLE;
bool busy, auth_yield_for_ffb;
uint32_t auth_yield_started_at;
static const uint8_t output_0xf3[] = {0, 0x38, 0x38, 0, 0, 0, 0};

void auth_task() {
    uint32_t now = adapter_auth_millis();
    if (auth_yield_for_ffb) {
        if (!adapter_auth_waiting_for_ffb() ||
            (uint32_t) (now - auth_yield_started_at) >= 25u) {
            auth_yield_for_ffb = false;
        } else {
            return;
        }
    }
    if (!busy && !adapter_wheel_transfer_pending() && auth_device) {
        switch (state) {
            case IDLE:
                break;
            case SENDING_RESET:
                busy = tuh_hid_get_report(auth_device, auth_instance, 0xF3, HID_REPORT_TYPE_FEATURE, get_buffer, 7 + 1);
                break;
            case SENDING_NONCE:
                set_buffer[0] = 0xF0;
                set_buffer[1] = nonce_id;
                set_buffer[2] = nonce_part;
                set_buffer[3] = 0;
                memcpy(set_buffer + 4, nonce + (nonce_part * 56), 56);
                printf(".");
                busy = tuh_hid_set_report(auth_device, auth_instance, 0xF0, HID_REPORT_TYPE_FEATURE, set_buffer, 64);
                break;
            case WAITING_FOR_SIG:
                busy = tuh_hid_get_report(auth_device, auth_instance, 0xF2, HID_REPORT_TYPE_FEATURE, get_buffer, 15 + 1);
                break;
            case RECEIVING_SIG:
                busy = tuh_hid_get_report(auth_device, auth_instance, 0xF1, HID_REPORT_TYPE_FEATURE, get_buffer, 63 + 1);
                break;
        }
    }
}

static void auth_transfer_finished(void) {
    busy = false;
    auth_yield_for_ffb = true;
    auth_yield_started_at = adapter_auth_millis();
}

void tuh_hid_get_report_complete_cb(uint8_t dev_addr, uint8_t idx, uint8_t report_id, uint8_t report_type, uint16_t len) {
    (void) report_type;
    if (dev_addr == auth_device && idx == auth_instance) {
        auth_transfer_finished();
        switch (report_id) {
            case 0xF3:
                if (state != SENDING_RESET || len < 8u) break;
                printf("Sending nonce to auth controller");
                state = SENDING_NONCE;
                break;
            case 0xF2:
                if (state != WAITING_FOR_SIG || len < 16u) break;
                // printf(".");
                if (get_buffer[2] == 0) {
                    signature_part = 0;
                    state = RECEIVING_SIG;
                    printf("\n");
                    printf("Receiving signature from auth controller");
                }
                break;
            case 0xF1:
                if (state != RECEIVING_SIG || len < 64u) break;
                memcpy(signature + (signature_part * 56), get_buffer + 4, 56);
                signature_part++;
                printf(".");
                if (signature_part == 19) {
                    state = IDLE;
                    expected_part = 0;
                    signature_ready = true;
                    signature_part = 0;
                    printf("\n");
                }
                break;
        }
    }
}

void tuh_hid_set_report_complete_cb(uint8_t dev_addr, uint8_t idx, uint8_t report_id, uint8_t report_type, uint16_t len) {
    (void) report_type;
    if (dev_addr == auth_device && idx == auth_instance && report_id == 0xF0) {
        auth_transfer_finished();
        if (state != SENDING_NONCE || len < 64u) return;
        nonce_part++;
        if (nonce_part == 5u) {
            printf("\n");
            printf("Waiting for auth controller to sign...\n");
            state = WAITING_FOR_SIG;
        }
    }
}

uint16_t auth_bridge_get_report(uint8_t report_id, uint8_t* buffer, uint16_t reqlen) {
    switch (report_id) {
        case 0xF3: {
            uint16_t length = reqlen < sizeof(output_0xf3) ? reqlen : sizeof(output_0xf3);
            memcpy(buffer, output_0xf3, length);
            signature_ready = false;
            return length;
        }
        case 0xF1: {  // GET_SIGNATURE_NONCE
            uint16_t length = reqlen < 63u ? reqlen : 63u;
            memset(buffer, 0, length);
            if (length < 59u) return length;
            buffer[0] = nonce_id;
            buffer[1] = signature_part;
            if (signature_part == 0) {
                printf("Sending signature to PS5");
            }
            printf(".");
            memcpy(&buffer[3], &signature[signature_part * 56], 56);
            signature_part++;
            if (signature_part == 19) {
                signature_part = 0;
                printf("\n");
                adapter_auth_completed();
            }
            return length;
        }
        case 0xF2: {  // GET_SIGNING_STATE
            uint16_t length = reqlen < 15u ? reqlen : 15u;
            if (length < 2u) return length;
            printf("PS5 asks if signature ready (%s).\n", signature_ready ? "yes" : "no");
            buffer[0] = nonce_id;
            buffer[1] = signature_ready ? 0 : 16;
            if (length > 2u) memset(&buffer[2], 0, length - 2u);
            return length;
        }
    }
    return 0;
}

void auth_bridge_set_report(const uint8_t* buffer, uint16_t bufsize) {
        if (!buffer || bufsize < 59u) return;
        uint8_t part = expected_part;
        if (bufsize == 63) {
            nonce_id = buffer[0];
            part = buffer[1];
        }
        if (part == 0) {
            nonce_ready = false;
            signature_ready = false;
            printf("Getting nonce from PS5");
        }
        printf(".");
        if (part > 4) {
            return;
        }
        expected_part = part + 1;
        memcpy(&nonce[part * 56], &buffer[3], 56);
        if (part == 4) {
            nonce_ready = 1;
            printf("\n");
            printf("Sending reset to auth controller...\n");
            state = SENDING_RESET;
            nonce_part = 0;
        }
}
