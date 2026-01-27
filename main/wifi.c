#include "certs.h"
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
#include "esp_mac.h"
#include "wifi.h"


static const char *TAG = "WIFI_MGR";
#define GPIO_RESET_BUTTON 0 
#define CONFIG_AP_SSID "ESP32_SETUP"
static EventGroupHandle_t s_wifi_event_group;
static bool is_ap_mode = false;
app_config_t current_config;

// Klucze NVS współdzielone z modułem MQTT
#define NVS_NAMESPACE_STORAGE "storage"
#define NVS_KEY_USER_ID       "user_id"
#define NVS_KEY_USER_CHANGED  "user_changed"


void get_device_mac_str(char *buf)
{
    uint8_t mac[6];
    esp_read_mac(mac,ESP_MAC_WIFI_STA);
    sprintf(buf, "%02X%02X%02X%02X%02X%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
}

bool wifi_is_ap_mode(void) {return is_ap_mode;}

static void save_user_id_and_mark_changed(const char *user_id)
{
    if (user_id == NULL || user_id[0] == '\0') {
        ESP_LOGW(TAG, "Pominięto zapis USER_ID - pusty string");
        return;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE_STORAGE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Blad otwarcia NVS dla USER_ID: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_str(handle, NVS_KEY_USER_ID, user_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Blad zapisu USER_ID do NVS: %s", esp_err_to_name(err));
        nvs_close(handle);
        return;
    }

    // Oznacz, że USER_ID zostało zmienione w trakcie konfiguracji WiFi
    err = nvs_set_u8(handle, NVS_KEY_USER_CHANGED, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Blad ustawienia flagi USER_CHANGED w NVS: %s", esp_err_to_name(err));
        nvs_close(handle);
        return;
    }

    nvs_commit(handle);
    nvs_close(handle);
    ESP_LOGI(TAG, "USER_ID zapisane w NVS i oznaczone jako zmienione: %s", user_id);
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
    
    if (configured == 1) 
    {
        size_t required_size = sizeof(cfg->ssid);
        nvs_get_str(my_handle, "ssid", cfg->ssid, &required_size);
        
        required_size = sizeof(cfg->password);
        nvs_get_str(my_handle, "pass", cfg->password, &required_size);

        required_size = sizeof(cfg->custom_param);
        if(nvs_get_str(my_handle, "custom", cfg->custom_param, &required_size) != ESP_OK)
            strcpy(cfg->custom_param, "");

        nvs_close(my_handle);
        return true;
    }
    nvs_close(my_handle);
    return false;
}

void clear_nvs_config() 
{
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) 
    {
        nvs_erase_all(my_handle);
        nvs_commit(my_handle);
        nvs_close(my_handle);
        ESP_LOGW(TAG, "NVS wyczyszczony.");
    }
}

void url_decode(char *dst, const char *src) {
    char a, b;
    while (*src) 
    {
        if ((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit(a) && isxdigit(b))) 
        {
            if (a >= 'a') a -= 'a'-'A';
            if (a >= 'A') a -= ('A' - 10); else a -= '0';
            if (b >= 'a') b -= 'a'-'A';
            if (b >= 'A') b -= ('A' - 10); else b -= '0';
            *dst++ = 16*a+b;
            src+=3;
        } 
        else if (*src == '+') {*dst++ = ' '; src++;} 
        else *dst++ = *src++;
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

esp_err_t save_post_handler(httpd_req_t *req) 
{
    char expected_token[13];
    get_device_mac_str(expected_token);

    char received_token[32];
    if(httpd_req_get_hdr_value_str(req,"X-Auth-Token",received_token,sizeof(received_token)) != ESP_OK)
    {
        ESP_LOGW(TAG, "Odrzucono: Brak nagłówka X-Auth-Token");
        httpd_resp_send_err(req,HTTPD_401_UNAUTHORIZED,"Brak tokenu MAC");
        return ESP_FAIL;
    }

    if(strcmp(received_token,expected_token) != 0)
    {
        ESP_LOGW(TAG,"Odrzucono: Niepoprawny token. Otrzymano: %s, Oczekiwano %s",received_token,expected_token);
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Niepoprawny token MAC");
        return ESP_FAIL;
    }

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

    // Odczyt nazwy użytkownika z requestu konfiguracji WiFi.
    // Obsługujemy zarówno "user", jak i "user_id" dla kompatybilności.
    char new_user_id[32] = {0};
    get_post_val(buf, "user", new_user_id, sizeof(new_user_id));
    if (new_user_id[0] == '\0') {
        get_post_val(buf, "user_id", new_user_id, sizeof(new_user_id));
    }


    save_user_id_and_mark_changed(new_user_id);
    save_config_to_nvs(&new_config);
    httpd_resp_send(req, "Ustawienia zapisane. Restartowanie...", HTTPD_RESP_USE_STRLEN);

    httpd_resp_send(req, "OK. Restart...", HTTPD_RESP_USE_STRLEN);
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
    return ESP_OK;
}

void start_webserver_http() {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;  // w razie potrzeby można zwiększyć

    ESP_LOGI(TAG, "Startowanie serwera HTTP...");
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t save_uri = {
            .uri = "/save",
            .method = HTTP_POST,
            .handler = save_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &save_uri);

        ESP_LOGI(TAG, "HTTP serwer dziala na porcie %d!", config.server_port);
    } else {
        ESP_LOGE(TAG, "Blad startu HTTP!");
    }
}

int tries = 0;
static void wifi_event_handler(void* arg, esp_event_base_t event_base,int32_t event_id, void* event_data) 
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) 
    {
        if (!wifi_is_ap_mode()) 
        {
            ESP_LOGI(TAG, "Łączenie z routerem...");
            esp_wifi_connect();
        } 
        else ESP_LOGI(TAG, "Tryb AP aktywny - pomijam automatyczne łączenie STA.");
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) 
    {
        ESP_LOGI(TAG, "Rozlaczono z WiFi, ponawiam...");
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        if(tries<5){esp_wifi_connect(); tries++;}
        else ESP_LOGI(TAG, "Nie znaleziono sieci Wi-Fi o podanym SSID i haśle.");
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) 
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
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
            .authmode = WIFI_AUTH_OPEN
        },
    };
    strlcpy((char*)wifi_config.ap.password, ap_password, sizeof(wifi_config.ap.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
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

void wifi_manager_init(void) 
{

    s_wifi_event_group = xEventGroupCreate();

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "------------------------------------------");
    ESP_LOGI(TAG, "TOKEN MAC (bez dwukropków): %02X%02X%02X%02X%02X%02X", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "------------------------------------------");


    gpio_set_direction(GPIO_NUM_0, GPIO_MODE_INPUT);
    vTaskDelay(pdMS_TO_TICKS(100));
    bool force_ap = false;
    
    
    for(int i = 0; i < 5; i++) 
    {
        ESP_LOGI(TAG, "Sprawdzam przycisk force_ap...");
        if (gpio_get_level(GPIO_NUM_0) == 0) {force_ap = true; break;}
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    bool has_config = load_config_from_nvs(&current_config);

    if (force_ap || !has_config)
    {
        if (force_ap) ESP_LOGW(TAG, "WYMUSZONO TRYB AP PRZYCISKIEM!");
        is_ap_mode = true;
        ESP_LOGW(TAG, "Tryb konfiguracji...");
        start_wifi_ap();
        start_webserver_http();
    }
    else 
    {
        is_ap_mode = false;
        ESP_LOGI(TAG,"Tryb stacji: Łączę się z %s", current_config.ssid);
        esp_netif_create_default_wifi_sta();
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_wifi_init(&cfg);
        esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event_handler,NULL,NULL);
        esp_event_handler_instance_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event_handler,NULL,NULL);

        wifi_config_t sta_cfg = {0};
        strlcpy((char*)sta_cfg.sta.ssid, current_config.ssid,32);
        strlcpy((char*)sta_cfg.sta.password, current_config.password,64);
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
        esp_wifi_start();
    }
}

bool wifi_is_connected(void)
{
    if(s_wifi_event_group == NULL) return false;
    return (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT);
}