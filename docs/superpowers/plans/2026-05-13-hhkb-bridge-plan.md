# HHKB Bridge Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a battery-powered ESP32-S3 dongle that bridges a USB-C HHKB Professional Classic to Bluetooth BLE HID.

**Architecture:** ESP32-S3 acts as USB host (TinyUSB) reading HID reports from the HHKB, repacks them, and sends via BLE HID (Bluedroid). Single-threaded main loop. BQ24075 power path + TPS61023 boost + AMS1117 LDO for power.

**Tech Stack:** ESP-IDF v5.x, TinyUSB host stack, Bluedroid BLE, FreeRTOS (internal)

---

## File Structure

```
firmware/
├── CMakeLists.txt              # Top-level ESP-IDF project
├── main/
│   ├── CMakeLists.txt          # Main component build
│   ├── main.c                  # Entry point, main loop, init sequences
│   ├── usb_host.h              # USB host public interface
│   ├── usb_host.c              # TinyUSB init, HHKB enumeration, HID report reading
│   ├── ble_hid.h               # BLE HID public interface
│   ├── ble_hid.c               # Bluedroid init, HID service, advertising, report sending
│   ├── translator.h            # HID report translation public interface
│   ├── translator.c            # HID report repacking
│   ├── power.h                 # Power monitoring public interface
│   ├── power.c                 # ADC battery monitoring, BLE battery service
│   └── sdkconfig.defaults      # ESP-IDF config overrides
hw/
├── hhkb-bridge.sch             # Schematic (Kicad)
├── hhkb-bridge.kicad_pcb       # PCB layout (Kicad)
└── bom.csv                     # Bill of materials
```

---

### Task 1: ESP-IDF Project Scaffold

**Files:**
- Create: `firmware/CMakeLists.txt`
- Create: `firmware/main/CMakeLists.txt`
- Create: `firmware/main/main.c` (stub)
- Create: `firmware/main/sdkconfig.defaults`

- [ ] **Step 1: Create top-level CMakeLists.txt**

Write `firmware/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(hhkb-bridge)
```

- [ ] **Step 2: Create main component CMakeLists.txt**

Write `firmware/main/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS
        "main.c"
        "usb_host.c"
        "ble_hid.c"
        "translator.c"
        "power.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        bt
        driver
        esp_hidd
        esp_adc
)
```

- [ ] **Step 3: Create sdkconfig.defaults**

Write `firmware/main/sdkconfig.defaults`:

```
CONFIG_BT_ENABLED=y
CONFIG_BT_BLUEDROID_ENABLED=y
CONFIG_BT_CLASSIC_ENABLED=n
CONFIG_BT_BLE_ENABLED=y
CONFIG_BT_GATTS_ENABLE=y
CONFIG_BT_HID_DEVICE_ENABLED=y
CONFIG_TINYUSB_ENABLED=y
CONFIG_TINYUSB_HOST_ENABLED=y
CONFIG_TINYUSB_HOST_HID=y
```

- [ ] **Step 4: Create main.c stub**

Write `firmware/main/main.c`:

```c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "HHKB Bridge starting...");
    // Subsystem init will be added in later tasks
}
```

- [ ] **Step 5: Verify project builds**

Run: `idf.py build`
Expected: Successful compilation (no errors, stub app_main links).

- [ ] **Step 6: Commit**

```bash
git add firmware/
git commit -m "feat: add ESP-IDF project scaffold for HHKB bridge"
```

---

### Task 2: USB Host — HHKB Enumeration

**Files:**
- Create: `firmware/main/usb_host.h`
- Create: `firmware/main/usb_host.c`
- Modify: `firmware/main/sdkconfig.defaults`

- [ ] **Step 1: Write usb_host.h interface**

Write `firmware/main/usb_host.h`:

```c
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define HID_REPORT_SIZE 8

bool usb_host_init(void);
bool usb_host_hid_report_ready(void);
bool usb_host_read_report(uint8_t *report, size_t len);
```

- [ ] **Step 2: Write usb_host.c implementation**

Write `firmware/main/usb_host.c`:

```c
#include "usb_host.h"
#include "esp_log.h"
#include "tinyusb.h"
#include "class/hid/hid_host.h"

static const char *TAG = "usb_host";

static uint8_t hid_report[HID_REPORT_SIZE];
static bool report_ready = false;

static void hid_report_callback(hidh_report_info_t *info, uint8_t *data, int len)
{
    if (len >= HID_REPORT_SIZE) {
        memcpy(hid_report, data, HID_REPORT_SIZE);
        report_ready = true;
    }
}

static void device_mount_callback(tuh_hid_mount_info_t *info)
{
    ESP_LOGI(TAG, "HID device mounted: vid=0x%04x pid=0x%04x",
             info->vendor_id, info->product_id);
    tuh_hid_receive_report(info->dev_addr, info->instance);
}

static void device_umount_callback(tuh_hid_mount_info_t *info)
{
    ESP_LOGI(TAG, "HID device unmounted");
    report_ready = false;
}

bool usb_host_init(void)
{
    const tinyusb_config_t tusb_cfg = {
        .external_phy = false,
    };
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    tuh_hid_register_cb(TUH_HID_REPORT_RECEIVED, (void*)hid_report_callback);
    tuh_hid_register_cb(TUH_HID_MOUNT, (void*)device_mount_callback);
    tuh_hid_register_cb(TUH_HID_UMOUNT, (void*)device_umount_callback);

    ESP_LOGI(TAG, "USB host initialized");
    return true;
}

bool usb_host_hid_report_ready(void)
{
    return report_ready;
}

bool usb_host_read_report(uint8_t *report, size_t len)
{
    if (!report_ready) return false;
    if (len < HID_REPORT_SIZE) return false;

    memcpy(report, hid_report, HID_REPORT_SIZE);
    report_ready = false;
    return true;
}
```

- [ ] **Step 3: Verify build**

Run: `idf.py build`
Expected: No errors. Links with TinyUSB and HID host class.

- [ ] **Step 4: Commit**

```bash
git add firmware/main/usb_host.h firmware/main/usb_host.c
git commit -m "feat: add USB host subsystem for HHKB enumeration"
```

---

### Task 3: BLE HID Device

**Files:**
- Create: `firmware/main/ble_hid.h`
- Create: `firmware/main/ble_hid.c`

- [ ] **Step 1: Write ble_hid.h interface**

Write `firmware/main/ble_hid.h`:

```c
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define BLE_HID_REPORT_SIZE 8

bool ble_hid_init(void);
bool ble_hid_connected(void);
bool ble_hid_send_report(uint8_t *report, size_t len);
```

- [ ] **Step 2: Write ble_hid.c — HID service and advertising setup**

Write `firmware/main/ble_hid.c`:

```c
#include "ble_hid.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_defs.h"
#include "esp_hidd.h"

static const char *TAG = "ble_hid";

static esp_hidd_dev_t *hidd_dev = NULL;
static esp_hidd_app_param_t app_param;
static esp_hidd_qos_param_t both_qos;
static bool connected = false;

// HID report map for boot keyboard (8-byte input report)
static const uint8_t hid_report_map[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0xE0,        //   Usage Minimum (224)  — Left Ctrl
    0x29, 0xE7,        //   Usage Maximum (231)  — Right GUI
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data, Variable, Absolute) — Modifier byte
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x01,        //   Input (Constant) — Reserved byte
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0xFF,        //   Logical Maximum (255)
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0x00,        //   Usage Minimum (0)
    0x29, 0xFF,        //   Usage Maximum (255)
    0x81, 0x00,        //   Input (Data, Array) — 6 keycodes
    0xC0               // End Collection
};

static void hidd_event_callback(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    if (event_id == ESP_HIDD_CONNECT_EVENT) {
        connected = true;
        ESP_LOGI(TAG, "BLE HID connected");
    } else if (event_id == ESP_HIDD_DISCONNECT_EVENT) {
        connected = false;
        ESP_LOGI(TAG, "BLE HID disconnected");
    }
}

bool ble_hid_init(void)
{
    esp_err_t ret;

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    app_param.name = "HHKB Bridge";
    app_param.description = "HHKB Bluetooth Bridge";
    app_param.provider = "ESP32";
    app_param.descriptor_list = NULL;
    app_param.descriptor_list_len = 0;

    both_qos.service_type = ESP_HIDD_SERVICE_TYPE_BLE;

    ret = esp_hidd_dev_init(&app_param, &both_qos, &hidd_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HID device init failed: %d", ret);
        return false;
    }

    esp_hidd_dev_set_report_map(hidd_dev, ESP_HID_REPORT_TYPE_INPUT,
                                hid_report_map, sizeof(hid_report_map));

    ret = esp_event_handler_register(ESP_HIDD_EVENTS, ESP_EVENT_ANY_ID,
                                     hidd_event_callback, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Event handler register failed: %d", ret);
        return false;
    }

    ESP_LOGI(TAG, "BLE HID initialized, advertising");
    return true;
}

bool ble_hid_connected(void)
{
    return connected;
}

bool ble_hid_send_report(uint8_t *report, size_t len)
{
    if (!connected || len < BLE_HID_REPORT_SIZE) return false;

    esp_err_t ret = esp_hidd_dev_input_set(hidd_dev, 0, report, BLE_HID_REPORT_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HID report send failed: %d", ret);
        return false;
    }
    return true;
}
```

- [ ] **Step 3: Verify build**

Run: `idf.py build`
Expected: No errors. Links with Bluedroid and HID device.

- [ ] **Step 4: Commit**

```bash
git add firmware/main/ble_hid.h firmware/main/ble_hid.c
git commit -m "feat: add BLE HID device subsystem"
```

---

### Task 4: HID Report Translator

**Files:**
- Create: `firmware/main/translator.h`
- Create: `firmware/main/translator.c`

- [ ] **Step 1: Write translator.h interface**

Write `firmware/main/translator.h`:

```c
#pragma once

#include <stdint.h>
#include <stddef.h>

#define USB_HID_REPORT_SIZE 8
#define BLE_HID_REPORT_SIZE 8

bool translate_report(const uint8_t *usb_report, size_t usb_len,
                      uint8_t *ble_report, size_t ble_len);
```

- [ ] **Step 2: Write translator.c — pass-through repack**

Write `firmware/main/translator.c`:

```c
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
```

- [ ] **Step 3: Verify build**

Run: `idf.py build`
Expected: No errors.

- [ ] **Step 4: Commit**

```bash
git add firmware/main/translator.h firmware/main/translator.c
git commit -m "feat: add HID report translator (pass-through)"
```

---

### Task 5: Power Monitor

**Files:**
- Create: `firmware/main/power.h`
- Create: `firmware/main/power.c`

- [ ] **Step 1: Write power.h interface**

Write `firmware/main/power.h`:

```c
#pragma once

#include <stdint.h>
#include <stdbool.h>

bool power_init(void);
uint8_t power_battery_level(void);
bool power_is_low_battery(void);
```

- [ ] **Step 2: Write power.c — ADC reading with voltage divider**

Write `firmware/main/power.c`:

```c
#include "power.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "driver/gpio.h"

static const char *TAG = "power";

#define BATTERY_ADC_CHAN    ADC_CHANNEL_5   // GPIO5
#define BATTERY_ADC_UNIT    ADC_UNIT_1
#define BATTERY_ADC_ATTEN   ADC_ATTEN_DB_11
#define VOLTAGE_DIVIDER_RATIO 2.0f  // 100k/100k divider halves LiPo voltage
#define BATTERY_FULL_MV     4200
#define BATTERY_EMPTY_MV    3300
#define LOW_BATTERY_MV      3300

static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t cali_handle;

bool power_init(void)
{
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = BATTERY_ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, BATTERY_ADC_CHAN, &chan_cfg));

    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = BATTERY_ADC_UNIT,
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_handle));

    ESP_LOGI(TAG, "Power monitor initialized");
    return true;
}

static uint32_t read_battery_mv(void)
{
    int raw, voltage;
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, BATTERY_ADC_CHAN, &raw));
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, raw, &voltage));
    return (uint32_t)(voltage * VOLTAGE_DIVIDER_RATIO);
}

uint8_t power_battery_level(void)
{
    uint32_t mv = read_battery_mv();
    if (mv >= BATTERY_FULL_MV) return 100;
    if (mv <= BATTERY_EMPTY_MV) return 0;
    return (uint8_t)((mv - BATTERY_EMPTY_MV) * 100 /
                     (BATTERY_FULL_MV - BATTERY_EMPTY_MV));
}

bool power_is_low_battery(void)
{
    return read_battery_mv() < LOW_BATTERY_MV;
}
```

- [ ] **Step 3: Verify build**

Run: `idf.py build`
Expected: No errors.

- [ ] **Step 4: Commit**

```bash
git add firmware/main/power.h firmware/main/power.c
git commit -m "feat: add battery power monitoring via ADC"
```

---

### Task 6: Main Loop — Glue Everything Together

**Files:**
- Modify: `firmware/main/main.c`

- [ ] **Step 1: Rewrite main.c with the main loop**

Write `firmware/main/main.c`:

```c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "usb_host.h"
#include "ble_hid.h"
#include "translator.h"
#include "power.h"

static const char *TAG = "main";

#define BATTERY_CHECK_MS 10000

void app_main(void)
{
    ESP_LOGI(TAG, "HHKB Bridge starting...");

    // Initialize NVS (required by Bluedroid)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // Initialize subsystems
    if (!usb_host_init()) {
        ESP_LOGE(TAG, "USB host init failed");
        return;
    }
    if (!ble_hid_init()) {
        ESP_LOGE(TAG, "BLE HID init failed");
        return;
    }
    if (!power_init()) {
        ESP_LOGE(TAG, "Power monitor init failed");
        return;
    }

    ESP_LOGI(TAG, "All subsystems initialized, entering main loop");

    uint8_t usb_report[HID_REPORT_SIZE];
    uint8_t ble_report[BLE_HID_REPORT_SIZE];
    TickType_t last_battery_check = xTaskGetTickCount();

    while (1) {
        // USB HID → BLE HID
        if (usb_host_hid_report_ready()) {
            if (usb_host_read_report(usb_report, sizeof(usb_report))) {
                if (translate_report(usb_report, sizeof(usb_report),
                                     ble_report, sizeof(ble_report))) {
                    ble_hid_send_report(ble_report, sizeof(ble_report));
                } else {
                    ESP_LOGW(TAG, "Report translation failed");
                }
            }
        }

        // Re-advertise if BLE disconnected
        if (!ble_hid_connected()) {
            // Bluedroid handles re-advertising automatically via HID device
        }

        // Periodic battery check
        TickType_t now = xTaskGetTickCount();
        if ((now - last_battery_check) >= pdMS_TO_TICKS(BATTERY_CHECK_MS)) {
            last_battery_check = now;
            uint8_t level = power_battery_level();
            if (power_is_low_battery()) {
                ESP_LOGW(TAG, "Battery low: %d%%", level);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
```

- [ ] **Step 2: Verify full build**

Run: `idf.py build`
Expected: Full firmware compiles and links all subsystems without errors.

- [ ] **Step 3: Flash to ESP32-S3 and verify serial output**

Run: `idf.py -p /dev/ttyUSB0 flash monitor`
Expected: See init messages for USB host, BLE HID (advertising), and power monitor.

- [ ] **Step 4: Commit**

```bash
git add firmware/main/main.c
git commit -m "feat: wire main loop — USB HID → BLE HID bridge working"
```

---

### Task 7: Integration Test — End-to-End Keystroke Verification

**Files:**
- Create: `firmware/main/test_verify.txt`

- [ ] **Step 1: Flash firmware with HHKB connected and host ready**

Run: `idf.py -p /dev/ttyUSB0 flash monitor`
Expected: Serial output shows "HID device mounted" with VID/PID (HHKB is 0x04FE/0x0038).

- [ ] **Step 2: Verify BLE advertising**

On host: scan for BLE devices.
Expected: "HHKB Bridge" appears in device list.

- [ ] **Step 3: Pair and connect**

On host: pair with "HHKB Bridge".
Expected: Serial shows "BLE HID connected". Host shows keyboard connected.

- [ ] **Step 4: Test single key**

Press 'A' on HHKB. Check on host.
Expected: 'a' appears. No other keys.

- [ ] **Step 5: Test modifiers**

Press Ctrl+C on HHKB.
Expected: Copy action triggers on host.

- [ ] **Step 6: Test rollover (6 keys simultaneously)**

Press A+S+D+F+J+K on HHKB.
Expected: All 6 keys register on host.

- [ ] **Step 7: Test disconnect/reconnect**

Power-cycle host Bluetooth.
Expected: "BLE HID disconnected" in serial. After BT re-enabled, device reconnects.

- [ ] **Step 8: Test USB detach/reattach**

Unplug HHKB from bridge.
Expected: "HID device unmounted" in serial. Plug back in: "HID device mounted".
Keystrokes resume.

- [ ] **Step 9: Verify battery level reporting**

Expected: Battery level logged to serial every 10 seconds. Connect charger to USB-C(F), verify level reflects charging.

- [ ] **Step 10: Write test verification document**

Write `firmware/main/test_verify.txt`:

```
HHKB Bridge Integration Test Results

USB Enumeration:       [ ] PASS — VID/PID 0x04FE/0x0038
BLE Advertising:       [ ] PASS — "HHKB Bridge" visible
BLE Pair/Connect:      [ ] PASS — Serial: "BLE HID connected"
Single Key:            [ ] PASS — 'a' appears
Modifier Keys:         [ ] PASS — Ctrl+C triggers copy
N-Key Rollover:        [ ] PASS — 6 keys register
BLE Reconnect:         [ ] PASS — Auto-reconnect after host BT cycle
USB Detach/Reattach:   [ ] PASS — Enumeration recovers
Battery Reporting:     [ ] PASS — Level logged every 10s
```

- [ ] **Step 11: Commit**

```bash
git add firmware/main/test_verify.txt
git commit -m "test: add integration test checklist for HHKB bridge"
```

---

### Task 8: Hardware — Schematic and BOM

**Files:**
- Create: `hw/hhkb-bridge.sch`
- Create: `hw/bom.csv`

- [ ] **Step 1: Create schematic — Sheet 1 (Power)**

Create the power sheet in `hw/hhkb-bridge.sch` with these net connections:

**USB-C Female (charging port):**
| Pin | Net | Notes |
|---|---|---|
| VBUS | CHG_5V_IN | |
| CC1 | via 5.1kΩ resistor | to GND |
| CC2 | via 5.1kΩ resistor | to GND |
| GND | GND | |
| D+/D- | NC | data not used |

**Battery-to-5V path (battery mode):**
```
LiPo BAT+ → BQ24075 BAT pin
BQ24075 OUT → TPS61023 VIN
TPS61023 VOUT → 5V_BOOST (boosted 5V when on battery)
```

**Direct 5V path (charging mode):**
```
CHG_5V_IN → BQ24075 IN pin
CHG_5V_IN → 5V_DIRECT
```

**Power mux (selects between 5V_DIRECT and 5V_BOOST):**
Two Schottky diodes OR-ing or a TPS2115A power mux:
```
5V_DIRECT ──► diode A ──┐
                         ├──► 5V_RAIL
5V_BOOST  ──► diode B ──┘
```

When CHG_5V_IN is present (~5V), the 5V_DIRECT path dominates and feeds 5V_RAIL directly. When absent, the boost output (~5V) feeds 5V_RAIL.

**5V to 3.3V and HHKB:**
```
5V_RAIL → AMS1117-3.3 VIN → 3.3V → ESP32-S3 VDD
5V_RAIL → USB-C(M) VBUS (to HHKB)
```

**BQ24075 setup:**
| Pin | Connect | Notes |
|---|---|---|
| IN | CHG_5V_IN | USB charger input |
| BAT | LiPo BAT+ | Battery positive |
| OUT | TPS61023 VIN | System output to boost |
| PG | GPIO6 | Charger status to ESP32 |
| EN1, EN2 | GND | 500mA charge current |
| TS | 10kΩ to GND | Thermistor disable |
| ILIM | via resistor to GND | Input current limit |

- [ ] **Step 2: Create schematic — Sheet 2 (ESP32-S3 + USB Host)**

| Net | From | To |
|---|---|---|
| USB_OTG_DN | ESP32 GPIO19 | USB-C(M) D- |
| USB_OTG_DP | ESP32 GPIO20 | USB-C(M) D+ |
| USB_OTG_ID | ESP32 GPIO18 | GND (host mode) |
| 3.3V | AMS1117 VOUT | ESP32 VDD, EN |
| VBUS | 5V_RAIL | USB-C(M) VBUS |
| GND | USB-C(M) GND | GND plane |

**USB-C Male (to HHKB):**
| Pin | Connect | Notes |
|---|---|---|
| VBUS | 5V_RAIL | Power to HHKB |
| D- | GPIO19 | USB data |
| D+ | GPIO20 | USB data |
| CC1/CC2 | NC | Host side, no CC needed |
| GND | GND | |
| SBU1/2 | NC | |

- [ ] **Step 3: Create schematic — Sheet 3 (Peripheral signals)**

| Signal | ESP32 Pin | Peripheral |
|---|---|---|
| BOOST_EN | GPIO4 | TPS61023 EN pin |
| BAT_ADC | GPIO5 | Midpoint of 100k/100k divider from LiPo BAT+ |
| CHG_STAT | GPIO6 | BQ24075 PG (low = power good) |

**Voltage divider for ADC:**
```
LiPo BAT+ → 100kΩ → GPIO5 (ADC) → 100kΩ → GND
```
At 4.2V full charge, GPIO5 sees 2.1V (safe for ESP32-S3 3.3V ADC).

**Boost enable logic:**
```
GPIO4 (BOOST_EN) → TPS61023 EN
```
ESP32 firmware drives GPIO4 high on boot. When CHG_5V_IN is present, 5V_RAIL is powered via the direct path regardless of boost state.

- [ ] **Step 4: Create BOM**

Write `hw/bom.csv`:

```csv
Ref,Part,Value/MPN,Package,Qty,Purpose
U1,ESP32-S3-MINI-1,ESP32-S3-MINI-1-N4,SMD module,1,MCU
U2,BQ24075,BQ24075RGTR,QFN-16,1,Battery charger + power path
U3,TPS61023,TPS61023DRLR,SOT-563,1,Boost converter 3.7V→5V
U4,AMS1117-3.3,AMS1117-3.3,SOT-223,1,3.3V LDO
J1,USB-C male,USB-C plug PCB mount,Through-hole,1,To HHKB (data+power)
J2,USB-C female,USB-C receptacle 6-pin,SMD,1,Charging port
D1,D2,Schottky diode,BAT54C,SOT-23,2,Power mux OR-ing
R1,R2,100kΩ 1%,0603,SMD,2,Battery voltage divider
R3,R4,5.1kΩ,0603,SMD,2,CC pulldown for USB-C(F)
R5,10kΩ,0603,SMD,1,BQ24075 TS thermistor disable
C1,10µF ceramic,0603,SMD,1,TPS61023 input cap
C2,22µF ceramic,0805,SMD,1,TPS61023 output cap
C3,C4,10µF ceramic,0603,SMD,2,AMS1117 in/out caps
C5,1µF ceramic,0402,SMD,1,ESP32-S3 decoupling
L1,2.2µH inductor,NR3015T-2R2M,SMD,1,TPS61023 boost inductor
BAT,LiPo 1S,3.7V nominal,Connector JST-PH,1,Battery
```

- [ ] **Step 5: Commit**

```bash
git add hw/hhkb-bridge.sch hw/bom.csv
git commit -m "feat: add hardware schematic and bill of materials"
```<｜end▁of▁thinking｜>

<｜｜DSML｜｜tool_calls>
<｜｜DSML｜｜invoke name="Read">
<｜｜DSML｜｜parameter name="file_path" string="true">/home/bgb/hhkbridge/docs/superpowers/plans/2026-05-13-hhkb-bridge-plan.md