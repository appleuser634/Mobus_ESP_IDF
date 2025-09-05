// BLE UART-like GATT service (NUS compatible UUIDs)
// Service UUID: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
// RX  (Phone -> ESP32, Write/WriteNR): 6E400002-B5A3-F393-E0A9-E50E24DCCA9E
// TX  (ESP32 -> Phone, Notify)       : 6E400003-B5A3-F393-E0A9-E50E24DCCA9E

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Enable BLE UART service and start advertising. Safe to call multiple times.
void ble_uart_enable(void);

// Stop advertising and disconnect, keep stack initialized for quick resume.
void ble_uart_disable(void);

// Returns 1 if there is an active connection and notifications are enabled.
int ble_uart_is_ready(void);

// Returns 0 on last successful enable, or a negative/ESP_ERR_* code on failure.
int ble_uart_last_err(void);

// Send bytes via TX (Notify). Splits into MTU-sized chunks. Returns 0 on ok.
int ble_uart_send(const uint8_t* data, size_t len);

// Convenience: send a zero-terminated string.
int ble_uart_send_str(const char* s);

#ifdef __cplusplus
}
#endif
