#include <Arduino.h>
#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEUtils.h>
#include <FastLED.h>
#include <M5Stack.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define BOARD_NAME "M5Stack Core ESP32"
#define ESPNOW_BOARD_NAME "M5STACK"
#define LED_PIN 4
#define NUM_LEDS 1
#define BUTTON_REFRESH_MS 80
#define ENCODER_POLL_MS 20
#define ENCODER_COLOR_STEP_INTERVAL_MS 180
#define ENCODER_STATUS_REFRESH_MS 200
#define UPTIME_REFRESH_MS 1000
#define ESPNOW_CHANNEL 1
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22
#define I2C_SCAN_REFRESH_MS 1500
#define FACES_ENCODER_I2C_ADDR 0x5E
#define FALLBACK_ENCODER_I2C_ADDR 0x62
#define MAX_I2C_SCAN_RESULTS 12
#define THINKGEAR_BT_TARGET_NAME "MindWave Mobile"
#define THINKGEAR_BLE_SCAN_SECONDS 5
#define THINKGEAR_BLE_RESCAN_MS 2000
#define THINKGEAR_BLE_SCAN_LINES 6
#define THINKGEAR_MAX_PAYLOAD 169
#define THINKGEAR_READ_BUDGET 96
#define THINKGEAR_DISPLAY_REFRESH_MS 500

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
BLEScan *bleScan = nullptr;

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

struct ThinkGearData {
  bool seen;
  uint8_t poorSignal;
  uint8_t attention;
  uint8_t meditation;
  uint32_t eegPower[8];
  uint32_t packets;
  uint32_t checksumErrors;
  uint32_t lengthErrors;
  uint32_t lastPacketMs;
};

enum ThinkGearParserState : uint8_t {
  TG_WAIT_SYNC_1,
  TG_WAIT_SYNC_2,
  TG_READ_LENGTH,
  TG_READ_PAYLOAD,
  TG_READ_CHECKSUM,
};

uint8_t colorIndex = 0;
uint32_t buttonEventCount = 0;
uint32_t espNowSequence = 0;
int32_t encoderStepCount = 0;
unsigned long lastButtonRefresh = 0;
unsigned long lastEncoderPoll = 0;
unsigned long lastEncoderColorStep = 0;
unsigned long lastEncoderStatusRefresh = 0;
unsigned long lastI2CScan = 0;
unsigned long lastUptimeRefresh = 0;
unsigned long lastThinkGearDisplayRefresh = 0;
unsigned long lastThinkGearBluetoothConnectAttempt = 0;
char lastEvent[32] = "boot";
bool facesEncoderReady = false;
bool thinkGearBluetoothReady = false;
bool thinkGearBluetoothConnected = false;
uint32_t thinkGearBluetoothConnectAttempts = 0;
uint32_t thinkGearBluetoothScanCount = 0;
uint32_t thinkGearBluetoothFoundCount = 0;
char thinkGearBluetoothStatus[40] = "bt boot";
char thinkGearBluetoothScanLines[THINKGEAR_BLE_SCAN_LINES][48] = {};
uint32_t facesEncoderFailCount = 0;
uint8_t facesEncoderButton = 1;
uint8_t lastFacesEncoderRawIncrement = 0;
uint8_t activeEncoderI2CAddr = FACES_ENCODER_I2C_ADDR;
uint8_t i2cScanResults[MAX_I2C_SCAN_RESULTS] = {};
uint8_t i2cScanCount = 0;
ThinkGearData thinkGear = {};
ThinkGearParserState thinkGearParserState = TG_WAIT_SYNC_1;
uint8_t thinkGearPayload[THINKGEAR_MAX_PAYLOAD] = {};
uint8_t thinkGearPayloadLength = 0;
uint8_t thinkGearPayloadIndex = 0;
uint8_t thinkGearPayloadSum = 0;

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
  M5.Lcd.print("Bluetooth EEG RX");
}

void drawColorArea() {
}

void drawCountArea() {
}

void drawEncoderArea() {
}

void drawUptimeArea() {
}

void drawLastEventArea() {
}

void drawStatusValue(int y, bool pressed) {
  M5.Lcd.setTextSize(2);
  M5.Lcd.fillRect(70, y - 4, 86, 22, 0x0000);
  M5.Lcd.setCursor(70, y);
  M5.Lcd.setTextColor(pressed ? 0x07E0 : 0x8410, 0x0000);
  M5.Lcd.print(pressed ? "DOWN" : "up  ");
}

void drawButtonStatusArea() {
}

void setThinkGearBluetoothStatus(const char *status) {
  strncpy(thinkGearBluetoothStatus, status, sizeof(thinkGearBluetoothStatus) - 1);
  thinkGearBluetoothStatus[sizeof(thinkGearBluetoothStatus) - 1] = '\0';
}

void clearThinkGearBluetoothScanLines() {
  for (uint8_t i = 0; i < THINKGEAR_BLE_SCAN_LINES; i++) {
    thinkGearBluetoothScanLines[i][0] = '\0';
  }
}

void drawThinkGearArea() {
  M5.Lcd.fillRect(0, 34, 320, 206, 0x0000);

  if (!thinkGearBluetoothReady) {
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(0xF800, 0x0000);
    M5.Lcd.setCursor(18, 54);
    M5.Lcd.print("BT init failed");
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(0xC618, 0x0000);
    M5.Lcd.setCursor(18, 88);
    M5.Lcd.print("Check BLE support");
    return;
  }

  if (!thinkGearBluetoothConnected) {
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(0xFFE0, 0x0000);
    M5.Lcd.setCursor(18, 42);
    M5.Lcd.print(thinkGearBluetoothStatus);

    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(0xFFFF, 0x0000);
    M5.Lcd.setCursor(18, 74);
    M5.Lcd.print("Target:");
    M5.Lcd.setCursor(76, 74);
    M5.Lcd.print(THINKGEAR_BT_TARGET_NAME);

    M5.Lcd.setCursor(18, 96);
    M5.Lcd.printf("dev:%lu try:%lu",
                  static_cast<unsigned long>(thinkGearBluetoothFoundCount),
                  static_cast<unsigned long>(thinkGearBluetoothConnectAttempts));

    M5.Lcd.setTextColor(0xC618, 0x0000);
    M5.Lcd.setCursor(18, 116);
    M5.Lcd.print("BLE devices:");
    for (uint8_t i = 0; i < THINKGEAR_BLE_SCAN_LINES; i++) {
      M5.Lcd.setCursor(18, 132 + i * 14);
      M5.Lcd.print(thinkGearBluetoothScanLines[i][0] != '\0'
                     ? thinkGearBluetoothScanLines[i]
                     : "-");
    }

    M5.Lcd.setCursor(18, 222);
    M5.Lcd.print("COM6 shows name, MAC, UUIDs");
    return;
  }

  if (!thinkGear.seen) {
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(0xFFE0, 0x0000);
    M5.Lcd.setCursor(18, 46);
    M5.Lcd.print("BT connected");
    M5.Lcd.setCursor(18, 82);
    M5.Lcd.print("Waiting data");

    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(0xC618, 0x0000);
    M5.Lcd.setCursor(18, 118);
    M5.Lcd.print("Waiting for AA AA packet header");
    M5.Lcd.setCursor(18, 140);
    M5.Lcd.print("If this stays here, device may be BLE");
    return;
  }

  const uint32_t ageMs = millis() - thinkGear.lastPacketMs;
  const uint16_t statusColor = ageMs < 2000 ? 0x07E0 : 0xFBE0;

  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(statusColor, 0x0000);
  M5.Lcd.setCursor(18, 42);
  M5.Lcd.print("Packet OK");

  M5.Lcd.setTextColor(0xFFFF, 0x0000);
  M5.Lcd.setCursor(18, 74);
  M5.Lcd.printf("Signal: %3u", thinkGear.poorSignal);
  M5.Lcd.setCursor(18, 106);
  M5.Lcd.printf("Attention:%3u", thinkGear.attention);
  M5.Lcd.setCursor(18, 138);
  M5.Lcd.printf("Meditate: %3u", thinkGear.meditation);

  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(0xC618, 0x0000);
  M5.Lcd.setCursor(18, 176);
  M5.Lcd.printf("packets:%lu  errors:%lu  age:%lus",
                static_cast<unsigned long>(thinkGear.packets),
                static_cast<unsigned long>(thinkGear.checksumErrors + thinkGear.lengthErrors),
                static_cast<unsigned long>(ageMs / 1000));
  M5.Lcd.setCursor(18, 198);
  M5.Lcd.printf("delta:%lu theta:%lu alpha:%lu",
                static_cast<unsigned long>(thinkGear.eegPower[0]),
                static_cast<unsigned long>(thinkGear.eegPower[1]),
                static_cast<unsigned long>(thinkGear.eegPower[2]));
  M5.Lcd.setCursor(18, 216);
  M5.Lcd.printf("beta:%lu gamma:%lu",
                static_cast<unsigned long>(thinkGear.eegPower[4] + thinkGear.eegPower[5]),
                static_cast<unsigned long>(thinkGear.eegPower[6] + thinkGear.eegPower[7]));
}

void drawDynamicAreas() {
  drawThinkGearArea();
}

void setColor(uint8_t index) {
  colorIndex = index % COLOR_COUNT;
  leds[0] = COLORS[colorIndex];
  FastLED.show();

  Serial.print("COLOR=");
  Serial.println(COLOR_NAMES[colorIndex]);
  drawColorArea();
  drawEncoderArea();
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

uint32_t readUint24BE(const uint8_t *bytes) {
  return (static_cast<uint32_t>(bytes[0]) << 16) |
         (static_cast<uint32_t>(bytes[1]) << 8) |
         static_cast<uint32_t>(bytes[2]);
}

void printThinkGearData() {
  Serial.print("EVENT=THINKGEAR_RX LEN=");
  Serial.print(thinkGearPayloadLength);
  Serial.print(" POOR_SIGNAL=");
  Serial.print(thinkGear.poorSignal);
  Serial.print(" ATTENTION=");
  Serial.print(thinkGear.attention);
  Serial.print(" MEDITATION=");
  Serial.print(thinkGear.meditation);
  Serial.print(" DELTA=");
  Serial.print(thinkGear.eegPower[0]);
  Serial.print(" THETA=");
  Serial.print(thinkGear.eegPower[1]);
  Serial.print(" LOW_ALPHA=");
  Serial.print(thinkGear.eegPower[2]);
  Serial.print(" HIGH_ALPHA=");
  Serial.print(thinkGear.eegPower[3]);
  Serial.print(" LOW_BETA=");
  Serial.print(thinkGear.eegPower[4]);
  Serial.print(" HIGH_BETA=");
  Serial.print(thinkGear.eegPower[5]);
  Serial.print(" LOW_GAMMA=");
  Serial.print(thinkGear.eegPower[6]);
  Serial.print(" MID_GAMMA=");
  Serial.print(thinkGear.eegPower[7]);
  Serial.print(" PACKETS=");
  Serial.println(thinkGear.packets);
}

void parseThinkGearPayload(const uint8_t *payload, uint8_t payloadLength) {
  uint8_t index = 0;

  while (index < payloadLength) {
    const uint8_t code = payload[index++];
    uint8_t valueLength = 1;

    if (code >= 0x80) {
      if (index >= payloadLength) {
        return;
      }
      valueLength = payload[index++];
    }

    if (index + valueLength > payloadLength) {
      return;
    }

    switch (code) {
      case 0x02:
        if (valueLength == 1) {
          thinkGear.poorSignal = payload[index];
        }
        break;
      case 0x04:
        if (valueLength == 1) {
          thinkGear.attention = payload[index];
        }
        break;
      case 0x05:
        if (valueLength == 1) {
          thinkGear.meditation = payload[index];
        }
        break;
      case 0x83:
        if (valueLength >= 24) {
          for (uint8_t i = 0; i < 8; i++) {
            thinkGear.eegPower[i] = readUint24BE(&payload[index + i * 3]);
          }
        }
        break;
      default:
        break;
    }

    index += valueLength;
  }

  thinkGear.seen = true;
  thinkGear.packets++;
  thinkGear.lastPacketMs = millis();
  setLastEvent("tg rx");
  printThinkGearData();
  drawThinkGearArea();
  drawLastEventArea();
}

void processThinkGearByte(uint8_t value) {
  switch (thinkGearParserState) {
    case TG_WAIT_SYNC_1:
      if (value == 0xAA) {
        thinkGearParserState = TG_WAIT_SYNC_2;
      }
      break;

    case TG_WAIT_SYNC_2:
      thinkGearParserState = value == 0xAA ? TG_READ_LENGTH : TG_WAIT_SYNC_1;
      break;

    case TG_READ_LENGTH:
      if (value == 0 || value > THINKGEAR_MAX_PAYLOAD) {
        thinkGear.lengthErrors++;
        thinkGearParserState = value == 0xAA ? TG_WAIT_SYNC_2 : TG_WAIT_SYNC_1;
        return;
      }
      thinkGearPayloadLength = value;
      thinkGearPayloadIndex = 0;
      thinkGearPayloadSum = 0;
      thinkGearParserState = TG_READ_PAYLOAD;
      break;

    case TG_READ_PAYLOAD:
      thinkGearPayload[thinkGearPayloadIndex++] = value;
      thinkGearPayloadSum += value;
      if (thinkGearPayloadIndex >= thinkGearPayloadLength) {
        thinkGearParserState = TG_READ_CHECKSUM;
      }
      break;

    case TG_READ_CHECKSUM: {
      const uint8_t expectedChecksum = ~thinkGearPayloadSum;
      if (value == expectedChecksum) {
        parseThinkGearPayload(thinkGearPayload, thinkGearPayloadLength);
      } else {
        thinkGear.checksumErrors++;
        Serial.print("EVENT=THINKGEAR_CHECKSUM_FAIL EXPECTED=0x");
        if (expectedChecksum < 0x10) {
          Serial.print("0");
        }
        Serial.print(expectedChecksum, HEX);
        Serial.print(" GOT=0x");
        if (value < 0x10) {
          Serial.print("0");
        }
        Serial.println(value, HEX);
        drawThinkGearArea();
      }
      thinkGearParserState = TG_WAIT_SYNC_1;
      break;
    }
  }
}

void initThinkGearBluetooth() {
  clearThinkGearBluetoothScanLines();
  setThinkGearBluetoothStatus("BLE init");

  BLEDevice::init("M5-BLE-SCAN");
  bleScan = BLEDevice::getScan();
  if (bleScan == nullptr) {
    thinkGearBluetoothReady = false;
    setThinkGearBluetoothStatus("BLE init failed");
    Serial.println("EVENT=THINKGEAR_BLE_INIT_FAIL");
    return;
  }

  bleScan->setActiveScan(true);
  bleScan->setInterval(100);
  bleScan->setWindow(99);

  thinkGearBluetoothReady = true;
  thinkGearBluetoothConnected = false;
  setThinkGearBluetoothStatus("BLE scan ready");
  Serial.println("EVENT=THINKGEAR_BLE_READY MODE=SCAN");
}

void rememberThinkGearBluetoothDevice(uint8_t lineIndex, BLEAdvertisedDevice &device) {
  if (lineIndex >= THINKGEAR_BLE_SCAN_LINES) {
    return;
  }

  const std::string name = device.haveName() ? device.getName() : std::string("(no name)");
  snprintf(thinkGearBluetoothScanLines[lineIndex],
           sizeof(thinkGearBluetoothScanLines[lineIndex]),
           "%u:%s %ddB",
           lineIndex + 1,
           name.c_str(),
           device.getRSSI());
}

bool deviceNameMatchesThinkGearTarget(BLEAdvertisedDevice &device) {
  if (!device.haveName()) {
    return false;
  }

  const String deviceName = String(device.getName().c_str());
  return deviceName.equals(THINKGEAR_BT_TARGET_NAME) ||
         deviceName.indexOf(THINKGEAR_BT_TARGET_NAME) >= 0 ||
         String(THINKGEAR_BT_TARGET_NAME).indexOf(deviceName) >= 0;
}

bool scanAndConnectThinkGearBluetooth() {
  thinkGearBluetoothScanCount++;
  thinkGearBluetoothFoundCount = 0;
  clearThinkGearBluetoothScanLines();
  setThinkGearBluetoothStatus("BLE scanning");
  drawThinkGearArea();

  Serial.print("EVENT=THINKGEAR_BLE_SCAN_START SECONDS=");
  Serial.print(THINKGEAR_BLE_SCAN_SECONDS);
  Serial.print(" TARGET=");
  Serial.println(THINKGEAR_BT_TARGET_NAME);

  if (bleScan == nullptr) {
    setThinkGearBluetoothStatus("BLE scan fail");
    Serial.println("EVENT=THINKGEAR_BLE_SCAN_FAIL NO_SCANNER");
    drawThinkGearArea();
    return false;
  }

  BLEScanResults scanResults = bleScan->start(THINKGEAR_BLE_SCAN_SECONDS, false);
  thinkGearBluetoothFoundCount = scanResults.getCount();
  Serial.print("EVENT=THINKGEAR_BLE_SCAN_DONE COUNT=");
  Serial.println(static_cast<int>(thinkGearBluetoothFoundCount));

  bool targetFound = false;
  for (int i = 0; i < scanResults.getCount(); i++) {
    BLEAdvertisedDevice device = scanResults.getDevice(i);

    Serial.print("EVENT=THINKGEAR_BLE_DEVICE INDEX=");
    Serial.print(i);
    Serial.print(" INFO=");
    Serial.println(device.toString().c_str());

    if (i < THINKGEAR_BLE_SCAN_LINES) {
      rememberThinkGearBluetoothDevice(static_cast<uint8_t>(i), device);
    }

    if (!targetFound && deviceNameMatchesThinkGearTarget(device)) {
      targetFound = true;
    }
  }

  bleScan->clearResults();

  setThinkGearBluetoothStatus(targetFound ? "BLE target seen" : "BLE target missing");
  Serial.println(targetFound ? "EVENT=THINKGEAR_BLE_TARGET_SEEN" : "EVENT=THINKGEAR_BLE_TARGET_NOT_FOUND");
  drawThinkGearArea();
  return false;
}

void updateThinkGearBluetoothConnection() {
  if (!thinkGearBluetoothReady) {
    thinkGearBluetoothConnected = false;
    return;
  }

  thinkGearBluetoothConnected = false;

  const unsigned long now = millis();
  if (thinkGearBluetoothConnectAttempts > 0 &&
      now - lastThinkGearBluetoothConnectAttempt < THINKGEAR_BLE_RESCAN_MS) {
    return;
  }

  lastThinkGearBluetoothConnectAttempt = now;
  thinkGearBluetoothConnectAttempts++;

  Serial.print("EVENT=THINKGEAR_BT_CONNECT_ATTEMPT TARGET=");
  Serial.print(THINKGEAR_BT_TARGET_NAME);
  Serial.print(" ATTEMPT=");
  Serial.println(thinkGearBluetoothConnectAttempts);

  scanAndConnectThinkGearBluetooth();
}

void handleThinkGearBluetooth() {
  updateThinkGearBluetoothConnection();

  if (millis() - lastThinkGearDisplayRefresh >= THINKGEAR_DISPLAY_REFRESH_MS) {
    lastThinkGearDisplayRefresh = millis();
    drawThinkGearArea();
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

void sendEspNowControl(const char *eventType, const char *button, const char *action, int32_t encoderDelta) {
  EspNowControlPacket packet = {};
  packet.magic = PACKET_MAGIC;
  strncpy(packet.board, ESPNOW_BOARD_NAME, sizeof(packet.board) - 1);
  strncpy(packet.event, eventType, sizeof(packet.event) - 1);
  strncpy(packet.button, button, sizeof(packet.button) - 1);
  strncpy(packet.action, action, sizeof(packet.action) - 1);
  strncpy(packet.color, COLOR_NAMES[colorIndex], sizeof(packet.color) - 1);
  packet.encoderDelta = encoderDelta;
  packet.flowPosition = encoderStepCount;
  packet.sequence = ++espNowSequence;
  packet.uptimeMs = millis();

  const esp_err_t result = esp_now_send(BROADCAST_ADDRESS, reinterpret_cast<uint8_t *>(&packet), sizeof(packet));

  Serial.print("EVENT=ESPNOW_TX BUTTON=");
  Serial.print(button);
  Serial.print(" ACTION=");
  Serial.print(action);
  Serial.print(" COLOR=");
  Serial.print(packet.color);
  Serial.print(" ENCODER_DELTA=");
  Serial.print(packet.encoderDelta);
  Serial.print(" ENCODER_POS=");
  Serial.print(packet.flowPosition);
  Serial.print(" SEQ=");
  Serial.print(packet.sequence);
  Serial.print(" RESULT=");
  Serial.println(result == ESP_OK ? "QUEUED" : "ERROR");
}

void sendEncoderColorStep(int32_t delta, const char *source) {
  encoderStepCount += delta;
  buttonEventCount++;

  if (delta > 0) {
    setColor((colorIndex + 1) % COLOR_COUNT);
    setLastEvent("enc next");
    sendEspNowControl("ENCODER_ROTATE", "ENC", "COLOR_NEXT", delta);
    Serial.print("EVENT=ENCODER_ROTATE DIR=CW ENCODER_POS=");
  } else {
    setColor((colorIndex + COLOR_COUNT - 1) % COLOR_COUNT);
    setLastEvent("enc ccw");
    sendEspNowControl("ENCODER_ROTATE", "ENC", "COLOR_PREVIOUS", delta);
    Serial.print("EVENT=ENCODER_ROTATE DIR=CCW ENCODER_POS=");
  }

  Serial.print(encoderStepCount);
  Serial.print(" SRC=");
  Serial.println(source);
  drawEncoderArea();
  drawCountArea();
  drawLastEventArea();
}

bool i2cDevicePresent(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

void scanI2CBus() {
  Serial.println("EVENT=I2C_SCAN_START");
  i2cScanCount = 0;

  for (uint8_t address = 1; address < 127; address++) {
    if (i2cDevicePresent(address)) {
      if (i2cScanCount < MAX_I2C_SCAN_RESULTS) {
        i2cScanResults[i2cScanCount] = address;
      }
      i2cScanCount++;

      Serial.print("EVENT=I2C_DEVICE_FOUND ADDR=0x");
      if (address < 0x10) {
        Serial.print("0");
      }
      Serial.println(address, HEX);
    }
  }

  Serial.print("EVENT=I2C_SCAN_DONE FOUND=");
  Serial.println(i2cScanCount);
}

bool chooseEncoderI2CAddress() {
  if (i2cDevicePresent(FACES_ENCODER_I2C_ADDR)) {
    activeEncoderI2CAddr = FACES_ENCODER_I2C_ADDR;
    return true;
  }

  if (i2cDevicePresent(FALLBACK_ENCODER_I2C_ADDR)) {
    activeEncoderI2CAddr = FALLBACK_ENCODER_I2C_ADDR;
    return true;
  }

  activeEncoderI2CAddr = FACES_ENCODER_I2C_ADDR;
  return false;
}

bool readFacesEncoder(uint8_t &rawIncrement, uint8_t &buttonState) {
  const uint8_t bytesRequested = 3;
  const uint8_t bytesReceived = Wire.requestFrom(activeEncoderI2CAddr, bytesRequested);

  if (bytesReceived < 2) {
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }

  rawIncrement = Wire.read();
  buttonState = Wire.read();

  while (Wire.available()) {
    Wire.read();
  }

  return true;
}

int32_t decodeFacesEncoderDelta(uint8_t rawIncrement) {
  if (rawIncrement == 0) {
    return 0;
  }

  if (rawIncrement > 127) {
    return -static_cast<int32_t>(256 - rawIncrement);
  }

  return rawIncrement;
}

void handleEncoder() {
  if (millis() - lastEncoderPoll < ENCODER_POLL_MS) {
    return;
  }
  lastEncoderPoll = millis();

  uint8_t rawIncrement = 0;
  uint8_t buttonState = facesEncoderButton;
  if (!readFacesEncoder(rawIncrement, buttonState)) {
    facesEncoderFailCount++;
    lastFacesEncoderRawIncrement = 0;

    if (facesEncoderReady) {
      facesEncoderReady = false;
      setLastEvent("i2c lost");
      Serial.print("EVENT=FACES_ENCODER_LOST FAIL_COUNT=");
      Serial.println(facesEncoderFailCount);
      drawEncoderArea();
      drawLastEventArea();
    }
    return;
  }

  if (!facesEncoderReady) {
    facesEncoderReady = true;
    setLastEvent("i2c ready");
    Serial.print("EVENT=FACES_ENCODER_READY ADDR=0x");
    Serial.println(activeEncoderI2CAddr, HEX);
    drawEncoderArea();
    drawLastEventArea();
  }

  facesEncoderButton = buttonState;
  lastFacesEncoderRawIncrement = rawIncrement;

  const int32_t delta = decodeFacesEncoderDelta(rawIncrement);
  if (delta == 0) {
    return;
  }

  const unsigned long now = millis();
  if (now - lastEncoderColorStep < ENCODER_COLOR_STEP_INTERVAL_MS) {
    Serial.print("EVENT=FACES_ENCODER_ROTATE_IGNORED RAW=");
    Serial.print(rawIncrement);
    Serial.print(" DELTA=");
    Serial.print(delta);
    Serial.print(" REASON=THROTTLE_MS_");
    Serial.println(ENCODER_COLOR_STEP_INTERVAL_MS);
    return;
  }
  lastEncoderColorStep = now;

  Serial.print("EVENT=FACES_ENCODER_ROTATE RAW=");
  Serial.print(rawIncrement);
  Serial.print(" DELTA=");
  Serial.print(delta);
  Serial.print(" COLOR_STEP=1");
  Serial.print(" KEY=");
  Serial.println(buttonState);

  sendEncoderColorStep(delta > 0 ? 1 : -1, "I2C");
}

void nextColor() {
  buttonEventCount++;
  setLastEvent("B next");
  setColor((colorIndex + 1) % COLOR_COUNT);
  sendEspNowControl("BUTTON_PRESS", "B", "NEXT", 0);
  Serial.print("EVENT=BUTTON_PRESS BUTTON=B ACTION=NEXT COLOR=");
  Serial.println(COLOR_NAMES[colorIndex]);
}

void previousColor() {
  buttonEventCount++;
  setLastEvent("A previous");
  setColor((colorIndex + COLOR_COUNT - 1) % COLOR_COUNT);
  sendEspNowControl("BUTTON_PRESS", "A", "PREVIOUS", 0);
  Serial.print("EVENT=BUTTON_PRESS BUTTON=A ACTION=PREVIOUS COLOR=");
  Serial.println(COLOR_NAMES[colorIndex]);
}

void offColor() {
  buttonEventCount++;
  setLastEvent("C off");
  setColor(COLOR_COUNT - 1);
  sendEspNowControl("BUTTON_PRESS", "C", "OFF", 0);
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
  M5.Power.begin();
  Serial.begin(115200);
  thinkGear.poorSignal = 255;
  delay(300);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(100000);
  dacWrite(25, 0);

  Serial.println();
  Serial.print("Starting screen button RGB test for ");
  Serial.println(BOARD_NAME);
  Serial.println("RGB LED data pin: GPIO 4");
  Serial.println("A=previous color, B=next color, C=off");
  Serial.println("Faces Encoder: I2C address 0x5E, Mega328 module");
  Serial.println("Fallback test address: 0x62");
  Serial.println("I2C pins: SDA=GPIO21, SCL=GPIO22, speed=100kHz");
  Serial.println("Rotate encoder = change color, ESP-NOW sends color to Microduino");
  Serial.println("ThinkGear input: BLE scan diagnosis, no wire");

  initThinkGearBluetooth();

  scanI2CBus();
  facesEncoderReady = chooseEncoderI2CAddress();
  Serial.print("EVENT=FACES_ENCODER_BOOT_STATUS ACTIVE_ADDR=0x");
  Serial.print(activeEncoderI2CAddr, HEX);
  Serial.print(" STATUS=");
  Serial.println(facesEncoderReady ? "OK" : "NOT_FOUND");

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
  handleEncoder();
  handleThinkGearBluetooth();

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

  if (millis() - lastEncoderStatusRefresh >= ENCODER_STATUS_REFRESH_MS) {
    lastEncoderStatusRefresh = millis();
    drawEncoderArea();
  }

  if (!facesEncoderReady && millis() - lastI2CScan >= I2C_SCAN_REFRESH_MS) {
    lastI2CScan = millis();
    scanI2CBus();
    facesEncoderReady = chooseEncoderI2CAddress();
    drawEncoderArea();
  }

  if (millis() - lastUptimeRefresh >= UPTIME_REFRESH_MS) {
    lastUptimeRefresh = millis();
    drawUptimeArea();
  }
}
