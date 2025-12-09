
#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "esp_netif.h"
#include "mqtt_client.h" 
#include "lwip/err.h"
#include "lwip/sys.h"
#include "driver/gpio.h"
#include "flora_mqtt.h"
#include "config.h"
#include "wifi.h"
//#include "itag_client.h"
#include "http_client.h"

#include "ble_client.h"
#include "ble_server.h"
#include "sensors/sensor_soil.h"
#include "sensors/sensor_light.h"
#include "sensors/sensor_temp.h"



#include "bmp280.h"
#include "sensor_hall.h"
#include "sensor_dock.h"
#include "sensor_ir.h"
#include "mpu6050_test.h"
#include "mpu6050.h"
#include "esp_log.h"

void nvs_init(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

static void wifi_init_task(void *param)
{
    wifi_init();
    vTaskDelete(NULL);
}

static void ble_server_task(void *param)
{
    ble_server_init();
    vTaskDelete(NULL);
}

static void ble_client_task(void *param)
{
    init_ble();
    vTaskDelete(NULL);
}

static void soil_sensor_task(void *param)
{
    if (sensor_soil_init() != ESP_OK) {
        ESP_LOGE("soil_sensor_task", "Nie udało się zainicjalizować czujnika gleby");
        vTaskDelete(NULL);
        return;
    }

    sensor_soil_reading_t reading = {0};
    while (1) {
        if (sensor_soil_read(&reading) == ESP_OK) {
            ESP_LOGI("soil_sensor", "Raw=%d mV=%d Moisture=%.1f%%",
                     reading.raw_value, reading.millivolts, reading.moisture_percent);
        } else {
            ESP_LOGW("soil_sensor", "Błąd odczytu z czujnika");
        }
        vTaskDelay(pdMS_TO_TICKS(SOIL_SENSOR_POLL_PERIOD_MS));
    }
}

static char *TAG = "flora-sense";


static void sensor_hall_task(void *param)
{
    // Inicjalizacja czujnika Hall
    esp_err_t ret = sensor_hall_init();
    if (ret != ESP_OK) {
        ESP_LOGE("MAIN", "Nie udało się zainicjalizować czujnika Hall: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI("MAIN", "Task czujnika Hall uruchomiony");
    
    // Odczyt wartości co 500ms
    while (1) {
        if (sensor_hall_is_initialized()) {
            int hall_value = sensor_hall_read();
            if (hall_value >= 0) {
                ESP_LOGI("MAIN", "Hall Sensor: %d", hall_value);
            } else {
                ESP_LOGW("MAIN", "Błąd odczytu czujnika Hall");
            }
        } else {
            ESP_LOGW("MAIN", "Czujnik Hall nie jest zainicjalizowany");
        }
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

static void sensor_dock_task(void *param)
{
    // Inicjalizacja czujnika dock
    esp_err_t ret = sensor_dock_init();
    if (ret != ESP_OK) {
        ESP_LOGE("MAIN", "Nie udało się zainicjalizować czujnika dock: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI("MAIN", "Task czujnika dock uruchomiony");
    
    // Odczyt wartości co 500ms
    while (1) {
        if (sensor_dock_is_initialized()) {
            int dock_value = sensor_dock_read();
            if (dock_value >= 0) {
                ESP_LOGI("MAIN", "Dock Sensor: %d", dock_value);
            } else {
                ESP_LOGW("MAIN", "Błąd odczytu czujnika dock");
            }
        } else {
            ESP_LOGW("MAIN", "Czujnik dock nie jest zainicjalizowany");
        }
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

static void sensor_ir_task(void *param)
{
    // Inicjalizacja czujnika IR Obstacle
    esp_err_t ret = sensor_ir_init();
    if (ret != ESP_OK) {
        ESP_LOGE("MAIN", "Nie udało się zainicjalizować czujnika IR: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI("MAIN", "Task czujnika IR uruchomiony");
    
    // Odczyt wartości co 500ms
    while (1) {
        if (sensor_ir_is_initialized()) {
            int ir_value = sensor_ir_read();
            if (ir_value >= 0) {
                ESP_LOGI("MAIN", "IR Obstacle Sensor: %d", ir_value);
            } else {
                ESP_LOGW("MAIN", "Błąd odczytu czujnika IR");
            }
        } else {
            ESP_LOGW("MAIN", "Czujnik IR nie jest zainicjalizowany");
        }
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

void app_main(void)
{
    nvs_init();
    //wifi_init();                 
    //mqtt_app_start();
    //xTaskCreate(mqtt_publish_task, "mqtt_pub_task", 4096, NULL, 5, NULL);
    sensor_temp_init();
    sensor_temp_reading_t reading;
    sensor_temp_read(&reading);
    ESP_LOGI(TAG, "Temperature: %.2f°C, Humidity: %.2f%%", reading.temperature_c, reading.humidity_percent);
    ////xTaskCreate(wifi_init_task, "wifi_init_task", 4096, NULL, 5, NULL);
    //xTaskCreate(ble_server_task, "ble_server_task", 4096, NULL, 5, NULL);
    ////xTaskCreate(ble_client_task, "ble_client_task", 8192, NULL, 5, NULL);
    ////xTaskCreate(http_get_task_raw, "http_get_task_raw", 8192, NULL, 5, NULL);

    // Utworzenie taska dla czujnika Hall
    //xTaskCreate(sensor_hall_task, "sensor_hall_task", 2048, NULL, 5, NULL);

    // Utworzenie taska dla czujnika dock
    //xTaskCreate(sensor_dock_task, "sensor_dock_task", 2048, NULL, 5, NULL);

    // Utworzenie taska dla czujnika IR Obstacle
    //xTaskCreate(sensor_ir_task, "sensor_ir_task", 2048, NULL, 5, NULL);

    // Utworzenie taska demonstracyjnego dla MPU6050 (nowa biblioteka)
    mpu6050_test_start();
    /*
    // Inicjalizacja sensora BMP280 i pętla odczytu
    esp_err_t ret = bmp280_init();
    if (ret != ESP_OK) {
        ESP_LOGE("MAIN", "Nie udało się zainicjalizować BMP280: %s", esp_err_to_name(ret));
        ESP_LOGE("MAIN", "Program będzie kontynuowany, ale odczyty temperatury nie będą działać");
    }

    while (1) {
        if (bmp280_is_initialized()) {
            double temp = bmp280_read_temperature();
            if (temp > -900.0) {  // Sprawdź czy odczyt był poprawny
                ESP_LOGI("MAIN", "Temperature: %.2f C", temp);
            } else {
                ESP_LOGW("MAIN", "Błąd odczytu temperatury");
            }
        } else {
            ESP_LOGW("MAIN", "BMP280 nie jest zainicjalizowany - pomijam odczyt");
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    */

    
    //xTaskCreate(soil_sensor_task, "soil_sensor_task", 4096, NULL, 5, NULL);
    //xTaskCreate(light_sensor_task, "light_sensor_task", 4096, NULL, 5, NULL);
}