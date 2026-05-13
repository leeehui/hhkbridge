#pragma once

#include <stdint.h>
#include <stdbool.h>

#define BLE_HID_REPORT_SIZE 8

bool ble_hid_init(void);
bool ble_hid_connected(void);
bool ble_hid_send_report(uint8_t *report, size_t len);
