#include "battery.h"
#include "board.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "battery";

#define SAMPLES         32
#define ABSENT_BELOW_MV 2000

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static adc_channel_t s_channel;
static bool s_ready;

bool battery_is_ready(void)
{
    return s_ready;
}

esp_err_t battery_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }
    adc_unit_t unit;
    ESP_RETURN_ON_ERROR(adc_oneshot_io_to_channel(BOARD_BAT_ADC_GPIO, &unit, &s_channel), TAG, "GPIO%d is not an ADC pin", BOARD_BAT_ADC_GPIO);

    const adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = unit };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_cfg, &s_adc), TAG, "adc unit");

    /* 12 dB attenuation: about 0..3.1 V at the pin, i.e. 0..6.2 V at the battery. */
    const adc_oneshot_chan_cfg_t chan_cfg = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc, s_channel, &chan_cfg), TAG, "adc channel");

    const adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = unit,
        .chan = s_channel,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali) != ESP_OK) {
        ESP_LOGW(TAG, "no ADC calibration data, readings will be approximate");
        s_cali = NULL;
    }
    s_ready = true;
    ESP_LOGI(TAG, "ready on GPIO%d (ADC%d channel %d)", BOARD_BAT_ADC_GPIO, unit + 1, s_channel);
    return ESP_OK;
}

esp_err_t battery_read_mv(int *out_mv)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    int sum = 0;
    for (int i = 0; i < SAMPLES; i++) {
        int raw;
        ESP_RETURN_ON_ERROR(adc_oneshot_read(s_adc, s_channel, &raw), TAG, "adc read");
        sum += raw;
    }
    const int raw = sum / SAMPLES;
    int pin_mv;
    if (s_cali) {
        ESP_RETURN_ON_ERROR(adc_cali_raw_to_voltage(s_cali, raw, &pin_mv), TAG, "calibration");
    } else {
        pin_mv = raw * 3100 / 4095;
    }
    *out_mv = pin_mv * BOARD_BAT_DIVIDER_NUM / BOARD_BAT_DIVIDER_DEN;
    return ESP_OK;
}

bool battery_is_absent(int mv)
{
    return mv < ABSENT_BELOW_MV;
}

int battery_percent_from_mv(int mv)
{
    /* open-circuit voltage of a typical Li-ion cell; under servo load the reading sags */
    static const struct { int mv; int pct; } curve[] = {
        { 4200, 100 }, { 4060, 90 }, { 3980, 80 }, { 3920, 70 }, { 3870, 60 }, { 3820, 50 },
        { 3790, 40 }, { 3770, 30 }, { 3740, 20 }, { 3680, 10 }, { 3450, 5 }, { 3300, 0 },
    };
    const int n = sizeof(curve) / sizeof(curve[0]);
    if (mv >= curve[0].mv) {
        return 100;
    }
    for (int i = 1; i < n; i++) {
        if (mv >= curve[i].mv) {
            const int span_mv = curve[i - 1].mv - curve[i].mv;
            const int span_pct = curve[i - 1].pct - curve[i].pct;
            return curve[i].pct + (mv - curve[i].mv) * span_pct / span_mv;
        }
    }
    return 0;
}
