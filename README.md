# FloraDock - Stacja Dokująca do Podlewania

## Opis projektu

**FloraDock** to stacja dokująca dla robota **FloraSense**, służąca do automatycznego podlewania roślin. Stacja komunikuje się z robotem przez Bluetooth Low Energy (BLE), wykrywa obecność robota za pomocą czujnika Hall i steruje pompą wodną.

## Architektura systemu

```
┌─────────────────────────────────────────────────────────────────┐
│                        FloraDock (Stacja)                        │
│                                                                  │
│  ┌─────────────────┐    ┌─────────────────┐    ┌─────────────┐  │
│  │   BLE Server    │    │  Czujnik Hall   │    │    Pompa    │  │
│  │                 │    │   (GPIO 32)     │    │  (GPIO 25)  │  │
│  │ - Water Service │    │                 │    │             │  │
│  │ - Hall State    │    │ Wykrywa magnes  │    │ MOSFET N-ch │  │
│  │                 │    │ robota          │    │             │  │
│  └────────┬────────┘    └────────┬────────┘    └──────┬──────┘  │
│           │                      │                     │         │
│           └──────────────────────┼─────────────────────┘         │
│                                  │                               │
│                          dock_control.c                          │
└─────────────────────────────────────────────────────────────────┘
                                  ▲
                                  │ BLE
                                  ▼
┌─────────────────────────────────────────────────────────────────┐
│                      FloraSense (Robot)                          │
│                                                                  │
│  Wysyła komendy podlewania przez BLE gdy:                       │
│  - Wilgotność gleby < próg                                      │
│  - Robot dotarł do stacji (IR z tyłu wykrywa ścianę)           │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

## Główne funkcjonalności

### Serwer BLE
- Udostępnia serwis podlewania (Water Service)
- Charakterystyka stanu Hall (read) - informuje czy robot jest obecny
- Charakterystyka komendy wody (write) - przyjmuje żądania podlewania
- Automatyczne rozgłaszanie (advertising) po rozłączeniu

### Wykrywanie robota
- Czujnik Hall wykrywa magnes zamontowany w robocie
- Stan 0 = robot obecny (magnes wykryty)
- Stan 1 = robot nieobecny
- Aktualizacja stanu w czasie rzeczywistym przez BLE

### Sterowanie pompą
- MOSFET N-channel do sterowania pompą/zaworem
- Konfigurowalna długość impulsu podlewania (100ms - 600s)
- Zabezpieczenie - podlewanie tylko gdy robot jest obecny (Hall = 0)

## Protokół BLE

### Serwis Water Service
| UUID | Typ | Opis |
|------|-----|------|
| Service | `0000FFE0-...` | Główny serwis podlewania |
| Hall State | `0000FFE1-...` | Charakterystyka read - stan czujnika Hall |
| Water Command | `0000FFE2-...` | Charakterystyka write - komenda podlewania |

### Charakterystyka Hall State
- **Typ:** Read
- **Wartość:** 1 bajt
  - `0x00` - Robot obecny (magnes wykryty)
  - `0x01` - Robot nieobecny

### Charakterystyka Water Command
- **Typ:** Write
- **Format:** 4 bajty (uint32_t little-endian)
- **Wartość:** Czas podlewania w milisekundach (100 - 600000 ms)

### Sekwencja podlewania (od strony robota)
1. Robot łączy się z FloraDock przez BLE
2. Odczytuje stan Hall - sprawdza czy jest obecny
3. Jeśli Hall = 0, wysyła komendę Water z czasem w ms
4. FloraDock włącza pompę na określony czas
5. Po zakończeniu robot może się rozłączyć lub wysłać kolejną komendę

## Struktura projektu

```
flora-sense-kopia/
├── main/
│   ├── flora-sense.c           # Główna aplikacja (tryb stacji dokującej)
│   ├── config.h                # Konfiguracja GPIO
│   │
│   ├── # BLE
│   ├── ble_server.c/h          # Serwer BLE (Water Service)
│   │
│   ├── # Kontrola stacji
│   ├── dock_control.c/h        # Sterowanie Hall + MOSFET/pompa
│   │
│   ├── # Nieużywane w trybie dock (zachowane dla kompatybilności)
│   ├── wifi.c/h
│   ├── mqtt_client.c
│   ├── motor_controller.c/h
│   ├── wsn_controller.c/h
│   ├── http_client.c/h
│   ├── ble_client.c/h
│   └── sensors/
│
├── docs/
│   ├── MQTT_API.md             # (nieużywane w trybie dock)
│   ├── BLE_API.md              # Dokumentacja protokołu BLE
│   └── mpu6050_DOCS.md
│
├── CMakeLists.txt
└── sdkconfig
```

## Konfiguracja sprzętowa

### Pinout ESP32

| Funkcja | GPIO | Opis |
|---------|------|------|
| DOCK_HALL_GPIO | 32 | Wejście czujnika Hall (pull-up) |
| DOCK_PUMP_GPIO | 25 | Wyjście sterujące MOSFET/pompą |
| BLINK_GPIO | 2 | LED statusu (opcjonalnie) |

### Schemat połączeń

```
                    ESP32
                  ┌───────┐
                  │       │
    Hall ────────│ GPIO32│ (INPUT, PULL-UP)
                  │       │
                  │ GPIO25│──────┬──── MOSFET Gate
                  │       │      │
                  │  GND  │──────┴──── MOSFET Source
                  └───────┘

                    MOSFET (N-channel)
                  ┌───────┐
    GPIO25 ──────│  G    │
                  │       │
    GND ─────────│  S    │
                  │       │
    Pompa (-) ───│  D    │
                  └───────┘

    Pompa (+) ──── VCC (12V/5V zależnie od pompy)
```

### Czujnik Hall
- Typ: Cyfrowy czujnik Hall (np. A3144, OH49E)
- Zasilanie: 3.3V lub 5V
- Wyjście: Open-drain lub push-pull
- Pull-up: Wewnętrzny ESP32 lub zewnętrzny 10kΩ

### Pompa/Zawór
- Sterowanie przez MOSFET N-channel (np. IRLZ44N, 2N7000)
- Napięcie pompy: 5V-12V (zależnie od modelu)
- Dioda zabezpieczająca równolegle do pompy (flyback)

## Konfiguracja

### GPIO (config.h)
```c
#define DOCK_HALL_GPIO    GPIO_NUM_32  // Czujnik Hall
#define DOCK_PUMP_GPIO    GPIO_NUM_25  // MOSFET/Pompa
```

## Kompilacja i flashowanie

```bash
# Przygotowanie środowiska
. $HOME/esp/esp-idf/export.sh

# Kompilacja
idf.py build

# Flashowanie i monitor
idf.py -p /dev/ttyUSB0 flash monitor
```

## Działanie systemu

### Sekwencja startowa
1. Inicjalizacja NVS
2. Inicjalizacja dock_control (Hall + MOSFET)
3. Rejestracja callbacka stanu Hall
4. Inicjalizacja i start serwera BLE
5. Rozpoczęcie rozgłaszania BLE (advertising)

### Logi
```
I (xxx) flora-sense: Start trybu stacji dokującej
I (xxx) DOCK_CTRL: Zainicjalizowano dock_control (Hall=GPIO32, Pump=GPIO25)
I (xxx) BLE_SERVER: BLE server initialized, advertising started
I (xxx) DOCK_CTRL: Hall state changed: 1 -> 0 (robot present)
I (xxx) DOCK_CTRL: Water command: 5000ms
I (xxx) DOCK_CTRL: Pump ON
I (xxx) DOCK_CTRL: Pump OFF (completed)
```

## API dock_control

```c
// Inicjalizacja
esp_err_t dock_control_init(
    gpio_num_t hall_gpio,      // GPIO czujnika Hall
    gpio_num_t pump_gpio,      // GPIO sterowania pompą
    dock_control_hall_cb_t cb  // Callback zmiany stanu Hall
);

// Odczyt stanu Hall
uint8_t dock_control_get_hall_state(void);  // 0=present, 1=absent

// Sprawdzenie czy pompa jest aktywna
bool dock_control_is_pump_active(void);

// Obsługa komendy podlewania
esp_err_t dock_control_handle_water_command(uint32_t duration_ms);

// Zatrzymanie pompy (emergency stop)
void dock_control_stop_pump(void);
```

## Dokumentacja

- **[BLE API](docs/BLE_API.md)** - Pełna dokumentacja protokołu BLE
- **[Hardware](docs/HARDWARE.md)** - Schemat połączeń i lista komponentów

## Powiązane projekty

- **[FloraSense](../flora-sense/)** - Mobilny robot monitoringu roślin

## Licencja

All rights reserved

## Autorzy

Robert Jacak, Maciej Jamrozy, Kacper Gałek, Paweł Knot
