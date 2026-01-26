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
#include "esp_https_server.h"
#include "esp_random.h"
#include <string.h>
#include "esp_log.h"
#include "mbedtls/gcm.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/rsa.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509_csr.h"
#include "esp_mac.h"

#define TAG "CUSTOM_CONFIG"
#define GPIO_RESET_BUTTON 0 
#define CONFIG_AP_SSID "ESP32_SETUP"

char *server_cert_pem = NULL;
char *server_key_pem = NULL;

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

esp_err_t save_str_to_nvs(const char *key, const char *value) {
    if (key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, key, value);
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    return err;
}

esp_err_t load_str_from_nvs(const char *key, char **out_value) {
    if (key == NULL || out_value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_value = NULL;

    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &handle);
    if (err != ESP_OK) return err;

    size_t required_size = 0;
    err = nvs_get_str(handle, key, NULL, &required_size);
    
    if (err == ESP_OK) {
        char *buffer = malloc(required_size);
        if (buffer) {
            err = nvs_get_str(handle, key, buffer, &required_size);
            if (err == ESP_OK) {
                *out_value = buffer;
            } else {
                free(buffer);
            }
        } else {
            err = ESP_ERR_NO_MEM;
        }
    }
    
    nvs_close(handle);
    return err;
}

void generate_certificates() {
    ESP_LOGW(TAG, "GEN: Generowanie kluczy RSA (moze potrwac do 20s)...");
    
    int ret;
    mbedtls_pk_context key;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_x509write_cert crt;

    mbedtls_pk_init(&key);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_entropy_init(&entropy);
    mbedtls_x509write_crt_init(&crt);

    if ((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)"ESP_V3", 6)) != 0) {
        ESP_LOGE(TAG, "GEN: Blad DRBG");
    }

    ESP_LOGI(TAG, "GEN: Obliczam RSA...");
    if ((ret = mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA))) != 0 ||
        (ret = mbedtls_rsa_gen_key(mbedtls_pk_rsa(key), mbedtls_ctr_drbg_random, &ctr_drbg, 2048, 65537)) != 0) {
        ESP_LOGE(TAG, "GEN: Blad RSA -0x%x", -ret);
        return;
    }

    ESP_LOGI(TAG, "GEN: Podpisywanie...");
    const char *subject_name = "CN=ESP32,O=IoT,C=PL";

    mbedtls_x509write_crt_set_subject_name(&crt, subject_name);
    mbedtls_x509write_crt_set_issuer_name(&crt, subject_name); 
    mbedtls_x509write_crt_set_issuer_key(&crt, &key);
    mbedtls_x509write_crt_set_subject_key(&crt, &key);
    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_validity(&crt, "20230101000000", "20351231235959");

    unsigned char serial[] = {0x01}; 
    mbedtls_x509write_crt_set_serial_raw(&crt, serial, 1);

    mbedtls_x509write_crt_set_basic_constraints(&crt, 0, -1);
    mbedtls_x509write_crt_set_subject_key_identifier(&crt);
    mbedtls_x509write_crt_set_authority_key_identifier(&crt);

    size_t buf_len = 4096;
    char *key_buf = calloc(1, buf_len);
    char *crt_buf = calloc(1, buf_len);

    mbedtls_pk_write_key_pem(&key, (unsigned char*)key_buf, buf_len);
    mbedtls_x509write_crt_pem(&crt, (unsigned char*)crt_buf, buf_len, mbedtls_ctr_drbg_random, &ctr_drbg);

    printf("%s\n", crt_buf);

    ESP_LOGI(TAG, "GEN: Zapis do NVS...");
    esp_err_t e1 = save_str_to_nvs("srv_key", key_buf);
    esp_err_t e2 = save_str_to_nvs("srv_cert", crt_buf);

    if (e1 == ESP_OK && e2 == ESP_OK) {
        ESP_LOGI(TAG, "GEN: Zapis zakonczony sukcesem!");
    } else {
        ESP_LOGE(TAG, "GEN: Blad zapisu do NVS! Key: %s, Cert: %s", esp_err_to_name(e1), esp_err_to_name(e2));
    }
    
    free(key_buf);
    free(crt_buf);
    mbedtls_pk_free(&key);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    mbedtls_x509write_crt_free(&crt);
}


void ensure_certificates_exist() {
    if (load_str_from_nvs("srv_cert", &server_cert_pem) != ESP_OK || 
        load_str_from_nvs("srv_key", &server_key_pem) != ESP_OK) {
        
        ESP_LOGW(TAG, "Brak kluczy. Generuje nowe...");
        if (server_cert_pem) free(server_cert_pem);
        if (server_key_pem) free(server_key_pem);
        
        generate_certificates();
        load_str_from_nvs("srv_cert", &server_cert_pem);
        load_str_from_nvs("srv_key", &server_key_pem);
    }
    
    if (server_cert_pem && server_key_pem) {
        ESP_LOGI(TAG, "Certyfikaty gotowe.");

    } else {
        ESP_LOGE(TAG, "KRYTYCZNY BLAD: Nie udalo sie zaladowac certyfikatow po wygenerowaniu!");
    }

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
esp_err_t root_get_handler(httpd_req_t *req) {
    const char* html_response = 
        "<!DOCTYPE html><html><body style=\"display: flex; flex-direction: column; justify-content: center; align-items: center;\">"
        "<h2>KONFIGURACJA SIECI WIFI</h2>"
        "<form action=\"/save\" method=\"post\">"
        "SSID:<br><input type=\"text\" name=\"ssid\"><br>"
        "Haslo:<br><input type=\"password\" name=\"pass\"><br>"
        "Dodatkowe parametry:<br><input type=\"text\" name=\"custom\"><br>"
        "<input type=\"submit\" value=\"Zapisz\">"
        "</form></body></html>";
    httpd_resp_send(req, html_response, HTTPD_RESP_USE_STRLEN);
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
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) httpd_resp_send_408(req);
        return ESP_FAIL;
    }
    buf[remaining] = '\0';
    
    ESP_LOGI(TAG, "Odebrano zaszyfrowane dane: %s", buf);
    app_config_t new_config = {0};
    get_post_val(buf, "ssid", new_config.ssid, sizeof(new_config.ssid));
    get_post_val(buf, "pass", new_config.password, sizeof(new_config.password));
    get_post_val(buf, "custom", new_config.custom_param, sizeof(new_config.custom_param));
    //ESP_LOGI(TAG, "Otrzymano: SSID=%s, Pass=%s", new_config.ssid, new_config.password);
    save_config_to_nvs(&new_config);
    httpd_resp_send(req, "Ustawienia zapisane. Restartowanie...", HTTPD_RESP_USE_STRLEN);

    httpd_resp_send(req, "OK. Restart...", HTTPD_RESP_USE_STRLEN);
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
    return ESP_OK;
}

void start_webserver_https() {
    httpd_handle_t server = NULL;
    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
    config.httpd.stack_size = 16384;

    config.servercert = (const uint8_t *)server_cert_pem;
    config.servercert_len = strlen(server_cert_pem) + 1;
    config.prvtkey_pem = (const uint8_t *)server_key_pem;
    config.prvtkey_len = strlen(server_key_pem) + 1;
    
    config.transport_mode = HTTPD_SSL_TRANSPORT_SECURE;

    ESP_LOGI(TAG, "Startowanie serwera HTTPS...");
    if (httpd_ssl_start(&server, &config) == ESP_OK) {
        httpd_uri_t root_uri = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
        httpd_register_uri_handler(server, &root_uri);

        httpd_uri_t save_uri = { .uri = "/save", .method = HTTP_POST, .handler = save_post_handler };
        httpd_register_uri_handler(server, &save_uri);
        
        ESP_LOGI(TAG, "HTTPS Serwer dziala na porcie 443!");
    } else {
        ESP_LOGE(TAG, "Blad startu HTTPS!");
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
    
    uint8_t mac[6];
    esp_read_mac(mac,ESP_MAC_WIFI_SOFTAP);
    char ap_password[16];
    snprintf(ap_password, sizeof(ap_password), "ESP32_%02X%02X%02X",mac[3],mac[4],mac[5]);

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = CONFIG_AP_SSID,
            .ssid_len = strlen(CONFIG_AP_SSID),
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    strlcpy((char*)wifi_config.ap.password, ap_password, sizeof(wifi_config.ap.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    char qr_payload[128];
    snprintf(qr_payload,sizeof(qr_payload),"WIFI:S:%s;T:WPA;P:%s;;",CONFIG_AP_SSID,ap_password);
    ESP_LOGI(TAG, "================================================");
    ESP_LOGI(TAG, "https://chart.googleapis.com/chart?cht=qr&chs=300x300&chl=WIFI:S:%s%%3BT:WPA%%3BP:%s%%3B%%3B", 
             CONFIG_AP_SSID, ap_password);
    ESP_LOGI(TAG, "================================================");

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

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS uszkodzony/niekompatybilny. Czyszczenie...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
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
        ensure_certificates_exist();
        start_wifi_ap();
        start_webserver_https();
    } else {
        ESP_LOGI(TAG, "Konfiguracja znaleziona. Uruchamianie WiFi STA.");
        start_wifi_sta();
    }
}