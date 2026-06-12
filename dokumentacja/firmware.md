# Firmware ESP32 — BM Assistant

Dokumentacja kodu `robot_esp32/robot_esp32.ino`.

---

## Biblioteki

| Biblioteka | Zastosowanie |
|------------|--------------|
| `WiFi.h` + `WiFiManager.h` | Połączenie z siecią WiFi, portal konfiguracyjny |
| `WiFiClientSecure.h` + `HTTPClient.h` | HTTPS polling serwera cloud |
| `ArduinoJson` | Parsowanie JSON z serwera |
| `Adafruit_SSD1306` + `Adafruit_GFX` | Wyświetlacz OLED 128×64 |
| `mbedtls/base64.h` | Dekodowanie audio Base64 (wbudowana w ESP-IDF) |

---

## Stałe konfiguracyjne

| Stała | Wartość | Opis |
|-------|---------|------|
| `SERVER_URL` | `https://bm-assistant.vercel.app` | Adres cloud servera (bez ukośnika na końcu) |
| `POLL_INTERVAL` | 250 ms | Częstotliwość odpytywania serwera |
| `CMD_QUEUE_MAX` | 20 | Maksymalna liczba komend w kolejce wewnętrznej |
| `AUDIO_BUFFER_SIZE` | 32 000 B | Rozmiar bufora PCM audio |

---

## Funkcje

### Wyświetlacz OLED

#### `updateDisplay(String text)`
Wyświetla tekst na OLED i drukuje go na Serial Monitor (ułatwia debugowanie).  
Nagłówek `--- ROBOT ---` jest zawsze na górze; poniżej treść `text`.

#### `showPairingCode(String code)`
Wyświetla kod parowania w formacie `XXX-XXX` (duża czcionka) z podpisem `wpisz na stronie!`.  
Wywoływana po rejestracji i po wykonaniu sekwencji AI.

---

### Audio

#### `playBeep()`
Generuje krótki sygnał 1000 Hz przez 200 ms — potwierdzenie odebrania komendy.  
Na ESP-IDF v3+ używa `tone()`; na starszych wersjach — pętla `digitalWrite`.

#### `playAudioFromBase64(const char* b64Data, size_t b64Len)`
Dekoduje dane Base64 → PCM i uruchamia odtwarzanie przez DAC (GPIO 25).  
Audio odtwarzane przez przerwanie sprzętowe (`hw_timer_t`) z częstotliwością 8 kHz (alarm co 125 µs).

#### `onAudioTimer()` *(ISR — IRAM)*
Handler przerwania timera — wypisuje kolejne bajty z `audioBuffer` na DAC.  
Po zakończeniu ustawia wyjście DAC na 128 (środek skali = cisza).

---

### Silniki

#### `stopMotors()`
Wyłącza oba silniki (IN1–IN4 LOW, PWM = 0). Zeruje flagi `isMoving`, `isTurning`, `continuous`.

#### `moveForward(int ms)`
Silnik A do przodu, silnik B do przodu z korekcją (`speedPWM + WHEEL_CORRECTION`).  
`ms > 0` → jazda przez `ms` milisekund; `ms == 0` → tryb ciągły (aż do komendy STOP).

#### `moveBackward(int ms)`
Jak `moveForward`, ale kierunek odwrotny.

#### `turnLeft(int ms)`
Silnik B do tyłu z impulsem `kickSpeed` przez `kickTime` ms → potem `holdSpeed`.  
Silnik A do przodu `speedPWM`.

#### `turnRight(int ms)`
Silnik B do przodu z impulsem `kickSpeed` → `holdSpeed`.  
Silnik A do przodu `speedPWM`.

#### `wyprostujKola()`
Wyłącza tylko silnik B (skrętny) — sprężyna powrotu wyprostowuje koła przednie.  
Silnik A (napędowy) nie jest zatrzymywany.

#### `executeCommand(String cmd, int durationSec)`
Dispatcher komend — mapuje nazwę komendy na wywołanie odpowiedniej funkcji ruchu.

| Komenda | Akcja |
|---------|-------|
| `przod` / `przód` | `moveForward(ms)` |
| `tyl` / `tył` | `moveBackward(ms)` |
| `lewo` | `turnLeft(ms)` |
| `prawo` | `turnRight(ms)` |
| `wyprostuj` | `wyprostujKola()` |
| inne | `stopMotors()` |

---

### Kolejka komend wewnętrznych

Wewnętrzna kolejka kołowa umożliwia kolejkowanie sekwencji AI bez blokowania pętli głównej.

```
cmdQueue[CMD_QUEUE_MAX]  — tablica komend (cmd: String, durationSec: int)
cmdQueueHead, cmdQueueTail, cmdQueueCount  — wskaźniki kolejki
```

#### `enqueueCommand(cmd, durationSec)`
Dodaje komendę na koniec kolejki. Jeśli kolejka pełna (20 elementów) — odrzuca.

#### `dequeueCommand(RobotCommand& out)`
Pobiera i usuwa komendę z początku kolejki. Zwraca `false` gdy pusta.

#### `clearQueue()`
Opróżnia kolejkę natychmiast (wywoływana przy komendzie STOP).

---

### Komunikacja z serwerem

#### `registerDevice()` → `bool`
POST `/api/register` z `device_id` (adres MAC).  
Odpowiedź: `{"code": "XXX-XXX", "ttl": 300}`.  
Zapisuje kod do `pairingCode`. Zwraca `true` przy sukcesie.  
Używa `WiFiClientSecure` z `setInsecure()` (akceptuje każdy certyfikat SSL).

#### `pollServer()`
GET `/api/poll?code=XXX-XXX` — pobiera zdarzenia z kolejki Redis.  
Odpowiedź: tablica JSON zdarzeń. Obsługiwane typy:

| Typ zdarzenia | Akcja ESP32 |
|---------------|-------------|
| `robot_command` z `cmd == "stop"` | `clearQueue()` + `stopMotors()` |
| `robot_command` (inne) | `enqueueCommand(cmd, val)` |
| `chat_response` | `updateDisplay("AI:\n" + response)` |
| `audio_response` | `playAudioFromBase64(audioB64)` |

Przy HTTP 404 (sesja wygasła) → automatyczna re-rejestracja.

---

### `setup()`

Kolejność inicjalizacji:

1. Serial 115200 baud
2. Konfiguracja pinów silników + `ledcAttach` PWM
3. `stopMotors()` — pewność zatrzymania przy starcie
4. Inicjalizacja audio (DAC, timer sprzętowy)
5. Inicjalizacja OLED (Wire, adres 0x3C)
6. WiFiManager — `autoConnect("bm-assistant")` — blokuje do połączenia
7. Rejestracja na serwerze (5 prób, 2 s przerwy)
8. Wyświetlenie kodu parowania

---

### `loop()`

Trzy zadania wykonywane w każdej iteracji:

1. **Polling serwera** (co `POLL_INTERVAL` = 250 ms) — wywołuje `pollServer()`.
2. **Koniec ruchu** — gdy `isMoving && !continuous && millis() >= actionEndTime`:  
   zatrzymuje silniki → pobiera następną komendę z kolejki → wykonuje ją.
3. **Kolejka niepusta i robot stoi** — gdy `!isMoving && cmdQueueCount > 0`:  
   pobiera i wykonuje pierwszą komendę z kolejki.

---

## Stany robota

```
IDLE  ──[komenda z serwera]──►  MOVING  ──[czas minął]──► IDLE
                                   │
                              [kolejna komenda
                               w kolejce]
                                   │
                                   ▼
                                 MOVING (nowa komenda)
```

- Tryb ciągły (`continuous = true`): robot jedzie do odebrania komendy STOP.
- Tryb sekwencji AI: każdy krok wykonywany przez zadaną liczbę sekund; po zakończeniu kroku automatycznie startuje kolejny.
