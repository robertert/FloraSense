
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

void app_main(void)
{
    nvs_init();
    mqtt_app_start(); 
    // init_ble();
    xTaskCreate(mqtt_publish_task, "mqtt_pub_task", 4096, NULL, 5, NULL);  

    // Zwiększony stack dla http_get_task - potrzebny dla operacji sieciowych
    //xTaskCreate(&http_get_task_raw, "http_get_task_raw", 8192, NULL, 5, NULL);

    xTaskCreate(wifi_init_task, "wifi_init_task", 4096, NULL, 5, NULL);
    //xTaskCreate(ble_server_task, "ble_server_task", 4096, NULL, 5, NULL);
    xTaskCreate(ble_client_task, "ble_client_task", 8192, NULL, 5, NULL);
    xTaskCreate(http_get_task_raw, "http_get_task_raw", 8192, NULL, 5, NULL);
}