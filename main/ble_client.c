#include "ble_client.h"

static const char *TAG = "BLE_CLIENT";
static uint8_t own_addr_type;
static uint16_t battery_handle = 0;
static const char *itag_name = "iTag";

/* ===== CALLBACK GATT READ ===== */
static int read_battery_cb(uint16_t conn_handle,
                           const struct ble_gatt_error *error,
                           struct ble_gatt_attr *attr,
                           void *arg)
{
    if (!error && attr && attr->om)
    {
        uint8_t battery = attr->om->om_data[0];
        ESP_LOGI(TAG, "Poziom baterii iTag: %d%%", battery);
    }
    else
    {
        ESP_LOGE(TAG, "Błąd odczytu charakterystyki");
    }
    return 0;
}

/* ===== CALLBACK DISCOVERY CHARACTERISTICS ===== */
static int gattc_disc_chrs_cb(uint16_t conn_handle,
                              const struct ble_gatt_error *error,
                              const struct ble_gatt_chr *chr,
                              void *arg)
{
    if (!error && chr)
    {
        if (chr->uuid.u16 == 0x2A19) // Battery Level
        {
            battery_handle = chr->val_handle;
            ESP_LOGI(TAG, "Znaleziono Battery Level handle=%d", battery_handle);
            ble_gattc_read(conn_handle, battery_handle, read_battery_cb, NULL);
        }
    }
    return 0;
}

/* ===== CALLBACK DISCOVERY SERVICES ===== */
static int gattc_disc_svcs_cb(uint16_t conn_handle,
                              const struct ble_gatt_error *error,
                              const struct ble_gatt_svc *svc,
                              void *arg)
{
    if (!error && svc)
    {
        if (svc->uuid.u16 == 0x180F) // Battery Service
        {
            ESP_LOGI(TAG, "Znaleziono Battery Service handle_start=%d handle_end=%d",
                     svc->start_handle, svc->end_handle);
            ble_gattc_disc_all_chrs(conn_handle, svc->start_handle, svc->end_handle,
                                    gattc_disc_chrs_cb, NULL);
        }
    }
    return 0;
}

/* ===== GAP CALLBACK ===== */
static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type)
    {
    case BLE_GAP_EVENT_DISC: {
        char name[32];
        int name_len = ble_hs_adv_parse_name(event->disc.data,
                                             event->disc.length_data, name, sizeof(name));

        if (name_len > 0)
        {
            name[name_len] = 0;
            ESP_LOGI(TAG, "Znaleziono urządzenie: %s RSSI=%d", name, event->disc.rssi);

            if (strstr(name, itag_name))
            {
                ESP_LOGI(TAG, "Znaleziono iTag! Łączenie...");
                ble_gap_disc_cancel();

                ble_gap_connect(own_addr_type, &event->disc.addr,
                                BLE_HS_FOREVER, NULL, gap_event_cb, NULL);
            }
        }
        return 0;
    }
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0)
        {
            ESP_LOGI(TAG, "Połączono z iTag, handle=%d", event->connect.conn_handle);
            ble_gattc_disc_all_svcs(event->connect.conn_handle, gattc_disc_svcs_cb, NULL);
        }
        else
        {
            ESP_LOGE(TAG, "Nie udało się połączyć");
            ble_gap_disc(own_addr_type, BLE_HS_FOREVER, NULL, gap_event_cb, NULL);
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Rozłączono, powtarzam skanowanie...");
        ble_gap_disc(own_addr_type, BLE_HS_FOREVER, NULL, gap_event_cb, NULL);
        return 0;

    default:
        return 0;
    }
}

/* ===== BLE INIT ===== */
static void ble_on_sync(void)
{
    ble_hs_id_infer_auto(0, &own_addr_type);
    ble_gap_disc(own_addr_type, BLE_HS_FOREVER, NULL, gap_event_cb, NULL);
}

/* ===== BLE TASK ===== */
void host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* ===== INIT BLE ===== */
void init_ble(void)
{
    ESP_ERROR_CHECK(esp_nimble_hci_and_controller_init());
    nimble_port_init();

    ble_hs_cfg.sync_cb = ble_on_sync;

    nimble_port_freertos_init(host_task);
}