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
