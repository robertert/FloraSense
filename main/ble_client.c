#include "ble_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include <string.h>

static const char *TAG = "BLE_ITAG";

static esp_gatt_if_t gattc_if_global = 0;
static uint16_t conn_id = 0;
static esp_bd_addr_t target_addr = {0};   // Zapisz MAC, gdy znajdziesz iTAG
static bool target_found = false;

static const uint16_t SERVICE_UUID = 0xFFE0;
static const uint16_t BATTERY_SERVICE_UUID = 0x180F;  // Battery Service
static const uint16_t BATTERY_LEVEL_UUID = 0x2A19;    // Battery Level Characteristic

static uint16_t battery_service_start_handle = 0;
static uint16_t battery_service_end_handle = 0;
static uint16_t battery_level_char_handle = 0;
static bool battery_service_found = false;

static void gap_callback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {

    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        // Filtrujemy po nazwie
        if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {

            uint8_t adv_name_len;
            uint8_t *adv_name = esp_ble_resolve_adv_data(
                param->scan_rst.ble_adv, ESP_BLE_AD_TYPE_NAME_CMPL, &adv_name_len
            );

            if (adv_name && adv_name_len) {
                // Wyświetl wszystkie urządzenia z nazwą
                char name[32] = {0};
                int len = adv_name_len < sizeof(name) - 1 ? adv_name_len : sizeof(name) - 1;
                memcpy(name, adv_name, len);
                name[len] = 0;
                ESP_LOGI(TAG, "Znaleziono urządzenie: '%s' RSSI=%d (len=%d)", name, param->scan_rst.rssi, adv_name_len);
                
                // Sprawdź, czy to iTAG (ignoruj spacje na końcu)
                // Usuń spacje z końca nazwy
                int name_len_trimmed = adv_name_len;
                while (name_len_trimmed > 0 && adv_name[name_len_trimmed - 1] == ' ') {
                    name_len_trimmed--;
                }
                
                // Porównaj pierwsze 4 znaki (lub mniej jeśli nazwa jest krótsza)
                int cmp_len = name_len_trimmed < 4 ? name_len_trimmed : 4;
                if (cmp_len >= 4 && strncmp((char*)adv_name, "iTAG", 4) == 0) {
                    ESP_LOGI(TAG, ">>> Znaleziono iTAG! RSSI=%d <<<", param->scan_rst.rssi);
                    memcpy(target_addr, param->scan_rst.bda, sizeof(esp_bd_addr_t));
                    target_found = true;

                    ESP_LOGI(TAG, "MAC=%02X:%02X:%02X:%02X:%02X:%02X",
                         param->scan_rst.bda[0], param->scan_rst.bda[1], param->scan_rst.bda[2],
                         param->scan_rst.bda[3], param->scan_rst.bda[4], param->scan_rst.bda[5]);

                    esp_ble_gap_stop_scanning();
                }
            }
        }
        break;
    }

    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        if (target_found) {
            ESP_LOGI(TAG, "Łączenie z iTAG...");
            // esp_ble_gattc_open wymaga 4 argumentów: gattc_if, remote_bda, remote_addr_type, is_direct
            esp_ble_gattc_open(gattc_if_global, target_addr, BLE_ADDR_TYPE_PUBLIC, true);
        }
        break;

    default:
        break;
    }
}

static void gattc_callback(esp_gattc_cb_event_t event,
                           esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param)
{
    switch (event) {

    case ESP_GATTC_REG_EVT:
        gattc_if_global = gattc_if;
        ESP_LOGI(TAG, "Start skanowania...");
        
        // Parametry skanowania
        esp_ble_scan_params_t scan_params = {
            .scan_type = BLE_SCAN_TYPE_ACTIVE,
            .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
            .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
            .scan_interval = 0x50,
            .scan_window = 0x30,
            .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE
        };
        
        esp_ble_gap_set_scan_params(&scan_params);
        esp_ble_gap_start_scanning(30); // 30 sekund
        break;

    case ESP_GATTC_CONNECT_EVT:
        conn_id = param->connect.conn_id;
        ESP_LOGI(TAG, "Połączono, zaczynam discovery...");
        esp_ble_gattc_search_service(gattc_if, conn_id, NULL);
        break;

    case ESP_GATTC_SEARCH_RES_EVT: {
        uint16_t uuid16 = param->search_res.srvc_id.uuid.uuid.uuid16;
        uint16_t start_handle = param->search_res.start_handle;
        uint16_t end_handle = param->search_res.end_handle;
        
        // Wylistuj wszystkie serwisy
        ESP_LOGI(TAG, "=== Serwis ===");
        ESP_LOGI(TAG, "UUID: 0x%04X", uuid16);
        ESP_LOGI(TAG, "Handle: %d-%d", start_handle, end_handle);
        
        // Sprawdź, czy to Battery Service
        if (uuid16 == BATTERY_SERVICE_UUID) {
            ESP_LOGI(TAG, ">>> Znaleziono Battery Service! <<<");
            battery_service_start_handle = start_handle;
            battery_service_end_handle = end_handle;
            battery_service_found = true;
        }
        
        // Sprawdź, czy to nasz custom service
        if (uuid16 == SERVICE_UUID) {
            ESP_LOGI(TAG, ">>> Znaleziono usługę FFE0 <<<");
        }
        break;
    }

    case ESP_GATTC_SEARCH_CMPL_EVT:
        ESP_LOGI(TAG, "Skanowanie usług zakończone");
        
        // Jeśli znaleziono Battery Service, odkryj charakterystykę Battery Level
        if (battery_service_found) {
            ESP_LOGI(TAG, "Odkrywam charakterystykę Battery Level (handle %d-%d)...", 
                     battery_service_start_handle, battery_service_end_handle);
            
            esp_bt_uuid_t battery_level_uuid = {
                .len = ESP_UUID_LEN_16,
                .uuid = {.uuid16 = BATTERY_LEVEL_UUID}
            };
            
            esp_gattc_char_elem_t result;
            uint16_t count = 1;
            
            esp_err_t ret = esp_ble_gattc_get_char_by_uuid(
                gattc_if, 
                conn_id,
                battery_service_start_handle,
                battery_service_end_handle,
                battery_level_uuid,
                &result,
                &count
            );
            
            if (ret == ESP_OK && count > 0) {
                battery_level_char_handle = result.char_handle;
                ESP_LOGI(TAG, ">>> Znaleziono Battery Level characteristic, handle=%d <<<", 
                         battery_level_char_handle);
                
                // Przeczytaj wartość Battery Level
                ESP_LOGI(TAG, "Czytam wartość Battery Level...");
                esp_ble_gattc_read_char(gattc_if, conn_id, battery_level_char_handle, ESP_GATT_AUTH_REQ_NONE);
            } else {
                ESP_LOGW(TAG, "Nie znaleziono Battery Level characteristic (ret=%d, count=%d)", ret, count);
            }
        }
        break;

    case ESP_GATTC_READ_CHAR_EVT:
        if (param->read.status == ESP_GATT_OK && param->read.handle == battery_level_char_handle) {
            uint8_t battery_level = param->read.value[0];
            ESP_LOGI(TAG, "=== Battery Level: %d%% ===", battery_level);
        }
        break;

    default:
        break;
    }
}

void init_ble(void)
{
    ESP_LOGI(TAG, "Inicjalizacja BLE...");
    
    esp_err_t ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT release failed: %s", esp_err_to_name(ret));
        return;
    }

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT controller init failed: %s", esp_err_to_name(ret));
        return;
    }
    
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT controller enable failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return;
    }
    
    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return;
    }

    esp_ble_gap_register_callback(gap_callback);
    esp_ble_gattc_register_callback(gattc_callback);

    ret = esp_ble_gattc_app_register(0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GATTC app register failed: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "BLE zainicjalizowane pomyślnie");
}