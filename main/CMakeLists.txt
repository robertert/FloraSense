idf_component_register(
    SRCS "flora-sense.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES freertos esp_system esp_wifi esp_event nvs_flash esp_http_client esp_netif driver lwip
)