# Dokumentacja BLE API - FloraDock (Serwer)

## Spis treści

1. [Przegląd](#przegląd)
2. [Architektura BLE](#architektura-ble)
3. [GATT Profile](#gatt-profile)
4. [API serwera BLE](#api-serwera-ble)
5. [Protokół komunikacji](#protokół-komunikacji)
6. [Integracja z dock_control](#integracja-z-dock_control)
7. [Rozwiązywanie problemów](#rozwiązywanie-problemów)

---

## Przegląd

FloraDock działa jako **serwer BLE** i udostępnia serwis GATT dla robota FloraSense. Serwer pozwala na odczyt stanu czujnika Hall (obecność robota) oraz przyjmowanie komend podlewania.

### Cechy
- Automatyczne rozgłaszanie (advertising) po uruchomieniu
- Ponowne advertising po rozłączeniu klienta
- Aktualizacja stanu Hall w czasie rzeczywistym
- Obsługa komend podlewania z weryfikacją obecności robota

---

## Architektura BLE

```
┌─────────────────────────────────────────────────────────────────┐
│                    FloraDock (BLE Server)                        │
│                                                                  │
│  ┌────────────────┐    ┌────────────────┐    ┌──────────────┐   │
│  │   BLE Stack    │    │  ble_server    │    │ dock_control │   │
│  │   (NimBLE)     │◄──►│     .c/h       │◄──►│    .c/h      │   │
│  └────────────────┘    └────────────────┘    └──────────────┘   │
│                              │                      │           │
│                              │                      │           │
│  GATT Server:                │    Callbacks:        │           │
│  - Water Service             │    - Hall state CB   │           │
│  - Hall State Char           │    - Water cmd CB    │           │
│  - Water Command Char        │                      │           │
│                              │                      │           │
│  Advertising:                └──────────────────────┘           │
│  - Device name: "FloraDock"                                     │
│  - Connectable                                                  │
└─────────────────────────────────────────────────────────────────┘
                              ▲
                              │ BLE GATT
                              │
┌─────────────────────────────────────────────────────────────────┐
│                    FloraSense (BLE Client)                       │
│                                                                  │
│  Operacje:                                                       │
│  - Skanowanie i łączenie                                        │
│  - Read Hall State                                              │
│  - Write Water Command                                          │
└─────────────────────────────────────────────────────────────────┘
```

---

## GATT Profile

### Hierarchia serwisów

```
FloraDock BLE Server
│
└── Water Service (0000FFE0-0000-1000-8000-00805F9B34FB)
    │
    ├── Hall State Characteristic (0000FFE1-...)
    │   └── Properties: Read
    │   └── Value: 1 byte (0=present, 1=absent)
    │
    └── Water Command Characteristic (0000FFE2-...)
        └── Properties: Write
        └── Value: 4 bytes (uint32_t duration_ms)
```

### UUID Serwisu

| Nazwa | UUID |
|-------|------|
| Water Service | `0000FFE0-0000-1000-8000-00805F9B34FB` |

### Charakterystyki

#### Hall State (0000FFE1-...)

| Właściwość | Wartość |
|------------|---------|
| UUID | `0000FFE1-0000-1000-8000-00805F9B34FB` |
| Typ | Read |
| Rozmiar | 1 bajt |

**Wartości:**
| Bajt | Znaczenie |
|------|-----------|
| `0x00` | Robot obecny (magnes wykryty przy czujniku Hall) |
| `0x01` | Robot nieobecny (brak magnesu) |

**Zachowanie:**
- Wartość jest aktualizowana automatycznie przez `dock_control`
- Klient może odczytać wartość w dowolnym momencie
- Odczyt nie wymaga autoryzacji

#### Water Command (0000FFE2-...)

| Właściwość | Wartość |
|------------|---------|
| UUID | `0000FFE2-0000-1000-8000-00805F9B34FB` |
| Typ | Write |
| Rozmiar | 4 bajty |

**Format danych:**
```
Offset  Rozmiar  Typ       Opis
0       4        uint32_t  Czas podlewania w milisekundach (little-endian)
```

**Zakres wartości:**
- Minimum: 100 ms
- Maximum: 600000 ms (10 minut)

**Przykład:**
```
Podlewanie 5 sekund (5000 ms = 0x00001388):
Bajty: 88 13 00 00 (little-endian)
```

**Zachowanie:**
- Serwer weryfikuje czy robot jest obecny (Hall = 0)
- Jeśli robot obecny → włącza pompę na określony czas
- Jeśli robot nieobecny → ignoruje komendę (bezpieczeństwo)

---

## API serwera BLE

### Inicjalizacja

```c
void ble_server_init(void);
```
Inicjalizuje stos BLE i uruchamia serwer GATT. Rozpoczyna advertising.

**Wywoływane:** Raz podczas startu aplikacji, po `dock_control_init()`.

### Rejestracja callbacków

```c
void ble_server_register_handlers(ble_server_water_cmd_cb_t water_cb);
```
Rejestruje callback wywoływany przy otrzymaniu komendy podlewania.

**Parametry:**
- `water_cb` - Funkcja obsługująca komendę podlewania

**Prototyp callbacka:**
```c
typedef esp_err_t (*ble_server_water_cmd_cb_t)(uint32_t duration_ms);
```

### Aktualizacja stanu Hall

```c
void ble_server_set_hall_state(uint8_t hall_state);
```
Aktualizuje wartość charakterystyki Hall State.

**Parametry:**
- `hall_state` - Nowy stan (0=obecny, 1=nieobecny)

**Uwaga:** Wywoływane automatycznie przez `dock_control` przy zmianie stanu.

### Sprawdzanie połączenia

```c
bool ble_server_is_connected(void);
```
Sprawdza czy klient jest połączony.

**Zwraca:** `true` jeśli jest aktywne połączenie z klientem

---

## Protokół komunikacji

### Sekwencja typowej sesji

```
FloraSense (Client)              FloraDock (Server)
       │                                │
       │──── Scan for "FloraDock" ─────►│
       │                                │
       │◄─── Advertising Response ──────│
       │                                │
       │──── Connect Request ──────────►│
       │                                │
       │◄─── Connection Complete ───────│
       │                                │
       │──── Discover Services ────────►│
       │                                │
       │◄─── Service List ──────────────│
       │     (Water Service 0xFFE0)     │
       │                                │
       │──── Discover Characteristics ─►│
       │                                │
       │◄─── Characteristic List ───────│
       │     (Hall 0xFFE1, Water 0xFFE2)│
       │                                │
       │──── Read Hall State ──────────►│
       │                                │
       │◄─── Hall State: 0x00 ──────────│
       │     (robot present)            │
       │                                │
       │──── Write Water Command ──────►│
       │     (0x88 0x13 0x00 0x00)      │
       │     = 5000ms                   │
       │                                │
       │                         [Pompa ON 5s]
       │                                │
       │──── Disconnect ───────────────►│
       │                                │
       │                         [Restart advertising]
```

### Format pakietów

#### Read Hall State Response
```
Bajt 0: Stan czujnika Hall
        0x00 = Robot obecny
        0x01 = Robot nieobecny
```

#### Write Water Command Request
```
Bajt 0-3: Czas podlewania (uint32_t, little-endian)
          Zakres: 100 - 600000 ms
```

---

## Integracja z dock_control

### Schemat integracji

```c
// W flora-sense.c (app_main)

void app_main(void) {
    nvs_init();

    // 1. Inicjalizacja dock_control z callbackiem Hall
    dock_control_init(
        DOCK_HALL_GPIO,
        DOCK_PUMP_GPIO,
        ble_server_set_hall_state  // Callback aktualizacji Hall
    );

    // 2. Rejestracja handlera komendy podlewania
    ble_server_register_handlers(dock_control_handle_water_command);

    // 3. Ustawienie początkowego stanu Hall
    ble_server_set_hall_state(dock_control_get_hall_state());

    // 4. Inicjalizacja i start serwera BLE
    ble_server_init();
}
```

### Przepływ danych

```
┌─────────────┐                ┌─────────────┐               ┌─────────────┐
│   Hardware  │                │dock_control │               │ ble_server  │
│ (GPIO, IRQ) │                │             │               │             │
└──────┬──────┘                └──────┬──────┘               └──────┬──────┘
       │                              │                              │
       │ Hall state change            │                              │
       │ (GPIO interrupt)             │                              │
       │────────────────────────────►│                              │
       │                              │                              │
       │                              │ ble_server_set_hall_state() │
       │                              │─────────────────────────────►│
       │                              │                              │
       │                              │                              │ Update GATT
       │                              │                              │ characteristic
       │                              │                              │
       │                              │                              │
       │                              │◄── BLE Write (Water Cmd) ────│
       │                              │                              │
       │                              │ dock_control_handle_water_  │
       │                              │ command(duration_ms)         │
       │                              │                              │
       │◄─ Pump GPIO HIGH ────────────│                              │
       │                              │                              │
       │ [Wait duration_ms]           │                              │
       │                              │                              │
       │◄─ Pump GPIO LOW ─────────────│                              │
       │                              │                              │
```

---

## Rozwiązywanie problemów

### Problemy z advertising

**Problem:** FloraDock nie jest widoczny dla FloraSense

**Rozwiązania:**
1. Sprawdź czy `ble_server_init()` zostało wywołane
2. Sprawdź logi - powinno być "advertising started"
3. Zresetuj ESP32
4. Sprawdź czy inne urządzenie BLE nie jest połączone

**Logi diagnostyczne:**
```
I (xxx) BLE_SERVER: BLE server initialized
I (xxx) BLE_SERVER: Advertising started
```

### Problemy z połączeniem

**Problem:** Klient nie może się połączyć

**Rozwiązania:**
1. Sprawdź dystans - max ~10m
2. Zresetuj zarówno klienta jak i serwer
3. Sprawdź czy nie ma interferencji WiFi (2.4 GHz)

### Problemy z komendami

**Problem:** Komenda podlewania nie działa

**Rozwiązania:**
1. Sprawdź stan Hall - musi być 0 (robot obecny)
2. Sprawdź czy callback jest zarejestrowany
3. Sprawdź połączenie MOSFET/pompy

**Logi diagnostyczne:**
```
I (xxx) DOCK_CTRL: Hall state: 0 (present)
I (xxx) DOCK_CTRL: Water command received: 5000ms
I (xxx) DOCK_CTRL: Pump ON
I (xxx) DOCK_CTRL: Pump OFF (completed)
```

### Logi błędów

| Log | Przyczyna | Rozwiązanie |
|-----|-----------|-------------|
| `Hall state: 1 - ignoring water cmd` | Robot nieobecny | Przesuń robota bliżej |
| `Invalid duration` | Nieprawidłowy czas | Zakres 100-600000 ms |
| `BLE init failed` | Błąd stosu BLE | Restart ESP32 |
| `Advertising failed` | Błąd GAP | Sprawdź konfigurację NimBLE |

---

## Konfiguracja

### Parametry BLE (w ble_server.c)

```c
#define DEVICE_NAME           "FloraDock"
#define SERVICE_UUID          0xFFE0
#define CHAR_HALL_UUID        0xFFE1
#define CHAR_WATER_UUID       0xFFE2
```

### Parametry advertising

```c
// Interwał advertising (ms)
#define ADV_INTERVAL_MIN      100
#define ADV_INTERVAL_MAX      200

// Tryb advertising
// - Connectable undirected
// - Discoverable
```

### Parametry połączenia

```c
// Connection interval (1.25ms units)
#define CONN_INTERVAL_MIN     6    // 7.5ms
#define CONN_INTERVAL_MAX     24   // 30ms

// Supervision timeout
#define CONN_TIMEOUT          400  // 4s
```

---

## Bezpieczeństwo

### Weryfikacja obecności
- Przed włączeniem pompy **zawsze** sprawdzany jest stan Hall
- Jeśli Hall != 0, komenda podlewania jest ignorowana
- Zapobiega to przypadkowemu podlewaniu gdy robot nie jest pod stacją

### Limity czasowe
- Maksymalny czas podlewania: 10 minut (600000 ms)
- Minimalny czas: 100 ms
- Wartości poza zakresem są odrzucane

### Rozłączenie klienta
- Po rozłączeniu serwer automatycznie wznawia advertising
- Pompa zostaje zatrzymana jeśli była aktywna (TODO: implementacja)

---

## Changelog

- **v1.0** - Podstawowa implementacja serwera BLE
  - Water Service z charakterystykami Hall i Water
  - Integracja z dock_control
  - Automatyczne advertising
  - Weryfikacja obecności robota przed podlewaniem
