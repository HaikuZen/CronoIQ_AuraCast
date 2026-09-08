#include "battery.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "battery";

#define BAT_CHANNEL ADC_CHANNEL_3   // GPIO4
#define BAT_DIVIDER 3.0f

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static bool s_cali_ok;

esp_err_t battery_init(void)
{
    if (s_adc) {
        return ESP_OK;
    }
    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = ADC_UNIT_1 };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_cfg, &s_adc), TAG, "adc unit");

    adc_oneshot_chan_cfg_t ch_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc, BAT_CHANNEL, &ch_cfg), TAG, "adc chan");

    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    s_cali_ok = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali) == ESP_OK;
    if (!s_cali_ok) {
        ESP_LOGW(TAG, "no ADC calibration; voltage is approximate");
    }
    return ESP_OK;
}

float battery_voltage(void)
{
    if (!s_adc) {
        return 0.0f;
    }
    int raw = 0;
    if (adc_oneshot_read(s_adc, BAT_CHANNEL, &raw) != ESP_OK) {
        return 0.0f;
    }
    int mv = 0;
    if (s_cali_ok) {
        adc_cali_raw_to_voltage(s_cali, raw, &mv);
    } else {
        mv = raw * 3100 / 4095;  // 12-bit, ~3.1V full-scale at 12dB
    }
    return (mv / 1000.0f) * BAT_DIVIDER;
}

int battery_percent(void)
{
    float v = battery_voltage();
    if (v <= 3.30f) {
        return 0;
    }
    if (v >= 4.15f) {
        return 100;
    }
    return (int)((v - 3.30f) / (4.15f - 3.30f) * 100.0f + 0.5f);
}
