#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_http_server.h"
#include "esp_random.h"
#include <string.h>
#include "esp_log.h"
#include "mbedtls/gcm.h"

#define TAG "CUSTOM_CONFIG"
#define GPIO_RESET_BUTTON 0 
#define CONFIG_AP_SSID "ESP32_SETUP"

typedef struct {
    char ssid[32];
    char password[64];
    char custom_param[32];
} app_config_t;

app_config_t current_config;

void generate_password_from_noise(char *buffer, size_t len) {
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*";
    size_t charset_len = strlen(charset);
    uint8_t *random_bytes = malloc(len);
    if (random_bytes == NULL) {
        ESP_LOGE(TAG, "Błąd alokacji pamięci");
        return;
    }
    esp_fill_random(random_bytes, len);
    for (size_t i = 0; i < len; i++) {
        buffer[i] = charset[random_bytes[i] % charset_len];
    }
    buffer[len] = '\0';

    free(random_bytes);
}


void save_config_to_nvs(app_config_t *cfg) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        nvs_set_str(my_handle, "ssid", cfg->ssid);
        nvs_set_str(my_handle, "pass", cfg->password);
        nvs_set_str(my_handle, "custom", cfg->custom_param);
        nvs_set_u8(my_handle, "configured", 1);
        nvs_commit(my_handle);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "Konfiguracja zapisana do NVS.");
    } else {
        ESP_LOGE(TAG, "Blad otwarcia NVS!");
    }
}

bool load_config_from_nvs(app_config_t *cfg) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
    if (err != ESP_OK) return false;

    uint8_t configured = 0;
    nvs_get_u8(my_handle, "configured", &configured);
    
    if (configured == 1) {
        size_t required_size = sizeof(cfg->ssid);
        nvs_get_str(my_handle, "ssid", cfg->ssid, &required_size);
        
        required_size = sizeof(cfg->password);
        nvs_get_str(my_handle, "pass", cfg->password, &required_size);

        required_size = sizeof(cfg->custom_param);
        if(nvs_get_str(my_handle, "custom", cfg->custom_param, &required_size) != ESP_OK) {
            strcpy(cfg->custom_param, "");
        }

        nvs_close(my_handle);
        return true;
    }
    nvs_close(my_handle);
    return false;
}

void clear_nvs_config() {
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_erase_all(my_handle);
        nvs_commit(my_handle);
        nvs_close(my_handle);
        ESP_LOGW(TAG, "NVS wyczyszczony.");
    }
}
const char* html_form = 
    "<!DOCTYPE html><html><body>"
    "<h2>Konfiguracja ESP32</h2>"
    "<form action=\"/save\" method=\"post\">"
    "SSID:<br><input type=\"text\" name=\"ssid\"><br>"
    "Haslo:<br><input type=\"text\" name=\"pass\"><br>"
    "Custom Param:<br><input type=\"text\" name=\"custom\"><br><br>"
    "<input type=\"submit\" value=\"Zapisz i Restartuj\">"
    "</form></body></html>";

esp_err_t root_get_handler(httpd_req_t *req) {
    httpd_resp_send(req, html_form, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

void url_decode(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if ((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a') a -= 'a'-'A';
            if (a >= 'A') a -= ('A' - 10); else a -= '0';
            if (b >= 'a') b -= 'a'-'A';
            if (b >= 'A') b -= ('A' - 10); else b -= '0';
            *dst++ = 16*a+b;
            src+=3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

void get_post_val(char *buf, const char *key, char *out_val, int max_len) {
    char *found = strstr(buf, key);
    if (found) {
        found += strlen(key) + 1;
        char *end = strchr(found, '&');
        int len = end ? (end - found) : strlen(found);
        if (len >= max_len) len = max_len - 1;
        
        char temp[128];
        strncpy(temp, found, len);
        temp[len] = 0;
        url_decode(out_val, temp);
    }
}

esp_err_t save_post_handler(httpd_req_t *req) {
    char buf[200];
    int ret, remaining = req->content_len;

    if (remaining >= sizeof(buf)) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    if ((ret = httpd_req_recv(req, buf, remaining)) <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    buf[remaining] = '\0';

    app_config_t new_config = {0};
    get_post_val(buf, "ssid", new_config.ssid, sizeof(new_config.ssid));
    get_post_val(buf, "pass", new_config.password, sizeof(new_config.password));
    get_post_val(buf, "custom", new_config.custom_param, sizeof(new_config.custom_param));
    ESP_LOGI(TAG, "Otrzymano: SSID=%s, Pass=%s", new_config.ssid, new_config.password);
    save_config_to_nvs(&new_config);
    httpd_resp_send(req, "Ustawienia zapisane. Restartowanie...", HTTPD_RESP_USE_STRLEN);
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    return ESP_OK;
}

void start_webserver() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root_uri = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
        httpd_register_uri_handler(server, &root_uri);

        httpd_uri_t save_uri = { .uri = "/save", .method = HTTP_POST, .handler = save_post_handler };
        httpd_register_uri_handler(server, &save_uri);
        
        ESP_LOGI(TAG, "Serwer HTTP uruchomiony na porcie 80");
    }
}

int tries = 0;
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Rozlaczono z WiFi, ponawiam...");
        if(tries<5){
            esp_wifi_connect();
            tries++;
        }
        else
        {
            ESP_LOGI(TAG, "Nie znaleziono sieci Wi-Fi o podanym SSID i haśle.");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Polaczono! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Custom Param z NVS: %s", current_config.custom_param);
    }
}

void start_wifi_ap() {
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    char pass[11];
    generate_password_from_noise(pass,10);
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = CONFIG_AP_SSID,
            .ssid_len = strlen(CONFIG_AP_SSID),
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    strlcpy((char*)wifi_config.ap.password, pass, sizeof(wifi_config.ap.password));
    if (strlen(pass) == 0) wifi_config.ap.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi AP uruchomione. SSID: %s, IP: 192.168.4.1 HASŁO: %s", CONFIG_AP_SSID,pass);
}

void start_wifi_sta() {
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);

    wifi_config_t wifi_config = {0};
    strcpy((char*)wifi_config.sta.ssid, current_config.ssid);
    strcpy((char*)wifi_config.sta.password, current_config.password);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void app_main(void) {
    const esp_partition_t *key_part = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, 
            ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS, 
            "nvs_key");

    if (key_part == NULL) {
        ESP_LOGE(TAG, "BŁĄD: Nie znaleziono partycji 'nvs_key' w tablicy partycji! Sprawdź partitions.csv");
        return; // Nie ma sensu iść dalej bez kluczy
    }

    nvs_sec_cfg_t cfg;
    esp_err_t err = nvs_flash_read_security_cfg(key_part, &cfg);
    
    if (err == ESP_ERR_NVS_NOT_INITIALIZED || err == ESP_ERR_NVS_KEYS_NOT_INITIALIZED) {
        ESP_LOGI(TAG, "Klucze NVS nie istnieją. Generowanie nowych...");
        err = nvs_flash_generate_keys(key_part, &cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Błąd generowania kluczy NVS: %s", esp_err_to_name(err));
            return;
        }
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Błąd odczytu kluczy NVS: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_flash_secure_init(&cfg);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS uszkodzony/niekompatybilny. Czyszczenie...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_secure_init(&cfg);
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());


    bool button_pressed = false;

    for (int i = 0; i < 30; i++) {
        gpio_set_direction(GPIO_RESET_BUTTON, GPIO_MODE_INPUT);
        if (gpio_get_level(GPIO_RESET_BUTTON) == 0) {
            button_pressed = true;
            ESP_LOGW(TAG, "Wykryto przycisk! Wchodze w tryb AP...");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    bool has_config = load_config_from_nvs(&current_config);

    if (button_pressed || !has_config) {
        start_wifi_ap();
        start_webserver();
    } else {
        ESP_LOGI(TAG, "Konfiguracja znaleziona. Uruchamianie WiFi STA.");
        start_wifi_sta();
    }
}