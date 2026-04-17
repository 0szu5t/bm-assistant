#include <WiFi.h>
#include <WiFiManager.h>          // https://github.com/tzapu/WiFiManager
#include <Preferences.h>
#include <WebSocketsClient.h>     // https://github.com/Links2004/arduinoWebSockets
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- PINY ---
const int SDA_PIN = 27;
const int SCL_PIN = 14;

// Silnik Tylny (Napęd)
const int ENA = 13;
const int IN1 = 4;
const int IN2 = 23;

// Silnik Przedni (Skręt)
const int ENB = 15;
const int IN3 = 33;
const int IN4 = 32;

const int AUDIO_PIN = 25;

const int freq = 5000;
const int resolution = 8;
int driveSpeed = 150; 
const int holdSpeed = 100;  

// ==========================================
// KOREKCJA SKRĘTU (ROZWIĄZANIE PROBLEMU KÓŁ)
// ==========================================
const int STEERING_OFFSET_DIR = 1; 
const int STEERING_OFFSET_PWM = 30; 


// ==========================================
// ADRES SERWERA - DO UZUPEŁNIENIA
// ==========================================
char websocket_server[40] = "192.168.1.100";  
const int websocket_port = 5000;

bool shouldSaveConfig = false;
void saveConfigCallback () {
  shouldSaveConfig = true;
}


WebSocketsClient webSocket;

void updateDisplay(String text1, String text2 = "") {
  display.clearDisplay();
  display.setCursor(0,0);
  display.println(text1);
  if(text2 != "") {
    display.println(text2);
  }
  display.display();
}

void beep() {
  for(int i = 0; i < 50; i++) {
    digitalWrite(AUDIO_PIN, HIGH);
    delayMicroseconds(500);
    digitalWrite(AUDIO_PIN, LOW);
    delayMicroseconds(500);
  }
}

void skrecWPrawo() {
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  ledcWrite(ENB, holdSpeed); 
}

void skrecWLewo() {
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  ledcWrite(ENB, holdSpeed);
}

void wyprostujKola() {
  // SPOSÓB ROZWIĄZANIA PROBLEMU ZE ŚCIĄGANIEM
  if (STEERING_OFFSET_PWM > 0) {
    if (STEERING_OFFSET_DIR == 1) { // Kontra lekko w prawo
      digitalWrite(IN3, HIGH);
      digitalWrite(IN4, LOW);
      ledcWrite(ENB, STEERING_OFFSET_PWM);
    } else { // Kontra lekko w lewo
      digitalWrite(IN3, LOW);
      digitalWrite(IN4, HIGH);
      ledcWrite(ENB, STEERING_OFFSET_PWM);
    }
  } else {
    // Normalne darmowe kręcenie kołami lub zwolnienie blokady
    ledcWrite(ENB, 0);
    digitalWrite(IN3, LOW);
    digitalWrite(IN4, LOW);
  }
}

void jedzDoPrzodu() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  ledcWrite(ENA, driveSpeed);
}

void jedzDoTylu() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  ledcWrite(ENA, driveSpeed);
}

void zatrzymajWszystko() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  ledcWrite(ENA, 0);
  // Gdy staje, też niech puszcza koła i wyrównuje je lekko - lub wcale, 
  // do rozważenia czy offset ma działać też podczas postoju. Dla bezpieczeństwa zdejmujemy napiecie:
  ledcWrite(ENB, 0);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("[WS] Rozłączono z serwerem!");
      updateDisplay("SERWER: BRAK", "Oczekuje...");
      zatrzymajWszystko();
      break;

    case WStype_CONNECTED:
      Serial.println("[WS] Połączono pomyślnie z laptopem!");
      updateDisplay("SERWER: OK!", (char*)payload);
      beep();
      break;

    case WStype_TEXT:
      String cmd = String((char*)payload);
      // Komendy z przegladarki internetowej
      if(cmd == "F") { // Do przodu (Forward)
        wyprostujKola(); // Zawiera zaimplementowaną odchyłkę Offset PWM!
        jedzDoPrzodu();
        updateDisplay("Jazda", "Do przodu");
      } 
      else if(cmd == "B") { // Do tyłu (Back)
        wyprostujKola();
        jedzDoTylu();
        updateDisplay("Jazda", "Do Tylu");
      } 
      else if(cmd == "L") { // Lewo (Left)
        jedzDoPrzodu();
        skrecWLewo();
        updateDisplay("Jazda", "Skret LEWO");
      } 
      else if(cmd == "R") { // Prawo (Right)
        jedzDoPrzodu();
        skrecWPrawo();
        updateDisplay("Jazda", "Skret PRAWO");
      } 
      else if(cmd == "S") { // Stop (S)
        zatrzymajWszystko();
        updateDisplay("Stop", "Gotowy.");
      }
      else if(cmd.startsWith("V")) { // Predkosc (V0 - V255)
        driveSpeed = cmd.substring(1).toInt();
        updateDisplay("Predkosc:", String(driveSpeed));
        // Jesli akurat jedziemy do przodu lub tyłu, mozliwe by było zaaktualizowanie
        // PWM w locie przez ledcWrite(ENA, driveSpeed) - uproszczenie zakłada, 
        // że wpłynie to na następny ruch lub jazdę przerywaną.
      }
      break;
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);

  // Inicjalizacja ekranu OLED (zachowana z kodu startowego)
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Błąd OLED");
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.display();
  }

  // Ustawienie pinów
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(AUDIO_PIN, OUTPUT);

  // Ustawienia PWM silników
  ledcAttach(ENA, freq, resolution);
  ledcAttach(ENB, freq, resolution);

  zatrzymajWszystko();

  // Wczytywanie zapisanego IP serwera (na wypadek braku wejscia w panel)
  Preferences preferences;
  preferences.begin("robot_app", false);
  String saved_ip = preferences.getString("server_ip", "192.168.1.100");
  saved_ip.toCharArray(websocket_server, 40);

  // Konfiguracja panelu WiFiManager
  WiFiManager wifiManager;
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  WiFiManagerParameter custom_server_ip("server", "IP Laptopa (Serwera)", websocket_server, 40);
  wifiManager.addParameter(&custom_server_ip);

  updateDisplay("Oczekiwanie", "na WiFi...");
  
  // Funkcja blokuje ESP dopóki się nie połączysz siecią
  wifiManager.autoConnect("bm-assstant-setup");

  // Jeśli wprowadzono nowe IP w panelu, zapisz do pamięci Flash
  if (shouldSaveConfig) {
    String new_ip = String(custom_server_ip.getValue());
    preferences.putString("server_ip", new_ip);
    new_ip.toCharArray(websocket_server, 40);
  }

  // Dotrzemy tutaj tylko, jeśli moduł pomyślnie sie podłączył do Twojego routera/telefonu
  Serial.println("Połączono z WiFi!");
  updateDisplay("WiFi OK!", WiFi.localIP().toString());
  delay(2000);

  // Inicjalizacja połączenia WebSocket (do Python Servera na laptopie)
  webSocket.begin(websocket_server, websocket_port, "/ws_robot");
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
}

void loop() {
  webSocket.loop();
}
