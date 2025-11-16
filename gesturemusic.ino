// GestureMusic — HC-SR04 + OLED + Buzzer
// - HC-SR04 TRIG -> D9, ECHO -> D10
// - Buzzer -> D8 (set ACTIVE_BUZZER true if it's an active on/off buzzer)
// - OLED SSD1306 I2C -> SDA A4, SCL A5 (addr 0x3C)
// Serial Monitor: 115200

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ====== CONFIG ======
const bool ACTIVE_BUZZER = false; // false = passive (variable pitch), true = active (on/off discrete notes)
const uint8_t trigPin   = 9;
const uint8_t echoPin   = 10;
const uint8_t buzzerPin = 8;

// playable range (adjust to your comfort)
const int minCM = 5;    // closest accepted distance (cm)
const int maxCM = 40;   // farthest accepted distance (cm)

// pitch range for passive buzzer (Hz)
const int minHz = 220;
const int maxHz = 2000;

// discrete notes for active buzzer fallback (8-note scale)
const int notesCount = 8;
const int notes[notesCount] = {262, 294, 330, 349, 392, 440, 494, 523}; // C4..C5

// smoothing
const uint8_t W = 5;
int windowCM[W];
uint8_t wIndex = 0;
bool filled = false;
float smoothFreq = minHz;
float alpha = 0.25; // smoothing factor: larger = snappier, smaller = smoother

// ====== UTIL ======
int medianN(int a[], uint8_t n) {
  // simple insertion-sort median
  int b[10]; // W <= 10 safe
  for (uint8_t i = 0; i < n; i++) b[i] = a[i];
  for (uint8_t i = 1; i < n; i++) {
    int key = b[i], j = i;
    while (j > 0 && b[j - 1] > key) { b[j] = b[j - 1]; j--; }
    b[j] = key;
  }
  return b[n/2];
}

long readUltrasonicCM() {
  digitalWrite(trigPin, LOW);  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH); delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH, 30000UL); // 30ms timeout
  if (duration == 0) return 999; // no echo
  long cm = (long)(duration * 0.034 / 2);
  return cm;
}

void drawOLED(long cm, int freq, bool playing) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.print(F("GestureMusic"));
  display.setCursor(0,12);
  display.print(F("Dist: "));
  if (cm >= 900) display.print(F("--"));
  else display.print(cm);
  display.print(F(" cm"));

  display.setCursor(0, 24);
  display.print(F("Freq: "));
  if (playing) display.print(freq);
  else display.print(F("--"));
  display.print(F(" Hz"));

  // pitch bar
  int barY = 38;
  display.drawRect(0, barY, 128, 10, SSD1306_WHITE);
  if (playing) {
    float t = (float)(freq - minHz) / (float)(maxHz - minHz);
    t = constrain(t, 0.0, 1.0);
    int w = (int)(t * 126.0);
    if (w > 0) display.fillRect(1, barY+1, w, 8, SSD1306_WHITE);
  }

  // emoji/face
  display.setTextSize(1);
  display.setCursor(90, 12);
  if (!playing) display.print("-_-");
  else if (freq < 700) display.print(":)");
  else if (freq < 1400) display.print("^_^");
  else display.print("O_O");

  // virtual keys
  int keysY = 54;
  for (int i = 0; i < notesCount; i++) {
    int x = 4 + i * 15;
    int w = 12, h = 8;
    bool on = false;
    if (playing) {
      int idx = map(freq, minHz, maxHz, 0, notesCount - 1);
      idx = constrain(idx, 0, notesCount - 1);
      on = (i == idx);
    }
    if (on) display.fillRect(x, keysY, w, h, SSD1306_WHITE);
    else    display.drawRect(x, keysY, w, h, SSD1306_WHITE);
  }

  display.display();
}

// ====== SETUP / LOOP ======
void setup() {
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(buzzerPin, OUTPUT);

  Serial.begin(115200);
  delay(100);
  Serial.println(F("GestureMusic starting..."));

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED init FAILED (0x3C). Check wiring."));
    while (true) delay(1000);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println(F("GestureMusic"));
  display.println();
  display.println(F("Starting..."));
  display.display();

  // fill smoothing window
  for (uint8_t i = 0; i < W; i++) windowCM[i] = 1000;
  smoothFreq = minHz;
  delay(300);

  // quick startup tone so you can tell buzzer type
  Serial.println(F("Startup tone (listen):"));
  if (!ACTIVE_BUZZER) {
    for (int f = 400; f <= 1600; f += 300) {
      tone(buzzerPin, f);
      Serial.print(F("tone: ")); Serial.println(f);
      delay(120);
    }
    noTone(buzzerPin);
  } else {
    // active buzzer blink
    for (int i = 0; i < 4; i++) {
      digitalWrite(buzzerPin, HIGH); delay(150);
      digitalWrite(buzzerPin, LOW);  delay(150);
    }
  }
  Serial.println(F("Startup done."));
}

void loop() {
  int cm = (int)readUltrasonicCM();
  windowCM[wIndex++] = cm;
  if (wIndex >= W) { wIndex = 0; filled = true; }
  int med = medianN(windowCM, filled ? W : max((int)wIndex, 1));

  bool inRange = (med > minCM && med < maxCM);
  int targetFreq = 0;

  if (inRange) {
    if (!ACTIVE_BUZZER) {
      targetFreq = map(med, minCM, maxCM, maxHz, minHz);
      targetFreq = constrain(targetFreq, minHz, maxHz);
      smoothFreq = (1.0 - alpha) * smoothFreq + alpha * targetFreq;
      tone(buzzerPin, (unsigned int)(smoothFreq)); // continuous variable pitch
    } else {
      // active buzzer: pick a discrete note and toggle on
      int idx = map(med, minCM, maxCM, 0, notesCount - 1);
      idx = constrain(idx, 0, notesCount - 1);
      targetFreq = notes[idx];
      // play note as short beeps to create musical feel
      digitalWrite(buzzerPin, HIGH);
      delay(80);
      digitalWrite(buzzerPin, LOW);
      delay(40);
    }
  } else {
    if (!ACTIVE_BUZZER) noTone(buzzerPin);
    // if active buzzer and not in range we leave it off
  }

  // OLED & Serial
  drawOLED(med, inRange ? (ACTIVE_BUZZER ? targetFreq : (int)smoothFreq) : 0, inRange);
  Serial.print(F("Distance: "));
  if (med >= 900) Serial.print(F("No echo"));
  else Serial.print(med);
  Serial.print(F(" cm  | Playing: "));
  Serial.print(inRange ? "YES" : "NO");
  Serial.print(F(" | Freq: "));
  Serial.println(inRange ? (ACTIVE_BUZZER ? targetFreq : (int)smoothFreq) : 0);

  delay(30);
}
