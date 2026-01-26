# Dokumentacja BLE API - FloraSense (Klient)

## Spis treści

1. [Przegląd](#przegląd)
2. [Architektura BLE](#architektura-ble)
3. [Serwis FloraDock](#serwis-floradock)
4. [API klienta BLE](#api-klienta-ble)
5. [Sekwencja podlewania](#sekwencja-podlewania)
6. [Obsługa błędów](#obsługa-błędów)
7. [Przykłady użycia](#przykłady-użycia)

---

## Przegląd

FloraSense działa jako **klient BLE** i łączy się ze stacją dokującą **FloraDock** w celu automatycznego podlewania. Komunikacja odbywa się przez dedykowany serwis GATT.

### Tryby pracy
- **Skanowanie** - wyszukiwanie urządzenia FloraDock
- **Połączenie** - aktywne połączenie z FloraDock
- **Rozłączenie** - automatyczne ponowne skanowanie

---

## Architektura BLE

```
┌─────────────────────────────────────────────────────────────────┐
│                    FloraSense (BLE Client)                       │
│                                                                  │
│  ┌────────────────┐    ┌────────────────┐    ┌──────────────┐   │
│  │   BLE Stack    │    │ ble_dock_client│    │water_controller│  │
│  │   (NimBLE)     │◄──►│     .c/h       │◄──►│    .c/h      │   │
│  └────────────────┘    └────────────────┘    └──────────────┘   │
│                                                                  │
│  Funkcje:                                                        │
│  - Skanowanie urządzeń BLE                                      │
│  - Łączenie z FloraDock                                         │
│  - Odkrywanie serwisów i charakterystyk                         │
│  - Odczyt stanu Hall                                            │
│  - Wysyłanie komend podlewania                                  │
└─────────────────────────────────────────────────────────────────┘
                              │
                              │ BLE GATT
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    FloraDock (BLE Server)                        │
│                                                                  │
│  Water Service (0000FFE0-0000-1000-8000-00805F9B34FB)           │
│  ├── Hall State (0000FFE1-...) - Read                           │
│  └── Water Command (0000FFE2-...) - Write                       │
└─────────────────────────────────────────────────────────────────┘
```

---

## Serwis FloraDock

### UUID Serwisu
```
Water Service: 0000FFE0-0000-1000-8000-00805F9B34FB
```

### Charakterystyki

#### Hall State (0000FFE1-...)
| Właściwość | Wartość |
|------------|---------|
| UUID | `0000FFE1-0000-1000-8000-00805F9B34FB` |
| Operacje | Read |
| Rozmiar | 1 bajt |
| Opis | Stan czujnika Hall |

**Wartości:**
- `0x00` - Robot obecny (magnes wykryty)
- `0x01` - Robot nieobecny

#### Water Command (0000FFE2-...)
| Właściwość | Wartość |
|------------|---------|
| UUID | `0000FFE2-0000-1000-8000-00805F9B34FB` |
| Operacje | Write |
| Rozmiar | 4 bajty |
| Opis | Komenda podlewania |

**Format:** uint32_t (little-endian)
- Wartość: Czas podlewania w milisekundach
- Zakres: 100 - 600000 ms (100ms - 10 minut)

---

## API klienta BLE

### Inicjalizacja

```c
void ble_dock_client_init(void);
```
Inicjalizuje stos BLE i rozpoczyna skanowanie. Wywoływane raz podczas startu aplikacji.

### Sprawdzanie połączenia

```c
bool ble_dock_is_connected(void);
```
Sprawdza czy jest aktywne połączenie z FloraDock.

**Zwraca:** `true` jeśli połączony (może być w trakcie odkrywania serwisów)

```c
bool ble_dock_is_ready(void);
```
Sprawdza czy połączenie jest gotowe do wysyłania komend.

**Zwraca:** `true` jeśli połączony i odkryto wszystkie serwisy

### Odczyt stanu Hall

```c
int ble_dock_get_hall_state(void);
```
Zwraca ostatni znany stan czujnika Hall (z cache).

**Zwraca:**
- `0` - Robot obecny
- `1` - Robot nieobecny
- `-1` - Błąd lub brak danych

```c
esp_err_t ble_dock_read_hall(uint8_t *hall_state);
```
Wykonuje synchroniczny odczyt stanu Hall przez BLE.

**Parametry:**
- `hall_state` - Wskaźnik na wynik (0=obecny, 1=nieobecny)

**Zwraca:** `ESP_OK` lub kod błędu

### Wysyłanie komend podlewania

```c
esp_err_t ble_dock_send_watering_ms(uint32_t duration_ms);
```
Wysyła komendę podlewania do FloraDock.

**Parametry:**
- `duration_ms` - Czas podlewania w milisekundach (100-600000)

**Zwraca:**
- `ESP_OK` - Sukces
- `ESP_ERR_INVALID_STATE` - Robot nie jest obecny (Hall != 0)
- `ESP_ERR_TIMEOUT` - Timeout BLE
- `ESP_FAIL` - Inny błąd

### Pełny cykl podlewania

```c
esp_err_t ble_dock_run_watering_cycle(uint32_t pulse_ms);
```
Wykonuje pełny cykl podlewania: sprawdza obecność, wysyła komendę.

**Parametry:**
- `pulse_ms` - Czas impulsu podlewania w ms

**Zwraca:** `ESP_OK` lub kod błędu

### Sterowanie skanowaniem

```c
void ble_dock_client_start_scan(void);
```
Ręczne uruchomienie skanowania (normalnie automatyczne).

```c
void ble_dock_client_disconnect(void);
```
Rozłączenie od FloraDock i wyłączenie auto-reconnect.

```c
void ble_dock_client_enable_reconnect(bool enable);
```
Włącza/wyłącza automatyczne ponowne łączenie.

---

## Sekwencja podlewania

### Automatyczne podlewanie (water_controller)

```
┌─────────────────────────────────────────────────────────────────┐
│ 1. Sprawdzenie wilgotności gleby                                │
│    └── Jeśli wilgotność < próg → kontynuuj                     │
│                                                                  │
│ 2. Jazda do stacji dokującej                                    │
│    └── water_controller_move_to_wall() - do tyłu aż IR         │
│                                                                  │
│ 3. Połączenie BLE (jeśli nie połączony)                        │
│    └── ble_dock_client_init() już uruchomione                  │
│                                                                  │
│ 4. Sprawdzenie gotowości                                        │
│    └── ble_dock_is_ready() == true                             │
│                                                                  │
│ 5. Odczyt stanu Hall                                            │
│    └── ble_dock_read_hall(&hall_state)                         │
│    └── hall_state == 0 → robot obecny                          │
│                                                                  │
│ 6. Wysłanie komendy podlewania                                  │
│    └── ble_dock_send_watering_ms(5000) // 5 sekund             │
│                                                                  │
│ 7. Oczekiwanie na zakończenie                                   │
│    └── vTaskDelay() lub sprawdzenie wilgotności                │
│                                                                  │
│ 8. Opcjonalnie: powtórz krok 6 jeśli wilgotność wciąż niska   │
└─────────────────────────────────────────────────────────────────┘
```

### Podlewanie wywołane przez MQTT

Komenda `/cmd/water` wywołuje podobną sekwencję:

1. Robot jedzie do tyłu do ściany (IR)
2. Łączy się z FloraDock przez BLE
3. Weryfikuje obecność (Hall)
4. Wysyła impuls podlewania
5. (Opcjonalnie) Powtarza impulsy w pętli

---

## Obsługa błędów

### Kody błędów

| Kod | Opis | Rozwiązanie |
|-----|------|-------------|
| `ESP_OK` | Sukces | - |
| `ESP_ERR_INVALID_STATE` | Robot nie jest obecny przy stacji | Sprawdź pozycję robota |
| `ESP_ERR_TIMEOUT` | Timeout operacji BLE | Sprawdź połączenie, spróbuj ponownie |
| `ESP_ERR_NOT_FOUND` | Nie znaleziono FloraDock | Sprawdź czy FloraDock jest włączony |
| `ESP_FAIL` | Ogólny błąd | Sprawdź logi, restart |

### Automatyczne odzyskiwanie

- **Utrata połączenia** → Automatyczne ponowne skanowanie
- **Timeout operacji** → Retry z backoff
- **Hall != 0** → Przerwanie operacji (bezpieczeństwo)

### Logi diagnostyczne

```
I (xxx) BLE_DOCK: Scanning for FloraDock...
I (xxx) BLE_DOCK: Found FloraDock, connecting...
I (xxx) BLE_DOCK: Connected, discovering services...
I (xxx) BLE_DOCK: Service discovery complete, ready
I (xxx) BLE_DOCK: Hall state: 0 (present)
I (xxx) BLE_DOCK: Sending water command: 5000ms
W (xxx) BLE_DOCK: Hall state: 1 (absent) - cannot water
E (xxx) BLE_DOCK: Connection lost, restarting scan
```

---

## Przykłady użycia

### Podstawowe podlewanie

```c
#include "ble_dock_client.h"

void water_plant(void) {
    // Sprawdź czy połączony i gotowy
    if (!ble_dock_is_ready()) {
        ESP_LOGW(TAG, "FloraDock not ready");
        return;
    }

    // Sprawdź czy robot jest przy stacji
    uint8_t hall_state;
    if (ble_dock_read_hall(&hall_state) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read Hall state");
        return;
    }

    if (hall_state != 0) {
        ESP_LOGW(TAG, "Robot not at dock station");
        return;
    }

    // Wyślij komendę podlewania (5 sekund)
    esp_err_t ret = ble_dock_send_watering_ms(5000);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Watering command sent successfully");
    } else {
        ESP_LOGE(TAG, "Failed to send watering command: %s",
                 esp_err_to_name(ret));
    }
}
```

### Użycie pełnego cyklu

```c
#include "ble_dock_client.h"

void auto_water(void) {
    // Pełny cykl: sprawdza Hall i wysyła komendę
    esp_err_t ret = ble_dock_run_watering_cycle(5000);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Watering cycle completed");
    } else if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Robot not at dock - move closer");
    } else {
        ESP_LOGE(TAG, "Watering failed: %s", esp_err_to_name(ret));
    }
}
```

### Integracja z water_controller

```c
// W water_controller.c
void water_controller_task(void *param) {
    while (1) {
        // Sprawdź wilgotność gleby
        float moisture = get_soil_moisture();

        if (moisture < MOISTURE_THRESHOLD) {
            // Jedź do stacji dokującej
            water_controller_move_to_wall();

            // Poczekaj na stabilizację
            vTaskDelay(pdMS_TO_TICKS(1000));

            // Wykonaj podlewanie
            esp_err_t ret = ble_dock_run_watering_cycle(5000);

            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Watering failed, will retry");
            }
        }

        // Sprawdzaj co 10 minut
        vTaskDelay(pdMS_TO_TICKS(600000));
    }
}
```

---

## Konfiguracja

### Timeout operacji

W `ble_dock_client.c`:
```c
#define BLE_CONNECT_TIMEOUT_MS    10000  // Timeout łączenia
#define BLE_DISCOVER_TIMEOUT_MS   5000   // Timeout discovery
#define BLE_WRITE_TIMEOUT_MS      3000   // Timeout zapisu
#define BLE_READ_TIMEOUT_MS       3000   // Timeout odczytu
```

### Parametry skanowania

```c
#define BLE_SCAN_DURATION_MS      10000  // Czas skanowania
#define BLE_SCAN_INTERVAL         0x0010 // Interwał skanowania
#define BLE_SCAN_WINDOW           0x0010 // Okno skanowania
```

---

## Changelog

- **v1.0** - Podstawowa implementacja klienta BLE
  - Skanowanie i łączenie z FloraDock
  - Odczyt stanu Hall
  - Wysyłanie komend podlewania
  - Auto-reconnect po utracie połączenia
