
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
#include "esp_netif.h"
#include "mqtt_client.h" 
#include "lwip/err.h"
#include "lwip/sys.h"
#include "driver/gpio.h"
#include "flora_mqtt.h"
#include "config.h"
#include "wifi.h"
#include "sensors/sensor_soil.h"
#include "sensors/sensor_light.h"
#include "sensors/sensor_temp.h"
#include "sensor_hall.h"
#include "sensor_dock.h"
#include "sensor_ir.h"
#include "motor_controller.h"
#include "wsn_controller.h"
#include "water_controller.h"
#include "ble_dock_client.h"
#include "flora_mqtt.h"
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

// Task monitorujący czujniki Hall i dock oraz sterujący LED
static void sensor_trigger_led_task(void *param)
{
    // Inicjalizacja czujników
    esp_err_t ret_hall = sensor_hall_init();
    esp_err_t ret_dock = sensor_dock_init();
    
    if (ret_hall != ESP_OK && ret_dock != ESP_OK) {
        ESP_LOGE("MAIN", "Nie udało się zainicjalizować żadnego z czujników");
        vTaskDelete(NULL);
        return;
    }
    
    // Konfiguracja LED (GPIO 2 - standardowy LED na ESP32)
    gpio_config_t io_conf_led = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << BLINK_GPIO),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    gpio_config(&io_conf_led);
    gpio_set_level(BLINK_GPIO, 0); // Wyłącz LED na starcie
    
    ESP_LOGI("MAIN", "Task monitorowania czujników i LED uruchomiony (LED na GPIO %d)", BLINK_GPIO);
    
    int last_hall = -1;
    int last_dock = -1;
    
    // Monitorowanie czujników co 100ms
    while (1) {
        bool led_on = false;
        
        // Sprawdź czujnik Hall
        if (sensor_hall_is_initialized()) {
            int hall_value = sensor_hall_read();
            if (hall_value >= 0) {
                if (hall_value != last_hall) {
                    ESP_LOGI("MAIN", "Hall Sensor zmiana: %d -> %d", last_hall, hall_value);
                    last_hall = hall_value;
                }
                if (hall_value == 0) {
                    led_on = true;
                }
            }
        }
        
        // Sprawdź czujnik dock
        if (sensor_dock_is_initialized()) {
            int dock_value = sensor_dock_read();
            if (dock_value >= 0) {
                if (dock_value != last_dock) {
                    ESP_LOGI("MAIN", "Dock Sensor zmiana: %d -> %d", last_dock, dock_value);
                    last_dock = dock_value;
                }
                if (dock_value == 0) {
                    led_on = true;
                }
            }
        }
        
        // Sterowanie LED - zapal jeśli którykolwiek czujnik jest aktywny
        gpio_set_level(BLINK_GPIO, led_on ? 1 : 0);
        
        vTaskDelay(pdMS_TO_TICKS(100)); // Sprawdzaj co 100ms
    }
}

static void sensor_ir_task(void *param)
{
    gpio_num_t ir_pin = (gpio_num_t)(intptr_t)param;
    
    // Inicjalizacja czujnika IR Obstacle
    esp_err_t ret = sensor_ir_init(ir_pin);
    if (ret != ESP_OK) {
        ESP_LOGE("MAIN", "Nie udało się zainicjalizować czujnika IR na pinie %d: %s", ir_pin, esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI("MAIN", "Task czujnika IR uruchomiony (GPIO %d)", ir_pin);
    
    // Odczyt wartości co 500ms
    while (1) {
        if (sensor_ir_is_initialized(ir_pin)) {
            int ir_value = sensor_ir_read(ir_pin);
            if (ir_value >= 0) {
                ESP_LOGI("MAIN", "IR Obstacle Sensor (GPIO %d): %d", ir_pin, ir_value);
            } else {
                ESP_LOGW("MAIN", "Błąd odczytu czujnika IR (GPIO %d)", ir_pin);
            }
        } else {
            ESP_LOGW("MAIN", "Czujnik IR na pinie %d nie jest zainicjalizowany", ir_pin);
        }
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

// Task demonstracyjny dla sterownika silników
static void motor_controller_task(void *param)
{
    // Inicjalizacja sterownika silników
    esp_err_t ret = motor_controller_init();
    if (ret != ESP_OK) {
        ESP_LOGE("MAIN", "Nie udało się zainicjalizować sterownika silników: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI("MAIN", "Task sterownika silników uruchomiony");
    
    // Przykładowa sekwencja testowa
    while (1) {
        if (motor_controller_is_initialized()) {
            ESP_LOGI("MAIN", "Test silników: Oba do przodu (prędkość 150)");
            motor_controller_set_speeds(128, 128);
            vTaskDelay(pdMS_TO_TICKS(400));
            
            ESP_LOGI("MAIN", "Test silników: Stop");
            motor_controller_stop();
            vTaskDelay(pdMS_TO_TICKS(1000));
            
            ESP_LOGI("MAIN", "Test silników: Oba do tyłu (prędkość 150)");
            motor_controller_set_speeds(-128, -128);
            vTaskDelay(pdMS_TO_TICKS(400));
            
            ESP_LOGI("MAIN", "Test silników: Stop");
            motor_controller_stop();
            vTaskDelay(pdMS_TO_TICKS(1000));
            
            ESP_LOGI("MAIN", "Test silników: Stop");
            motor_controller_stop();
            vTaskDelay(pdMS_TO_TICKS(5000)); // Długa pauza przed następnym cyklem
        } else {
            ESP_LOGW("MAIN", "Sterownik silników nie jest zainicjalizowany");
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

void app_main(void)
{
    nvs_init();
    
    // Inicjalizacja WiFi (w osobnym tasku)
    xTaskCreate(wifi_init_task, "wifi_init_task", 3072, NULL, 5, NULL);
    
    // Poczekaj chwilę na inicjalizację WiFi przed uruchomieniem MQTT
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Inicjalizacja i start MQTT
    mqtt_app_start();
    
    // Task publikujący dane z czujników przez MQTT
    // Priorytet 7 - wyższy niż WSN (5) aby MQTT nie był blokowany przez operacje silnika
    xTaskCreate(mqtt_publish_task, "mqtt_pub_task", 3072, NULL, 7, NULL);
    
    // Poczekaj na inicjalizację czujników przed uruchomieniem WSN
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // Task monitorowania przeszkód - niezależny, najwyższy priorytet bezpieczeństwa
    // Zatrzymuje silniki gdy wykryje przeszkodę, niezależnie od sterowania MQTT lub WSN
    // Sprawdza oba czujniki IR (przód i tył)
    static obstacle_monitor_config_t obstacle_config;
    obstacle_config.ir_sensor_front = mqtt_get_ir_sensor_pin_2();  // Czujnik IR 2 z przodu
    obstacle_config.ir_sensor_back = mqtt_get_ir_sensor_pin_1();   // Czujnik IR 1 z tyłu
    xTaskCreate(obstacle_monitor_task, "obstacle_monitor_task", 2048, 
                &obstacle_config, 10, NULL);  // Priorytet 10 - najwyższy
    
    // Task kontrolera WSN - autonomiczne poruszanie się w kierunku światła
    // Uwaga: czujniki są fizycznie zamontowane odwrotnie niż w konfiguracji
    static wsn_controller_config_t wsn_config;
    wsn_config.light_sensor_1 = mqtt_get_light_config_2();  // Fizycznie z przodu
    wsn_config.light_sensor_2 = mqtt_get_light_config_1();  // Fizycznie z tyłu
    wsn_config.ir_sensor_front = mqtt_get_ir_sensor_pin_2();  // Czujnik IR 2 z przodu
    wsn_config.base_speed = 128;                              // Bazowa prędkość silników
    wsn_config.light_threshold_lux = 10.0f;                   // Próg różnicy światła (10 lux)
    wsn_config.check_interval_ms = 200;                       // Sprawdzanie co 200ms
    wsn_config.move_distance_cm = 2.5f;                       // Przesunięcie o 5cm
    wsn_config.wait_after_threshold_ms = 5 * 60 * 1000;      // Czekaj 5 minut po osiągnięciu progu
    xTaskCreate(wsn_controller_task, "wsn_controller_task", 3072, &wsn_config, 5, NULL);

    // Task obsługujący żądania light_search z MQTT (nieblokujący dla MQTT event handlera)
    xTaskCreate(light_search_task, "light_search_task", 4096, NULL, 5, NULL);

    // Task kontrolera automatycznego podlewania
    xTaskCreate(water_controller_task, "water_controller_task", 3072, NULL, 4, NULL);

    // Klient dokowania BLE (połączenie z FloraDock do podlewania)
    ble_dock_client_init();
    /*
    sensor_temp_init();
    sensor_temp_reading_t reading;
    sensor_temp_read(&reading);
    ESP_LOGI(TAG, "Temperature: %.2f°C, Humidity: %.2f%%", reading.temperature_c, reading.humidity_percent);
    */
    ////xTaskCreate(wifi_init_task, "wifi_init_task", 4096, NULL, 5, NULL);
    //xTaskCreate(ble_server_task, "ble_server_task", 4096, NULL, 5, NULL);
    ////xTaskCreate(ble_client_task, "ble_client_task", 8192, NULL, 5, NULL);
    ////xTaskCreate(http_get_task_raw, "http_get_task_raw", 8192, NULL, 5, NULL);


    
    // Utworzenie taska dla czujnika Hall
    //xTaskCreate(sensor_hall_task, "sensor_hall_task", 2048, NULL, 5, NULL);

    // Utworzenie taska dla czujnika dock
    //xTaskCreate(sensor_dock_task, "sensor_dock_task", 2048, NULL, 5, NULL);
    
    // Task monitorujący czujniki Hall i dock oraz sterujący LED
    //xTaskCreate(sensor_trigger_led_task, "sensor_trigger_led_task", 2048, NULL, 5, NULL);

    // Utworzenie taska dla czujnika IR Obstacle (GPIO 23)
    //xTaskCreate(sensor_ir_task, "sensor_ir_task", 2048, (void*)(intptr_t)25, 5, NULL);
    //xTaskCreate(sensor_ir_task, "sensor_ir_task", 2048, (void*)(intptr_t)26, 5, NULL);
    
    // Utworzenie taska demonstracyjnego dla MPU6050 (nowa biblioteka)
    //mpu6050_test_start();

    //xTaskCreate(soil_sensor_task, "soil_sensor_task", 4096, NULL, 5, NULL);
    /*
    xTaskCreate(sensor_temp_task, "sensor_temp_task", 4096, NULL, 5, NULL);

    // Utworzenie tasków dla czujników światła VEML7700
    // Czujnik 1: Port I2C 0, piny standardowe (21, 22)
    static sensor_light_config_t light_config_1 = {
        .i2c_port = I2C_NUM_0,
        .sda_pin = GPIO_NUM_21,
        .scl_pin = GPIO_NUM_22,
        .i2c_freq_hz = 50000,
        .i2c_address = 0x10
    };
    xTaskCreate(light_sensor_task, "light_sensor_task_1", 4096, &light_config_1, 5, NULL);
    vTaskDelay(pdMS_TO_TICKS(100)); // Krótkie opóźnienie między inicjalizacjami
    
    // Czujnik 2: Port I2C 1, piny 32, 34
    static sensor_light_config_t light_config_2 = {
        .i2c_port = I2C_NUM_1,
        .sda_pin = GPIO_NUM_32,
        .scl_pin = GPIO_NUM_33,
        .i2c_freq_hz = 50000,
        .i2c_address = 0x10
    };
    xTaskCreate(light_sensor_task, "light_sensor_task_2", 4096, &light_config_2, 5, NULL);
    vTaskDelay(pdMS_TO_TICKS(100)); // Krótkie opóźnienie między inicjalizacjami
    
    // Utworzenie taska dla czujnika temperatury BME280
    //xTaskCreate(sensor_temp_task, "sensor_temp_task", 4096, NULL, 5, NULL);
    
    // Utworzenie taska dla sterownika silników
    
    xTaskCreate(motor_controller_task, "motor_controller_task", 4096, NULL, 5, NULL);
    */
}