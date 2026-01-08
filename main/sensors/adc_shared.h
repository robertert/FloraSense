#pragma once

#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"

/**
 * @brief Inicjalizuje wspólny moduł ADC1
 * 
 * Ta funkcja może być wywołana wielokrotnie - zwróci ESP_OK jeśli ADC jest już zainicjalizowany.
 * 
 * @return ESP_OK jeśli sukces
 */
esp_err_t adc_shared_init(void);

/**
 * @brief Pobiera wspólny handle ADC1
 * 
 * @return Handle ADC1 lub NULL jeśli nie zainicjalizowany
 */
adc_oneshot_unit_handle_t adc_shared_get_handle(void);

/**
 * @brief Konfiguruje kanał ADC
 * 
 * @param channel Kanał ADC do skonfigurowania
 * @param bitwidth Rozdzielczość bitowa
 * @param atten Attenuacja
 * @return ESP_OK jeśli sukces
 */
esp_err_t adc_shared_config_channel(adc_channel_t channel, adc_bitwidth_t bitwidth, adc_atten_t atten);

/**
 * @brief Tworzy kalibrację ADC dla danego kanału
 * 
 * @param unit_id ID jednostki ADC
 * @param atten Attenuacja
 * @param bitwidth Rozdzielczość bitowa
 * @param cali_handle Wskaźnik na handle kalibracji (wyjściowy)
 * @return ESP_OK jeśli sukces
 */
esp_err_t adc_shared_create_calibration(adc_unit_t unit_id, adc_atten_t atten, adc_bitwidth_t bitwidth, adc_cali_handle_t *cali_handle);

/**
 * @brief Sprawdza czy ADC jest zainicjalizowany
 * 
 * @return true jeśli zainicjalizowany
 */
bool adc_shared_is_initialized(void);

