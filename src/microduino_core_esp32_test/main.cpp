#include <Arduino.h>
#include <FastLED.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define BOARD_NAME "MICRODUINO"
#define LED_PIN 4
#define NUM_LEDS 1
#define LED_BRIGHTNESS 50
#define ESPNOW_CHANNEL 1
#define HEARTBEAT_MS 3000

struct EspNowControlPacket {
  uint32_t magic;
  char board[16];
  char event[16];
  char button[8];
  char action[16];
  char color[16];
  int32_t encoderDelta;
  int32_t flowPosition;
  uint32_t sequence;
  uint32_t uptimeMs;
};

const uint32_t PACKET_MAGIC = 0xAD202606;

CRGB leds[NUM_LEDS];
uint32_t receivedCount = 0;
unsigned long lastHeartbeatTime = 0;
bool colorUpdatePending = false;
char pendingColor[16] = "off";
char currentColor[16] = "red";

CRGB colorFromName(const char *name) {
  if (strcmp(name, "red") == 0) {
    return CRGB::Red;
  }
  if (strcmp(name, "green") == 0) {
    return CRGB::Green;
  }
  if (strcmp(name, "blue") == 0) {
    return CRGB::Blue;
  }
  if (strcmp(name, "white") == 0) {
    return CRGB::White;
  }
  return CRGB::Black;
}

void showCurrentColor() {
  leds[0] = colorFromName(currentColor);
  FastLED.show();
}

void applyColor(const char *name) {
  strncpy(currentColor, name, sizeof(currentColor) - 1);
  currentColor[sizeof(currentColor) - 1] = '\0';

  showCurrentColor();

  Serial.print("EVENT=RGB_APPLIED COLOR=");
  Serial.println(currentColor);
}

void flashStartupMarker() {
  const CRGB bootColors[] = {
    CRGB::Red,
    CRGB::Green,
    CRGB::Blue,
    CRGB::White,
  };

  for (uint8_t i = 0; i < sizeof(bootColors) / sizeof(bootColors[0]); i++) {
    fill_solid(leds, NUM_LEDS, bootColors[i]);
    FastLED.show();
    delay(180);
  }

  applyColor("red");
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

void printHexBytes(const uint8_t *data, int length) {
  for (int i = 0; i < length; i++) {
    if (data[i] < 0x10) {
      Serial.print("0");
    }
    Serial.print(data[i], HEX);
    if (i < length - 1) {
      Serial.print(" ");
    }
  }
}

void printAsciiBytes(const uint8_t *data, int length) {
  for (int i = 0; i < length; i++) {
    const char c = static_cast<char>(data[i]);
    Serial.print(isPrintable(c) ? c : '.');
  }
}

void onDataReceived(const uint8_t *macAddress, const uint8_t *incomingData, int length) {
  receivedCount++;

  Serial.print("EVENT=ESPNOW_RX_RAW COUNT=");
  Serial.print(receivedCount);
  Serial.print(" FROM_MAC=");
  printMacAddress(macAddress);
  Serial.print(" LEN=");
  Serial.print(length);
  Serial.print(" HEX=");
  printHexBytes(incomingData, length);
  Serial.print(" ASCII=");
  printAsciiBytes(incomingData, length);
  Serial.println();

  if (length == sizeof(EspNowControlPacket)) {
    EspNowControlPacket packet;
    memcpy(&packet, incomingData, sizeof(packet));

    if (packet.magic == PACKET_MAGIC) {
      Serial.print("EVENT=ESPNOW_CONTROL_RX FROM_BOARD=");
      Serial.print(packet.board);
      Serial.print(" EVENT_TYPE=");
      Serial.print(packet.event);
      Serial.print(" BUTTON=");
      Serial.print(packet.button);
      Serial.print(" ACTION=");
      Serial.print(packet.action);
      Serial.print(" COLOR=");
      Serial.print(packet.color);
      Serial.print(" ENCODER_DELTA=");
      Serial.print(packet.encoderDelta);
      Serial.print(" ENCODER_POS=");
      Serial.print(packet.flowPosition);
      Serial.print(" SEQ=");
      Serial.print(packet.sequence);
      Serial.print(" REMOTE_UPTIME_MS=");
      Serial.println(packet.uptimeMs);

      strncpy(pendingColor, packet.color, sizeof(pendingColor) - 1);
      pendingColor[sizeof(pendingColor) - 1] = '\0';
      colorUpdatePending = true;
    } else {
      Serial.print("EVENT=ESPNOW_PACKET_MAGIC_MISMATCH MAGIC=0x");
      Serial.println(packet.magic, HEX);
    }
  }
}

void initEspNowReceiver() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  Serial.print("LOCAL_BOARD=");
  Serial.println(BOARD_NAME);
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

  esp_now_register_recv_cb(onDataReceived);
  Serial.println("EVENT=ESPNOW_RECEIVER_READY");
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("ESP-NOW receiver/logger starting.");

  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(LED_BRIGHTNESS);
  Serial.println("RGB LED data pin: GPIO 4");
  flashStartupMarker();

  initEspNowReceiver();
}

void loop() {
  if (colorUpdatePending) {
    char colorToApply[16];
    strncpy(colorToApply, pendingColor, sizeof(colorToApply) - 1);
    colorToApply[sizeof(colorToApply) - 1] = '\0';
    colorUpdatePending = false;
    applyColor(colorToApply);
  }

  if (millis() - lastHeartbeatTime >= HEARTBEAT_MS) {
    lastHeartbeatTime = millis();
    Serial.print("EVENT=HEARTBEAT BOARD=");
    Serial.print(BOARD_NAME);
    Serial.print(" RX_COUNT=");
    Serial.print(receivedCount);
    Serial.print(" COLOR=");
    Serial.print(currentColor);
    Serial.print(" UPTIME_MS=");
    Serial.println(millis());
  }
}
