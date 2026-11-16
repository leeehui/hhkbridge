#include <string.h>
#include "ble_hid.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_defs.h"
#include "esp_hidd.h"
#include "esp_hidd_gatts.h"

static const char *TAG = "ble_hid";

static esp_hidd_dev_t *hidd_dev = NULL;
static volatile bool connected = false;

// Standard boot-keyboard report map: 8-byte input report
// (1 modifier + 1 reserved + 6 keycodes), no report ID.
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
    0x81, 0x02,        //   Input (Data, Var, Abs) — modifier byte
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x01,        //   Input (Constant) — reserved byte
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x65,        //   Logical Maximum (101)
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0x00,        //   Usage Minimum (0)
    0x29, 0x65,        //   Usage Maximum (101)
    0x81, 0x00,        //   Input (Data, Array) — 6 keycodes
    0xC0               // End Collection
};

static esp_hid_raw_report_map_t report_maps[] = {
    {
        .data = hid_report_map,
        .len = sizeof(hid_report_map),
    },
};

static esp_hid_device_config_t hid_config = {
    .vendor_id = 0x04FE,      // PFU Limited (HHKB)
    .product_id = 0x0038,     // HHKB Professional Classic
    .version = 0x0100,
    .device_name = "HHKB Bridge",
    .manufacturer_name = "hhkbridge",
    .serial_number = "0001",
    .report_maps = report_maps,
    .report_maps_len = 1,
};

static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x30,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static void hidd_event_callback(void *handler_args, esp_event_base_t base,
                                int32_t id, void *event_data)
{
    esp_hidd_event_t event = (esp_hidd_event_t)id;

    if (event == ESP_HIDD_CONNECT_EVENT) {
        connected = true;
        ESP_LOGI(TAG, "BLE HID connected");
    } else if (event == ESP_HIDD_DISCONNECT_EVENT) {
        connected = false;
        ESP_LOGI(TAG, "BLE HID disconnected, restarting advertising");
        esp_ble_gap_start_advertising(&adv_params);
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

    // Register the HID device GATTS event handler.
    ret = esp_ble_gatts_register_callback(esp_hidd_gatts_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GATTS register callback failed: %d", ret);
        return false;
    }

    // Init the BLE HID device.
    ret = esp_hidd_dev_init(&hid_config, ESP_HID_TRANSPORT_BLE,
                            hidd_event_callback, &hidd_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HID device init failed: %d", ret);
        return false;
    }

    // Configure advertising (HID keyboard appearance + HID service UUID).
    const uint8_t hidd_service_uuid128[] = {
        0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x12, 0x18, 0x00, 0x00,
    };
    esp_ble_adv_data_t adv_data = {
        .set_scan_rsp = false,
        .include_name = true,
        .include_txpower = true,
        .appearance = 0x03C1,   // HID keyboard appearance
        .service_uuid_len = sizeof(hidd_service_uuid128),
        .p_service_uuid = (uint8_t *)hidd_service_uuid128,
        .flag = 0x06,
    };

    ESP_ERROR_CHECK(esp_ble_gap_set_device_name("HHKB Bridge"));
    ESP_ERROR_CHECK(esp_ble_gap_config_adv_data(&adv_data));
    ESP_ERROR_CHECK(esp_ble_gap_start_advertising(&adv_params));

    ESP_LOGI(TAG, "BLE HID initialized, advertising");
    return true;
}

bool ble_hid_connected(void)
{
    return connected;
}

bool ble_hid_send_report(uint8_t *report, size_t len)
{
    if (!connected || len != BLE_HID_REPORT_SIZE) {
        return false;
    }

    esp_err_t ret = esp_hidd_dev_input_set(hidd_dev, 0, 0, report, BLE_HID_REPORT_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HID report send failed: %d", ret);
        return false;
    }
    return true;
}
