#include "sensor_soil.h"

#include "config.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sensor_soil";

static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_cali_handle_t cali_handle = NULL;
static bool cali_enabled = false;
static bool sensor_initialized = false;

static esp_err_t sensor_soil_init_calibration(void)
{
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = SOIL_SENSOR_ADC_UNIT,
        .atten = SOIL_SENSOR_ADC_ATTEN,
        .bitwidth = SOIL_SENSOR_ADC_BITWIDTH,
    };
    esp_err_t err = adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Używam kalibracji ADC (curve fitting)");
        return ESP_OK;
    } else if (err != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "Nie udało się utworzyć kalibracji curve fitting: %s", esp_err_to_name(err));
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = SOIL_SENSOR_ADC_UNIT,
        .atten = SOIL_SENSOR_ADC_ATTEN,
        .bitwidth = SOIL_SENSOR_ADC_BITWIDTH,
    };
    esp_err_t err = adc_cali_create_scheme_line_fitting(&cali_config, &cali_handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Używam kalibracji ADC (line fitting)");
        return ESP_OK;
    } else if (err != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "Nie udało się utworzyć kalibracji line fitting: %s", esp_err_to_name(err));
    }
#endif

    ESP_LOGW(TAG, "Kalibracja ADC niedostępna, używam prostej konwersji");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t sensor_soil_init(void)
{
    if (sensor_initialized) {
        return ESP_OK;
    }

    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = SOIL_SENSOR_ADC_UNIT,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_cfg, &adc_handle), TAG, "Nie udało się utworzyć jednostki ADC");

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = SOIL_SENSOR_ADC_BITWIDTH,
        .atten = SOIL_SENSOR_ADC_ATTEN,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(adc_handle, SOIL_SENSOR_ADC_CHANNEL, &chan_cfg),
                        TAG, "Nie udało się skonfigurować kanału ADC");

    if (sensor_soil_init_calibration() == ESP_OK) {
        cali_enabled = true;
    } else {
        cali_enabled = false;
    }

    sensor_initialized = true;
    ESP_LOGI(TAG, "Czujnik gleby zainicjalizowany (kanał %d)", SOIL_SENSOR_ADC_CHANNEL);
    return ESP_OK;
}

static float sensor_soil_mv_to_percent(int millivolts)
{
    const int dry_mv = SOIL_SENSOR_MV_AT_DRY;
    const int wet_mv = SOIL_SENSOR_MV_AT_SATURATED;

    if (dry_mv <= wet_mv) {
        return 0.0f;
    }

    if (millivolts >= dry_mv) {
        return 0.0f;
    }
    if (millivolts <= wet_mv) {
        return 100.0f;
    }

    float ratio = (float)(dry_mv - millivolts) / (float)(dry_mv - wet_mv);
    return ratio * 100.0f;
}

esp_err_t sensor_soil_read(sensor_soil_reading_t *reading)
{
    if (!sensor_initialized || adc_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (reading == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int accum = 0;
    int raw_value = 0;
    for (int i = 0; i < SOIL_SENSOR_SAMPLE_COUNT; ++i) {
        ESP_RETURN_ON_ERROR(adc_oneshot_read(adc_handle, SOIL_SENSOR_ADC_CHANNEL, &raw_value),
                            TAG, "Błąd odczytu ADC");
        accum += raw_value;
        vTaskDelay(1);
    }

    int avg_raw = accum / SOIL_SENSOR_SAMPLE_COUNT;
    int millivolts = 0;

    if (cali_enabled) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(adc_cali_raw_to_voltage(cali_handle, avg_raw, &millivolts));
    }
    if (!cali_enabled || millivolts == 0) {
        // fallback 12 bit -> 0-3300mV
        const int max_adc = (1 << SOIL_SENSOR_ADC_BITWIDTH) - 1;
        millivolts = (avg_raw * 3300) / max_adc;
    }

    reading->raw_value = avg_raw;
    reading->millivolts = millivolts;
    reading->moisture_percent = sensor_soil_mv_to_percent(millivolts);

    return ESP_OK;
}