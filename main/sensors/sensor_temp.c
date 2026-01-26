#include "sensor_temp.h"
#include "sensor_light.h"  // Dla wspólnego mutexa I2C
#include "config.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "sensor_temp";

// Rejestry BME280
#define BME280_REG_CHIP_ID                0xD0
#define BME280_REG_RESET                  0xE0
#define BME280_REG_CTRL_HUM               0xF2
#define BME280_REG_CTRL_MEAS              0xF4
#define BME280_REG_CONFIG                 0xF5
#define BME280_REG_PRESS_MSB              0xF7
#define BME280_REG_TEMP_MSB               0xFA
#define BME280_REG_HUM_MSB                0xFD

// Rejestry kalibracyjne
#define BME280_REG_DIG_T1                0x88
#define BME280_REG_DIG_H1                0xA1

#define BME280_RESET_VALUE                0xB6
#define BME280_SLEEP_MODE                 0x00
#define BME280_FORCED_MODE                0x01
#define BME280_NORMAL_MODE                0x03

static bool i2c_initialized = false;
static bool sensor_initialized = false;
static uint8_t bme280_actual_address = BME280_I2C_ADDRESS; // Rzeczywisty adres po wykryciu

// Współczynniki kalibracyjne
static struct {
    uint16_t dig_T1;
    int16_t dig_T2, dig_T3;
    uint16_t dig_H1;
    int8_t dig_H2, dig_H3;
    int16_t dig_H4, dig_H5, dig_H6;
} calib_data;

static esp_err_t sensor_temp_i2c_init(void)
{
    if (i2c_initialized) {
        return ESP_OK;
    }

    // Sprawdź czy I2C jest już zainstalowany (np. przez sensor_light)
    // Próba odczytu konfiguracji - jeśli zwróci ESP_ERR_INVALID_STATE, znaczy że nie jest zainstalowany
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = TEMP_SENSOR_I2C_SDA_GPIO,
        .scl_io_num = TEMP_SENSOR_I2C_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = TEMP_SENSOR_I2C_FREQ_HZ,
        .clk_flags = 0,
    };

    // Najpierw spróbuj zainstalować sterownik
    esp_err_t err = i2c_driver_install(TEMP_SENSOR_I2C_PORT, conf.mode, 0, 0, 0);
    if (err == ESP_ERR_INVALID_STATE || err == ESP_FAIL) {
        // I2C już zainicjalizowany (np. przez sensor_light) - to OK, używamy istniejącej konfiguracji
        // ESP_FAIL może również oznaczać że sterownik jest już zainstalowany
        ESP_LOGI(TAG, "I2C Port %d już zainicjalizowany przez inny moduł (err=%s), używam istniejącej konfiguracji", 
                 TEMP_SENSOR_I2C_PORT, esp_err_to_name(err));
        i2c_initialized = true;
        return ESP_OK;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Błąd instalacji sterownika I2C: %s (0x%x)", esp_err_to_name(err), err);
        ESP_RETURN_ON_ERROR(err, TAG, "Nie udało się zainstalować sterownika I2C");
    }
    
    // Jeśli udało się zainstalować, skonfiguruj parametry
    err = i2c_param_config(TEMP_SENSOR_I2C_PORT, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Błąd konfiguracji I2C: %s (0x%x)", esp_err_to_name(err), err);
        ESP_RETURN_ON_ERROR(err, TAG, "Nie udało się skonfigurować I2C");
    }

    ESP_LOGI(TAG, "I2C zainicjalizowany: SDA=GPIO%d, SCL=GPIO%d, freq=%d Hz", 
             TEMP_SENSOR_I2C_SDA_GPIO, TEMP_SENSOR_I2C_SCL_GPIO, TEMP_SENSOR_I2C_FREQ_HZ);

    i2c_initialized = true;
    return ESP_OK;
}

static esp_err_t bme280_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {reg, value};
    esp_err_t ret = i2c_master_write_to_device(
        TEMP_SENSOR_I2C_PORT,
        bme280_actual_address, // Używaj wykrytego adresu
        data,
        sizeof(data),
        pdMS_TO_TICKS(500)); // Zwiększony timeout
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Błąd zapisu do rejestru 0x%02X: %s (adres I2C: 0x%02X)", 
                 reg, esp_err_to_name(ret), bme280_actual_address);
    } else {
        vTaskDelay(pdMS_TO_TICKS(2)); // Krótki delay po zapisie
    }
    return ret;
}

static esp_err_t bme280_read_reg(uint8_t reg, uint8_t *value, size_t len)
{
    esp_err_t ret = i2c_master_write_read_device(
        TEMP_SENSOR_I2C_PORT,
        bme280_actual_address, // Używaj wykrytego adresu
        &reg,
        1,
        value,
        len,
        pdMS_TO_TICKS(500)); // Zwiększony timeout
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Błąd odczytu z rejestru 0x%02X: %s (adres I2C: 0x%02X)", 
                 reg, esp_err_to_name(ret), bme280_actual_address);
    }
    return ret;
}

// Funkcja do skanowania adresów I2C i znalezienia BME280
static esp_err_t bme280_scan_address(uint8_t *found_address)
{
    uint8_t test_addresses[] = {0x76, 0x77};
    uint8_t chip_id_reg = BME280_REG_CHIP_ID;
    uint8_t chip_id = 0;
    
    ESP_LOGI(TAG, "Skanowanie %zu adresów I2C w poszukiwaniu BME280...", sizeof(test_addresses));
    
    for (size_t i = 0; i < sizeof(test_addresses); i++) {
        uint8_t addr = test_addresses[i];
        ESP_LOGI(TAG, "[%zu/%zu] Testuję adres 0x%02X...", i+1, sizeof(test_addresses), addr);
        
        // Spróbuj odczytać Chip ID
        esp_err_t ret = i2c_master_write_read_device(
            TEMP_SENSOR_I2C_PORT,
            addr,
            &chip_id_reg,
            1,
            &chip_id,
            1,
            pdMS_TO_TICKS(500));
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "  ✓ Adres 0x%02X odpowiada! Chip ID=0x%02X", addr, chip_id);
            if (chip_id == BME280_CHIP_ID) {
                ESP_LOGI(TAG, "  ✓✓✓ ZNALEZIONO BME280 na adresie 0x%02X! ✓✓✓", addr);
                *found_address = addr;
                return ESP_OK;
            } else {
                ESP_LOGW(TAG, "  ⚠ Chip ID=0x%02X nie pasuje do BME280 (oczekiwano 0x%02X)", 
                         chip_id, BME280_CHIP_ID);
            }
        } else {
            ESP_LOGD(TAG, "  ✗ Adres 0x%02X nie odpowiada: %s", addr, esp_err_to_name(ret));
        }
        vTaskDelay(pdMS_TO_TICKS(50)); // Dłuższy delay między próbami
    }
    
    ESP_LOGE(TAG, "Nie znaleziono BME280 na żadnym z %zu testowanych adresów", sizeof(test_addresses));
    ESP_LOGE(TAG, "Możliwe przyczyny:");
    ESP_LOGE(TAG, "  1. Czujnik nie jest podłączony lub nie ma zasilania");
    ESP_LOGE(TAG, "  2. Błędne połączenia I2C (SDA=GPIO%d, SCL=GPIO%d)", 
             TEMP_SENSOR_I2C_SDA_GPIO, TEMP_SENSOR_I2C_SCL_GPIO);
    ESP_LOGE(TAG, "  3. Brak rezystorów pull-up (4.7kΩ na SDA i SCL do 3.3V)");
    ESP_LOGE(TAG, "  4. Konflikt z innym urządzeniem I2C na tej samej szynie");
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t bme280_read_calibration(void)
{
    uint8_t calib[24] = {0};
    ESP_RETURN_ON_ERROR(bme280_read_reg(BME280_REG_DIG_T1, calib, 24),
                        TAG, "Błąd odczytu kalibracji T");

    calib_data.dig_T1 = (uint16_t)calib[0] | ((uint16_t)calib[1] << 8);
    calib_data.dig_T2 = (int16_t)calib[2] | ((int16_t)calib[3] << 8);
    calib_data.dig_T3 = (int16_t)calib[4] | ((int16_t)calib[5] << 8);

    uint8_t hum_calib[7] = {0};
    ESP_RETURN_ON_ERROR(bme280_read_reg(BME280_REG_DIG_H1, hum_calib, 1),
                        TAG, "Błąd odczytu kalibracji H1");
    calib_data.dig_H1 = hum_calib[0];

    ESP_RETURN_ON_ERROR(bme280_read_reg(0xE1, hum_calib, 7),
                        TAG, "Błąd odczytu kalibracji H2-H6");
    calib_data.dig_H2 = (int16_t)hum_calib[0] | ((int16_t)hum_calib[1] << 8);
    calib_data.dig_H3 = (int8_t)hum_calib[2];
    calib_data.dig_H4 = ((int16_t)hum_calib[3] << 4) | (hum_calib[4] & 0x0F);
    calib_data.dig_H5 = ((int16_t)(hum_calib[4] & 0xF0) >> 4) | ((int16_t)hum_calib[5] << 4);
    calib_data.dig_H6 = (int8_t)hum_calib[6];

    return ESP_OK;
}

static float bme280_compensate_temp(int32_t adc_T)
{
    int32_t var1, var2;
    var1 = ((((adc_T >> 3) - ((int32_t)calib_data.dig_T1 << 1))) *
            ((int32_t)calib_data.dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)calib_data.dig_T1)) *
              ((adc_T >> 4) - ((int32_t)calib_data.dig_T1))) >> 12) *
            ((int32_t)calib_data.dig_T3)) >> 14;
    int32_t t_fine = var1 + var2;
    float T = (t_fine * 5 + 128) >> 8;
    return T / 100.0f;
}

static float bme280_compensate_humidity(int32_t adc_H, int32_t t_fine)
{
    // Sprawdź czy kalibracja wilgotności jest prawidłowa
    if (calib_data.dig_H1 == 0 && calib_data.dig_H2 == 0) {
        ESP_LOGW(TAG, "UWAGA: Kalibracja wilgotności jest zerowa - możliwe że to BMP280, nie BME280!");
        return 0.0f;
    }
    
    int32_t v_x1_u32r;
    v_x1_u32r = (t_fine - ((int32_t)76800));
    v_x1_u32r = (((((adc_H << 14) - (((int32_t)calib_data.dig_H4) << 20) -
                    (((int32_t)calib_data.dig_H5) * v_x1_u32r)) + ((int32_t)16384)) >> 15) *
                 (((((((v_x1_u32r * ((int32_t)calib_data.dig_H6)) >> 10) *
                      (((v_x1_u32r * ((int32_t)calib_data.dig_H3)) >> 11) + ((int32_t)32768))) >> 10) +
                    ((int32_t)2097152)) * ((int32_t)calib_data.dig_H2) + 8192) >> 14));
    v_x1_u32r = (v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) *
                                ((int32_t)calib_data.dig_H1)) >> 4));
    v_x1_u32r = (v_x1_u32r < 0) ? 0 : v_x1_u32r;
    v_x1_u32r = (v_x1_u32r > 419430400) ? 419430400 : v_x1_u32r;
    float h = (v_x1_u32r >> 12);
    return h / 1024.0f;
}

esp_err_t sensor_temp_init(void)
{
    if (sensor_initialized) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(sensor_temp_i2c_init(), TAG, "I2C niegotowe");

    // Daj czas na stabilizację I2C
    vTaskDelay(pdMS_TO_TICKS(100));

    // Najpierw znajdź adres BME280 przez skanowanie
    uint8_t found_addr = 0;
    esp_err_t scan_ret = bme280_scan_address(&found_addr);
    if (scan_ret != ESP_OK) {
        ESP_LOGE(TAG, "Nie udało się znaleźć BME280. Sprawdź:");
        ESP_LOGE(TAG, "  - Połączenia I2C (SDA=GPIO%d, SCL=GPIO%d)", 
                 TEMP_SENSOR_I2C_SDA_GPIO, TEMP_SENSOR_I2C_SCL_GPIO);
        ESP_LOGE(TAG, "  - Zasilanie BME280 (3.3V)");
        ESP_LOGE(TAG, "  - Pin SDO (GND=0x76, VCC=0x77)");
        return scan_ret;
    }
    
    bme280_actual_address = found_addr;
    ESP_LOGI(TAG, "Używam adresu 0x%02X dla BME280", bme280_actual_address);
    
    // Weryfikacja - odczytaj Chip ID jeszcze raz
    uint8_t chip_id = 0;
    esp_err_t ret = bme280_read_reg(BME280_REG_CHIP_ID, &chip_id, 1);
    if (ret != ESP_OK || chip_id != BME280_CHIP_ID) {
        ESP_LOGE(TAG, "Błąd weryfikacji Chip ID: ret=%s, chip_id=0x%02X", 
                 esp_err_to_name(ret), chip_id);
        return (ret != ESP_OK) ? ret : ESP_ERR_NOT_FOUND;
    }
    
    ESP_LOGI(TAG, "✓ BME280 potwierdzony (Chip ID: 0x%02X)", chip_id);
    
    // Sprawdź czy to rzeczywiście BME280 (0x60), a nie BMP280 (0x58)
    if (chip_id == 0x58) {
        ESP_LOGW(TAG, "UWAGA: Chip ID 0x58 = BMP280 (bez wilgotności)! BME280 powinien mieć Chip ID 0x60");
    }

    // Odczytaj kalibrację
    ESP_RETURN_ON_ERROR(bme280_read_calibration(), TAG, "Błąd odczytu kalibracji");
    
    // Logowanie współczynników kalibracyjnych wilgotności
    ESP_LOGI(TAG, "Kalibracja wilgotności: H1=%u, H2=%d, H3=%d, H4=%d, H5=%d, H6=%d",
             calib_data.dig_H1, calib_data.dig_H2, calib_data.dig_H3,
             calib_data.dig_H4, calib_data.dig_H5, calib_data.dig_H6);

    // Konfiguracja: oversampling x1, normal mode
    // WAŻNE: CTRL_HUM musi być zapisany PRZED CTRL_MEAS!
    ESP_RETURN_ON_ERROR(bme280_write_reg(BME280_REG_CTRL_HUM, 0x01),
                        TAG, "Błąd konfiguracji CTRL_HUM");
    
    uint8_t ctrl_meas = (0x01 << 5) | (0x01 << 2) | BME280_NORMAL_MODE; // temp x1, press x1, normal
    ESP_RETURN_ON_ERROR(bme280_write_reg(BME280_REG_CTRL_MEAS, ctrl_meas),
                        TAG, "Błąd konfiguracji CTRL_MEAS");

    uint8_t config = (0x00 << 5) | (0x00 << 2) | 0x00; // standby 0.5ms, filter off
    ESP_RETURN_ON_ERROR(bme280_write_reg(BME280_REG_CONFIG, config),
                        TAG, "Błąd konfiguracji CONFIG");
    
    // Poczekaj na pierwszy pomiar (standby time + measurement time)
    vTaskDelay(pdMS_TO_TICKS(100));

    sensor_initialized = true;
    ESP_LOGI(TAG, "Czujnik BME280 gotowy (adres 0x%02X)", bme280_actual_address);
    return ESP_OK;
}

esp_err_t sensor_temp_read(sensor_temp_reading_t *reading)
{
    if (!sensor_initialized || reading == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // Pobierz wspólny mutex I2C
    SemaphoreHandle_t i2c_mutex = sensor_light_get_i2c_mutex(TEMP_SENSOR_I2C_PORT);
    if (i2c_mutex == NULL) {
        ESP_LOGE(TAG, "Nie można uzyskać mutexa I2C");
        return ESP_ERR_INVALID_STATE;
    }

    // Zdobądź mutex z timeoutem 500ms
    if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        ESP_LOGW(TAG, "Timeout oczekiwania na mutex I2C - bus zajęty");
        return ESP_ERR_TIMEOUT;
    }

    // Odczytaj dane surowe (8 bajtów: press 3, temp 3, hum 2)
    uint8_t data[8] = {0};
    esp_err_t ret = bme280_read_reg(BME280_REG_PRESS_MSB, data, 8);
    if (ret != ESP_OK) {
        xSemaphoreGive(i2c_mutex);
        ESP_LOGE(TAG, "Błąd odczytu danych BME280: %s", esp_err_to_name(ret));
        return ret;
    }

    // Parsuj temperaturę (20-bit)
    int32_t adc_T = ((int32_t)data[3] << 12) | ((int32_t)data[4] << 4) | ((int32_t)data[5] >> 4);
    
    // Parsuj wilgotność (16-bit) - odczyt z rejestrów 0xFD i 0xFE
    int32_t adc_H = ((int32_t)data[6] << 8) | (int32_t)data[7];
    
    // Logowanie surowych danych (tylko przy pierwszym odczycie lub gdy wilgotność = 0)
    static bool first_read = true;
    if (first_read || adc_H == 0) {
        ESP_LOGI(TAG, "Surowe dane: press=[0x%02X 0x%02X 0x%02X], temp=[0x%02X 0x%02X 0x%02X], hum=[0x%02X 0x%02X]",
                 data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
        ESP_LOGI(TAG, "Parsowane: adc_T=%ld, adc_H=%ld", adc_T, adc_H);
        first_read = false;
    }

    // Oblicz t_fine dla kompensacji
    int32_t var1, var2;
    var1 = ((((adc_T >> 3) - ((int32_t)calib_data.dig_T1 << 1))) *
            ((int32_t)calib_data.dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)calib_data.dig_T1)) *
              ((adc_T >> 4) - ((int32_t)calib_data.dig_T1))) >> 12) *
            ((int32_t)calib_data.dig_T3)) >> 14;
    int32_t t_fine = var1 + var2;

    reading->temperature_c = bme280_compensate_temp(adc_T);
    
    // Kompensacja wilgotności - sprawdź czy adc_H jest prawidłowe
    if (adc_H == 0 || adc_H == 0xFFFF || adc_H == 0x8000) {
        ESP_LOGW(TAG, "Nieprawidłowa wartość surowej wilgotności: adc_H=%ld (0x%04lX)", adc_H, adc_H);
        reading->humidity_percent = 0.0f;
    } else {
        reading->humidity_percent = bme280_compensate_humidity(adc_H, t_fine);
    }
    
    reading->raw_temp = adc_T;
    reading->raw_humidity = adc_H;
    
    // Logowanie jeśli wilgotność = 0
    if (reading->humidity_percent == 0.0f && adc_H != 0) {
        ESP_LOGW(TAG, "Wilgotność = 0%%! adc_H=%ld, t_fine=%ld", adc_H, t_fine);
    }

    // Zwolnij mutex I2C
    xSemaphoreGive(i2c_mutex);

    return ESP_OK;
}

void sensor_temp_task(void *param)
{
    // Inicjalizacja czujnika BME280
    esp_err_t ret = sensor_temp_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Nie udało się zainicjalizować czujnika BME280: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "Task czujnika temperatury BME280 uruchomiony");
    
    sensor_temp_reading_t reading;
    
    // Odczyt wartości co 2 sekundy
    while (1) {
        ret = sensor_temp_read(&reading);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Temperatura: %.2f°C, Wilgotność: %.2f%%", 
                     reading.temperature_c, reading.humidity_percent);
        } else {
            ESP_LOGW(TAG, "Błąd odczytu czujnika BME280: %s", esp_err_to_name(ret));
        }
        vTaskDelay(pdMS_TO_TICKS(2000)); // 2 sekundy
    }
}
