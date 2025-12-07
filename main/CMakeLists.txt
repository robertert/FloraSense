idf_component_register(
    SRCS
        "access-point.c"
    INCLUDE_DIRS
        "." "sensors"
    REQUIRES
        bt
        nvs_flash
        esp_http_client
        esp_http_server
        esp_netif
        freertos
        driver
        esp_driver_gpio
        esp_wifi
        lwip
        mqtt
)
