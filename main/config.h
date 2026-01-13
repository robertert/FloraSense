#pragma once

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/i2c.h"

#define EXAMPLE_ESP_WIFI_SSID      "iPhone Robert"
#define EXAMPLE_ESP_WIFI_PASS      "SWER1234"
#define EXAMPLE_ESP_MAXIMUM_RETRY  10000
#define BLINK_GPIO 2

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// Konfiguracja czujnika gleby (pojemnościowy analog)
#define SOIL_SENSOR_ADC_UNIT            ADC_UNIT_1
#define SOIL_SENSOR_ADC_CHANNEL         ADC_CHANNEL_6   // GPIO34
#define SOIL_SENSOR_ADC_ATTEN           ADC_ATTEN_DB_12
#define SOIL_SENSOR_ADC_BITWIDTH        ADC_BITWIDTH_12
#define SOIL_SENSOR_SAMPLE_COUNT        32
#define SOIL_SENSOR_POLL_PERIOD_MS      2000

// Kalibracja mapowania napięcia (mV)
#define SOIL_SENSOR_MV_AT_DRY           3000
#define SOIL_SENSOR_MV_AT_SATURATED     1100

// Konfiguracja monitorowania baterii (ADC)
// UWAGA: Jeśli bateria jest podłączona przez dzielnik napięcia, 
// ustaw BATTERY_VOLTAGE_DIVIDER na odpowiednią wartość (np. 2.0 dla dzielnika 1:1)
// Jeśli bateria jest podłączona bezpośrednio, ustaw na 1.0
#define BATTERY_ADC_UNIT                ADC_UNIT_1
#define BATTERY_ADC_CHANNEL              ADC_CHANNEL_7   // GPIO35 (można zmienić na inny wolny pin ADC)
#define BATTERY_ADC_ATTEN                ADC_ATTEN_DB_12  // Max 3.3V na ADC
#define BATTERY_ADC_BITWIDTH             ADC_BITWIDTH_12
#define BATTERY_SAMPLE_COUNT             32
#define BATTERY_VOLTAGE_DIVIDER          3.0f  // Współczynnik dzielnika napięcia (3.0 dla dzielnika 1:3)

// Kalibracja mapowania napięcia baterii (mV)
// Dla LiPo 3.7V: pełna 4.2V (4200mV), pusta 3.0V (3000mV)
// Dla LiPo 7.4V: pełna 8.4V (8400mV), pusta 6.0V (6000mV)
// UWAGA: Te wartości są PRZED uwzględnieniem dzielnika napięcia!
// Kod automatycznie przeliczy rzeczywiste napięcie baterii mnożąc odczyt ADC przez BATTERY_VOLTAGE_DIVIDER
#define BATTERY_MV_AT_FULL              8400  // Napięcie pełnej baterii 7.4V (mV) - PRZED dzielnikiem
#define BATTERY_MV_AT_EMPTY             6000  // Napięcie pustej baterii 7.4V (mV) - PRZED dzielnikiem

// Konfiguracja LED ostrzegawczego dla niskiego poziomu baterii
#define BATTERY_LOW_LED_GPIO             GPIO_NUM_23  // GPIO dla LED (można zmienić na inny wolny pin)
#define BATTERY_LOW_THRESHOLD_PERCENT    60.0f        // Próg niskiego poziomu baterii (w %)
#define BATTERY_LED_BLINK_INTERVAL_MS    500          // Interwał migania LED (ms)

// Konfiguracja czujnika światła (I2C, np. BH1750)
#define LIGHT_SENSOR_I2C_PORT           I2C_NUM_0
#define LIGHT_SENSOR_I2C_SDA_GPIO         GPIO_NUM_21
#define LIGHT_SENSOR_I2C_SCL_GPIO         GPIO_NUM_22
#define LIGHT_SENSOR_I2C_FREQ_HZ          50000
#define VEML7700_I2C_ADDRESS              0x10 // 0x48
#define VEML7700_REG_ALS_CONF             0x00
#define VEML7700_REG_ALS_WH               0x01
#define VEML7700_REG_ALS_WL               0x02
#define VEML7700_REG_POWER_SAVING         0x03
#define VEML7700_REG_ALS                  0x04
#define VEML7700_REG_WHITE                0x05
#define VEML7700_REG_ALS_INT              0x06

#define VEML7700_GAIN_1X                  0x00
#define VEML7700_GAIN_2X                  0x01
#define VEML7700_GAIN_1_DIV_8             0x02
#define VEML7700_GAIN_1_DIV_4             0x03

#define VEML7700_IT_25MS                  0x0C
#define VEML7700_IT_50MS                  0x08
#define VEML7700_IT_100MS                 0x00
#define VEML7700_IT_200MS                 0x01
#define VEML7700_IT_400MS                 0x02
#define VEML7700_IT_800MS                 0x03

#define VEML7700_PERSISTENCE_1            0x00
#define VEML7700_INT_DISABLE              0x00
#define VEML7700_INT_ENABLE               0x01
#define VEML7700_POWER_ON                 0x00
#define VEML7700_POWER_OFF                0x01

#define VEML7700_DEFAULT_GAIN             VEML7700_GAIN_1X
#define VEML7700_DEFAULT_IT               VEML7700_IT_100MS
#define LIGHT_SENSOR_POLL_PERIOD_MS       3000

// Konfiguracja czujnika temperatury/wilgotności/ciśnienia (I2C, BME280)
#define TEMP_SENSOR_I2C_PORT              I2C_NUM_0  // Ten sam port co sensor światła
#define TEMP_SENSOR_I2C_SDA_GPIO          GPIO_NUM_21
#define TEMP_SENSOR_I2C_SCL_GPIO          GPIO_NUM_22
#define TEMP_SENSOR_I2C_FREQ_HZ           50000  // Zmniejszona częstotliwość dla lepszej stabilności
#define BME280_I2C_ADDRESS                0x76  // GND=0x76, VCC=0x77
#define BME280_CHIP_ID                    0x60
#define TEMP_SENSOR_POLL_PERIOD_MS        5000

// Konfiguracja sterownika silników (TB6612FNG lub podobny)
// Silnik A
#define MOTOR_A_PWM_GPIO                  GPIO_NUM_13  // PWMA - prędkość silnika A
#define MOTOR_A_IN1_GPIO                  GPIO_NUM_27   // AIN1 - kierunek silnika A
#define MOTOR_A_IN2_GPIO                  GPIO_NUM_14   // AIN2 - kierunek silnika A

// Kalibracja silników (do ustawienia na podstawie testów)
// Ile centymetrów robot przejeżdża w ciągu 1 sekundy przy prędkości 128
#define MOTOR_CM_PER_SECOND_128          24.0f  // TODO: Skalibrować!
// Silnik B
#define MOTOR_B_PWM_GPIO                  GPIO_NUM_4   // PWMB - prędkość silnika B
#define MOTOR_B_IN1_GPIO                  GPIO_NUM_16  // BIN1 - kierunek silnika B
#define MOTOR_B_IN2_GPIO                  GPIO_NUM_17  // BIN2 - kierunek silnika B
// PWM
#define MOTOR_PWM_FREQ_HZ                 5000         // Częstotliwość PWM
#define MOTOR_PWM_RESOLUTION              LEDC_TIMER_8_BIT  // Rozdzielczość PWM (0-255)

// Konfiguracja stopniowego zwiększania prędkości (ramp-up)
#define MOTOR_RAMP_UP_STEP                5           // Krok zwiększania prędkości przy każdym wywołaniu
#define MOTOR_RAMP_UP_INTERVAL_MS         20          // Interwał aktualizacji prędkości podczas przyspieszania (ms)