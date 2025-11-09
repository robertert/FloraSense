
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
#include "driver/gpio.h"

#include "config.h"
#include "wifi.h"


void app_main(void)
{
    wifi_init();

}
