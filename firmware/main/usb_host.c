#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "usb/usb_host.h"
#include "usb/hid_host.h"
#include "usb_host.h"

static const char *TAG = "usb_host";

static uint8_t hid_report[HID_REPORT_SIZE];
static volatile bool report_ready = false;
static portMUX_TYPE report_lock = portMUX_INITIALIZER_UNLOCKED;

static void hid_host_interface_callback(hid_host_device_handle_t hid_device_handle,
                                        const hid_host_interface_event_t event,
                                        void *arg)
{
    switch (event) {
    case HID_HOST_INTERFACE_EVENT_INPUT_REPORT: {
        uint8_t data[HID_REPORT_SIZE];
        size_t data_length = 0;
        if (hid_host_device_get_raw_input_report_data(hid_device_handle, data,
                                                      sizeof(data), &data_length) == ESP_OK) {
            portENTER_CRITICAL(&report_lock);
            memset(hid_report, 0, sizeof(hid_report));
            size_t copy_len = (data_length < sizeof(hid_report)) ? data_length : sizeof(hid_report);
            memcpy(hid_report, data, copy_len);
            report_ready = true;
            portEXIT_CRITICAL(&report_lock);
        } else {
            ESP_LOGW(TAG, "Failed to read raw input report data");
        }
        break;
    }
    case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HID device disconnected");
        hid_host_device_close(hid_device_handle);
        break;
    case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
        ESP_LOGW(TAG, "HID device transfer error");
        break;
    default:
        break;
    }
}

static void hid_host_device_event(hid_host_device_handle_t hid_device_handle,
                                  const hid_host_driver_event_t event,
                                  void *arg)
{
    if (event != HID_HOST_DRIVER_EVENT_CONNECTED) {
        return;
    }

    hid_host_dev_params_t dev_params;
    if (hid_host_device_get_params(hid_device_handle, &dev_params) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get HID device params");
        return;
    }

    const char *proto_name = "unknown";
    if (dev_params.proto == HID_PROTOCOL_KEYBOARD) {
        proto_name = "keyboard";
    } else if (dev_params.proto == HID_PROTOCOL_MOUSE) {
        proto_name = "mouse";
    }

    hid_host_dev_info_t dev_info;
    if (hid_host_get_device_info(hid_device_handle, &dev_info) == ESP_OK) {
        ESP_LOGI(TAG, "HID device connected: vid=0x%04x pid=0x%04x proto=%s",
                 dev_info.VID, dev_info.PID, proto_name);
    } else {
        ESP_LOGI(TAG, "HID device connected: proto=%s", proto_name);
    }

    const hid_host_device_config_t dev_config = {
        .callback = hid_host_interface_callback,
        .callback_arg = NULL,
    };
    if (hid_host_device_open(hid_device_handle, &dev_config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HID device");
        return;
    }

    if (HID_SUBCLASS_BOOT_INTERFACE == dev_params.sub_class) {
        hid_class_request_set_protocol(hid_device_handle, HID_REPORT_PROTOCOL_BOOT);
        if (HID_PROTOCOL_KEYBOARD == dev_params.proto) {
            hid_class_request_set_idle(hid_device_handle, 0, 0);
        }
    }

    if (hid_host_device_start(hid_device_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HID device");
        return;
    }
    ESP_LOGI(TAG, "HID device ready");
}

static void usb_lib_task(void *arg)
{
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };

    if (usb_host_install(&host_config) != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_install failed");
        vTaskDelete(NULL);
        return;
    }

    const hid_host_driver_config_t hid_host_driver_config = {
        .create_background_task = true,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = 0,
        .callback = hid_host_device_event,
        .callback_arg = NULL,
    };
    if (hid_host_install(&hid_host_driver_config) != ESP_OK) {
        ESP_LOGE(TAG, "hid_host_install failed");
        usb_host_uninstall();
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "USB host initialized, waiting for HHKB");

    while (true) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
            break;
        }
    }

    ESP_LOGI(TAG, "USB host shutting down");
    vTaskDelay(10);
    usb_host_uninstall();
    vTaskDelete(NULL);
}

bool usb_host_init(void)
{
    BaseType_t ret = xTaskCreatePinnedToCore(usb_lib_task, "usb_host", 4096,
                                             NULL, 2, NULL, 0);
    return ret == pdTRUE;
}

bool usb_host_hid_report_ready(void)
{
    return report_ready;
}

bool usb_host_read_report(uint8_t *report, size_t len)
{
    if (!report_ready || len < HID_REPORT_SIZE) {
        return false;
    }
    portENTER_CRITICAL(&report_lock);
    memcpy(report, hid_report, HID_REPORT_SIZE);
    report_ready = false;
    portEXIT_CRITICAL(&report_lock);
    return true;
}
