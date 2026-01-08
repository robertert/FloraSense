#include "sensor_battery.h"
#include "adc_shared.h"

#include "config.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_adc/adc_cali.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>  // dla qsort

static const char *TAG = "sensor_battery";

static adc_cali_handle_t cali_handle = NULL;
static bool cali_enabled = false;
static bool sensor_initialized = false;
static bool led_initialized = false;

// Filtr dolnoprzepustowy (exponential moving average)
static int filtered_millivolts = 0;
static bool filter_initialized = false;
#define FILTER_ALPHA 0.1f  // Współczynnik filtrowania (0.0-1.0, mniejszy = bardziej wygładzony)

// Stan LED ostrzegawczego
static volatile bool led_should_blink = false;
static TaskHandle_t led_task_handle = NULL;

/**
 * @brief Task migający LED gdy bateria jest niska
 */
static void battery_led_task(void *pvParameters)
{
    bool led_state = false;
    
    while (1) {
        if (led_should_blink) {
            // Migaj LED
            led_state = !led_state;
            gpio_set_level(BATTERY_LOW_LED_GPIO, led_state);
            vTaskDelay(pdMS_TO_TICKS(BATTERY_LED_BLINK_INTERVAL_MS));
        } else {
            // Wyłącz LED gdy bateria jest OK
            gpio_set_level(BATTERY_LOW_LED_GPIO, 0);
            led_state = false;
            vTaskDelay(pdMS_TO_TICKS(100));  // Sprawdzaj co 100ms
        }
    }
}

/**
 * @brief Inicjalizuje GPIO dla LED ostrzegawczego
 */
static esp_err_t sensor_battery_led_init(void)
{
    if (led_initialized) {
        return ESP_OK;
    }
    
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << BATTERY_LOW_LED_GPIO),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Błąd konfiguracji GPIO dla LED: %s", esp_err_to_name(ret));
        return ret;
    }
    
    gpio_set_level(BATTERY_LOW_LED_GPIO, 0);  // Wyłącz na starcie
    
    // Utwórz task do migania LED
    xTaskCreate(battery_led_task, "battery_led_task", 2048, NULL, 5, &led_task_handle);
    if (led_task_handle == NULL) {
        ESP_LOGE(TAG, "Nie udało się utworzyć taska LED");
        return ESP_FAIL;
    }
    
    led_initialized = true;
    ESP_LOGI(TAG, "LED ostrzegawczy zainicjalizowany (GPIO %d, próg: %.1f%%)", 
             BATTERY_LOW_LED_GPIO, BATTERY_LOW_THRESHOLD_PERCENT);
    return ESP_OK;
}

/**
 * @brief Porównanie dla sortowania (qsort)
 */
static int compare_int(const void *a, const void *b)
{
    int ia = *(const int *)a;
    int ib = *(const int *)b;
    return (ia > ib) - (ia < ib);
}

/**
 * @brief Oblicza medianę z tablicy wartości
 */
static int calculate_median(int *values, int count)
{
    if (count <= 0) return 0;
    
    // Sortuj wartości
    qsort(values, count, sizeof(int), compare_int);
    
    // Zwróć medianę
    if (count % 2 == 0) {
        return (values[count/2 - 1] + values[count/2]) / 2;
    } else {
        return values[count/2];
    }
}

esp_err_t sensor_battery_init(void)
{
    if (sensor_initialized) {
        ESP_LOGW(TAG, "Moduł baterii już zainicjalizowany");
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

    // Skonfiguruj kanał baterii
    ESP_RETURN_ON_ERROR(adc_shared_config_channel(BATTERY_ADC_CHANNEL, BATTERY_ADC_BITWIDTH, BATTERY_ADC_ATTEN),
                        TAG, "Nie udało się skonfigurować kanału ADC");

    // Utwórz kalibrację
    esp_err_t ret = adc_shared_create_calibration(BATTERY_ADC_UNIT, BATTERY_ADC_ATTEN, BATTERY_ADC_BITWIDTH, &cali_handle);
    if (ret == ESP_OK) {
        cali_enabled = true;
        ESP_LOGI(TAG, "Kalibracja ADC włączona");
    } else {
        cali_enabled = false;
        ESP_LOGW(TAG, "Kalibracja ADC niedostępna, używam prostej konwersji");
    }

    // Inicjalizuj LED ostrzegawczy
    sensor_battery_led_init();
    
    sensor_initialized = true;
    ESP_LOGI(TAG, "Moduł baterii zainicjalizowany (kanał %d)", BATTERY_ADC_CHANNEL);
    ESP_LOGI(TAG, "  Napięcie pełne: %d mV, Napięcie puste: %d mV", 
             BATTERY_MV_AT_FULL, BATTERY_MV_AT_EMPTY);
    ESP_LOGI(TAG, "  Współczynnik dzielnika: %.2f", BATTERY_VOLTAGE_DIVIDER);
    return ESP_OK;
}

/**
 * @brief Konwertuje napięcie baterii (w mV) na procent poziomu naładowania
 * 
 * @param millivolts Napięcie baterii w miliwoltach (po uwzględnieniu dzielnika)
 * @return Poziom baterii w procentach (0-100%)
 */
static float sensor_battery_mv_to_percent(int millivolts)
{
    const int full_mv = BATTERY_MV_AT_FULL;
    const int empty_mv = BATTERY_MV_AT_EMPTY;

    // Sprawdź czy zakres jest prawidłowy
    if (full_mv <= empty_mv) {
        ESP_LOGE(TAG, "Nieprawidłowa konfiguracja: pełne napięcie (%d) <= puste napięcie (%d)", 
                 full_mv, empty_mv);
        return 0.0f;
    }

    // Ograniczenie wartości do zakresu
    if (millivolts >= full_mv) {
        return 100.0f;
    }
    if (millivolts <= empty_mv) {
        return 0.0f;
    }

    // Liniowa interpolacja między pustym a pełnym napięciem
    float ratio = (float)(millivolts - empty_mv) / (float)(full_mv - empty_mv);
    return ratio * 100.0f;
}

esp_err_t sensor_battery_read(sensor_battery_reading_t *reading)
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

    // Wykonaj wiele próbek dla lepszej dokładności
    // Używamy większej liczby próbek i mediany dla lepszego filtrowania szumu
    #define MAX_SAMPLES 64
    int samples[MAX_SAMPLES];
    int raw_value = 0;
    int valid_samples = 0;
    
    for (int i = 0; i < MAX_SAMPLES && i < BATTERY_SAMPLE_COUNT * 2; ++i) {
        esp_err_t ret = adc_oneshot_read(adc_handle, BATTERY_ADC_CHANNEL, &raw_value);
        if (ret == ESP_OK) {
            samples[valid_samples++] = raw_value;
        }
        vTaskDelay(pdMS_TO_TICKS(2));  // Dłuższe opóźnienie dla stabilizacji
    }
    
    if (valid_samples == 0) {
        ESP_LOGE(TAG, "Brak prawidłowych próbek ADC");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Oblicz medianę zamiast średniej - lepiej filtruje skoki
    int median_raw = calculate_median(samples, valid_samples);
    
    // Alternatywnie: średnia z próbek (można użyć zamiast mediany)
    int accum = 0;
    for (int i = 0; i < valid_samples; ++i) {
        accum += samples[i];
    }
    int avg_raw = accum / valid_samples;
    
    // Użyj średniej z mediany i średniej dla jeszcze lepszej stabilności
    int final_raw = (median_raw + avg_raw) / 2;
    
    int millivolts = 0;

    // Konwersja surowej wartości ADC na napięcie
    if (cali_enabled) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(adc_cali_raw_to_voltage(cali_handle, final_raw, &millivolts));
    }
    if (!cali_enabled || millivolts == 0) {
        // Fallback: prosta konwersja 12 bit -> 0-3300mV
        const int max_adc = (1 << BATTERY_ADC_BITWIDTH) - 1;
        millivolts = (final_raw * 3300) / max_adc;
    }

    // Uwzględnij dzielnik napięcia (jeśli jest używany)
    // Jeśli bateria jest podłączona bezpośrednio, BATTERY_VOLTAGE_DIVIDER = 1.0
    int battery_millivolts = (int)(millivolts * BATTERY_VOLTAGE_DIVIDER);
    
    // Zastosuj filtr dolnoprzepustowy (exponential moving average) dla wygładzenia
    if (!filter_initialized) {
        filtered_millivolts = battery_millivolts;
        filter_initialized = true;
    } else {
        // EMA: filtered = alpha * new + (1 - alpha) * old
        filtered_millivolts = (int)(FILTER_ALPHA * battery_millivolts + (1.0f - FILTER_ALPHA) * filtered_millivolts);
    }
    
    // Użyj wyfiltrowanej wartości
    battery_millivolts = filtered_millivolts;

    reading->raw_value = final_raw;
    reading->millivolts = battery_millivolts;
    reading->battery_percent = sensor_battery_mv_to_percent(battery_millivolts);

    // Aktualizuj stan LED na podstawie poziomu baterii
    sensor_battery_update_led(reading->battery_percent);

    return ESP_OK;
}

bool sensor_battery_is_initialized(void)
{
    return sensor_initialized;
}

void sensor_battery_update_led(float battery_percent)
{
    if (!led_initialized) {
        return;
    }
    
    // Włącz miganie jeśli poziom baterii jest poniżej progu
    if (battery_percent < BATTERY_LOW_THRESHOLD_PERCENT) {
        if (!led_should_blink) {
            ESP_LOGW(TAG, "Niski poziom baterii: %.1f%% - włączam miganie LED", battery_percent);
        }
        led_should_blink = true;
    } else {
        if (led_should_blink) {
            ESP_LOGI(TAG, "Poziom baterii OK: %.1f%% - wyłączam miganie LED", battery_percent);
        }
        led_should_blink = false;
    }
}

