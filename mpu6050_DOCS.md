# Dokumentacja Biblioteki ESP-IDF MPU6050

To jest sterownik (komponent) dla układu **MPU-6050** (akcelerometr + żyroskop 6-osiowy) przeznaczony dla frameworka **ESP-IDF** (wersja 5.x+). Biblioteka obsługuje kluczowe funkcje rejestrowe, w tym FIFO, przerwania sprzętowe oraz zarządzanie energią.

## Cechy Biblioteki

- **Pełna obsługa sensorów:** Akcelerometr, Żyroskop, Temperatura.
- **Konfiguracja:** Wybór zakresów pomiarowych (Scale Range), filtrów cyfrowych (DLPF) i dzielnika częstotliwości (Sample Rate).
- **Zarządzanie Energią:** Obsługa trybów Sleep Mode oraz Cycle Mode (Low Power Accelerometer).
- **FIFO:** Pełna obsługa wewnętrznego bufora (1024 bajty) z możliwością wyboru zapisywanych danych.
- **Przerwania:** Konfiguracja pinu INT (Active Low/High, Push-Pull/Open-Drain, Latch) oraz źródeł przerwań (Data Ready, FIFO Overflow).
- **Kalibracja:** Funkcje do odczytu i zapisu sprzętowych offsetów.

## Struktura Plików

- `mpu6050.h` - Plik nagłówkowy z definicjami i deklaracjami funkcji publicznych.
- `mpu6050.c` - Implementacja sterownika.

## Instalacja

1.  Utwórz katalog `components/mpu6050` w swoim projekcie ESP-IDF.
2.  Skopiuj pliki `mpu6050.h` i `mpu6050.c` do tego katalogu.
3.  Utwórz plik `CMakeLists.txt` wewnątrz katalogu `components/mpu6050` z treścią:
    ```cmake
    idf_component_register(SRCS "mpu6050.c"
                           INCLUDE_DIRS "."
                           REQUIRES driver)
    ```

## Podłączenie Sprzętowe

| Pin MPU6050 | ESP32 (Domyślne) | Opis                                             |
| :---------- | :--------------- | :----------------------------------------------- |
| **VCC**     | 3.3V             | Zasilanie (2.375V - 3.46V)                       |
| **GND**     | GND              | Masa                                             |
| **SCL**     | GPIO 22          | Linia zegarowa I2C                               |
| **SDA**     | GPIO 21          | Linia danych I2C                                 |
| **AD0**     | GND              | Adres I2C ustawiony programowo na **0x68**       |
| **INT**     | GPIO 19 (Opcja)  | Pin przerwania (wymagany dla funkcji Interrupts) |

---

## Parametry Konfiguracji

Podstawą działania biblioteki jest struktura `mpu6050_config_t`, którą należy wypełnić przed inicjalizacją. Pozwala ona na elastyczne dopasowanie sterownika do sprzętu.

```c
typedef struct {
    i2c_port_t i2c_port;          // Numer portu I2C w ESP32
    uint8_t i2c_address;          // Adres urządzenia na magistrali
    gpio_num_t sda_pin;           // Numer pinu SDA
    gpio_num_t scl_pin;           // Numer pinu SCL
    uint32_t i2c_freq_hz;         // Prędkość zegara magistrali (w Hz)
    bool enable_internal_pullup;  // Włączenie wewnętrznych rezystorów podciągających
} mpu6050_config_t;
```

### Opis Pól Konfiguracji

| Parametr                     | Typ          | Opis i Wartości                                                                                                                                                                                 |
| :--------------------------- | :----------- | :---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **`i2c_port`**               | `i2c_port_t` | Wybór sprzętowego kontrolera I2C w ESP32.<br>• `I2C_NUM_0` (Domyślny)<br>• `I2C_NUM_1`                                                                                                          |
| **`i2c_address`**            | `uint8_t`    | 7-bitowy adres urządzenia I2C.<br>• **`0x70`**: DEFAULT                                                                                                                                         |
| **`sda_pin`**                | `gpio_num_t` | Numer pinu GPIO dla linii danych (np. `GPIO_NUM_21`).                                                                                                                                           |
| **`scl_pin`**                | `gpio_num_t` | Numer pinu GPIO dla linii zegarowej (np. `GPIO_NUM_22`).                                                                                                                                        |
| **`i2c_freq_hz`**            | `uint32_t`   | Częstotliwość taktowania magistrali.<br>• `100000` (100 kHz) – Standard Mode.<br>• `400000` (400 kHz) – Fast Mode (Zalecane dla FIFO).                                                          |
| **`enable_internal_pullup`** | `bool`       | Włącza wewnętrzne rezystory podciągające w ESP32 na liniach SDA/SCL.<br>• `true`: Zalecane, jeśli moduł nie ma własnych rezystorów.<br>• `false`: Użyj, jeśli masz zewnętrzne rezystory na PCB. |

---

## Przewodnik Użycia (API Reference)

### 1\. Inicjalizacja

Przed użyciem czujnika należy skonfigurować magistralę I2C i zainicjalizować układ. Funkcja `init` automatycznie wybudza układ ze stanu uśpienia.

```c
#include "mpu6050.h"

mpu6050_handle_t mpu;
mpu6050_config_t conf = {
    .i2c_port = I2C_NUM_0,
    .i2c_address = 0x68,      // Adres urządzenia
    .sda_pin = GPIO_NUM_21,
    .scl_pin = GPIO_NUM_22,
    .i2c_freq_hz = 400000,
    .enable_internal_pullup = true
};

if (mpu6050_init(&conf, &mpu) == ESP_OK) {
    // Inicjalizacja udana
}
```

### 2\. Odczyt Danych (Polling)

Najprostszy sposób odczytu. Funkcja `mpu6050_read_data` pobiera surowe dane i przelicza je na jednostki fizyczne, uwzględniając aktualnie ustawioną czułość.

```c
mpu6050_data_t data;
mpu6050_read_data(&mpu, &data);

printf("Acc: %.2f g, Gyro: %.2f dps, Temp: %.2f C\n",
       data.accel_x, data.gyro_z, data.temp_c);
```

### 3\. Konfiguracja Sensorów

Możesz zmieniać parametry pracy czujnika w locie.

- **Zakres Akcelerometru:** `MPU6050_ACCEL_FS_2G`, `_4G`, `_8G`, `_16G`.
- **Zakres Żyroskopu:** `MPU6050_GYRO_FS_250DPS` ... `_2000DPS`.
- **Filtr DLPF:** Pozwala wygładzić szumy (np. `MPU6050_DLPF_5HZ`).

<!-- end list -->

```c
// Ustawienie małej czułości akcelerometru (+/- 16g)
mpu6050_set_accel_fs(&mpu, MPU6050_ACCEL_FS_16G);

// Włączenie mocnego filtrowania (opóźnienie sygnału, ale gładki wykres)
mpu6050_set_dlpf(&mpu, MPU6050_DLPF_5HZ);
```

### 4\. Zarządzanie Energią

- **Sleep Mode:** Wyłącza wszystko, pobór prądu \~5µA.
- **Cycle Mode:** Układ budzi się okresowo, robi pomiar akcelerometru i zasypia. (Wymaga wyłączenia czujnika temperatury\!).

<!-- end list -->

```c
// Usypianie
mpu6050_set_sleep_mode(&mpu, true);

// Cycle Mode (Low Power)
mpu6050_set_temp_sensor(&mpu, false); // Wymagane dla Cycle Mode
mpu6050_set_cycle_mode(&mpu, true);
```

### 5\. Obsługa FIFO (Buforowanie)

FIFO pozwala zbierać dane w tle bez udziału procesora ESP32.

1.  Skonfiguruj co ma być zbierane (`mpu6050_set_fifo_enable_config`).
2.  Włącz FIFO (`mpu6050_set_fifo_enable`).
3.  Odczytaj ilość danych (`mpu6050_get_fifo_count`).
4.  Pobierz dane (`mpu6050_read_fifo`).

<!-- end list -->

```c
// Konfiguracja: tylko Temperatura i Accel
mpu6050_fifo_enable_t fifo_cfg = { .temp_fifo_en = true, .accel_fifo_en = true };
mpu6050_set_fifo_enable_config(&mpu, &fifo_cfg);

// ... po pewnym czasie ...
uint16_t count;
mpu6050_get_fifo_count(&mpu, &count);
if (count > 0) {
    uint8_t buf[64];
    mpu6050_read_fifo(&mpu, buf, count);
}
```

### 6\. Przerwania (Interrupts)

Pin INT pozwala na sprzętową synchronizację.

```c
// Konfiguracja pinu: Active High, Push-Pull, Latch (trzyma stan do odczytu)
mpu6050_int_pin_cfg_t int_cfg = {
    .int_level = false,
    .int_open = false,
    .latch_enable = true,
    .int_rd_clear = true
};
mpu6050_set_int_pin_cfg(&mpu, &int_cfg);

// Włączenie przerwania "Data Ready"
mpu6050_int_enable_t int_en = { .data_ready = true };
mpu6050_set_int_enable(&mpu, &int_en);
```

### 7\. Kalibracja (Offsety)

Możesz odczytać i zapisać sprzętowe rejestry offsetów, aby skorygować błąd zerowy czujnika (np. gdy leżąc płasko pokazuje 0.05g zamiast 0.00g).

```c
float ax, ay, az;
mpu6050_get_accel_offsets(&mpu, &ax, &ay, &az);
// Możesz zmodyfikować wartości i zapisać z powrotem (w jednostkach fizycznych):
mpu6050_set_accel_offsets(&mpu, ax + 0.01f, ay, az);
```

---

## API Reference

Poniżej znajduje się kompletna lista wszystkich funkcji udostępnianych przez bibliotekę, pogrupowanych według kategorii funkcjonalnych.

### Inicjalizacja i Zarządzanie Zasobami

#### `mpu6050_init()`

```c
esp_err_t mpu6050_init(const mpu6050_config_t *config, mpu6050_handle_t *handle);
```

**Opis:** Inicjalizuje szynę I2C, konfiguruje urządzenie MPU6050, wybudza je ze stanu uśpienia i weryfikuje połączenie poprzez odczyt rejestru WHO_AM_I (0x75).  
**Parametry:**

- `config` - wskaźnik do struktury konfiguracyjnej
- `handle` - wskaźnik do struktury handle (zostanie wypełniona po sukcesie)
  **Zwraca:** `ESP_OK` przy sukcesie, kod błędu w przeciwnym razie

#### `mpu6050_deinit()`

```c
esp_err_t mpu6050_deinit(mpu6050_handle_t *handle);
```

**Opis:** Usuwa urządzenie z szyny I2C, usuwa szynę i zwalnia zasoby.  
**Parametry:** `handle` - wskaźnik do struktury handle  
**Zwraca:** `ESP_OK` przy sukcesie

---

### Odczyt Danych

#### `mpu6050_read_raw()`

```c
esp_err_t mpu6050_read_raw(mpu6050_handle_t *handle, mpu6050_raw_data_t *data);
```

**Opis:** Wykonuje burst read 14 bajtów zaczynając od rejestru ACCEL_XOUT_H (0x3B). Zwraca surowe wartości całkowite (int16_t) dla wszystkich osi.  
**Parametry:**

- `handle` - wskaźnik do handle
- `data` - wskaźnik do struktury z surowymi danymi
  **Zwraca:** `ESP_OK` przy sukcesie

#### `mpu6050_read_data()`

```c
esp_err_t mpu6050_read_data(mpu6050_handle_t *handle, mpu6050_data_t *data);
```

**Opis:** Odczytuje surowe dane i automatycznie przelicza je na jednostki fizyczne (g dla akcelerometru, deg/s dla żyroskopu, °C dla temperatury) uwzględniając aktualnie ustawione zakresy.  
**Parametry:**

- `handle` - wskaźnik do handle
- `data` - wskaźnik do struktury z danymi w jednostkach fizycznych
  **Zwraca:** `ESP_OK` przy sukcesie

#### `mpu6050_accel_to_g()`

```c
float mpu6050_accel_to_g(mpu6050_handle_t *handle, int16_t raw);
```

**Opis:** Konwertuje surową wartość akcelerometru na jednostki g, uwzględniając aktualnie ustawiony zakres.  
**Parametry:**

- `handle` - wskaźnik do handle
- `raw` - surowa wartość akcelerometru (int16_t)
  **Zwraca:** Wartość w jednostkach g (float)

#### `mpu6050_gyro_to_dps()`

```c
float mpu6050_gyro_to_dps(mpu6050_handle_t *handle, int16_t raw);
```

**Opis:** Konwertuje surową wartość żyroskopu na jednostki deg/s, uwzględniając aktualnie ustawiony zakres.  
**Parametry:**

- `handle` - wskaźnik do handle
- `raw` - surowa wartość żyroskopu (int16_t)
  **Zwraca:** Wartość w jednostkach deg/s (float)

#### `mpu6050_temp_to_celsius()`

```c
float mpu6050_temp_to_celsius(int16_t raw);
```

**Opis:** Konwertuje surową wartość temperatury na stopnie Celsiusza. Formuła: T(°C) = (TEMP_OUT / 340) + 36.53  
**Parametry:** `raw` - surowa wartość temperatury (int16_t)  
**Zwraca:** Temperatura w °C (float)

---

### Konfiguracja Zakresów i Filtrów

#### `mpu6050_set_gyro_fs()` / `mpu6050_get_gyro_fs()`

```c
esp_err_t mpu6050_set_gyro_fs(mpu6050_handle_t *handle, mpu6050_gyro_fs_t fs_range);
esp_err_t mpu6050_get_gyro_fs(mpu6050_handle_t *handle, mpu6050_gyro_fs_t *fs_range);
```

**Opis:** Ustawia/odczytuje zakres żyroskopu w rejestrze GYRO_CONFIG (0x1B, bity 4:3).  
**Zakresy:** `MPU6050_GYRO_FS_250DPS`, `_500DPS`, `_1000DPS`, `_2000DPS`  
**Zwraca:** `ESP_OK` przy sukcesie

#### `mpu6050_set_accel_fs()` / `mpu6050_get_accel_fs()`

```c
esp_err_t mpu6050_set_accel_fs(mpu6050_handle_t *handle, mpu6050_accel_fs_t fs_range);
esp_err_t mpu6050_get_accel_fs(mpu6050_handle_t *handle, mpu6050_accel_fs_t *fs_range);
```

**Opis:** Ustawia/odczytuje zakres akcelerometru w rejestrze ACCEL_CONFIG (0x1C, bity 4:3).  
**Zakresy:** `MPU6050_ACCEL_FS_2G`, `_4G`, `_8G`, `_16G`  
**Zwraca:** `ESP_OK` przy sukcesie

#### `mpu6050_set_dlpf()` / `mpu6050_get_dlpf()`

```c
esp_err_t mpu6050_set_dlpf(mpu6050_handle_t *handle, mpu6050_dlpf_t dlpf);
esp_err_t mpu6050_get_dlpf(mpu6050_handle_t *handle, mpu6050_dlpf_t *dlpf);
```

**Opis:** Ustawia/odczytuje przepustowość filtra cyfrowego dolnoprzepustowego (DLPF) w rejestrze CONFIG (0x1A, bity 2:0).  
**Opcje:** `MPU6050_DLPF_260HZ`, `_184HZ`, `_94HZ`, `_44HZ`, `_21HZ`, `_10HZ`, `_5HZ`  
**Zwraca:** `ESP_OK` przy sukcesie

---

### Zarządzanie Zasilaniem

#### `mpu6050_set_sleep_mode()`

```c
esp_err_t mpu6050_set_sleep_mode(mpu6050_handle_t *handle, bool enable);
```

**Opis:** Włącza/wyłącza tryb uśpienia w rejestrze PWR_MGMT_1 (0x6B, bit 6). W trybie uśpienia wszystkie sensory są wyłączone, pobór prądu ~5µA.  
**Parametry:**

- `handle` - wskaźnik do handle
- `enable` - `true` aby włączyć sleep mode, `false` aby wyłączyć
  **Zwraca:** `ESP_OK` przy sukcesie

#### `mpu6050_set_cycle_mode()`

```c
esp_err_t mpu6050_set_cycle_mode(mpu6050_handle_t *handle, bool enable);
```

**Opis:** Włącza/wyłącza tryb cykliczny (Cycle Mode) w rejestrze PWR_MGMT_1 (0x6B, bit 5). W trybie cyklicznym układ budzi się okresowo, wykonuje pomiar akcelerometru i zasypia. Wymaga wyłączenia czujnika temperatury.  
**Parametry:**

- `handle` - wskaźnik do handle
- `enable` - `true` aby włączyć cycle mode, `false` aby wyłączyć
  **Zwraca:** `ESP_OK` przy sukcesie

#### `mpu6050_set_temp_sensor()`

```c
esp_err_t mpu6050_set_temp_sensor(mpu6050_handle_t *handle, bool enable);
```

**Opis:** Włącza/wyłącza czujnik temperatury w rejestrze PWR_MGMT_1 (0x6B, bit 3). Wyłączenie jest wymagane dla Cycle Mode.  
**Parametry:**

- `handle` - wskaźnik do handle
- `enable` - `true` aby włączyć, `false` aby wyłączyć
  **Zwraca:** `ESP_OK` przy sukcesie

---

### Obsługa FIFO

#### `mpu6050_set_fifo_enable()`

```c
esp_err_t mpu6050_set_fifo_enable(mpu6050_handle_t *handle, bool enable);
```

**Opis:** Włącza/wyłącza bufor FIFO w rejestrze USER_CTRL (0x6A, bit 6). FIFO może przechowywać do 1024 bajtów danych.  
**Parametry:**

- `handle` - wskaźnik do handle
- `enable` - `true` aby włączyć FIFO, `false` aby wyłączyć
  **Zwraca:** `ESP_OK` przy sukcesie

#### `mpu6050_reset_fifo()`

```c
esp_err_t mpu6050_reset_fifo(mpu6050_handle_t *handle);
```

**Opis:** Resetuje bufor FIFO poprzez ustawienie bitu reset w rejestrze USER_CTRL (0x6A, bit 2).  
**Parametry:** `handle` - wskaźnik do handle  
**Zwraca:** `ESP_OK` przy sukcesie

#### `mpu6050_get_fifo_count()`

```c
esp_err_t mpu6050_get_fifo_count(mpu6050_handle_t *handle, uint16_t *count);
```

**Opis:** Odczytuje liczbę bajtów dostępnych w buforze FIFO z rejestrów FIFO_COUNTH (0x72) i FIFO_COUNTL (0x73).  
**Parametry:**

- `handle` - wskaźnik do handle
- `count` - wskaźnik do zmiennej, gdzie zostanie zapisana liczba bajtów (0-1024)
  **Zwraca:** `ESP_OK` przy sukcesie

#### `mpu6050_read_fifo()`

```c
esp_err_t mpu6050_read_fifo(mpu6050_handle_t *handle, uint8_t *data, size_t len);
```

**Opis:** Odczytuje dane z bufora FIFO z rejestru FIFO_R_W (0x74). Rejestr automatycznie zwiększa adres przy każdym odczycie.  
**Parametry:**

- `handle` - wskaźnik do handle
- `data` - wskaźnik do bufora na dane
- `len` - liczba bajtów do odczytu
  **Zwraca:** `ESP_OK` przy sukcesie

#### `mpu6050_set_fifo_enable_config()`

```c
esp_err_t mpu6050_set_fifo_enable_config(mpu6050_handle_t *handle, const mpu6050_fifo_enable_t *fifo_en);
```

**Opis:** Konfiguruje, które dane mają być zapisywane do FIFO w rejestrze FIFO_EN (0x23).  
**Parametry:**

- `handle` - wskaźnik do handle
- `fifo_en` - wskaźnik do struktury konfiguracyjnej (temp_fifo_en, accel_fifo_en, xg_fifo_en, yg_fifo_en, zg_fifo_en)
  **Zwraca:** `ESP_OK` przy sukcesie

---

### Obsługa Przerwań

#### `mpu6050_set_int_pin_cfg()`

```c
esp_err_t mpu6050_set_int_pin_cfg(mpu6050_handle_t *handle, const mpu6050_int_pin_cfg_t *cfg);
```

**Opis:** Konfiguruje zachowanie pinu przerwania INT w rejestrze INT_PIN_CFG (0x37).  
**Parametry:**

- `handle` - wskaźnik do handle
- `cfg` - wskaźnik do struktury konfiguracyjnej (int_level, int_open, latch_enable, int_rd_clear, fsync_int_level, fsync_int_mode)
  **Zwraca:** `ESP_OK` przy sukcesie

#### `mpu6050_set_int_enable()`

```c
esp_err_t mpu6050_set_int_enable(mpu6050_handle_t *handle, const mpu6050_int_enable_t *int_en);
```

**Opis:** Włącza/wyłącza określone źródła przerwań w rejestrze INT_ENABLE (0x38).  
**Parametry:**

- `handle` - wskaźnik do handle
- `int_en` - wskaźnik do struktury z flagami (data_ready, fifo_overflow, motion_detect)
  **Zwraca:** `ESP_OK` przy sukcesie

#### `mpu6050_get_int_status()`

```c
esp_err_t mpu6050_get_int_status(mpu6050_handle_t *handle, mpu6050_int_enable_t *int_en);
```

**Opis:** Odczytuje status przerwań z rejestru INT_STATUS (0x3A).  
**Parametry:**

- `handle` - wskaźnik do handle
- `int_en` - wskaźnik do struktury, gdzie zostaną zapisane flagi statusu
  **Zwraca:** `ESP_OK` przy sukcesie

---

### Kalibracja Offsetów

#### `mpu6050_set_accel_offsets()` / `mpu6050_get_accel_offsets()`

```c
esp_err_t mpu6050_set_accel_offsets(mpu6050_handle_t *handle, float x_offset, float y_offset, float z_offset);
esp_err_t mpu6050_get_accel_offsets(mpu6050_handle_t *handle, float *x_offset, float *y_offset, float *z_offset);
```

**Opis:** Ustawia/odczytuje sprzętowe offsety akcelerometru w rejestrach XA/YA/ZA_OFFSET_H/L (0x06-0x0B). Wartości są podawane w jednostkach fizycznych (g) i automatycznie przeliczane na LSB na podstawie aktualnie ustawionego zakresu.  
**Parametry:**

- `handle` - wskaźnik do handle
- `x_offset`, `y_offset`, `z_offset` - offsety w jednostkach g (float)
  **Zwraca:** `ESP_OK` przy sukcesie

#### `mpu6050_set_gyro_offsets()` / `mpu6050_get_gyro_offsets()`

```c
esp_err_t mpu6050_set_gyro_offsets(mpu6050_handle_t *handle, float x_offset, float y_offset, float z_offset);
esp_err_t mpu6050_get_gyro_offsets(mpu6050_handle_t *handle, float *x_offset, float *y_offset, float *z_offset);
```

**Opis:** Ustawia/odczytuje sprzętowe offsety żyroskopu w rejestrach XG/YG/ZG_OFFSET_H/L (0x13-0x18). Wartości są podawane w jednostkach fizycznych (deg/s) i automatycznie przeliczane na LSB na podstawie aktualnie ustawionego zakresu.  
**Parametry:**

- `handle` - wskaźnik do handle
- `x_offset`, `y_offset`, `z_offset` - offsety w jednostkach deg/s (float)
  **Zwraca:** `ESP_OK` przy sukcesie

---

## Struktury Danych

### `mpu6050_config_t`

Struktura konfiguracyjna używana przy inicjalizacji. Zawiera parametry I2C (port, adres, piny, częstotliwość).

### `mpu6050_handle_t`

Struktura handle przechowująca informacje o połączeniu I2C i stanie inicjalizacji. Przekazywana do wszystkich funkcji biblioteki.

### `mpu6050_raw_data_t`

Struktura zawierająca surowe wartości odczytane z sensora (int16_t):

- `accel_x`, `accel_y`, `accel_z` - surowe wartości akcelerometru
- `temp` - surowa wartość temperatury
- `gyro_x`, `gyro_y`, `gyro_z` - surowe wartości żyroskopu

### `mpu6050_data_t`

Struktura zawierająca dane przeliczone na jednostki fizyczne (float):

- `accel_x`, `accel_y`, `accel_z` - wartości akcelerometru w g
- `temp_c` - temperatura w °C
- `gyro_x`, `gyro_y`, `gyro_z` - wartości żyroskopu w deg/s

### `mpu6050_fifo_enable_t`

Struktura konfiguracyjna dla FIFO określająca, które dane mają być zapisywane:

- `temp_fifo_en` - temperatura
- `accel_fifo_en` - akcelerometr (wszystkie osie)
- `xg_fifo_en`, `yg_fifo_en`, `zg_fifo_en` - poszczególne osie żyroskopu

### `mpu6050_int_pin_cfg_t`

Struktura konfiguracyjna pinu przerwania:

- `int_level` - poziom aktywny (false = high, true = low)
- `int_open` - tryb pinu (false = push-pull, true = open-drain)
- `latch_enable` - tryb zatrzaskowy (false = pulse, true = latch)
- `int_rd_clear` - czyścić przy odczycie
- `fsync_int_level`, `fsync_int_mode` - konfiguracja FSYNC

### `mpu6050_int_enable_t`

Struktura określająca źródła przerwań:

- `data_ready` - przerwanie gdy nowe dane są gotowe
- `fifo_overflow` - przerwanie przy przepełnieniu FIFO
- `motion_detect` - przerwanie przy wykryciu ruchu
