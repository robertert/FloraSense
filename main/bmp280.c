#include "bmp280.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <unistd.h>
#include "lwip/sys.h"

static const char *TAG = "BMP280";

// I2C handles
static i2c_master_bus_handle_t bus_handle = NULL;
static i2c_master_dev_handle_t dev_handle = NULL;
static bool initialized = false;

// Rejestry BMP280
#define BMP280_REG_ID           0xD0
#define BMP280_REG_TEMP_MSB     0xFA
#define BMP280_REG_CTRL_MEAS    0xF4
#define BMP280_REG_CONFIG       0xF5
#define BMP280_REG_DIG_T1       0x88
#define BMP280_REG_DIG_T2       0x8A
#define BMP280_REG_DIG_T3       0x8C
#define BMP280_CHIP_ID          0x58

// Kalibracja
static uint16_t dig_T1;
static int16_t  dig_T2;
static int16_t  dig_T3;

// Zmienna przechowująca znaleziony adres
static uint8_t detected_address = 0;

// Funkcja pomocnicza do skanowania I2C (uproszczona wersja)
static void scan_i2c_bus(i2c_master_bus_handle_t bus)
{
    ESP_LOGI(TAG, "Skanowanie szyny I2C...");
    int found_count = 0;
    
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        i2c_master_dev_handle_t test_handle;
        i2c_device_config_t test_config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addr,
            .scl_speed_hz = 100000,
        };
        
        esp_err_t ret = i2c_master_bus_add_device(bus, &test_config, &test_handle);
        if (ret == ESP_OK) {
            // Próba prostego zapisu (write) - bardziej niezawodne niż read
            // Większość urządzeń I2C odpowiada na write nawet jeśli nie ma danych
            ret = i2c_master_transmit(test_handle, (uint8_t[]){0x00}, 1, 50 / portTICK_PERIOD_MS);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "  Znaleziono urządzenie na adresie 0x%02X", addr);
                found_count++;
            }
            i2c_master_bus_rm_device(test_handle);
        }
    }
    
    if (found_count == 0) {
        ESP_LOGW(TAG, "Nie znaleziono żadnych urządzeń na szynie I2C!");
        ESP_LOGW(TAG, "Możliwe przyczyny:");
        ESP_LOGW(TAG, "  - Brak urządzeń na szynie");
        ESP_LOGW(TAG, "  - Problem z połączeniami (SDA/SCL)");
        ESP_LOGW(TAG, "  - Brak rezystorów pull-up (4.7kΩ na SDA i SCL do 3.3V)");
        ESP_LOGW(TAG, "  - Złe piny GPIO");
    } else {
        ESP_LOGI(TAG, "Znaleziono %d urządzeń na szynie I2C", found_count);
    }
}

// Funkcja pomocnicza do sprawdzenia czy urządzenie odpowiada na danym adresie
static esp_err_t try_detect_bmp280(i2c_master_bus_handle_t bus, uint8_t address, uint8_t *chip_id)
{
    esp_err_t ret;
    i2c_master_dev_handle_t test_handle;
    
    i2c_device_config_t test_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = 10000,  // 10 kHz zamiast 100 kHz
        };
    
    ret = i2c_master_bus_add_device(bus, &test_config, &test_handle);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Krótkie opóźnienie
    vTaskDelay(10 / portTICK_PERIOD_MS);
    
    // Próba odczytu ID chipa
    ret = i2c_master_transmit_receive(test_handle, (uint8_t[]){BMP280_REG_ID}, 1, chip_id, 1, 1000 / portTICK_PERIOD_MS);
    
    if (ret == ESP_OK && *chip_id == BMP280_CHIP_ID) {
        // Znaleziono BMP280 - zachowaj handle
        dev_handle = test_handle;
        detected_address = address;
        return ESP_OK;
    }
    
    // Jeśli nie znaleziono, usuń urządzenie
    i2c_master_bus_rm_device(test_handle);
    return ret != ESP_OK ? ret : ESP_ERR_INVALID_RESPONSE;
}

// Inicjalizacja I2C i BMP280
esp_err_t bmp280_init(void)
{
    esp_err_t ret;
    uint8_t chip_id;
    uint8_t calib_data[6];

    // Inicjalizacja I2C
    // Uwaga: Jeśli masz zewnętrzne rezystory pull-up (4.7kΩ), ustaw enable_internal_pullup na false
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_1,
        .sda_io_num = 21,
        .scl_io_num = 22,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,  // Zmień na false jeśli masz zewnętrzne pull-up
    };
    ret = i2c_new_master_bus(&bus_config, &bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C bus: %s", esp_err_to_name(ret));
        ESP_ERROR_CHECK(ret);
    }
    ESP_LOGI(TAG, "I2C bus initialized");

    // Opcjonalne skanowanie całej szyny I2C (do diagnostyki)
    scan_i2c_bus(bus_handle);

    // Próba wykrycia BMP280 na obu możliwych adresach
    ESP_LOGI(TAG, "Skanowanie I2C w poszukiwaniu BMP280...");
    
    // Próba adresu 0x76
    ESP_LOGI(TAG, "Próba adresu 0x76...");
    ret = try_detect_bmp280(bus_handle, 0x76, &chip_id);
    if (ret == ESP_OK && chip_id == BMP280_CHIP_ID) {
        ESP_LOGI(TAG, "BMP280 znaleziony na adresie 0x76, Chip ID: 0x%02X", chip_id);
    } else {
        // Próba adresu 0x77
        ESP_LOGI(TAG, "Próba adresu 0x77...");
        ret = try_detect_bmp280(bus_handle, 0x77, &chip_id);
        if (ret == ESP_OK && chip_id == BMP280_CHIP_ID) {
            ESP_LOGI(TAG, "BMP280 znaleziony na adresie 0x77, Chip ID: 0x%02X", chip_id);
        } else {
            ESP_LOGE(TAG, "Nie znaleziono BMP280 na żadnym z adresów (0x76, 0x77)");
            ESP_LOGE(TAG, "Sprawdź:");
            ESP_LOGE(TAG, "  - Czy sensor jest podłączony (SDA=GPIO21, SCL=GPIO22)");
            ESP_LOGE(TAG, "  - Czy sensor jest zasilany (VCC=3.3V i GND)");
            ESP_LOGE(TAG, "  - Czy rezystory pull-up są podłączone (4.7kΩ na SDA i SCL do 3.3V)");
            ESP_LOGE(TAG, "  - Jeśli masz zewnętrzne pull-up, wyłącz wewnętrzne w kodzie");
            ESP_LOGE(TAG, "  - Czy to rzeczywiście BMP280 (może być inny sensor)");
            return ESP_ERR_NOT_FOUND;
        }
    }

    // Odczyt kalibracji temperatury (6 bajtów: T1, T2, T3)
    ret = i2c_master_transmit_receive(dev_handle, (uint8_t[]){BMP280_REG_DIG_T1}, 1, calib_data, 6, 1000 / portTICK_PERIOD_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read calibration data: %s", esp_err_to_name(ret));
        ESP_ERROR_CHECK(ret);
    }

    // Konwersja z little-endian
    dig_T1 = calib_data[0] | (calib_data[1] << 8);
    dig_T2 = (int16_t)(calib_data[2] | (calib_data[3] << 8));
    dig_T3 = (int16_t)(calib_data[4] | (calib_data[5] << 8));

    ESP_LOGI(TAG, "Calibration data read: dig_T1=%u, dig_T2=%d, dig_T3=%d", dig_T1, dig_T2, dig_T3);

    // Konfiguracja sensora: oversampling 1x, normal mode
    uint8_t ctrl_meas = (1 << 5) | (1 << 2) | 3;  // osrs_t=1, osrs_p=1, mode=3
    uint8_t config_reg = (5 << 5) | (0 << 2) | 0; // t_sb=1000ms, filter off, 3-wire SPI disable

    ret = i2c_master_transmit(dev_handle, (uint8_t[]){BMP280_REG_CTRL_MEAS, ctrl_meas}, 2, 1000 / portTICK_PERIOD_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write CTRL_MEAS register: %s", esp_err_to_name(ret));
        ESP_ERROR_CHECK(ret);
    }

    ret = i2c_master_transmit(dev_handle, (uint8_t[]){BMP280_REG_CONFIG, config_reg}, 2, 1000 / portTICK_PERIOD_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write CONFIG register: %s", esp_err_to_name(ret));
        ESP_ERROR_CHECK(ret);
    }

    ESP_LOGI(TAG, "BMP280 initialized successfully");
    initialized = true;
    return ESP_OK;
}

// Sprawdzenie czy BMP280 jest zainicjalizowany
bool bmp280_is_initialized(void)
{
    return initialized;
}

// Kompensacja temperatury
static double bmp280_compensate_T_double(int32_t adc_T)
{
    double var1 = (((double)adc_T)/16384.0 - ((double)dig_T1)/1024.0) * dig_T2;
    double var2 = ((((double)adc_T)/131072.0 - ((double)dig_T1)/8192.0) *
                   (((double)adc_T)/131072.0 - ((double)dig_T1)/8192.0)) * dig_T3;
    return (var1 + var2) / 5120.0;
}

// Odczyt temperatury
double bmp280_read_temperature(void)
{
    if (!initialized || dev_handle == NULL) {
        ESP_LOGE(TAG, "BMP280 nie jest zainicjalizowany!");
        return -999.0;  // Zwróć nieprawidłową wartość
    }

    uint8_t buf[BMP280_TEMP_REG_SIZE];
    int32_t temp_raw;
    esp_err_t ret;

    ret = i2c_master_transmit_receive(dev_handle, (uint8_t[]){BMP280_REG_TEMP_MSB}, 1, buf, BMP280_TEMP_REG_SIZE, 1000 / portTICK_PERIOD_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read temperature: %s", esp_err_to_name(ret));
        return -999.0;
    }

    temp_raw = (buf[0] << 12) | (buf[1] << 4) | (buf[2] >> 4);
    return bmp280_compensate_T_double(temp_raw);
}