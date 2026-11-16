#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define USB_HID_REPORT_SIZE 8
#define BLE_HID_REPORT_SIZE 8

bool translate_report(const uint8_t *usb_report, size_t usb_len,
                      uint8_t *ble_report, size_t ble_len);
