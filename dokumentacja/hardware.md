# Sprzęt — BM Assistant

Opis elektroniki, komponentów i okablowania robota.

---

## Komponenty

| Komponent | Model | Rola |
|-----------|-------|------|
| Mikrokontroler | ESP32 (30-pin) | Główny komputer robota — WiFi, logika, PWM, DAC |
| Mostek H | L298N | Sterowanie silnikami DC (kierunek + prędkość) |
| Silniki napędowe | 2× silnik DC 3–6 V | Napęd tylnej osi |
| Silnik skrętny | 1× silnik DC (os przednia) | Zmiana kierunku jazdy |
| Wyświetlacz | OLED SSD1306 128×64 | Wyświetlanie kodu parowania, stanu, komunikatów AI |
| Wzmacniacz audio | PAM8403 (klasa D) | Wzmocnienie sygnału audio z DAC ESP32 |
| Głośnik | ~0.5–3 W | Głos AI, sygnały dźwiękowe |
| Przetwornica | LM2596 (DC-DC) | Stabilizacja zasilania z 7,4 V → 5 V |
| Akumulatory | 2× 18650 Li-Ion (szeregowo) | 7,4 V, zasilanie całego systemu |

---

## Schemat zasilania

```
[2× 18650]  →  [Główny wyłącznik]  →┬→ [L298N pin 12V]   (silniki)
  7,4 V                              └→ [LM2596]  →  5V  →┬→ ESP32 (VIN)
                                                           ├→ OLED
                                                           └→ PAM8403
```

> Mostek L298N zasilany jest bezpośrednio z akumulatorów (~7,4 V).
> Zworka „5V EN" na L298N **musi być zdjęta** — 5 V dostarcza przetwornica LM2596.

---

## Mapowanie pinów ESP32

### Mostek H — L298N

| Pin ESP32 | Sygnał | Opis |
|-----------|--------|------|
| GPIO 13 | ENA | PWM — prędkość silnika A (napędowego) |
| GPIO 4 | IN1 | Kierunek silnika A |
| GPIO 23 | IN2 | Kierunek silnika A |
| GPIO 15 | ENB | PWM — prędkość silnika B (skrętnego) |
| GPIO 33 | IN3 | Kierunek silnika B |
| GPIO 32 | IN4 | Kierunek silnika B |

> **Uwaga:** Silnik A to napęd (oś tylna), silnik B to skręt (oś przednia).
> Zamiana ENA ↔ ENB grozi przepaleniem silnika skrętnego (blokuje się na sprężynie).
> Patrz: [bezpieczenstwo.md](bezpieczenstwo.md#awaria-2).

### OLED SSD1306 (I²C)

| Pin ESP32 | Sygnał | Opis |
|-----------|--------|------|
| GPIO 27 | SDA | Linia danych I²C |
| GPIO 14 | SCL | Linia zegara I²C |

Adres I²C: `0x3C`  
Rozdzielczość: 128 × 64 px

### Audio

| Pin ESP32 | Sygnał | Opis |
|-----------|--------|------|
| GPIO 25 | AUDIO_OUT | DAC — sygnał audio do wzmacniacza PAM8403 |

Sygnał beep generowany przez `tone()` (ESP-IDF v3+) lub pętlę `digitalWrite`.  
Sygnał audio (TTS) odtwarzany przez sprzętowy DAC z buforem 32 000 bajtów PCM.

---

## Parametry PWM silników

| Parametr | Wartość | Opis |
|----------|---------|------|
| `speedPWM` | 150 | Normalna prędkość jazdy (8-bit, max 255) |
| `kickSpeed` | 255 | Impuls startowy skrętu (150 ms) |
| `holdSpeed` | 100 | Prędkość utrzymania skrętu po impulsie |
| `kickTime` | 150 ms | Czas impulsu skrętnego |
| `WHEEL_CORRECTION` | −50 | Korekcja asymetrii kół (odejmowana od silnika B) |
| Częstotliwość PWM | 5 000 Hz | Konfiguracja `ledcAttach` |
| Rozdzielczość PWM | 8-bit | 0–255 |

> Nie ustawiaj `speedPWM` powyżej ~180 przy ciągłej jeździe — silniki są znamionowane na 3,7 V.
> Wartość 255 odpowiada ~6 V i może je trwale uszkodzić.

---

## Podwozie i mechanika

- **Oś tylna** — napędowa; silnik DC obraca oba tylne koła.
- **Oś przednia** — skrętna; silnik DC przesuwa mechanizm skrętu w lewo/prawo (sprężyna powrotu).
- Obrót skrętny: mechanizm z fizycznym ogranicznikiem ~35°.
- Funkcja `wyprostujKola()` wyłącza silnik B — sprężyna wraca koła do środka.

---

## Przycisk BOOT i RESET

- **BOOT (GPIO 0)** — trzymaj podczas wgrywania firmware (szczegóły → [konfiguracja.md](konfiguracja.md#wgrywanie-firmware)).
- **RESET (EN)** — restart oprogramowania; nie wpływa na konfigurację WiFi.

---

## WiFiManager

Robot używa biblioteki **WiFiManager** do konfiguracji WiFi:

- Przy pierwszym uruchomieniu (lub po `wm.resetSettings()`) tworzy punkt dostępowy **`bm-assistant`**.
- Użytkownik łączy się z tym AP i w portalu konfiguracyjnym podaje dane sieci domowej.
- Dane są zapisywane w pamięci flash ESP32 i używane przy kolejnych startach.
