#include "ble_dock_client.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "sensor_soil.h"
#include "motor_controller.h"
#include "flora_mqtt.h"
#include "water_controller.h"
#include "sensor_ir.h"
#include <string.h>

static const char *TAG = "BLE_DOCK_CLIENT";

// Nazwa urządzenia z serwera (ble_server.c)
static const char TARGET_NAME[] = "FloraDock";

// Te same UUID-y co w ble_server.c
static const uint8_t dock_service_uuid[ESP_UUID_LEN_128] = {
    0x55, 0x44, 0x33, 0x22, 0x11, 0x00, 0xFF, 0xEE,
    0xDD, 0xCC, 0xBB, 0xAA, 0x00, 0x56, 0x34, 0x12
};

static const uint8_t hall_char_uuid[ESP_UUID_LEN_128] = {
    0xA1, 0x01, 0x00, 0x00, 0x55, 0x55, 0x44, 0x44,
    0x33, 0x33, 0x22, 0x22, 0x11, 0x11, 0x10, 0x01
};

static const uint8_t water_char_uuid[ESP_UUID_LEN_128] = {
    0xA1, 0x02, 0x00, 0x00, 0x55, 0x55, 0x44, 0x44,
    0x33, 0x33, 0x22, 0x22, 0x11, 0x11, 0x10, 0x02
};

static esp_gatt_if_t gattc_if_global = ESP_GATT_IF_NONE;
static uint16_t conn_id = 0;
static esp_bd_addr_t target_addr = {0};
static bool target_found = false;

static bool dock_ready = false;          // czy mamy serwis i char-y
static uint8_t dock_hall_state = 0xFF;  // 0 = doniczka, 1 = brak, 0xFF = nieznany

static uint16_t service_start_handle = 0;
static uint16_t service_end_handle   = 0;
static uint16_t hall_char_handle     = 0;
static uint16_t hall_ccc_handle      = 0;
static uint16_t water_char_handle    = 0;

// Funkcja do odczytu wartości Hall (używamy read zamiast notify)
static void read_hall_value(void);

static void gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        if (param->scan_rst.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT) {
            break;
        }

        uint8_t adv_name_len = 0;
        uint8_t *adv_name = esp_ble_resolve_adv_data(
            param->scan_rst.ble_adv,
            ESP_BLE_AD_TYPE_NAME_CMPL,
            &adv_name_len
        );

        if (adv_name && adv_name_len) {
            char name[32] = {0};
            int len = adv_name_len < (sizeof(name) - 1) ? adv_name_len : (sizeof(name) - 1);
            memcpy(name, adv_name, len);

            ESP_LOGI(TAG, "Znaleziono: '%s' RSSI=%d", name, param->scan_rst.rssi);

            if (strncmp(name, TARGET_NAME, strlen(TARGET_NAME)) == 0) {
                ESP_LOGI(TAG, ">>> Trafiony FloraDock! <<<");
                memcpy(target_addr, param->scan_rst.bda, sizeof(esp_bd_addr_t));
                target_found = true;
                esp_ble_gap_stop_scanning();
            }
        }
        break;
    }

    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        if (target_found && gattc_if_global != ESP_GATT_IF_NONE) {
            ESP_LOGI(TAG, "Łączenie z FloraDock...");
            esp_ble_gattc_open(gattc_if_global, target_addr, BLE_ADDR_TYPE_PUBLIC, true);
        }
        break;

    default:
        break;
    }
}

static void gattc_cb(esp_gattc_cb_event_t event,
                     esp_gatt_if_t gattc_if,
                     esp_ble_gattc_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTC_REG_EVT: {
        gattc_if_global = gattc_if;
        ESP_LOGI(TAG, "Zarejestrowano GATTC, start skanowania...");

        esp_ble_scan_params_t scan_params = {
            .scan_type = BLE_SCAN_TYPE_ACTIVE,
            .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
            .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
            .scan_interval = 0x50,
            .scan_window = 0x30,
            .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE,
        };
        esp_ble_gap_set_scan_params(&scan_params);
        esp_ble_gap_start_scanning(30);
        break;
    }

    case ESP_GATTC_CONNECT_EVT:
        conn_id = param->connect.conn_id;
        dock_ready = false;
        ESP_LOGI(TAG, "Połączono z FloraDock, conn_id=%d", conn_id);

        // Szukamy naszej usługi po 128‑bit UUID
        esp_bt_uuid_t srv_uuid = {
            .len = ESP_UUID_LEN_128,
        };
        memcpy(srv_uuid.uuid.uuid128, dock_service_uuid, ESP_UUID_LEN_128);
        esp_ble_gattc_search_service(gattc_if, conn_id, &srv_uuid);
        break;

    case ESP_GATTC_SEARCH_RES_EVT: {
        const esp_gatt_id_t *srvc_id = &param->search_res.srvc_id;

        if (srvc_id->uuid.len == ESP_UUID_LEN_128 &&
            memcmp(srvc_id->uuid.uuid.uuid128, dock_service_uuid, ESP_UUID_LEN_128) == 0) {

            service_start_handle = param->search_res.start_handle;
            service_end_handle   = param->search_res.end_handle;
            ESP_LOGI(TAG, "Znaleziono usługę dock: handle %d-%d",
                     service_start_handle, service_end_handle);
        }
        break;
    }

    case ESP_GATTC_SEARCH_CMPL_EVT:
        if (service_start_handle && service_end_handle) {
            ESP_LOGI(TAG, "Szukam charakterystyk HALL / WATER...");

            esp_gattc_char_elem_t char_elem;
            uint16_t count;

            // HALL
            esp_bt_uuid_t hall_uuid = {
                .len = ESP_UUID_LEN_128,
            };
            memcpy(hall_uuid.uuid.uuid128, hall_char_uuid, ESP_UUID_LEN_128);
            count = 1;
            if (esp_ble_gattc_get_char_by_uuid(
                    gattc_if, conn_id,
                    service_start_handle, service_end_handle,
                    hall_uuid, &char_elem, &count) == ESP_OK && count > 0) {

                hall_char_handle = char_elem.char_handle;
                ESP_LOGI(TAG, "Handle HALL=%d, properties=0x%02X",
                         hall_char_handle, char_elem.properties);
            } else {
                ESP_LOGW(TAG, "Nie znaleziono charakterystyki HALL");
            }

            // WATER
            esp_bt_uuid_t water_uuid = {
                .len = ESP_UUID_LEN_128,
            };
            memcpy(water_uuid.uuid.uuid128, water_char_uuid, ESP_UUID_LEN_128);
            count = 1;
            if (esp_ble_gattc_get_char_by_uuid(
                    gattc_if, conn_id,
                    service_start_handle, service_end_handle,
                    water_uuid, &char_elem, &count) == ESP_OK && count > 0) {

                water_char_handle = char_elem.char_handle;
                ESP_LOGI(TAG, "Handle WATER=%d, properties=0x%02X",
                         water_char_handle, char_elem.properties);
            } else {
                ESP_LOGW(TAG, "Nie znaleziono charakterystyki WATER");
            }

            // CCC do HALL
            if (hall_char_handle) {
                esp_gattc_descr_elem_t descr_elem;
                count = 1;
                esp_bt_uuid_t ccc_uuid = {
                    .len = ESP_UUID_LEN_16,
                    .uuid = { .uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG }
                };
                if (esp_ble_gattc_get_descr_by_char_handle(
                        gattc_if, conn_id,
                        hall_char_handle,
                        ccc_uuid, &descr_elem, &count) == ESP_OK && count > 0) {
                    hall_ccc_handle = descr_elem.handle;
                    ESP_LOGI(TAG, "Handle CCC HALL=%d", hall_ccc_handle);
                } else {
                    ESP_LOGW(TAG, "Nie znaleziono CCC dla HALL");
                }
            }

            if (hall_char_handle && water_char_handle) {
                dock_ready = true;
                ESP_LOGI(TAG, "Dock gotowy (HALL i WATER znalezione)");
                
                // Odczytaj aktualną wartość Hall po połączeniu (używamy read zamiast notify)
                esp_ble_gattc_read_char(gattc_if, conn_id, hall_char_handle, ESP_GATT_AUTH_REQ_NONE);
                ESP_LOGI(TAG, "Wysłano żądanie odczytu HALL");
            } else {
                dock_ready = false;
            }
        } else {
            ESP_LOGW(TAG, "Nie znaleziono usługi dock");
        }
        break;

    case ESP_GATTC_NOTIFY_EVT:
        // Notyfikacje nie są używane dla Hall - używamy read
        ESP_LOGD(TAG, "NOTIFY event (nie używamy dla HALL): handle=%d", param->notify.handle);
        break;

    case ESP_GATTC_READ_CHAR_EVT:
        if (param->read.handle == hall_char_handle) {
            ESP_LOGI(TAG, "READ HALL response, len=%d", param->read.value_len);
            if (param->read.value_len > 0) {
                // Loguj całą zawartość bufora dla debugowania
                ESP_LOGI(TAG, "HALL read raw data: ");
                for (int i = 0; i < param->read.value_len && i < 20; i++) {
                    ESP_LOGI(TAG, "  [%d] = 0x%02X (%u)", i, param->read.value[i], param->read.value[i]);
                }
                
                // Odczytaj pierwszy bajt jako wartość Hall
                dock_hall_state = param->read.value[0];
                ESP_LOGI(TAG, "Stan doniczki (Hall) z READ = %u (0x%02X) - %s", 
                         dock_hall_state, dock_hall_state,
                         dock_hall_state == 0 ? "doniczka OBECNA" : 
                         dock_hall_state == 1 ? "doniczka BRAK" : "NIEZNANY");
            } else {
                ESP_LOGW(TAG, "HALL read ma pustą wartość (len=0)");
            }
        }
        break;

    case ESP_GATTC_DISCONNECT_EVT:
        ESP_LOGW(TAG, "Rozłączenie z FloraDock (reason=0x%x)", param->disconnect.reason);
        dock_ready = false;
        hall_char_handle = hall_ccc_handle = water_char_handle = 0;
        service_start_handle = service_end_handle = 0;
        target_found = false;
        dock_hall_state = 0xFF;
        // ponowne skanowanie
        esp_ble_gap_start_scanning(30);
        break;

    default:
        break;
    }
}

// Funkcja do odczytu wartości Hall (używamy read zamiast notify)
static void read_hall_value(void)
{
    if (!hall_char_handle || !dock_ready) {
        ESP_LOGW(TAG, "HALL niegotowy lub dock nie połączony");
        return;
    }

    esp_err_t ret = esp_ble_gattc_read_char(
        gattc_if_global,
        conn_id,
        hall_char_handle,
        ESP_GATT_AUTH_REQ_NONE
    );
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Błąd żądania odczytu HALL: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGD(TAG, "Wysłano żądanie odczytu HALL");
    }
}

// Wysyła czas podlewania w milisekundach do charakterystyki WATER
esp_err_t ble_dock_send_watering_ms(uint32_t duration_ms)
{
    if (!water_char_handle) {
        ESP_LOGW(TAG, "WATER niegotowy");
        return ESP_FAIL;
    }

    uint8_t buf[4];
    buf[0] = (duration_ms >> 0)  & 0xFF;
    buf[1] = (duration_ms >> 8)  & 0xFF;
    buf[2] = (duration_ms >> 16) & 0xFF;
    buf[3] = (duration_ms >> 24) & 0xFF;

    return esp_ble_gattc_write_char(
        gattc_if_global,
        conn_id,
        water_char_handle,
        sizeof(buf),
        buf,
        ESP_GATT_WRITE_TYPE_RSP,
        ESP_GATT_AUTH_REQ_NONE
    );
}

uint8_t ble_dock_get_hall_state(void)
{
    return dock_hall_state;
}

bool ble_dock_is_ready(void)
{
    return dock_ready;
}

// Prosty cykl podlewania oparty o czujnik gleby i dock BLE.
// Zakłada, że progi wilgotności i włączenie automatu trzymamy w MQTT.
esp_err_t ble_dock_run_watering_cycle(uint32_t pump_pulse_ms)
{
    if (!dock_ready) {
        ESP_LOGW(TAG, "Dock nie jest gotowy – brak połączenia lub charakterystyk");
        return ESP_FAIL;
    }

    // Domyślny czas impulsu jeśli nie podano (0 oznacza użyj domyślnego)
    uint32_t actual_pulse_ms = pump_pulse_ms;
    if (actual_pulse_ms == 0) {
        actual_pulse_ms = 100;  // Domyślny 100ms
    }

    // Jeśli duration >= 1000ms, to to jest pojedynczy impuls bez pętli (z MQTT)
    // Jeśli duration < 1000ms, to to jest pętla z wieloma impulsami (z water_controller)
    bool single_impulse = (actual_pulse_ms >= 1000);

    if (single_impulse) {
        // Pojedynczy impuls bez pętli - bezpośrednio z MQTT
        ESP_LOGI(TAG, "Pojedynczy impuls podlewania: %lu ms (bez pętli)", actual_pulse_ms);
        esp_err_t err = ble_dock_send_watering_ms(actual_pulse_ms);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Nie udało się wysłać komendy WATER: %s", esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(TAG, "Wysłano pojedynczy impuls WATER: %lu ms", actual_pulse_ms);
        return ESP_OK;
    }

    // Pętla z wieloma impulsami (z water_controller)
    sensor_soil_reading_t soil = {0};
    if (sensor_soil_read(&soil) != ESP_OK) {
        ESP_LOGW(TAG, "Błąd odczytu wilgotności gleby");
        return ESP_FAIL;
    }

    float threshold = mqtt_get_soil_humidity_threshold();
    ESP_LOGI(TAG, "Start cyklu podlewania: wilgotność=%.2f%%, próg=%.2f%%, impuls=%lu ms",
             soil.moisture_percent, threshold, actual_pulse_ms);

    if (soil.moisture_percent >= threshold) {
        ESP_LOGI(TAG, "Wilgotność już wystarczająca – pomijam podlewanie");
        return ESP_OK;
    }

    // Parametry pętli
    const TickType_t settle_delay = pdMS_TO_TICKS(5000);   // czas na \"dojście\" wody
    const int max_pulses = 30;                             // bezpieczny limit
    const int max_repositions = 5;

    int pulses_sent = 0;
    int repositions = 0;

    while (pulses_sent < max_pulses) {
        // 1) Krótki impuls wody
        esp_err_t err = ble_dock_send_watering_ms(actual_pulse_ms);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Nie udało się wysłać komendy WATER: %s", esp_err_to_name(err));
            return err;
        }
        pulses_sent++;
        ESP_LOGI(TAG, "Impuls WATER %d/%d: %lu ms", pulses_sent, max_pulses, actual_pulse_ms);

        vTaskDelay(settle_delay);

        // 2) Ponowny pomiar wilgotności
        if (sensor_soil_read(&soil) != ESP_OK) {
            ESP_LOGW(TAG, "Błąd odczytu wilgotności gleby po impulsie");
            continue;
        }

        ESP_LOGI(TAG, "Po impulsie: wilgotność=%.2f%% (próg=%.2f%%)",
                 soil.moisture_percent, threshold);

        if (soil.moisture_percent >= threshold) {
            ESP_LOGI(TAG, "Osiągnięto docelową wilgotność – koniec cyklu");
            return ESP_OK;
        }

        // 3) Jeśli wilgotność się nie poprawia – sprawdź HALL (odczytaj aktualną wartość)
        read_hall_value();
        // Poczekaj na odpowiedź odczytu (zwykle <100ms)
        vTaskDelay(pdMS_TO_TICKS(200));
        
        uint8_t hall = dock_hall_state;
        ESP_LOGI(TAG, "Aktualny stan HALL z doku: %u (0=doniczka obecna, 1=brak)", hall);

        // Sprawdź czy doniczka nie jest na doku (hall == 1 oznacza brak doniczki)
        // Uwaga: jeśli hall == 0, to doniczka JEST, więc nie trzeba przestawiać
        if (hall == 1 && motor_controller_is_initialized()) {
            // doniczka nie jest na doku – spróbuj przestawić wózek
            if (repositions >= max_repositions) {
                ESP_LOGW(TAG, "Osiągnięto maksymalną liczbę prób przestawienia wózka");
                break;
            }

            ESP_LOGW(TAG, "Hall == 1 (brak doniczki) – dojazd do przodu o 10cm i cofnięcie do ściany");

            // 10 cm do przodu
            esp_err_t mret = motor_controller_move_distance("forward", 10.0f, 128);
            if (mret != ESP_OK) {
                ESP_LOGE(TAG, "Błąd ruchu w przód: %s", esp_err_to_name(mret));
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(1000));

            // Jedź do tyłu aż do ściany (używając logiki z water_controller)
            esp_err_t wall_res = water_controller_move_to_wall();
            if (wall_res != ESP_OK) {
                ESP_LOGE(TAG, "Błąd podczas jazdy do ściany: %s", esp_err_to_name(wall_res));
                break;
            }

            repositions++;
            ESP_LOGI(TAG, "Wykonano przestawienie wózka (%d/%d)", repositions, max_repositions);
        }
    }

    ESP_LOGW(TAG, "Cykl podlewania zakończony bez osiągnięcia progu wilgotności");
    return ESP_FAIL;
}

void ble_dock_client_init(void)
{
    ESP_LOGI(TAG, "Inicjalizacja BLE Dock client...");

    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));

    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    esp_ble_gap_register_callback(gap_cb);
    esp_ble_gattc_register_callback(gattc_cb);
    ESP_ERROR_CHECK(esp_ble_gattc_app_register(1)); // app_id dowolne
}

