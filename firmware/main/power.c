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
