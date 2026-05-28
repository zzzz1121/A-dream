#include <Arduino.h>

const int PIN_14 = 14;
const int PIN_25 = 25;
const int PIN_26 = 26;
const int PIN_27 = 27;

const int STEPS_PER_BURST = 400;
const int PULSE_HALF_PERIOD_US = 8000;
const int BETWEEN_BURSTS_MS = 1200;
const int BETWEEN_PATTERNS_MS = 2500;

void pulseSteps(int stepPin, int steps) {
  for (int i = 0; i < steps; i++) {
    digitalWrite(stepPin, LOW);
    delayMicroseconds(PULSE_HALF_PERIOD_US);
    digitalWrite(stepPin, HIGH);
    delayMicroseconds(PULSE_HALF_PERIOD_US);
  }
}

void runPattern(const char *name, int stepPin, int dirPin) {
  Serial.print("EVENT=PIN_DIAG_PATTERN NAME=");
  Serial.print(name);
  Serial.print(" STEP=");
  Serial.print(stepPin);
  Serial.print(" DIR=");
  Serial.println(dirPin);

  digitalWrite(dirPin, HIGH);
  delay(100);
  Serial.println("EVENT=PIN_DIAG_FORWARD");
  pulseSteps(stepPin, STEPS_PER_BURST);
  delay(BETWEEN_BURSTS_MS);

  digitalWrite(dirPin, LOW);
  delay(100);
  Serial.println("EVENT=PIN_DIAG_BACKWARD");
  pulseSteps(stepPin, STEPS_PER_BURST);
  delay(BETWEEN_PATTERNS_MS);
}

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(PIN_14, OUTPUT);
  pinMode(PIN_25, OUTPUT);
  pinMode(PIN_26, OUTPUT);
  pinMode(PIN_27, OUTPUT);

  digitalWrite(PIN_14, HIGH);
  digitalWrite(PIN_25, HIGH);
  digitalWrite(PIN_26, HIGH);
  digitalWrite(PIN_27, HIGH);

  Serial.println();
  Serial.println("EVENT=PIN_DIAG_BOOT TARGET=Microduino_Core_ESP32");
  Serial.println("EVENT=PIN_DIAG_NOTE LOGICAL_LEFT_STEP=27 LOGICAL_LEFT_DIR=26 LOGICAL_RIGHT_STEP=25 LOGICAL_RIGHT_DIR=14");
}

void loop() {
  runPattern("LEFT_STEP27_DIR26", PIN_27, PIN_26);
  runPattern("RIGHT_STEP25_DIR14", PIN_25, PIN_14);
}
