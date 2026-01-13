// Klient BLE dla dokowania FloraDock
// - subskrybuje charakterystykę HALL (notyfikacje stanu doniczki)
// - wysyła czas podlewania (ms) na charakterystykę WATER

#pragma once

#include "esp_err.h"

// Zwraca ostatni znany stan HALL z doku:
// 0 = doniczka obecna, 1 = doniczka brak, 0xFF = stan nieznany
uint8_t ble_dock_get_hall_state(void);

// Zwraca true, jeśli klient jest połączony z dokiem i odkrył usługę/char.
bool ble_dock_is_ready(void);

// Inicjalizacja stosu BLE i start klienta (skanowanie, połączenie, discovery)
void ble_dock_client_init(void);

// Wysyła czas podlewania w milisekundach do charakterystyki WATER.
// Zwraca ESP_OK lub błąd z warstwy GATT.
esp_err_t ble_dock_send_watering_ms(uint32_t duration_ms);

// Uruchamia pojedynczy cykl podlewania:
// - sprawdza wilgotność gleby,
// - jeśli za niska, wysyła krótkie impulsy WATER,
// - korzysta z HALL i napędu do korekty pozycji doniczki.
// @param pump_pulse_ms Czas pojedynczego impulsu wody w milisekundach (domyślnie 100ms)
esp_err_t ble_dock_run_watering_cycle(uint32_t pump_pulse_ms);
