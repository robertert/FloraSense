#include "adc_shared.h"

#include "config.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "adc_shared";

static adc_oneshot_unit_handle_t adc_handle = NULL;
static bool adc_initialized = false;

esp_err_t adc_shared_init(void)
{
    if (adc_initialized && adc_handle != NULL) {
        ESP_LOGD(TAG, "ADC1 już zainicjalizowany");
        return ESP_OK;
    }

    // Używamy ADC_UNIT_1 (ADC1) - wspólny dla wszystkich czujników
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    
    esp_err_t ret = adc_oneshot_new_unit(&unit_cfg, &adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Nie udało się utworzyć jednostki ADC1: %s", esp_err_to_name(ret));
        return ret;
    }

    adc_initialized = true;
    ESP_LOGI(TAG, "Wspólny moduł ADC1 zainicjalizowany");
    return ESP_OK;
}

adc_oneshot_unit_handle_t adc_shared_get_handle(void)
{
    return adc_handle;
}

esp_err_t adc_shared_config_channel(adc_channel_t channel, adc_bitwidth_t bitwidth, adc_atten_t atten)
{
    if (!adc_initialized || adc_handle == NULL) {
        ESP_LOGE(TAG, "ADC nie jest zainicjalizowany");
        return ESP_ERR_INVALID_STATE;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = bitwidth,
        .atten = atten,
    };
    
    esp_err_t ret = adc_oneshot_config_channel(adc_handle, channel, &chan_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Nie udało się skonfigurować kanału ADC %d: %s", channel, esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGD(TAG, "Kanał ADC %d skonfigurowany", channel);
    return ESP_OK;
}

esp_err_t adc_shared_create_calibration(adc_unit_t unit_id, adc_atten_t atten, adc_bitwidth_t bitwidth, adc_cali_handle_t *cali_handle)
{
    if (cali_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = unit_id,
        .atten = atten,
        .bitwidth = bitwidth,
    };
    esp_err_t err = adc_cali_create_scheme_curve_fitting(&cali_config, cali_handle);
    if (err == ESP_OK) {
        ESP_LOGD(TAG, "Utworzono kalibrację ADC (curve fitting)");
        return ESP_OK;
    } else if (err != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "Nie udało się utworzyć kalibracji curve fitting: %s", esp_err_to_name(err));
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = unit_id,
        .atten = atten,
        .bitwidth = bitwidth,
    };
    esp_err_t err = adc_cali_create_scheme_line_fitting(&cali_config, cali_handle);
    if (err == ESP_OK) {
        ESP_LOGD(TAG, "Utworzono kalibrację ADC (line fitting)");
        return ESP_OK;
    } else if (err != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "Nie udało się utworzyć kalibracji line fitting: %s", esp_err_to_name(err));
    }
#endif

    ESP_LOGW(TAG, "Kalibracja ADC niedostępna");
    return ESP_ERR_NOT_SUPPORTED;
}

bool adc_shared_is_initialized(void)
{
    return adc_initialized && adc_handle != NULL;
}

