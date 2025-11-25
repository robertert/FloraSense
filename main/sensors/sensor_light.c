#include "sensor_light.h"

#include "config.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"

static const char *TAG = "sensor_light";

static bool i2c_initialized = false;
static bool sensor_initialized = false;

static esp_err_t sensor_light_i2c_init(void)
{
    if (i2c_initialized) {
        return ESP_OK;
    }

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = LIGHT_SENSOR_I2C_SDA_GPIO,
        .scl_io_num = LIGHT_SENSOR_I2C_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = LIGHT_SENSOR_I2C_FREQ_HZ,
        .clk_flags = 0,
    };

    ESP_RETURN_ON_ERROR(i2c_param_config(LIGHT_SENSOR_I2C_PORT, &conf),
                        TAG, "Nie udało się skonfigurować I2C");

    esp_err_t err = i2c_driver_install(LIGHT_SENSOR_I2C_PORT, conf.mode, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(err, TAG, "Nie udało się zainstalować sterownika I2C");
    }

    i2c_initialized = true;
    return ESP_OK;
}

static esp_err_t veml7700_write_reg(uint8_t reg, uint16_t value)
{
    uint8_t payload[3] = {
        reg,
        (uint8_t)((value >> 8) & 0xFF),
        (uint8_t)(value & 0xFF),
    };

    return i2c_master_write_to_device(
        LIGHT_SENSOR_I2C_PORT,
        VEML7700_I2C_ADDRESS,
        payload,
        sizeof(payload),
        pdMS_TO_TICKS(50));
}

static esp_err_t veml7700_read_reg(uint8_t reg, uint16_t *value)
{
    uint8_t data[2] = {0};
    ESP_RETURN_ON_ERROR(
        i2c_master_write_read_device(
            LIGHT_SENSOR_I2C_PORT,
            VEML7700_I2C_ADDRESS,
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

esp_err_t sensor_light_init(void)
{
    if (sensor_initialized) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(sensor_light_i2c_init(), TAG, "I2C niegotowe");

    // Czujnik potrzebuje chwili po zasileniu zanim przyjmie konfigurację
    vTaskDelay(pdMS_TO_TICKS(5));

    uint16_t config =
        (VEML7700_DEFAULT_GAIN << 11) |
        (VEML7700_DEFAULT_IT << 6) |
        (VEML7700_PERSISTENCE_1 << 4) |
        (VEML7700_INT_DISABLE << 1) |
        VEML7700_POWER_ON;

    ESP_RETURN_ON_ERROR(veml7700_write_reg(VEML7700_REG_ALS_CONF, config),
                        TAG, "Nie udało się zapisać konfiguracji VEML7700");

    sensor_initialized = true;
    ESP_LOGI(TAG, "Czujnik VEML7700 gotowy (adres 0x%02X)", VEML7700_I2C_ADDRESS);
    return ESP_OK;
}

esp_err_t sensor_light_read(sensor_light_reading_t *reading)
{
    if (!sensor_initialized || reading == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint16_t raw_als = 0;
    ESP_RETURN_ON_ERROR(veml7700_read_reg(VEML7700_REG_ALS, &raw_als),
                        TAG, "Błąd odczytu ALS");

    uint16_t raw_white = 0;
    esp_err_t white_err = veml7700_read_reg(VEML7700_REG_WHITE, &raw_white);
    if (white_err != ESP_OK) {
        ESP_LOGW(TAG, "Błąd odczytu kanału WHITE: %s", esp_err_to_name(white_err));
        raw_white = 0;
    }

    reading->raw_als = raw_als;
    reading->raw_white = raw_white;
    reading->lux = veml7700_raw_to_lux(raw_als);

    return ESP_OK;
}

void light_sensor_task(void *param)
{
    if (sensor_light_init() != ESP_OK) {
        ESP_LOGE(TAG, "Nie udało się zainicjalizować VEML7700");
        vTaskDelete(NULL);
        return;
    }

    sensor_light_reading_t reading = {0};
    while (1) {
        if (sensor_light_read(&reading) == ESP_OK) {
            ESP_LOGI(TAG, "Lux=%.2f RAW_ALS=%u RAW_WHITE=%u",
                     reading.lux, reading.raw_als, reading.raw_white);
        } else {
            ESP_LOGW(TAG, "Nieudany odczyt z VEML7700");
        }
        vTaskDelay(pdMS_TO_TICKS(LIGHT_SENSOR_POLL_PERIOD_MS));
    }
}
