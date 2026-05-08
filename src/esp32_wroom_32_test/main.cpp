#include <Arduino.h>
#include <FastLED.h>

#define BOARD_NAME "ESP32-WROOM-32"
#define LED_PIN 4
#define NUM_LEDS 30

CRGB leds[NUM_LEDS];

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.print("Starting WS2812B test for ");
  Serial.println(BOARD_NAME);

  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(50);
  Serial.println("LED controller initialized.");
}

void loop() {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  Serial.println("All LEDs off.");
  delay(1000);

  leds[0] = CRGB::Red;
  FastLED.show();
  Serial.println("First LED red.");
  delay(1000);

  leds[1] = CRGB::Green;
  FastLED.show();
  Serial.println("Second LED green.");
  delay(1000);

  leds[2] = CRGB::Blue;
  FastLED.show();
  Serial.println("Third LED blue.");
  delay(1000);
}
