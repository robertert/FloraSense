
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
#include "motor_controller.h"
#include "wsn_controller.h"
#include "flora_mqtt.h"
#include "esp_log.h"
#include "dock_control.h"

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

// Testowy task do sprawdzania pompy i logowania czujnika Halla
static void dock_test_task(void *param)
{
    (void)param;
    
    ESP_LOGI(TAG, "Dock test task uruchomiony");
    
    // Poczekaj na inicjalizację dock_control
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    uint32_t test_cycle = 0;
    
    while (1) {
        // Loguj stan Halla
        uint8_t hall_state = dock_control_get_hall_state();
        bool pump_active = dock_control_is_pump_active();
        
        ESP_LOGI(TAG, "[TEST] Hall: %u, Pompa aktywna: %s", 
                 hall_state, pump_active ? "TAK" : "NIE");
        
        // Co 10 sekund testuj pompę (jeśli Hall == 1)
        if (test_cycle % 5 == 0) {  // 5 * 2s = 10s
            if (hall_state == 1) {
                ESP_LOGI(TAG, "[TEST] Uruchamianie testu pompy (2 sekundy)...");
                esp_err_t ret = dock_control_handle_water_command(2000); // 2 sekundy
                if (ret == ESP_OK) {
                    ESP_LOGI(TAG, "[TEST] Pompa uruchomiona pomyślnie");
                    
                    // Czekaj i sprawdzaj czy pompa działa
                    for (int i = 0; i < 10; i++) {
                        vTaskDelay(pdMS_TO_TICKS(200));
                        bool active = dock_control_is_pump_active();
                        ESP_LOGI(TAG, "[TEST] Pompa po %d ms: %s", 
                                 (i + 1) * 200, active ? "AKTYWNA" : "ZATRZYMANA");
                    }
                } else {
                    ESP_LOGW(TAG, "[TEST] Nie udało się uruchomić pompy: %s", 
                             esp_err_to_name(ret));
                }
            } else {
                ESP_LOGW(TAG, "[TEST] Pompa nie może być uruchomiona - Hall != 1 (aktualnie: %u)", 
                         hall_state);
            }
        }
        
        test_cycle++;
        vTaskDelay(pdMS_TO_TICKS(2000)); // Sprawdzaj co 2 sekundy
    }
}

void app_main(void)
{
    nvs_init();
    ESP_LOGI(TAG, "Start trybu stacji dokującej");

    // Zakomentowane stare taski (WiFi/MQTT/WSN) - pozostawione do ewentualnego użycia
#if 0
    xTaskCreate(wifi_init_task, "wifi_init_task", 4096, NULL, 5, NULL);
    vTaskDelay(pdMS_TO_TICKS(2000));
    mqtt_app_start();
    xTaskCreate(mqtt_publish_task, "mqtt_pub_task", 4096, NULL, 5, NULL);
    vTaskDelay(pdMS_TO_TICKS(3000));

    static obstacle_monitor_config_t obstacle_config;
    obstacle_config.ir_sensor_front = mqtt_get_ir_sensor_pin_2();
    obstacle_config.ir_sensor_back = mqtt_get_ir_sensor_pin_1();
    xTaskCreate(obstacle_monitor_task, "obstacle_monitor_task", 2048,
                &obstacle_config, 10, NULL);

    static wsn_controller_config_t wsn_config;
    wsn_config.light_sensor_1 = mqtt_get_light_config_2();
    wsn_config.light_sensor_2 = mqtt_get_light_config_1();
    wsn_config.ir_sensor_front = mqtt_get_ir_sensor_pin_2();
    wsn_config.base_speed = 150;
    wsn_config.light_threshold_lux = 10.0f;
    wsn_config.check_interval_ms = 200;
    wsn_config.move_distance_cm = 5.0f;
    wsn_config.wait_after_threshold_ms = 5 * 60 * 1000;
    xTaskCreate(wsn_controller_task, "wsn_controller_task", 4096, &wsn_config, 5, NULL);

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
    
    // Task monitorujący czujniki Hall i dock oraz sterujący LED
    //xTaskCreate(sensor_trigger_led_task, "sensor_trigger_led_task", 2048, NULL, 5, NULL);

    // Utworzenie taska dla czujnika IR Obstacle (GPIO 23)
    //xTaskCreate(sensor_ir_task, "sensor_ir_task", 2048, (void*)(intptr_t)25, 5, NULL);
    //xTaskCreate(sensor_ir_task, "sensor_ir_task", 2048, (void*)(intptr_t)26, 5, NULL);
    
    // Utworzenie taska demonstracyjnego dla MPU6050 (nowa biblioteka)
    //mpu6050_test_start();

    //xTaskCreate(soil_sensor_task, "soil_sensor_task", 4096, NULL, 5, NULL);

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
#endif

    // Inicjalizacja stacji dokującej (Hall + MOSFET/pompa) oraz BLE
    esp_err_t dock_err = dock_control_init(DOCK_HALL_GPIO, DOCK_PUMP_GPIO, ble_server_set_hall_state);
    if (dock_err != ESP_OK) {
        ESP_LOGE(TAG, "Init dock_control nieudany: %s", esp_err_to_name(dock_err));
    }

    ble_server_register_handlers(dock_control_handle_water_command);
    ble_server_set_hall_state(dock_control_get_hall_state());
    ble_server_init();
    
    // Testowy task do sprawdzania pompy i logowania Halla
    xTaskCreate(dock_test_task, "dock_test_task", 4096, NULL, 5, NULL);
}