#include "sensor_soil.h"
#include "adc_shared.h"

#include "config.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_adc/adc_cali.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sensor_soil";

static adc_cali_handle_t cali_handle = NULL;
static bool cali_enabled = false;
static bool sensor_initialized = false;

esp_err_t sensor_soil_init(void)
{
    if (sensor_initialized) {
        return ESP_OK;
    }

    // Użyj wspólnego modułu ADC
    ESP_RETURN_ON_ERROR(adc_shared_init(), TAG, "Nie udało się zainicjalizować wspólnego ADC");

    // Pobierz wspólny handle
    adc_oneshot_unit_handle_t adc_handle = adc_shared_get_handle();
    if (adc_handle == NULL) {
        ESP_LOGE(TAG, "Wspólny handle ADC jest NULL");
        return ESP_ERR_INVALID_STATE;
    }

    // Skonfiguruj kanał czujnika gleby
    ESP_RETURN_ON_ERROR(adc_shared_config_channel(SOIL_SENSOR_ADC_CHANNEL, SOIL_SENSOR_ADC_BITWIDTH, SOIL_SENSOR_ADC_ATTEN),
                        TAG, "Nie udało się skonfigurować kanału ADC");

    // Utwórz kalibrację
    esp_err_t ret = adc_shared_create_calibration(SOIL_SENSOR_ADC_UNIT, SOIL_SENSOR_ADC_ATTEN, SOIL_SENSOR_ADC_BITWIDTH, &cali_handle);
    if (ret == ESP_OK) {
        cali_enabled = true;
        ESP_LOGI(TAG, "Kalibracja ADC włączona");
    } else {
        cali_enabled = false;
        ESP_LOGW(TAG, "Kalibracja ADC niedostępna, używam prostej konwersji");
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
    if (!sensor_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (reading == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Pobierz wspólny handle ADC
    adc_oneshot_unit_handle_t adc_handle = adc_shared_get_handle();
    if (adc_handle == NULL) {
        ESP_LOGE(TAG, "Wspólny handle ADC jest NULL");
        return ESP_ERR_INVALID_STATE;
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