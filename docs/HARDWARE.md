# Dokumentacja Sprzętowa - FloraSense (Robot)

## Spis treści

1. [Przegląd](#przegląd)
2. [Lista komponentów](#lista-komponentów)
3. [Schemat blokowy](#schemat-blokowy)
4. [Pinout ESP32](#pinout-esp32)
5. [Schematy połączeń](#schematy-połączeń)
6. [Zasilanie](#zasilanie)
7. [Montaż mechaniczny](#montaż-mechaniczny)

---

## Przegląd

FloraSense to mobilny robot oparty na ESP32 z napędem różnicowym (2 silniki DC). Robot wyposażony jest w szereg czujników do monitorowania środowiska rośliny oraz wykrywania przeszkód.

### Specyfikacja

| Parametr | Wartość |
|----------|---------|
| Mikrokontroler | ESP32-WROOM-32 |
| Zasilanie | Bateria LiPo 7.4V (2S) |
| Silniki | 2x DC z przekładnią |
| Wymiary | TBD |
| Masa | TBD |

---

## Lista komponentów

### Elektronika główna

| Komponent | Model | Ilość | Opis |
|-----------|-------|-------|------|
| Mikrokontroler | ESP32-WROOM-32 | 1 | Moduł WiFi+BLE |
| Sterownik silników | TB6612FNG | 1 | Dual H-Bridge |
| Regulator napięcia | LM2596 / AMS1117-3.3 | 1 | Step-down do 3.3V |

### Czujniki

| Komponent | Model | Ilość | Interfejs | Opis |
|-----------|-------|-------|-----------|------|
| Czujnik światła | VEML7700 | 2 | I2C | Przód i tył |
| Czujnik temp/wilg | BME280 | 1 | I2C | Temperatura, wilgotność, ciśnienie |
| Czujnik gleby | Pojemnościowy | 1 | ADC | Wilgotność gleby |
| Czujnik IR | FC-51 / TCRT5000 | 2 | GPIO | Przeszkody przód/tył |
| Czujnik Hall | A3144 / OH49E | 1 | GPIO | Wykrywanie stacji |
| Czujnik dock | Mechaniczny | 1 | GPIO | Kontakt ze stacją |

### Napęd

| Komponent | Model | Ilość | Opis |
|-----------|-------|-------|------|
| Silnik DC | N20 z przekładnią | 2 | 6V, ~200 RPM |
| Koło | Gumowe 65mm | 2 | Z mocowaniem do N20 |
| Koło podporowe | Kulka lub rolka | 1 | Swobodne |

### Zasilanie

| Komponent | Model | Ilość | Opis |
|-----------|-------|-------|------|
| Bateria | LiPo 2S 7.4V | 1 | 1000-2000 mAh |
| Moduł BMS | 2S 7.4V | 1 | Ochrona baterii |
| Złącze ładowania | XT30 / JST | 1 | |

---

## Schemat blokowy

```
                                    ┌─────────────────┐
                                    │   Bateria 7.4V  │
                                    │   LiPo 2S       │
                                    └────────┬────────┘
                                             │
                              ┌──────────────┼──────────────┐
                              │              │              │
                              ▼              ▼              ▼
                    ┌─────────────┐  ┌─────────────┐  ┌─────────────┐
                    │  Regulator  │  │  TB6612FNG  │  │  ADC (bat)  │
                    │  3.3V       │  │  VM = 7.4V  │  │  GPIO35     │
                    └──────┬──────┘  └──────┬──────┘  └─────────────┘
                           │                │
                           ▼                │
┌──────────────────────────────────────────────────────────────────────┐
│                              ESP32                                    │
│                                                                       │
│  I2C_0 (21,22)          GPIO Silniki        ADC                      │
│  ┌─────────────┐        ┌─────────────┐     ┌─────────────┐          │
│  │ VEML7700 #1 │        │ PWMA (13)   │     │ Gleba (34)  │          │
│  │ BME280      │        │ AIN1 (27)   │     │ Bat   (35)  │          │
│  └─────────────┘        │ AIN2 (14)   │     └─────────────┘          │
│                         │ PWMB (4)    │                               │
│  I2C_1 (32,33)          │ BIN1 (16)   │     GPIO Digital              │
│  ┌─────────────┐        │ BIN2 (17)   │     ┌─────────────┐          │
│  │ VEML7700 #2 │        └─────────────┘     │ IR #1 (25)  │          │
│  └─────────────┘                             │ IR #2 (26)  │          │
│                                              │ Hall        │          │
│                                              │ Dock        │          │
│                                              │ LED (2)     │          │
│                                              └─────────────┘          │
│                                                                       │
│                    WiFi + BLE (wbudowane)                            │
└──────────────────────────────────────────────────────────────────────┘
```

---

## Pinout ESP32

### Pełna tabela pinów

| GPIO | Funkcja | Typ | Opis |
|------|---------|-----|------|
| 2 | BLINK_LED | OUTPUT | LED statusu |
| 4 | MOTOR_B_PWM | PWM | Prędkość silnika B |
| 13 | MOTOR_A_PWM | PWM | Prędkość silnika A |
| 14 | MOTOR_A_IN2 | OUTPUT | Kierunek silnika A |
| 16 | MOTOR_B_IN1 | OUTPUT | Kierunek silnika B |
| 17 | MOTOR_B_IN2 | OUTPUT | Kierunek silnika B |
| 21 | I2C_0_SDA | I2C | VEML7700 #1, BME280 |
| 22 | I2C_0_SCL | I2C | VEML7700 #1, BME280 |
| 25 | IR_SENSOR_1 | INPUT | Czujnik IR #1 (tył) |
| 26 | IR_SENSOR_2 | INPUT | Czujnik IR #2 (przód) |
| 27 | MOTOR_A_IN1 | OUTPUT | Kierunek silnika A |
| 32 | I2C_1_SDA | I2C | VEML7700 #2 |
| 33 | I2C_1_SCL | I2C | VEML7700 #2 |
| 34 | SOIL_ADC | ADC (input only) | Czujnik gleby |
| 35 | BATTERY_ADC | ADC (input only) | Monitoring baterii |

### Piny zarezerwowane (nie używać)

| GPIO | Powód |
|------|-------|
| 0, 1, 3 | Boot, TX, RX |
| 6-11 | Flash SPI |
| 12 | Boot strapping |
| 34-39 | Input only (brak pull-up) |

---

## Schematy połączeń

### Sterownik silników TB6612FNG

```
                TB6612FNG
              ┌───────────┐
    ESP32     │           │     Silniki
              │           │
    GPIO13 ──►│ PWMA      │
    GPIO27 ──►│ AIN1   AOUT1├──► Motor A (+)
    GPIO14 ──►│ AIN2   AOUT2├──► Motor A (-)
              │           │
    GPIO4  ──►│ PWMB      │
    GPIO16 ──►│ BIN1   BOUT1├──► Motor B (+)
    GPIO17 ──►│ BIN2   BOUT2├──► Motor B (-)
              │           │
    3.3V   ──►│ STBY      │     (zawsze HIGH)
              │           │
    7.4V   ──►│ VM        │     (zasilanie silników)
    GND    ──►│ GND       │
    3.3V   ──►│ VCC       │     (logika)
              └───────────┘
```

### Czujnik światła VEML7700 (I2C)

```
    VEML7700                ESP32
   ┌────────┐
   │        │
   │ VCC    ├────────────── 3.3V
   │ GND    ├────────────── GND
   │ SDA    ├────────────── GPIO21 (I2C_0) lub GPIO32 (I2C_1)
   │ SCL    ├────────────── GPIO22 (I2C_0) lub GPIO33 (I2C_1)
   │        │
   └────────┘

   Adres I2C: 0x10
   Częstotliwość: 50 kHz (zmniejszona dla stabilności)
```

### Czujnik temperatury BME280 (I2C)

```
    BME280                  ESP32
   ┌────────┐
   │        │
   │ VCC    ├────────────── 3.3V
   │ GND    ├────────────── GND
   │ SDA    ├────────────── GPIO21 (I2C_0)
   │ SCL    ├────────────── GPIO22 (I2C_0)
   │ SDO    ├────────────── GND (adres 0x76) lub VCC (adres 0x77)
   │        │
   └────────┘

   Adres I2C: 0x76 (SDO → GND)
```

### Czujnik wilgotności gleby (ADC)

```
    Czujnik gleby           ESP32
   ┌────────┐
   │        │
   │ VCC    ├────────────── 3.3V
   │ GND    ├────────────── GND
   │ AOUT   ├────────────── GPIO34 (ADC1_CH6)
   │        │
   └────────┘

   Kalibracja (config.h):
   - Sucha gleba: ~3000 mV
   - Mokra gleba: ~1100 mV
```

### Czujniki IR (FC-51 / TCRT5000)

```
    Czujnik IR              ESP32
   ┌────────┐
   │        │
   │ VCC    ├────────────── 3.3V lub 5V
   │ GND    ├────────────── GND
   │ OUT    ├────────────── GPIO25 (IR #1) lub GPIO26 (IR #2)
   │        │
   └────────┘

   Logika:
   - HIGH (1): Brak przeszkody
   - LOW (0): Przeszkoda wykryta
```

### Monitoring baterii (dzielnik napięcia)

```
    Bateria 7.4V            ESP32
        │
        │
        ├────[R1 20kΩ]────┬──────── GPIO35 (ADC1_CH7)
        │                 │
        │            [R2 10kΩ]
        │                 │
        └─────────────────┴──────── GND

   Dzielnik 1:3 (BATTERY_VOLTAGE_DIVIDER = 3.0)
   Zakres: 0-9.9V → 0-3.3V na ADC
```

---

## Zasilanie

### Schemat zasilania

```
    Bateria LiPo 2S (7.4V)
           │
           │
           │
           ├────────────────────────────────► TB6612FNG VM (7.4V)
           │
           │
    ┌──────┴──────┐
    │   Step-Down │
    │   LM2596    │     7.4V → 5V (opcjonalne dla czujników 5V)
    │   lub       │
    │   AMS1117   │     7.4V → 3.3V
    │             │
    └──────┬──────┘
           │
           └────────────────────────────────► ESP32 VIN (3.3V)
                                             Czujniki (3.3V)
```

### Pobór prądu

| Komponent | Typowy pobór | Maksymalny |
|-----------|--------------|------------|
| ESP32 (WiFi+BLE) | 80-150 mA | 300 mA |
| Silniki (2x) | 100-200 mA | 500 mA |
| Czujniki | 10-20 mA | 50 mA |
| **Suma** | ~200-400 mA | ~850 mA |

### Szacunkowy czas pracy

Z baterią 1500 mAh:
- Aktywna praca: ~3-4 godziny
- Stan spoczynku (deep sleep): wiele dni

---

## Montaż mechaniczny

### Układ komponentów

```
                    PRZÓD
         ┌─────────────────────┐
         │    VEML7700 #1      │   Czujnik światła (przód)
         │    IR #2            │   Czujnik przeszkód (przód)
         │                     │
    ┌────┤                     ├────┐
    │    │      ┌───────┐      │    │
    │ M  │      │ ESP32 │      │  M │   Silniki po bokach
    │ O  │      │       │      │  O │
    │ T  │      │ BME280│      │  T │
    │ O  │      │       │      │  O │
    │ R  │      └───────┘      │  R │
    │    │                     │    │
    │ A  │    ┌─────────────┐  │  B │
    │    │    │   BATERIA   │  │    │
    └────┤    │   LiPo 2S   │  ├────┘
         │    └─────────────┘  │
         │    TB6612FNG        │
         │                     │
         │    VEML7700 #2      │   Czujnik światła (tył)
         │    IR #1            │   Czujnik przeszkód (tył)
         │    MAGNES (Hall)    │   Do wykrycia przez stację
         └─────────────────────┘
                    TYŁ
              [Koło podporowe]
```

### Montaż czujnika gleby

```
         Robot
    ┌──────────────┐
    │              │
    │              │
    │              │
    └──────────────┘
           │
           │   Przewód elastyczny
           │
    ┌──────┴──────┐
    │   Czujnik   │
    │   gleby     │   Wbity w ziemię doniczki
    └─────────────┘
```

---

---

## Changelog

- **v1.0** - Podstawowa dokumentacja sprzętowa
