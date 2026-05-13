#include "usb_host.h"
#include "esp_log.h"
#include "tinyusb.h"
#include "class/hid/hid_host.h"
#include <string.h>

static const char *TAG = "usb_host";

static uint8_t hid_report[HID_REPORT_SIZE];
static volatile bool report_ready = false;
static portMUX_TYPE report_lock = portMUX_INITIALIZER_UNLOCKED;

static void hid_report_callback(hidh_report_info_t *info, uint8_t *data, int len)
{
    portENTER_CRITICAL(&report_lock);
    if (len >= HID_REPORT_SIZE) {
        memcpy(hid_report, data, HID_REPORT_SIZE);
    } else {
        memset(hid_report, 0, HID_REPORT_SIZE);
        memcpy(hid_report, data, len);
        ESP_LOGW(TAG, "Short HID report: %d bytes (expected %d)", len, HID_REPORT_SIZE);
    }
    report_ready = true;
    portEXIT_CRITICAL(&report_lock);

    if (!tuh_hid_receive_report(info->dev_addr, info->instance)) {
        ESP_LOGW(TAG, "Failed to re-queue HID report receive");
    }
}

static void device_mount_callback(tuh_hid_mount_info_t *info)
{
    ESP_LOGI(TAG, "HID device mounted: vid=0x%04x pid=0x%04x",
             info->vendor_id, info->product_id);
    if (!tuh_hid_receive_report(info->dev_addr, info->instance)) {
        ESP_LOGW(TAG, "Failed to queue first HID report receive");
    }
}

static void device_umount_callback(tuh_hid_mount_info_t *info)
{
    ESP_LOGI(TAG, "HID device unmounted");
    portENTER_CRITICAL(&report_lock);
    report_ready = false;
    portEXIT_CRITICAL(&report_lock);
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

    portENTER_CRITICAL(&report_lock);
    memcpy(report, hid_report, HID_REPORT_SIZE);
    report_ready = false;
    portEXIT_CRITICAL(&report_lock);
    return true;
}
