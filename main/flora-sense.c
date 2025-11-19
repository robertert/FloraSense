
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

void app_main(void)
{
    nvs_init();
    wifi_init();
    init_ble();
    // Zwiększony stack dla http_get_task - potrzebny dla operacji sieciowych
    xTaskCreate(&http_get_task_raw, "http_get_task_raw", 8192, NULL, 5, NULL);


}