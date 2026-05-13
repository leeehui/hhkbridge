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
