# Dokumentacja Sprzętowa - FloraDock (Stacja Dokująca)

## Spis treści

1. [Przegląd](#przegląd)
2. [Lista komponentów](#lista-komponentów)
3. [Schemat blokowy](#schemat-blokowy)
4. [Pinout ESP32](#pinout-esp32)
5. [Schematy połączeń](#schematy-połączeń)
6. [Zasilanie](#zasilanie)
7. [Montaż](#montaż)

---

## Przegląd

FloraDock to stacjonarna stacja dokująca służąca do podlewania roślin. Wykrywa obecność robota FloraSense za pomocą czujnika Hall i steruje pompą wodną przez MOSFET.

### Specyfikacja

| Parametr | Wartość |
|----------|---------|
| Mikrokontroler | ESP32-WROOM-32 |
| Zasilanie | 12V (pompa), 5V kabel |
| Pobór prądu | ~100 mA (ESP32) + pompa |
| Wymiary | TBD |

---

## Lista komponentów

### Elektronika

| Komponent | Model | Ilość | Opis |
|-----------|-------|-------|------|
| Mikrokontroler | ESP32-WROOM-32 | 1 | Moduł WiFi+BLE |
| MOSFET N-channel | IRLZ44N / IRF3205 | 1 | Sterowanie pompą |
| Czujnik Hall | A3144 / OH49E | 1 | Wykrywanie magnesu |
| Dioda | 1N4007 | 1 | Flyback dla pompy |
| Rezystor | 10kΩ | 1 | Pull-down gate MOSFET |
| Rezystor | 10kΩ | 1 | Pull-up Hall (opcjonalny) |

### System wodny

| Komponent | Model | Ilość | Opis |
|-----------|-------|-------|------|
| Pompa wodna | Mini 5V/12V DC | 1 | Zanurzeniowa lub membranowa |
| Wąż silikonowy | 6mm wewnętrzny | 1m | Doprowadzenie wody |
| Zbiornik wody | Plastikowy | 1 | 0.5-2L |
| Dysza | Opcjonalnie | 1 | Końcówka podlewająca |

---

## Schemat blokowy

```
┌─────────────────────────────────────────────────────────────────────┐
│                          FloraDock                                   │
│                                                                      │
│   ┌────────────────────────────────────────────────────────────┐    │
│   │                         ESP32                               │    │
│   │                                                             │    │
│   │   GPIO32 ◄──────── Czujnik Hall ◄──── [Magnes w robocie]   │    │
│   │      │                                                      │    │
│   │      │   ┌────────┐                                        │    │
│   │      └──►│ BLE    │◄───── [FloraSense Robot]              │    │
│   │          │ Server │                                        │    │
│   │          └────────┘                                        │    │
│   │                                                             │    │
│   │   GPIO25 ──────────► MOSFET Gate                           │    │
│   │                         │                                  │    │
│   └─────────────────────────┼──────────────────────────────────┘    │
│                             │                                        │
│                             ▼                                        │
│                      ┌──────────┐                                   │
│                      │  MOSFET  │                                   │
│                      │  N-ch    │                                   │
│                      └────┬─────┘                                   │
│                           │                                         │
│                           ▼                                         │
│    ┌──────────────────────────────────────────────────────┐        │
│    │                    Pompa wodna                        │        │
│    │                                                       │        │
│    │    Zbiornik ──► Pompa ──► Wąż ──► Doniczka           │        │
│    └──────────────────────────────────────────────────────┘        │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Pinout ESP32

### Używane piny

| GPIO | Funkcja | Typ | Opis |
|------|---------|-----|------|
| 32 | DOCK_HALL_GPIO | INPUT | Czujnik Hall (pull-up) |
| 25 | DOCK_PUMP_GPIO | OUTPUT | Sterowanie MOSFET |
| 2 | BLINK_GPIO | OUTPUT | LED statusu (opcjonalny) |

### Piny zarezerwowane (nie używać)

| GPIO | Powód |
|------|-------|
| 0, 1, 3 | Boot, TX, RX |
| 6-11 | Flash SPI |
| 12 | Boot strapping |

### Wolne piny (do rozbudowy)

| GPIO | Uwagi |
|------|-------|
| 4, 5, 13-19, 21-23 | Dostępne GPIO |
| 34-39 | Tylko wejścia (ADC) |

---

## Schematy połączeń

### Pełny schemat

```
                                    VCC (12V)
                                        │
                                        │
                                   ┌────┴────┐
                                   │  Pompa  │
                                   │   (+)   │
                                   └────┬────┘
                                        │
                              ┌─────────┴─────────┐
                              │                   │
                         ┌────┴────┐         ┌───┴───┐
                         │  Dioda  │         │       │
                         │ 1N4007  │         │ Pompa │
                         │ (katoda)│         │  (-)  │
                         └────┬────┘         └───┬───┘
                              │                   │
                              └─────────┬─────────┘
                                        │
                                   ┌────┴────┐
                                   │ MOSFET  │
                                   │ Drain   │
                                   │         │
         ESP32                     │  IRLZ44N│
        ┌────────┐                 │         │
        │        │                 └────┬────┘
        │ GPIO25 │──────[100Ω]────────►│ Gate
        │        │                      │
        │        │              [10kΩ]──┤
        │        │                      │
        │  GND   │──────────────────────┴──────────────► GND
        │        │                                        │
        │        │                                   ┌────┴────┐
        │        │                                   │ MOSFET  │
        │        │                                   │ Source  │
        └────────┘                                   └─────────┘


         Czujnik Hall                ESP32
        ┌────────┐                  ┌────────┐
        │        │                  │        │
        │ VCC    │──────────────────│ 3.3V   │
        │        │                  │        │
        │ GND    │──────────────────│ GND    │
        │        │                  │        │
        │ OUT    │──────┬───────────│ GPIO32 │
        │        │      │           │        │
        └────────┘   [10kΩ]         └────────┘
                        │
                        │
                       GND (opcjonalny pull-down)
```

### Czujnik Hall - szczegóły

```
    Czujnik Hall A3144          ESP32
    ┌───────────────┐
    │               │
    │  ┌───┐        │
    │  │ H │ VCC    ├─────────── 3.3V
    │  │ A │        │
    │  │ L │ GND    ├─────────── GND
    │  │ L │        │
    │  │   │ OUT    ├───┬─────── GPIO32
    │  └───┘        │   │
    │               │  10kΩ      (wewnętrzny pull-up ESP32
    │    [M]        │   │         lub zewnętrzny)
    │    Magnes     │  GND
    │   (robot)     │
    └───────────────┘

    Logika:
    - Brak magnesu: OUT = HIGH (1) - robot nieobecny
    - Magnes blisko: OUT = LOW (0) - robot obecny
```

### MOSFET i pompa - szczegóły

```
                            VCC (12V)
                                │
                           ┌────┴────┐
                           │         │
                           │  POMPA  │
                           │  (+)    │
                           └────┬────┘
                                │
                    ┌───────────┼───────────┐
                    │           │           │
              ┌─────┴─────┐     │     ┌─────┴─────┐
              │   Dioda   │     │     │   Pompa   │
              │  1N4007   │     │     │    (-)    │
              │  ───|◄─── │     │     └─────┬─────┘
              │  (katoda  │     │           │
              │   do VCC) │     │           │
              └─────┬─────┘     │           │
                    │           │           │
                    └───────────┴───────────┘
                                │
                           ┌────┴────┐
                           │ MOSFET  │
    ESP32                  │ Drain   │
    ┌────────┐             │         │
    │        │             │ IRLZ44N │
    │ GPIO25 ├───[100Ω]───►│ Gate    │
    │        │             │         │
    │        │      ┌──────┤         │
    │        │      │      │ Source  │
    │        │   [10kΩ]    └────┬────┘
    │        │      │           │
    │  GND   ├──────┴───────────┴─────── GND
    └────────┘

    Rezystor 100Ω - ogranicza prąd ładowania pojemności gate
    Rezystor 10kΩ - pull-down zapobiegający przypadkowemu włączeniu
    Dioda 1N4007 - ochrona przed przepięciami indukcyjnymi pompy
```

### Ważne uwagi o MOSFET

| MOSFET | Vgs(th) | Rds(on) | Uwagi |
|--------|---------|---------|-------|
| IRLZ44N | 1-2V | 0.022Ω | Logic-level, idealny dla 3.3V |
| IRF3205 | 2-4V | 0.008Ω | Wymaga 5V na gate |
| 2N7000 | 0.8-3V | 1.2Ω | Małe prądy < 200mA |

**Zalecenie:** Używaj IRLZ44N lub podobnego logic-level MOSFET, który pewnie włącza się przy 3.3V.

---

## Zasilanie

### Opcja 1: Zasilanie 5V (pompa 5V)

```
    Adapter 5V/2A
         │
         ├───────────────────────────► ESP32 VIN (przez regulator na płytce)
         │
         └───────────────────────────► Pompa 5V (przez MOSFET)
```

### Opcja 2: Zasilanie 12V (pompa 12V)

```
    Adapter 12V/2A
         │
         ├───► Step-Down (LM2596) ───► 5V ───► ESP32 VIN
         │
         └───────────────────────────────────► Pompa 12V (przez MOSFET)
```

### Pobór prądu

| Komponent | Pobór prądu |
|-----------|-------------|
| ESP32 (BLE active) | 80-150 mA |
| Czujnik Hall | < 10 mA |
| Pompa 5V | 100-300 mA |
| Pompa 12V | 200-500 mA |

---

## Montaż

### Układ komponentów

```
    ┌─────────────────────────────────────────────────────────┐
    │                     FloraDock                            │
    │                                                          │
    │   ┌─────────────────────────────────────────────────┐   │
    │   │                   Obudowa                        │   │
    │   │                                                  │   │
    │   │     [ESP32]           [Zbiornik wody]           │   │
    │   │        │                    │                    │   │
    │   │        │                    │                    │   │
    │   │     [MOSFET]         [Pompa zanurzeniowa]       │   │
    │   │                                                  │   │
    │   │                                                  │   │
    │   └─────────────────────────────────────────────────┘   │
    │                                                          │
    │   ┌──────────────────────────────────────┐              │
    │   │        Podstawka dla robota           │              │
    │   │                                       │              │
    │   │     [Czujnik Hall]                   │              │
    │   │          ▲                            │              │
    │   │          │                            │              │
    │   │     [Magnes w robocie]               │              │
    │   │                                       │              │
    │   └──────────────────────────────────────┘              │
    │                    │                                     │
    │                    │  Wąż do doniczki                   │
    │                    ▼                                     │
    └─────────────────────────────────────────────────────────┘
```

### Pozycja czujnika Hall

```
    Widok z góry - pozycja robota na stacji:

         Robot (tył)
    ┌─────────────────┐
    │                 │
    │    [MAGNES]     │  ◄── Magnes neodymowy (np. 10x5mm)
    │                 │
    └─────────────────┘
           │
           │ ~5-10mm odstęp
           ▼
    ┌─────────────────┐
    │   [HALL]        │  ◄── Czujnik Hall (aktywna strona w górę)
    │                 │
    │    STACJA       │
    └─────────────────┘
```

### Montaż pompy

```
    Zbiornik wody
    ┌─────────────────────────────────────┐
    │                                     │
    │    ┌─────────┐                      │
    │    │ Pompa   │                      │
    │    │ zanurz. ├──── Wąż ────► do doniczki
    │    │         │                      │
    │    └─────────┘                      │
    │                                     │
    │~~~~~~~~~~~~~~~~~~~~ Woda ~~~~~~~~~~│
    │                                     │
    └─────────────────────────────────────┘

    Uwagi:
    - Pompa musi być zanurzona w wodzie
    - Wąż wyprowadzony na zewnątrz zbiornika
    - Sprawdź szczelność połączeń
```

---

## Testowanie

### Test czujnika Hall

```bash
# W terminalu ESP32 (monitor)
# Zbliżaj i oddalaj magnes od czujnika

I (xxx) DOCK_CTRL: Hall state changed: 1 -> 0 (robot present)
I (xxx) DOCK_CTRL: Hall state changed: 0 -> 1 (robot absent)
```

### Test pompy

```bash
# Wyślij komendę BLE z robota lub użyj aplikacji nRF Connect:
# Zapisz do charakterystyki 0xFFE2: 0x88 0x13 0x00 0x00 (5000 ms)

I (xxx) DOCK_CTRL: Water command: 5000ms
I (xxx) DOCK_CTRL: Pump ON
# [Po 5 sekundach]
I (xxx) DOCK_CTRL: Pump OFF (completed)
```

---

## Changelog

- **v1.0** - Podstawowa dokumentacja sprzętowa
