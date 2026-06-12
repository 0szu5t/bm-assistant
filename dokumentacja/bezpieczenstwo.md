# Bezpieczeństwo — BM Assistant

Zasady bezpiecznej pracy ze sprzętem, procedury wgrywania i katalog awarii.

---

## Żelazna Procedura Wgrywania Firmware

> Niestosowanie tej procedury grozi uszkodzeniem portu USB komputera lub ESP32.

1. **Odłącz ENA i ENB** — wyciągnij oba przewody z pinów ENA (GPIO 13) i ENB (GPIO 15) w ESP32.  
   *(Oznaczone białą taśmą — pierwszy i ostatni pin na mostku H.)*
2. **Włącz zasilanie akumulatorów** — przełącznik na **ON**.
3. **Podłącz kabel USB** do ESP32.
4. W Arduino IDE kliknij **Wgraj**.
5. Gdy pojawi się `Connecting...` → wciśnij i **trzymaj BOOT** aż do końca procesu.
6. Po sukcesie: **odłącz USB**, **wepnij ENA i ENB** z powrotem.

---

## Zasady dotyczące pinów i kabli

- Nie szarp za kable — większość połączeń jest lutowana. Może się urwać lub zrobić zwarcie.
- Nigdy nie dotykaj płytki metalowymi narzędziami (śrubokręt, pęseta) przy włączonym zasilaniu.
- Goły drucik wystający z połączenia = natychmiast wyłącz zasilanie.

---

## Bezpieczne parametry PWM

- **Maksymalna bezpieczna wartość `speedPWM` do ciągłej jazdy: ~150–180.**
- Silniki są znamionowane na 3,7 V; wartość 255 daje ~6 V i może je uszkodzić (stopienie obudów, spalenie szczotek).

---

## Bezpieczeństwo akumulatorów (18650)

- Zapach spalenizny lub dym → natychmiastowe **OFF** głównego wyłącznika.
- Co jakiś czas sprawdzaj temperaturę akumulatorów i przetwornicy LM2596. Ciepłe = OK; gorące = błąd w kodzie lub zwarcie.
- Nie zostawiaj robota włączonego bez nadzoru na wiele godzin (ryzyko rozładowania poniżej bezpiecznego poziomu).

---

## Katalog awarii

### Awaria 1 — Dioda przetwornicy ledwo świeci, napięcie ~1,3 V

**Co się stało:** Zwarcie na linii 5 V (plus z minusem).  
**Mechanizm:** Przetwornica LM2596 aktywowała ochronę i ograniczyła napięcie.  
**Co robić:**
1. Wyłącz zasilanie (OFF).
2. Niczego nie spaliłeś — ochrona zadziałała.
3. Metodycznie szukaj fizycznego zwarcia w kablach i usuń je.

---

### Awaria 2 — Zamiana ENA ↔ ENB {#awaria-2}

**Co się stało:** Piny ENA i ENB wpięte w odwrotne miejsca.  
**Co się dzieje:**
- Tylne koła — szarpią przez chwilę, potem ledwo się kręcą (efekt „kick & hold" na silniku skrętnym).
- Przedni silnik (skrętny) — dostaje instrukcję ciągłego obrotu, ale jest zablokowany fizycznie przez sprężynę po ~35°. **Wchodzi w stan utyku (stall) i się nagrzewa.**  
**Skutek:** Kilka minut w tym stanie = przepalony silnik przedni + przegrzany mostek L298N.  
**Jak zapobiec:** Zawsze potrójnie sprawdzaj kolejność ENA/ENB przed uruchomieniem.

---

### Awaria 3 — PWM ustawiony na 255 (100%)

**Co się stało:** Zmiana `speedPWM = 255` w kodzie.  
**Co się dzieje:** ~6 V na silnikach zamiast ~3,7 V.  
**Skutek:** Po 10–15 minutach — topią się obudowy silników lub spalają szczotki wewnętrzne.  
**Limit:** Maksimum ciągłej jazdy to wartość ~150–180.

---

### Awaria 4 — Arduino IDE błąd `PermissionError` / `No serial data received`

**Co się stało:** Próba wgrania kodu z wpiętonymi ENA/ENB i wyłączonym zasilaniem.  
**Co się dzieje:** Silniki przy starcie wyssają prąd z USB — laptop odcina połączenie.  
**Rozwiązanie:** Przestrzegaj **Żelaznej Procedury Wgrywania** — ENA/ENB odłączone na czas wgrywania.

---

### Awaria 5 — Robot nie rusza na dywanie, ale działa w powietrzu

**Co się stało:** ENA/ENB wpięte, zasilanie akumulatorów na OFF — robot zasilany wyłącznie z USB.  
**Co się dzieje:** Port USB daje maks. ~500 mA — wystarczy do obrotu kół bez obciążenia, ale nie uniesie ciężaru robota.  
**Rozwiązanie:** Włącz zasilanie akumulatorów (przełącznik ON).

---

### Awaria 6 — Zworka „5V EN" założona z powrotem na mostek L298N

**Co się stało:** Zworka `5V EN` (jumpera) włożona na mostek L298N.  
**Co się dzieje:** Wewnętrzny regulator L298N „walczy" z przetwornicą LM2596 o napięcie 5 V.  
**Skutek:** Niestabilne zasilanie, dziwne zachowanie OLED, ryzyko uszkodzenia mostka.  
**Reguła:** Zworka `5V EN` na L298N **musi być zdjęta**. 5 V dostarcza zewnętrzna przetwornica.

---

### Awaria 7 — Odwrócona polaryzacja akumulatora (KRYTYCZNA)

**Co się stało:** Czerwony kabel do minusa, czarny do plusa.  
**Co się dzieje:** Odwrotna polaryzacja — układy nie mają przed tym zabezpieczenia.  
**Skutek:** Natychmiastowe zwęglenie LM2596, przepalenie L298N, prawdopodobne uszkodzenie ESP32.  

> **Kolory kabli zasilających są absolutne:**  
> 🔴 **Czerwony = Plus (+, VCC)**  
> ⚫ **Czarny = Minus (−, GND)**  
> Nigdy ich nie zamieniaj.
