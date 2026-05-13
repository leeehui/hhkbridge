#include "translator.h"
#include <string.h>

bool translate_report(const uint8_t *usb_report, size_t usb_len,
                      uint8_t *ble_report, size_t ble_len)
{
    if (usb_len < USB_HID_REPORT_SIZE || ble_len < BLE_HID_REPORT_SIZE) {
        return false;
    }

    // Both are standard USB HID boot keyboard 8-byte reports.
    // Byte 0: modifier bitmap
    // Byte 1: reserved
    // Bytes 2-7: up to 6 keycodes
    memcpy(ble_report, usb_report, BLE_HID_REPORT_SIZE);
    return true;
}
