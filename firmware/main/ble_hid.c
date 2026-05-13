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
