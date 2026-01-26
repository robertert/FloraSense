# FloraSense - Mobilny Robot Monitoringu Roślin

## Opis projektu

**FloraSense** to inteligentny, mobilny robot do monitorowania i pielęgnacji roślin oparty na mikrokontrolerze ESP32. Robot autonomicznie podąża za światłem (fototropia), monitoruje warunki środowiskowe roślin i komunikuje się ze stacją dokującą **FloraDock** w celu automatycznego podlewania.

## Architektura systemu

```
┌─────────────────────────────────────────────────────────────────┐
│                        FloraSense (Robot)                        │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐  │
│  │   Czujniki  │  │   Silniki   │  │    Komunikacja          │  │
│  │  - Światło  │  │  - Motor A  │  │  - WiFi + MQTT          │  │
│  │  - Gleba    │  │  - Motor B  │  │  - BLE Client (Dock)    │  │
│  │  - Temp     │  │             │  │                         │  │
│  │  - IR (x2)  │  └─────────────┘  └─────────────────────────┘  │
│  │  - Hall     │                                                 │
│  │  - Dock     │                                                 │
│  └─────────────┘                                                 │
└─────────────────────────────────────────────────────────────────┘
           │                                      │
           │ BLE                                  │ MQTT
           ▼                                      ▼
┌─────────────────────┐                ┌─────────────────────┐
│    FloraDock        │                │    MQTT Broker      │
│  (Stacja dokująca)  │                │    + Aplikacja      │
│  - BLE Server       │                └─────────────────────┘
│  - Pompa wody       │
│  - Czujnik Hall     │
└─────────────────────┘
```

## Główne funkcjonalności

### Autonomiczna fototropia (WSN Controller)
- Automatyczne podążanie za źródłem światła
- Dwa czujniki światła VEML7700 (przód/tył) do wykrywania kierunku
- Konfigurowalne progi różnicy światła i interwały sprawdzania
- Możliwość włączania/wyłączania przez MQTT

### Monitorowanie przeszkód
- Dwa czujniki IR (przód/tył) do wykrywania przeszkód
- Niezależny task bezpieczeństwa z najwyższym priorytetem
- Automatyczne zatrzymywanie silników przy wykryciu przeszkody

### Automatyczne podlewanie
- Monitorowanie wilgotności gleby
- Automatyczna jazda do stacji dokującej (FloraDock) przy niskiej wilgotności
- Komunikacja BLE ze stacją dokującą
- Weryfikacja obecności robota (czujnik Hall) przed podlewaniem

### Komunikacja MQTT
- Publikacja danych z czujników co konfigurowalne interwały
- Zdalne sterowanie silnikami (przód/tył/lewo/prawo)
- Konfiguracja alarmów (temperatura, wilgotność gleby, bateria)
- Zdalne wywoływanie podlewania i wyszukiwania światła

## Czujniki

| Czujnik | Model | Interfejs | Opis |
|---------|-------|-----------|------|
| Światło (x2) | VEML7700 | I2C | Pomiar natężenia światła (lux) |
| Wilgotność gleby | Pojemnościowy | ADC | Pomiar wilgotności podłoża |
| Temperatura/Wilgotność | BME280 | I2C | Warunki atmosferyczne |
| Przeszkody (x2) | IR Obstacle | GPIO | Wykrywanie przeszkód (przód/tył) |
| Hall | Cyfrowy | GPIO | Wykrywanie pola magnetycznego |
| Dock | Cyfrowy | GPIO | Wykrywanie stacji dokującej |

## Struktura projektu

```
flora-sense/
├── main/
│   ├── flora-sense.c           # Główna aplikacja, inicjalizacja tasków
│   ├── config.h                # Konfiguracja GPIO, I2C, ADC, PWM
│   │
│   ├── # Komunikacja
│   ├── wifi.c/h                # Obsługa WiFi (STA mode)
│   ├── mqtt_client.c           # Klient MQTT, publikacja/subskrypcja
│   ├── flora_mqtt.h            # Definicje MQTT (topiki, konfiguracja)
│   ├── ble_dock_client.c/h     # Klient BLE do komunikacji z FloraDock
│   │
│   ├── # Kontrolery
│   ├── motor_controller.c/h    # Sterownik silników (TB6612FNG)
│   ├── wsn_controller.c/h      # Kontroler fototropii (podążanie za światłem)
│   ├── water_controller.c/h    # Kontroler automatycznego podlewania
│   │
│   ├── # Czujniki
│   ├── bmp280.c/h              # Driver czujnika BMP280
│   ├── mpu6050/                # Driver akcelerometru MPU6050
│   └── sensors/
│       ├── sensor_soil.c/h     # Czujnik wilgotności gleby
│       ├── sensor_light.c/h    # Czujnik światła VEML7700
│       ├── sensor_temp.c/h     # Czujnik temperatury BME280
│       ├── sensor_ir.c/h       # Czujniki IR (przeszkody)
│       ├── sensor_hall.c/h     # Czujnik Hall
│       └── sensor_dock.c/h     # Czujnik stacji dokującej
│
├── docs/
│   ├── MQTT_API.md             # Dokumentacja API MQTT
│   └── mpu6050_DOCS.md         # Dokumentacja MPU6050
│
├── CMakeLists.txt
└── sdkconfig
```

## Konfiguracja sprzętowa

### Pinout ESP32

#### Silniki (TB6612FNG)
| Funkcja | GPIO | Opis |
|---------|------|------|
| MOTOR_A_PWM | 13 | PWM silnika A |
| MOTOR_A_IN1 | 27 | Kierunek A |
| MOTOR_A_IN2 | 14 | Kierunek A |
| MOTOR_B_PWM | 4 | PWM silnika B |
| MOTOR_B_IN1 | 16 | Kierunek B |
| MOTOR_B_IN2 | 17 | Kierunek B |

#### I2C
| Funkcja | GPIO | Opis |
|---------|------|------|
| I2C_0 SDA | 21 | Czujnik światła 1, BME280 |
| I2C_0 SCL | 22 | Czujnik światła 1, BME280 |
| I2C_1 SDA | 32 | Czujnik światła 2 |
| I2C_1 SCL | 33 | Czujnik światła 2 |

#### ADC
| Funkcja | GPIO | Kanał |
|---------|------|-------|
| Czujnik gleby | 34 | ADC1_CH6 |
| Bateria | 35 | ADC1_CH7 |

## Wymagania

- **ESP-IDF** v5.0 lub nowszy
- **Mikrokontroler ESP32** (z WiFi i BLE)
- **Sterownik silników** TB6612FNG lub kompatybilny
- **Stacja dokująca** FloraDock (osobny projekt)

## Konfiguracja

### 1. WiFi
Edytuj `main/config.h`:
```c
#define EXAMPLE_ESP_WIFI_SSID      "Twoja_Sieć"
#define EXAMPLE_ESP_WIFI_PASS      "Twoje_Hasło"
```

### 2. MQTT Broker
Edytuj `main/mqtt_client.c`:
```c
#define MQTT_BROKER_URI "mqtt://adres_brokera:1883"
```

### 3. Kalibracja silników
W pliku `main/config.h` ustaw ile cm robot przejeżdża w 1 sekundę:
```c
#define MOTOR_CM_PER_SECOND_128  24.0f
```

## Kompilacja i flashowanie

```bash
# Przygotowanie środowiska
. $HOME/esp/esp-idf/export.sh

# Konfiguracja (opcjonalne)
idf.py menuconfig

# Kompilacja
idf.py build

# Flashowanie i monitor
idf.py -p /dev/ttyUSB0 flash monitor
```

## Działanie systemu

### Sekwencja startowa
1. Inicjalizacja NVS
2. Połączenie WiFi
3. Start klienta MQTT
4. Inicjalizacja czujników i silników
5. Start kontrolera WSN (fototropia)
6. Start kontrolera podlewania
7. Połączenie BLE z FloraDock

### Taski FreeRTOS
| Task | Priorytet | Opis |
|------|-----------|------|
| obstacle_monitor | 10 | Monitorowanie przeszkód (najwyższy) |
| mqtt_pub_task | 7 | Publikacja danych MQTT |
| wsn_controller | 5 | Kontroler fototropii |
| light_search | 5 | Obsługa żądań wyszukiwania światła |
| water_controller | 4 | Automatyczne podlewanie |

## Dokumentacja

- **[MQTT API](docs/MQTT_API.md)** - Pełna dokumentacja API MQTT (tematy, komendy, konfiguracja)
- **[BLE API](docs/BLE_API.md)** - Komunikacja BLE z FloraDock
- **[Hardware](docs/HARDWARE.md)** - Schemat połączeń i lista komponentów

## Powiązane projekty

- **[FloraDock](../flora-sense-kopia/)** - Stacja dokująca do podlewania

## Licencja

All rights reserved

## Autorzy

Robert Jacak, Maciej Jamrozy, Kacper Gałek, Paweł Knot
