# HHKB Bridge Design Spec

## Overview

A direct-plug dongle that bridges a USB-C HHKB Professional Classic to Bluetooth BLE HID, battery-powered, built on ESP32-S3.

## Hardware Architecture

### MCU: ESP32-S3-MINI-1

Chosen for integrated USB OTG host and Bluetooth 5 BLE radio.

### Power Topology

USB-C female (5V in) powers HHKB directly when charging, and charges the battery. On battery, a boost converter provides 5V to the HHKB. ESP32-S3 always runs from a 3.3V LDO.

```
USB-C(F) 5V ──┬──► Charger ──► LiPo (charge only when plugged in)
               │
               ├──► USB-C(M) VBUS (direct pass-through when charging)
               │
               └──► LDO ──► 3.3V ──► ESP32-S3 (always)

LiPo ──► Boost ──► 5V ──► USB-C(M) VBUS (when on battery)

Power mux selects between charger 5V and boost 5V for VBUS output.
```

VBUS is tied directly to 5V — no switching needed.

### Key Components

| Component | Part (reference) | Purpose |
|---|---|---|
| MCU | ESP32-S3-MINI-1 | USB OTG host + BLE 5 |
| Charger | BQ24075 | LiPo charge + integrated power path mux |
| Boost | TPS61023 | 3.7V → 5V for HHKB VBUS |
| LDO | AMS1117-3.3 | 5V → 3.3V for ESP32-S3 |
| Battery | 1S LiPo | Power source |
| USB-C male | Plug to HHKB | USB 2.0 data + 5V out on VBUS |
| USB-C female | Charging port | 5V in only (CC resistors for 5V negotiation) |

### ESP32-S3 Pin Assignments

| Pin | Signal | Notes |
|---|---|---|
| GPIO19/20 | USB OTG D-/D+ | To HHKB USB-C male data lines |
| GPIO4 | Boost EN | Enable boost when on battery |
| GPIO5 | Battery ADC | Voltage divider for level monitoring |
| GPIO6 | Charger status | BQ24075 PG or STAT pin |

## Software Architecture

### Data Flow

```
HHKB ──USB HID──► TinyUSB Host ──scancode──► Translator ──HID report──► BLE HID ──► Host
```

### Subsystems

**1. USB Host (TinyUSB host stack)**
- Enumerates HHKB as USB HID boot keyboard
- Parses HID report descriptor
- Reads 8-byte HID input reports from interrupt endpoint
- Callback on device attach/detach

**2. Scancode Translator**
- HHKB uses standard USB HID keycodes
- BLE HID uses same format — pass-through by default
- Repack from source report into BLE HID report structure
- Extension point for key remapping (future)

**3. BLE HID Device (Bluedroid + HID Service)**
- Advertises as BLE HID keyboard
- Implements HID over GATT service (HIDS)
- Sends input reports via notifications on report characteristic
- Handles pairing, reconnection, bond storage

**4. Power Monitor**
- Reads battery voltage via ADC divider on a periodic interval
- Reports via BLE battery service characteristic (BAS)
- Low battery threshold: 3.3V

### Main Loop (Single-threaded)

```
init_usb_host()       // TinyUSB init, enumerate HHKB
init_ble_hid()        // Bluedroid init, HID service, start advertising

loop:
    if usb_hid_report_ready():
        report = read_hid_report()
        ble_hid_send(report)

    if ble_disconnected():
        restart_advertising()

    every 10s:
        read_battery_adc()
        update_ble_battery_service()
```

TinyUSB and Bluedroid manage their own internal FreeRTOS tasks. Application logic is synchronous in the main loop.

## HID Report Translation

HHKB Professional Classic sends standard USB HID boot keyboard reports (8 bytes):

| Byte | Field |
|---|---|
| 0 | Modifier bitmap |
| 1 | Reserved |
| 2-7 | Keycodes (up to 6, 0 = empty) |

BLE HID over GATT uses identical report format. Translation is a direct repack — no mapping for the initial version.

## Error Handling

| Scenario | Behavior |
|---|---|
| HHKB disconnected | TinyUSB detach callback → stop BLE reports, periodic re-enumeration |
| BLE disconnected | Bluedroid callback → restart advertising, flush report buffer |
| USB enumeration failure | Retry every 2s, BLE advertising continues |
| Battery low (< 3.3V) | BLE battery service notification |
| Malformed HID report | Drop report, increment error counter |

## Testing Strategy

| Test | Method |
|---|---|
| USB enumeration | Serial console shows device + HID descriptors |
| Single key | Press every key, verify via `evtest` |
| N-key rollover | Hold 6 keys simultaneously, verify report |
| BT reconnect | Power-cycle host BT, verify auto-reconnect |
| Power path | Toggle charger, verify no brownout on scope |
| Battery reading | Compare ADC value to multimeter reading |
