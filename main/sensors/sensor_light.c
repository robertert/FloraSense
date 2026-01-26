#include "sensor_light.h"

#include "config.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/i2c.h"
#include <string.h>

static const char *TAG = "sensor_light";

#define MAX_LIGHT_SENSORS 4

// Mutex dla każdego portu I2C (aby uniknąć konfliktów między taskami)
static SemaphoreHandle_t i2c_mutex[I2C_NUM_MAX] = {NULL};

// Struktura do przechowywania stanu każdej instancji
typedef struct {
    sensor_light_config_t config;
    bool i2c_initialized;
    bool sensor_initialized;
} light_sensor_instance_t;

static light_sensor_instance_t instances[MAX_LIGHT_SENSORS] = {0};
static int num_instances = 0;

// Znajdź instancję po konfiguracji
static int find_instance_index(const sensor_light_config_t *config)
{
    for (int i = 0; i < num_instances; i++) {
        if (instances[i].config.i2c_port == config->i2c_port &&
            instances[i].config.sda_pin == config->sda_pin &&
            instances[i].config.scl_pin == config->scl_pin &&
            instances[i].config.i2c_address == config->i2c_address) {
            return i;
        }
    }
    return -1;
}

// Znajdź lub utwórz instancję
static int get_or_create_instance(const sensor_light_config_t *config)
{
    int idx = find_instance_index(config);
    if (idx >= 0) {
        return idx;
    }
    
    if (num_instances >= MAX_LIGHT_SENSORS) {
        ESP_LOGE(TAG, "Osiągnięto maksymalną liczbę czujników światła (%d)", MAX_LIGHT_SENSORS);
        return -1;
    }
    
    idx = num_instances++;
    memcpy(&instances[idx].config, config, sizeof(sensor_light_config_t));
    instances[idx].i2c_initialized = false;
    instances[idx].sensor_initialized = false;
    
    return idx;
}

static esp_err_t sensor_light_i2c_init(const sensor_light_config_t *config)
{
    int idx = get_or_create_instance(config);
    if (idx < 0) {
        return ESP_ERR_NO_MEM;
    }

    // Utwórz mutex dla tego portu I2C jeśli nie istnieje
    if (i2c_mutex[config->i2c_port] == NULL) {
        i2c_mutex[config->i2c_port] = xSemaphoreCreateMutex();
        if (i2c_mutex[config->i2c_port] == NULL) {
            ESP_LOGE(TAG, "Nie udało się utworzyć mutexa I2C dla portu %d", config->i2c_port);
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGI(TAG, "Utworzono mutex I2C dla portu %d", config->i2c_port);
    }

    if (instances[idx].i2c_initialized) {
        return ESP_OK;
    }

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = config->sda_pin,
        .scl_io_num = config->scl_pin,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = config->i2c_freq_hz,
        .clk_flags = 0,
    };

    // Najpierw spróbuj zainstalować sterownik
    esp_err_t err = i2c_driver_install(config->i2c_port, conf.mode, 0, 0, 0);
    if (err == ESP_ERR_INVALID_STATE || err == ESP_FAIL) {
        // I2C już zainicjalizowany (np. przez sensor_temp) - to OK, używamy istniejącej konfiguracji
        // ESP_FAIL może również oznaczać że sterownik jest już zainstalowany
        ESP_LOGI(TAG, "I2C Port %d już zainicjalizowany przez inny moduł (err=%s), używam istniejącej konfiguracji",
                 config->i2c_port, esp_err_to_name(err));
        instances[idx].i2c_initialized = true;
        return ESP_OK;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Błąd instalacji sterownika I2C Port %d: %s (0x%x)", 
                 config->i2c_port, esp_err_to_name(err), err);
        ESP_RETURN_ON_ERROR(err, TAG, "Nie udało się zainstalować sterownika I2C");
    }

    // Jeśli udało się zainstalować, skonfiguruj parametry
    err = i2c_param_config(config->i2c_port, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Błąd konfiguracji I2C Port %d: %s (0x%x)", 
                 config->i2c_port, esp_err_to_name(err), err);
        ESP_RETURN_ON_ERROR(err, TAG, "Nie udało się skonfigurować I2C");
    }

    instances[idx].i2c_initialized = true;
    ESP_LOGI(TAG, "I2C zainicjalizowany: Port=%d, SDA=GPIO%d, SCL=GPIO%d, freq=%lu Hz",
             config->i2c_port, config->sda_pin, config->scl_pin, config->i2c_freq_hz);
    return ESP_OK;
}

static esp_err_t veml7700_write_reg(const sensor_light_config_t *config, uint8_t reg, uint16_t value)
{
    uint8_t payload[3] = {
        reg,
        (uint8_t)((value >> 8) & 0xFF),
        (uint8_t)(value & 0xFF),
    };

    return i2c_master_write_to_device(
        config->i2c_port,
        config->i2c_address,
        payload,
        sizeof(payload),
        pdMS_TO_TICKS(50));
}

static esp_err_t veml7700_read_reg(const sensor_light_config_t *config, uint8_t reg, uint16_t *value)
{
    uint8_t data[2] = {0};
    ESP_RETURN_ON_ERROR(
        i2c_master_write_read_device(
            config->i2c_port,
            config->i2c_address,
            &reg,
            1,
            data,
            sizeof(data),
            pdMS_TO_TICKS(50)),
        TAG, "Błąd magistrali I2C");

    *value = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    return ESP_OK;
}

static float veml7700_gain_factor(uint8_t gain)
{
    switch (gain) {
        case VEML7700_GAIN_1X:
            return 1.0f;
        case VEML7700_GAIN_2X:
            return 2.0f;
        case VEML7700_GAIN_1_DIV_4:
            return 0.25f;
        case VEML7700_GAIN_1_DIV_8:
            return 0.125f;
        default:
            return 1.0f;
    }
}

static float veml7700_integration_ms(uint8_t it)
{
    switch (it) {
        case VEML7700_IT_25MS:
            return 25.0f;
        case VEML7700_IT_50MS:
            return 50.0f;
        case VEML7700_IT_100MS:
            return 100.0f;
        case VEML7700_IT_200MS:
            return 200.0f;
        case VEML7700_IT_400MS:
            return 400.0f;
        case VEML7700_IT_800MS:
            return 800.0f;
        default:
            return 100.0f;
    }
}

static float veml7700_raw_to_lux(uint16_t raw)
{
    const float base_resolution = 0.0576f; // dla gain=1x oraz IT=100ms
    float gain = veml7700_gain_factor(VEML7700_DEFAULT_GAIN);
    float integration_ms = veml7700_integration_ms(VEML7700_DEFAULT_IT);

    if (integration_ms <= 0.0f) {
        integration_ms = 100.0f;
    }

    float resolution = base_resolution * (100.0f / integration_ms) / gain;
    return raw * resolution;
}

esp_err_t sensor_light_init(const sensor_light_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    int idx = get_or_create_instance(config);
    if (idx < 0) {
        return ESP_ERR_NO_MEM;
    }
    
    if (instances[idx].sensor_initialized) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(sensor_light_i2c_init(config), TAG, "I2C niegotowe");

    // Czujnik potrzebuje chwili po zasileniu zanim przyjmie konfigurację
    vTaskDelay(pdMS_TO_TICKS(5));

    uint16_t veml_config =
        (VEML7700_DEFAULT_GAIN << 11) |
        (VEML7700_DEFAULT_IT << 6) |
        (VEML7700_PERSISTENCE_1 << 4) |
        (VEML7700_INT_DISABLE << 1) |
        VEML7700_POWER_ON;

    ESP_RETURN_ON_ERROR(veml7700_write_reg(config, VEML7700_REG_ALS_CONF, veml_config),
                        TAG, "Nie udało się zapisać konfiguracji VEML7700");

    instances[idx].sensor_initialized = true;
    ESP_LOGI(TAG, "Czujnik VEML7700 gotowy: Port=%d, SDA=GPIO%d, SCL=GPIO%d, Adres=0x%02X",
             config->i2c_port, config->sda_pin, config->scl_pin, config->i2c_address);
    return ESP_OK;
}

esp_err_t sensor_light_read(const sensor_light_config_t *config, sensor_light_reading_t *reading)
{
    if (config == NULL || reading == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!sensor_light_is_initialized(config)) {
        return ESP_ERR_INVALID_STATE;
    }

    // Sprawdź czy mutex istnieje
    if (i2c_mutex[config->i2c_port] == NULL) {
        ESP_LOGE(TAG, "Mutex I2C dla portu %d nie istnieje", config->i2c_port);
        return ESP_ERR_INVALID_STATE;
    }

    // Zdobądź mutex z timeoutem 500ms
    if (xSemaphoreTake(i2c_mutex[config->i2c_port], pdMS_TO_TICKS(500)) != pdTRUE) {
        ESP_LOGW(TAG, "Timeout oczekiwania na mutex I2C (port %d) - bus zajęty", config->i2c_port);
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;
    uint16_t raw_als = 0;
    uint16_t raw_white = 0;

    ret = veml7700_read_reg(config, VEML7700_REG_ALS, &raw_als);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Błąd odczytu ALS: %s", esp_err_to_name(ret));
        xSemaphoreGive(i2c_mutex[config->i2c_port]);
        return ret;
    }

    esp_err_t white_err = veml7700_read_reg(config, VEML7700_REG_WHITE, &raw_white);
    if (white_err != ESP_OK) {
        ESP_LOGW(TAG, "Błąd odczytu kanału WHITE: %s", esp_err_to_name(white_err));
        raw_white = 0;
    }

    // Zwolnij mutex
    xSemaphoreGive(i2c_mutex[config->i2c_port]);

    reading->raw_als = raw_als;
    reading->raw_white = raw_white;
    reading->lux = veml7700_raw_to_lux(raw_als);

    return ESP_OK;
}

bool sensor_light_is_initialized(const sensor_light_config_t *config)
{
    if (config == NULL) {
        return false;
    }
    
    int idx = find_instance_index(config);
    if (idx < 0) {
        return false;
    }
    
    return instances[idx].sensor_initialized;
}

void light_sensor_task(void *param)
{
    sensor_light_config_t *config = (sensor_light_config_t *)param;
    
    if (config == NULL) {
        ESP_LOGE(TAG, "Brak konfiguracji dla taska czujnika światła");
        vTaskDelete(NULL);
        return;
    }
    
    esp_err_t ret = sensor_light_init(config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Nie udało się zainicjalizować VEML7700: Port=%d, SDA=GPIO%d, SCL=GPIO%d, Adres=0x%02X",
                 config->i2c_port, config->sda_pin, config->scl_pin, config->i2c_address);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Task czujnika światła uruchomiony: Port=%d, SDA=GPIO%d, SCL=GPIO%d, Adres=0x%02X",
             config->i2c_port, config->sda_pin, config->scl_pin, config->i2c_address);

    sensor_light_reading_t reading = {0};
    while (1) {
        if (sensor_light_read(config, &reading) == ESP_OK) {
            ESP_LOGI(TAG, "[Port=%d, Adres=0x%02X] Lux=%.2f RAW_ALS=%u RAW_WHITE=%u",
                     config->i2c_port, config->i2c_address,
                     reading.lux, reading.raw_als, reading.raw_white);
        } else {
            ESP_LOGW(TAG, "[Port=%d, Adres=0x%02X] Nieudany odczyt z VEML7700",
                     config->i2c_port, config->i2c_address);
        }
        vTaskDelay(pdMS_TO_TICKS(LIGHT_SENSOR_POLL_PERIOD_MS));
    }
}

SemaphoreHandle_t sensor_light_get_i2c_mutex(i2c_port_t port)
{
    if (port >= I2C_NUM_MAX) {
        ESP_LOGE(TAG, "Nieprawidłowy port I2C: %d", port);
        return NULL;
    }

    // Utwórz mutex jeśli nie istnieje
    if (i2c_mutex[port] == NULL) {
        i2c_mutex[port] = xSemaphoreCreateMutex();
        if (i2c_mutex[port] == NULL) {
            ESP_LOGE(TAG, "Nie udało się utworzyć mutexa I2C dla portu %d", port);
            return NULL;
        }
        ESP_LOGI(TAG, "Utworzono mutex I2C dla portu %d (z get_i2c_mutex)", port);
    }

    return i2c_mutex[port];
}
