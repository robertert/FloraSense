# Flora Sense

## Opis projektu

**Flora Sense** to inteligentny system monitoringu roślin oparty na mikrokontrolerze ESP32. Projekt umożliwia ciągłe monitorowanie warunków środowiskowych roślin poprzez zbiór różnych czujników, a także komunikację z systemami zewnętrznymi poprzez WiFi, Bluetooth Low Energy (BLE) oraz protokół MQTT.

## Główne funkcjonalności

### Czujniki środowiskowe

- **Czujnik wilgotności gleby** - pomiar poziomu wilgotności podłoża
- **Czujnik temperatury i wilgotności** (BME280) - monitorowanie warunków atmosferycznych
- **Czujnik ciśnienia** (BMP280) - pomiar ciśnienia atmosferycznego
- **Czujnik światła** (VEML7700) - pomiar natężenia światła
- **Czujnik zbliżeniowy** - wykrywanie obiektów w pobliżu
- **Czujnik Hall** - wykrywanie pola magnetycznego
- **Czujnik IR** - wykrywanie przeszkód
- **Czujnik dock** - wykrywanie podłączenia do stacji dokującej
- **Akcelerometr/żyroskop** (MPU6050) - pomiar orientacji i ruchu

### Komunikacja

- **WiFi** - połączenie z siecią lokalną
- **Bluetooth Low Energy (BLE)** - komunikacja bezprzewodowa (klient i serwer)
- **MQTT** - publikacja danych do brokerów MQTT
- **HTTP Client** - komunikacja z serwerami HTTP

### Kontrola

- **Kontroler silników** - sterowanie urządzeniami wykonawczymi

## Struktura projektu

```
flora-sense/
├── main/
│   ├── flora-sense.c          # Główny plik aplikacji
│   ├── config.h               # Konfiguracja GPIO, I2C, ADC
│   ├── wifi.c/h               # Obsługa WiFi
│   ├── mqtt_client.c          # Klient MQTT
│   ├── http_client.c/h        # Klient HTTP
│   ├── ble_client.c/h         # BLE klient
│   ├── ble_server.c/h         # BLE serwer
│   ├── motor_controller.c/h   # Kontroler silników
│   ├── bmp280.c/h             # Obsługa czujnika BMP280
│   ├── mpu6050.c/h            # Obsługa czujnika MPU6050
│   └── sensors/               # Moduły czujników
│       ├── sensor_soil.c/h    # Czujnik gleby
│       ├── sensor_temp.c/h    # Czujnik temperatury/wilgotności
│       ├── sensor_light.c/h   # Czujnik światła
│       ├── sensor_proximity.c/h
│       ├── sensor_hall.c/h
│       ├── sensor_ir.c/h
│       ├── sensor_dock.c/h
│       └── hardware_test.c/h
├── CMakeLists.txt
└── sdkconfig                  # Konfiguracja ESP-IDF
```

## Wymagania

- **ESP-IDF** (Espressif IoT Development Framework) v5.0 lub nowszy
- **Mikrokontroler ESP32**
- **Komponenty sprzętowe**:
  - Czujniki zgodne z wymienionymi powyżej
  - Moduł WiFi/Bluetooth (wbudowany w ESP32)

## Konfiguracja

### 1. Konfiguracja WiFi

Edytuj plik `main/config.h` i ustaw parametry sieci:

```c
#define EXAMPLE_ESP_WIFI_SSID      "Twoja_Sieć_WiFi"
#define EXAMPLE_ESP_WIFI_PASS      "Twoje_Hasło"
```

### 2. Konfiguracja MQTT

W pliku `main/mqtt_client.c` ustaw adres brokera MQTT:

```c
#define MQTT_BROKER_URI "mqtt://adres_brokera:1883"
#define USER_ID     "twoj_user_id"
#define DEVICE_ID   "twoj_device_id"
```

### 3. Konfiguracja czujników

Parametry GPIO i I2C można skonfigurować w pliku `main/config.h`:

- Piny I2C (domyślnie GPIO21 - SDA, GPIO22 - SCL)
- Kanały ADC dla czujników analogowych
- Adresy I2C czujników

## Kompilacja i flashowanie

1. **Przygotowanie środowiska**:

   ```bash
   . $HOME/esp/esp-idf/export.sh
   ```

2. **Konfiguracja projektu**:

   ```bash
   idf.py menuconfig
   ```

3. **Kompilacja**:

   ```bash
   idf.py build
   ```

4. **Flashowanie**:
   ```bash
   idf.py -p /dev/ttyUSB0 flash monitor
   ```
   (Zastąp `/dev/ttyUSB0` odpowiednim portem szeregowym)

## Użycie

Po uruchomieniu urządzenie:

1. Inicjalizuje wszystkie skonfigurowane czujniki
2. Rozpoczyna odczyty z czujników w określonych interwałach
3. Publikuje dane przez MQTT (jeśli włączone)
4. Umożliwia komunikację przez BLE (jeśli włączone)

Dane z czujników są logowane do konsoli szeregowej oraz mogą być wysyłane przez MQTT do tematu:

```
florasense/{USER_ID}/{DEVICE_ID}/{nazwa_czujnika}
```

## Status projektu

Projekt jest w fazie rozwoju. Niektóre funkcjonalności mogą być wykomentowane w kodzie głównym (`flora-sense.c`) i wymagają aktywacji poprzez odkomentowanie odpowiednich linii.

## Licencja

All rights reserved

## Autorzy

Robert Jacak, Maciej Jamrozy, Kacper Gałek, Paweł Knot
