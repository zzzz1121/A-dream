#include <Arduino.h>
#include <FastLED.h>
#include <M5Stack.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define BOARD_NAME "M5Stack Core ESP32"
#define ESPNOW_BOARD_NAME "M5STACK"
#define LED_PIN 4
#define NUM_LEDS 1
#define BUTTON_REFRESH_MS 80
#define UPTIME_REFRESH_MS 1000
#define ESPNOW_CHANNEL 1

CRGB leds[NUM_LEDS];

const CRGB COLORS[] = {
  CRGB::Red,
  CRGB::Green,
  CRGB::Blue,
  CRGB::White,
  CRGB::Black,
};

const char *const COLOR_NAMES[] = {
  "red",
  "green",
  "blue",
  "white",
  "off",
};

const uint8_t COLOR_COUNT = sizeof(COLORS) / sizeof(COLORS[0]);
const uint8_t BROADCAST_ADDRESS[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
const uint32_t PACKET_MAGIC = 0xAD202606;

struct EspNowControlPacket {
  uint32_t magic;
  char board[16];
  char event[16];
  char button[8];
  char action[16];
  char color[16];
  uint32_t sequence;
  uint32_t uptimeMs;
};

uint8_t colorIndex = 0;
uint32_t buttonEventCount = 0;
uint32_t espNowSequence = 0;
unsigned long lastButtonRefresh = 0;
unsigned long lastUptimeRefresh = 0;
char lastEvent[32] = "boot";

uint16_t currentLcdColor() {
  const CRGB color = COLORS[colorIndex];
  if (colorIndex == COLOR_COUNT - 1) {
    return 0x4208;
  }
  return M5.Lcd.color565(color.r, color.g, color.b);
}

void drawStaticScreen() {
  M5.Lcd.fillScreen(0x0000);

  M5.Lcd.fillRect(0, 0, 320, 32, 0x18E3);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(0xFFFF, 0x18E3);
  M5.Lcd.setCursor(12, 8);
  M5.Lcd.print("M5 RGB Button Test");

  M5.Lcd.setTextColor(0xFFFF, 0x0000);
  M5.Lcd.setCursor(18, 48);
  M5.Lcd.print("Color:");

  M5.Lcd.setCursor(18, 88);
  M5.Lcd.print("Count:");

  M5.Lcd.setCursor(18, 116);
  M5.Lcd.print("Uptime:");

  M5.Lcd.setCursor(18, 144);
  M5.Lcd.print("Last:");

  M5.Lcd.setCursor(18, 172);
  M5.Lcd.print("A:");
  M5.Lcd.setCursor(18, 194);
  M5.Lcd.print("B:");
  M5.Lcd.setCursor(18, 216);
  M5.Lcd.print("C:");

  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(0xC618, 0x0000);
  M5.Lcd.setCursor(152, 226);
  M5.Lcd.print("A prev  B next  C off  IO4 RGB");
}

void drawColorArea() {
  M5.Lcd.setTextSize(2);
  M5.Lcd.fillRect(108, 44, 104, 28, 0x0000);
  M5.Lcd.setTextColor(0xFFFF, 0x0000);
  M5.Lcd.setCursor(110, 48);
  M5.Lcd.print(COLOR_NAMES[colorIndex]);

  M5.Lcd.fillRoundRect(220, 44, 72, 40, 6, currentLcdColor());
  M5.Lcd.drawRoundRect(220, 44, 72, 40, 6, 0xFFFF);
}

void drawCountArea() {
  M5.Lcd.setTextSize(2);
  M5.Lcd.fillRect(110, 84, 170, 24, 0x0000);
  M5.Lcd.setTextColor(0xFFFF, 0x0000);
  M5.Lcd.setCursor(110, 88);
  M5.Lcd.print(buttonEventCount);
}

void drawUptimeArea() {
  M5.Lcd.setTextSize(2);
  M5.Lcd.fillRect(126, 112, 150, 24, 0x0000);
  M5.Lcd.setTextColor(0xFFFF, 0x0000);
  M5.Lcd.setCursor(126, 116);
  M5.Lcd.printf("%lus", millis() / 1000);
}

void drawLastEventArea() {
  M5.Lcd.setTextSize(2);
  M5.Lcd.fillRect(92, 140, 210, 24, 0x0000);
  M5.Lcd.setTextColor(0xFFFF, 0x0000);
  M5.Lcd.setCursor(92, 144);
  M5.Lcd.print(lastEvent);
}

void drawStatusValue(int y, bool pressed) {
  M5.Lcd.setTextSize(2);
  M5.Lcd.fillRect(70, y - 4, 86, 22, 0x0000);
  M5.Lcd.setCursor(70, y);
  M5.Lcd.setTextColor(pressed ? 0x07E0 : 0x8410, 0x0000);
  M5.Lcd.print(pressed ? "DOWN" : "up  ");
}

void drawButtonStatusArea() {
  drawStatusValue(172, M5.BtnA.isPressed());
  drawStatusValue(194, M5.BtnB.isPressed());
  drawStatusValue(216, M5.BtnC.isPressed());
}

void drawDynamicAreas() {
  drawColorArea();
  drawCountArea();
  drawUptimeArea();
  drawLastEventArea();
  drawButtonStatusArea();
}

void setColor(uint8_t index) {
  colorIndex = index % COLOR_COUNT;
  leds[0] = COLORS[colorIndex];
  FastLED.show();

  Serial.print("COLOR=");
  Serial.println(COLOR_NAMES[colorIndex]);
  drawColorArea();
  drawCountArea();
  drawLastEventArea();
}

void setLastEvent(const char *eventName) {
  strncpy(lastEvent, eventName, sizeof(lastEvent) - 1);
  lastEvent[sizeof(lastEvent) - 1] = '\0';
}

void printMacAddress(const uint8_t *mac) {
  for (uint8_t i = 0; i < 6; i++) {
    if (mac[i] < 0x10) {
      Serial.print("0");
    }
    Serial.print(mac[i], HEX);
    if (i < 5) {
      Serial.print(":");
    }
  }
}

void onDataSent(const uint8_t *macAddress, esp_now_send_status_t status) {
  Serial.print("EVENT=ESPNOW_SEND_RESULT TARGET=");
  printMacAddress(macAddress);
  Serial.print(" STATUS=");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

void initEspNowSender() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  Serial.print("LOCAL_WIFI_MAC=");
  Serial.println(WiFi.macAddress());
  Serial.print("ESPNOW_CHANNEL=");
  Serial.println(ESPNOW_CHANNEL);

  const esp_err_t initResult = esp_now_init();
  if (initResult != ESP_OK) {
    Serial.print("EVENT=ESPNOW_INIT_FAIL ERR=");
    Serial.println(initResult);
    return;
  }

  esp_now_register_send_cb(onDataSent);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, BROADCAST_ADDRESS, sizeof(BROADCAST_ADDRESS));
  peerInfo.channel = ESPNOW_CHANNEL;
  peerInfo.encrypt = false;

  const esp_err_t peerResult = esp_now_add_peer(&peerInfo);
  if (peerResult != ESP_OK && peerResult != ESP_ERR_ESPNOW_EXIST) {
    Serial.print("EVENT=ESPNOW_ADD_PEER_FAIL ERR=");
    Serial.println(peerResult);
    return;
  }

  Serial.println("EVENT=ESPNOW_SENDER_READY MODE=BROADCAST");
}

void sendEspNowControl(const char *button, const char *action) {
  EspNowControlPacket packet = {};
  packet.magic = PACKET_MAGIC;
  strncpy(packet.board, ESPNOW_BOARD_NAME, sizeof(packet.board) - 1);
  strncpy(packet.event, "BUTTON_PRESS", sizeof(packet.event) - 1);
  strncpy(packet.button, button, sizeof(packet.button) - 1);
  strncpy(packet.action, action, sizeof(packet.action) - 1);
  strncpy(packet.color, COLOR_NAMES[colorIndex], sizeof(packet.color) - 1);
  packet.sequence = ++espNowSequence;
  packet.uptimeMs = millis();

  const esp_err_t result = esp_now_send(BROADCAST_ADDRESS, reinterpret_cast<uint8_t *>(&packet), sizeof(packet));

  Serial.print("EVENT=ESPNOW_TX BUTTON=");
  Serial.print(button);
  Serial.print(" ACTION=");
  Serial.print(action);
  Serial.print(" COLOR=");
  Serial.print(packet.color);
  Serial.print(" SEQ=");
  Serial.print(packet.sequence);
  Serial.print(" RESULT=");
  Serial.println(result == ESP_OK ? "QUEUED" : "ERROR");
}

void nextColor() {
  buttonEventCount++;
  setLastEvent("B next");
  setColor((colorIndex + 1) % COLOR_COUNT);
  sendEspNowControl("B", "NEXT");
  Serial.print("EVENT=BUTTON_PRESS BUTTON=B ACTION=NEXT COLOR=");
  Serial.println(COLOR_NAMES[colorIndex]);
}

void previousColor() {
  buttonEventCount++;
  setLastEvent("A previous");
  setColor((colorIndex + COLOR_COUNT - 1) % COLOR_COUNT);
  sendEspNowControl("A", "PREVIOUS");
  Serial.print("EVENT=BUTTON_PRESS BUTTON=A ACTION=PREVIOUS COLOR=");
  Serial.println(COLOR_NAMES[colorIndex]);
}

void offColor() {
  buttonEventCount++;
  setLastEvent("C off");
  setColor(COLOR_COUNT - 1);
  sendEspNowControl("C", "OFF");
  Serial.println("EVENT=BUTTON_PRESS BUTTON=C ACTION=OFF COLOR=off");
}

void flashUploadMarker() {
  const CRGB bootColors[] = {
    CRGB::Red,
    CRGB::Green,
    CRGB::Blue,
    CRGB::White,
  };

  for (uint8_t i = 0; i < sizeof(bootColors) / sizeof(bootColors[0]); i++) {
    leds[0] = bootColors[i];
    FastLED.show();
    delay(250);
  }
}

void setup() {
  M5.begin();
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.print("Starting screen button RGB test for ");
  Serial.println(BOARD_NAME);
  Serial.println("RGB LED data pin: GPIO 4");
  Serial.println("A=previous color, B=next color, C=off");

  initEspNowSender();

  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(50);

  M5.Lcd.setBrightness(160);
  flashUploadMarker();
  drawStaticScreen();
  setColor(colorIndex);
  drawDynamicAreas();
}

void loop() {
  M5.update();

  if (M5.BtnA.wasPressed()) {
    previousColor();
  }

  if (M5.BtnB.wasPressed()) {
    nextColor();
  }

  if (M5.BtnC.wasPressed()) {
    offColor();
  }

  if (M5.BtnA.wasReleased()) {
    Serial.println("EVENT=BUTTON_RELEASE BUTTON=A");
  }

  if (M5.BtnB.wasReleased()) {
    Serial.println("EVENT=BUTTON_RELEASE BUTTON=B");
  }

  if (M5.BtnC.wasReleased()) {
    Serial.println("EVENT=BUTTON_RELEASE BUTTON=C");
  }

  if (millis() - lastButtonRefresh >= BUTTON_REFRESH_MS) {
    lastButtonRefresh = millis();
    drawButtonStatusArea();
  }

  if (millis() - lastUptimeRefresh >= UPTIME_REFRESH_MS) {
    lastUptimeRefresh = millis();
    drawUptimeArea();
  }
}
