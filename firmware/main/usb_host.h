#pragma once

#include <stdint.h>
#include <stdbool.h>

#define HID_REPORT_SIZE 8

bool usb_host_init(void);
bool usb_host_hid_report_ready(void);
bool usb_host_read_report(uint8_t *report, size_t len);
