
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

#include "lwip/err.h"
#include "lwip/sys.h"

#include "config.h"
#include "wifi.h"
#include "http_client.h"

#include "ble_client.h"
#include "ble_server.h"
#include "sensors/sensor_soil.h"
#include "sensors/sensor_light.h"


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


void app_main(void)
{
    nvs_init();

    //xTaskCreate(wifi_init_task, "wifi_init_task", 4096, NULL, 5, NULL);
    //xTaskCreate(ble_server_task, "ble_server_task", 4096, NULL, 5, NULL);
    //xTaskCreate(ble_client_task, "ble_client_task", 8192, NULL, 5, NULL);
    //xTaskCreate(http_get_task_raw, "http_get_task_raw", 8192, NULL, 5, NULL);
    //xTaskCreate(soil_sensor_task, "soil_sensor_task", 4096, NULL, 5, NULL);
    //xTaskCreate(light_sensor_task, "light_sensor_task", 4096, NULL, 5, NULL);
}