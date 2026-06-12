# REST API — BM Assistant

Dokumentacja endpointów HTTP dla cloud servera i laptop servera.

---

## Cloud Server (`cloud_server/app.py`)

Wdrożony na Vercel pod adresem `https://bm-assistant.vercel.app`.  
Używa Redis jako kolejki zdarzeń i magazynu sesji.

---

### Strony HTML

#### `GET /`
Strona główna — formularz wpisywania kodu parowania.

**Odpowiedź:** HTML (`home.html`)

---

#### `GET/POST /join`
Walidacja kodu i przekierowanie do panelu sterowania.

**Parametry:**
- `code` (form/query) — kod parowania w formacie `XXX-XXX`

**Zachowanie:**
- Kod nieznaleziony → renderuje `home.html` z komunikatem błędu.
- Kod prawidłowy → przekierowanie `302` do `/control/<code>`.

---

#### `GET /control/<code>`
Panel sterowania robotem.

**Parametry URL:**
- `code` — kod parowania (automatycznie uppercase)

**Odpowiedź:** HTML (`control.html`) lub `home.html` z błędem jeśli sesja wygasła.

---

### API — diagnostyka

#### `GET /api/health`
Sprawdzenie stanu serwera i połączenia z Redis.

**Odpowiedź:**
```json
{
  "status": "ok",
  "redis": true,
  "redis_url": "redis://...",
  "session_ttl": 300
}
```

---

### API — ESP32

#### `POST /api/register`
Rejestracja nowego robota — generuje kod parowania.

**Body (JSON):**
```json
{ "device_id": "AA:BB:CC:DD:EE:FF" }
```

**Odpowiedź 200:**
```json
{ "code": "ABC-123", "ttl": 300 }
```

**Odpowiedź 500:**
```json
{ "error": "Server error: ..." }
```

**Szczegóły:**
- Generuje unikalny 6-znakowy kod (format `XXX-XXX`, alfabet bez podobnych znaków: `ABCDEFGHJKLMNPQRSTUVWXYZ23456789`).
- Tworzy klucze Redis `robot:session:<code>` i inicjalizuje kolejkę `robot:queue:<code>`.
- TTL sesji ustawiany z `SESSION_TTL_SECONDS` (domyślnie 300 s).

---

#### `GET /api/poll?code=XXX-XXX`
Polling komend przez ESP32. Odświeża TTL sesji.

**Parametry query:**
- `code` — kod parowania

**Odpowiedź 200** (tablica zdarzeń, może być pusta `[]`):
```json
[
  {
    "event": "robot_command",
    "data": { "command": "przód", "value": 2 }
  },
  {
    "event": "chat_response",
    "data": { "response": "Tekst z AI..." }
  }
]
```

**Odpowiedź 404:** `{ "error": "Sesja wygasla" }` — ESP32 powinien się ponownie zarejestrować.

**Typy zdarzeń:**

| `event` | Pole `data` | Opis |
|---------|-------------|------|
| `robot_command` | `command` (string), `value` (int) | Komenda ruchu; `value=0` → tryb ciągły |
| `chat_response` | `response` (string) | Tekst do wyświetlenia na OLED |
| `audio_response` | `audio` (base64 PCM) | Dane audio do odtworzenia |

---

### API — Przeglądarka

#### `POST /api/command/<code>`
Wysłanie komendy sterowania ręcznego.

**Parametry URL:**
- `code` — kod parowania

**Body (JSON):**
```json
{ "cmd": "F", "value": 0 }
```

| Pole | Typ | Opis |
|------|-----|------|
| `cmd` | string | Litera komendy (patrz tabela niżej) |
| `value` | int | `0` = tryb ciągły; `>0` = czas w sekundach |

**Kody komend:**

| `cmd` | Polska nazwa | Akcja |
|-------|-------------|-------|
| `F` | przód | Jedź do przodu |
| `B` | tył | Jedź do tyłu |
| `L` | lewo | Skręć w lewo |
| `R` | prawo | Skręć w prawo |
| `S` | stop | Zatrzymaj |
| `W` | wyprostuj | Wyprostuj koła przednie |

**Odpowiedź 200:** `{ "status": "ok" }`  
**Odpowiedź 404:** `{ "error": "Sesja wygasla" }`

---

#### `POST /api/nvidia/<code>`
Wysłanie polecenia w języku naturalnym do modelu AI.

**Parametry URL:**
- `code` — kod parowania

**Body (JSON):**
```json
{ "prompt": "jedź do przodu 2 sekundy, skręć w prawo przez 1 sekundę" }
```

**Odpowiedź 200 (sukces):**
```json
{
  "status": "success",
  "message": "Zrozumiano, wykonuje!",
  "sequence": [
    { "cmd": "F", "time": 2 },
    { "cmd": "R", "time": 1 }
  ]
}
```

**Odpowiedź 500 (błąd):**
```json
{ "status": "error", "message": "Zly klucz do NVIDIA API" }
```

**Działanie:**
1. Wysyła `prompt` do NVIDIA API (`meta/llama-3.1-8b-instruct`, `temperature=0.1`).
2. LLM zwraca tablicę JSON z krokami sekwencji.
3. Każdy krok z `time > 0` jest dodawany do kolejki Redis (komenda `robot_command`).
4. Kroki z `cmd == "S"` lub `time <= 0` są pomijane.

---

## Laptop Server (`laptop_server/app.py`)

Serwer lokalny — działa w sieci domowej, komunikuje się z ESP32 przez WebSocket lub HTTP polling.  
Uruchamiany poleceniem `python app.py` na laptopie/PC.

Różnice względem cloud servera:
- Brak Redis — kolejka zdarzeń w pamięci (`event_queue` lista Python).
- Brak systemu kodów parowania — jeden robot na serwer.
- WebSocket zamiast polling dla przeglądarki.
- UDP beacon co 2 s (`ROBOT_SERVER` na port 5555) — rozgłasza IP serwera w sieci lokalnej.

---

### Endpointy

#### `GET /`
Panel sterowania (wersja lokalna).

---

#### `GET /api/poll`
Polling komend przez ESP32. Zwraca wszystkie zdarzenia z kolejki i czyści ją.

**Odpowiedź:** tablica zdarzeń (format identyczny jak cloud server).

---

#### `POST /api/nvidia`
Identyczna logika jak `/api/nvidia/<code>` w cloud serverze, bez parametru `code`.  
Sekwencja wykonywana w osobnym wątku (`threading.Thread`) — nie blokuje odpowiedzi HTTP.

---

#### `WebSocket /ws_client`
Połączenie WebSocket dla przeglądarki (flask-sock).

**Wysyłane dane:** jedna litera komendy (`F`, `B`, `L`, `R`, `S`, `W`).  
Komenda jest mapowana na polską nazwę i trafia do `event_queue`.

---

## Redis — struktura kluczy

| Klucz | Typ Redis | Zawartość |
|-------|-----------|-----------|
| `robot:session:<code>` | Hash | `device_id`, `last_seen`, `created_at` |
| `robot:queue:<code>` | List | Serializowane JSON zdarzenia (FIFO) |

TTL obu kluczy odświeżany przy każdym pollingu ESP32.
