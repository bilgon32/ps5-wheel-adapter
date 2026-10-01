#ifndef ADAPTER_AUTH_BRIDGE_H
#define ADAPTER_AUTH_BRIDGE_H
#include <stdbool.h>
#include <stdint.h>
enum { IDLE = 0, SENDING_RESET, SENDING_NONCE, WAITING_FOR_SIG, RECEIVING_SIG };
extern uint8_t nonce_id, nonce[280], nonce_part, signature[1064], signature_part;
extern uint8_t signature_ready, nonce_ready, expected_part, state;
extern uint8_t auth_device, auth_instance, get_buffer[64], set_buffer[64];
extern bool busy, auth_yield_for_ffb;
extern uint32_t auth_yield_started_at;
/* Scheduler and user-visible status stay outside the authentication module. */
bool adapter_auth_waiting_for_ffb(void);
bool adapter_wheel_transfer_pending(void);
uint32_t adapter_auth_millis(void);
void adapter_auth_completed(void);
void auth_task(void);
uint16_t auth_bridge_get_report(uint8_t id, uint8_t* buffer, uint16_t length);
void auth_bridge_set_report(const uint8_t* buffer, uint16_t length);
#endif
