#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- PINY ---
const int SDA_PIN = 27;
const int SCL_PIN = 14;

// Silnik Tylny (Napęd) - ZAMIENIONE PINY DLA KOREKTY KIERUNKU
const int ENA = 13;
const int IN1 = 23;
const int IN2 = 4;

// Silnik Przedni (Skręt)
const int ENB = 15;
const int IN3 = 33;
const int IN4 = 32;

const int AUDIO_PIN = 25;

const int freq = 5000;
const int resolution = 8;
const int testDriveSpeed = 150; 

const int kickSpeed = 255;  
const int holdSpeed = 100;  
const int kickTime = 150;   

void skrecWPrawo() {
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  ledcWrite(ENB, kickSpeed);
  delay(kickTime);
  ledcWrite(ENB, holdSpeed);
}

void skrecWLewo() {
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  ledcWrite(ENB, kickSpeed);
  delay(kickTime);
  ledcWrite(ENB, holdSpeed);
}

void wyprostujKola() {
  ledcWrite(ENB, 0);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Błąd OLED");
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.display();
  }

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(AUDIO_PIN, OUTPUT);

  ledcAttach(ENA, freq, resolution);
  ledcAttach(ENB, freq, resolution);

  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  wyprostujKola();
  delay(2000);
}

void loop() {
  // TEST AUDIO
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("TEST AUDIO...");
  display.display();

  for(int i = 0; i < 300; i++) {
    digitalWrite(AUDIO_PIN, HIGH);
    delayMicroseconds(1000);
    digitalWrite(AUDIO_PIN, LOW);
    delayMicroseconds(1000);
  }
  delay(1000);

  // TEST JAZDY (Teraz powinno być do przodu!)
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("JAZDA: DO PRZODU");
  display.display();

  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  ledcWrite(ENA, testDriveSpeed);
  delay(2000);

  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  ledcWrite(ENA, 0);
  delay(1000);

  // TEST SKRETOW
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("SKRET: PRAWO");
  display.display();
  skrecWPrawo();
  delay(2000);
  wyprostujKola();

  delay(1000);

  display.clearDisplay();
  display.setCursor(0,0);
  display.println("SKRET: LEWO");
  display.display();
  skrecWLewo();
  delay(2000);
  wyprostujKola();

  delay(3000);
}