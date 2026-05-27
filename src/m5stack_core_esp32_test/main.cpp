#include <Arduino.h>
#include <M5Stack.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_arduino_version.h>

#define BOARD_NAME "M5Stack Core ESP32"
#define DEVICE_ROLE "m5GatewayMonitor"
#define ESPNOW_CHANNEL 1
#define SERIAL_BAUD 115200
#define SERIAL_LINE_BUFFER_SIZE 192
#define DISPLAY_REFRESH_MS 1000
#define STATUS_PRINT_MS 1000
#define SERIAL_TIMEOUT_MS 3000
#define MIC_STATUS_TIMEOUT_MS 3000
#define DREAM_PACKET_MAGIC 0x44524541UL
#define DREAM_PROTOCOL_VERSION 1

const uint8_t BROADCAST_MAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

enum DreamPacketType : uint8_t {
  DREAM_PACKET_EEG = 1,
  DREAM_PACKET_MIC_STATUS = 2,
  DREAM_PACKET_CONTROL = 3,
};

enum DreamPacketFlags : uint8_t {
  EEG_FLAG_SIGNAL_OK = 1 << 0,
  EEG_FLAG_SOURCE_TIMEOUT = 1 << 1,
  EEG_FLAG_CHECKSUM_OK = 1 << 2,
};

enum DreamLightMode : uint8_t {
  LIGHT_OFF,
  LIGHT_IDLE,
  LIGHT_EEG_BLEND,
  LIGHT_SIGNAL_BAD,
  LIGHT_TIMEOUT,
  LIGHT_MANUAL,
};

enum DreamStepperState : uint8_t {
  STEPPER_DISABLED,
  STEPPER_IDLE,
  STEPPER_MOVING,
  STEPPER_BREATHING,
  STEPPER_LIMITED,
  STEPPER_FAULT,
};

enum DreamRelayState : uint8_t {
  RELAY_OFF,
  RELAY_ARMING,
  RELAY_ON,
  RELAY_COOLDOWN,
  RELAY_FAULT,
};

enum DreamSafetyState : uint8_t {
  SAFETY_NORMAL,
  SAFETY_SIGNAL_BAD,
  SAFETY_LINK_TIMEOUT,
  SAFETY_ESTOP,
  SAFETY_FAULT,
};

enum DreamControlAction : uint8_t {
  CONTROL_NONE,
  CONTROL_SYSTEM_ENABLE,
  CONTROL_SYSTEM_DISABLE,
  CONTROL_LIGHT_AUTO,
  CONTROL_LIGHT_COLOR,
  CONTROL_LIGHT_OFF,
  CONTROL_RELAY_ON,
  CONTROL_RELAY_OFF,
  CONTROL_STEPPER_FORWARD,
  CONTROL_STEPPER_BACKWARD,
  CONTROL_STEPPER_STOP,
  CONTROL_ALL_STOP,
};

struct __attribute__((packed)) DreamEegEspNowPacket {
  uint32_t magic;
  uint16_t version;
  uint8_t type;
  uint8_t flags;
  uint32_t seq;
  uint32_t pcTimeMs;
  uint32_t m5UptimeMs;
  uint8_t poorSignal;
  uint8_t attention;
  uint8_t meditation;
  uint8_t reserved;
  uint32_t eegPower[8];
  uint16_t checksum;
};

struct __attribute__((packed)) DreamMicStatusPacket {
  uint32_t magic;
  uint16_t version;
  uint8_t type;
  uint8_t flags;
  uint32_t seq;
  uint32_t uptimeMs;
  uint32_t lastEegSeq;
  uint32_t eegAgeMs;
  uint32_t rxCount;
  uint32_t dropCount;
  uint32_t timeoutCount;
  uint8_t poorSignal;
  uint8_t attention;
  uint8_t meditation;
  uint8_t lightLevel;
  uint8_t lightMode;
  uint8_t stepperState;
  uint8_t relayState;
  uint8_t safetyState;
  uint32_t controlRxCount;
  uint8_t lastControlAction;
  uint8_t manualLightEnabled;
  uint8_t relayOutputEnabled;
  uint8_t stepperOutputEnabled;
  uint8_t systemEnabled;
  uint16_t checksum;
};

struct __attribute__((packed)) DreamControlEspNowPacket {
  uint32_t magic;
  uint16_t version;
  uint8_t type;
  uint8_t flags;
  uint32_t seq;
  uint32_t pcTimeMs;
  uint32_t m5UptimeMs;
  uint8_t action;
  uint8_t reserved;
  uint16_t arg1;
  uint16_t arg2;
  uint16_t arg3;
  uint16_t arg4;
  uint16_t checksum;
};

DreamEegEspNowPacket latestEegPacket = {};
DreamMicStatusPacket latestMicStatus = {};

char serialLine[SERIAL_LINE_BUFFER_SIZE] = {};
uint8_t serialLineIndex = 0;
uint32_t serialFrameCount = 0;
uint32_t serialErrorCount = 0;
uint32_t espNowSendCount = 0;
uint32_t espNowSendFailCount = 0;
uint32_t controlRxCount = 0;
uint32_t controlSendCount = 0;
uint32_t controlSendFailCount = 0;
uint32_t localControlSeq = 0;
uint32_t espNowLastSendMs = 0;
uint32_t lastSerialFrameMs = 0;
uint32_t lastMicStatusMs = 0;
uint32_t lastDisplayRefreshMs = 0;
uint32_t lastStatusPrintMs = 0;
bool espNowReady = false;
bool haveEeg = false;
bool haveMicStatus = false;
esp_now_send_status_t lastSendStatus = ESP_NOW_SEND_FAIL;

uint16_t packetChecksum(const uint8_t *data, size_t length) {
  uint32_t sum = 0;
  for (size_t i = 0; i < length; i++) {
    sum += data[i];
  }
  return static_cast<uint16_t>(sum & 0xFFFF);
}

template <typename Packet>
uint16_t calculatePacketChecksum(const Packet &packet) {
  return packetChecksum(reinterpret_cast<const uint8_t *>(&packet), sizeof(Packet) - sizeof(packet.checksum));
}

const char *lightModeName(uint8_t value) {
  switch (value) {
    case LIGHT_OFF: return "OFF";
    case LIGHT_IDLE: return "IDLE";
    case LIGHT_EEG_BLEND: return "EEG";
    case LIGHT_SIGNAL_BAD: return "BAD";
    case LIGHT_TIMEOUT: return "TIMEOUT";
    case LIGHT_MANUAL: return "MANUAL";
    default: return "?";
  }
}

const char *stepperStateName(uint8_t value) {
  switch (value) {
    case STEPPER_DISABLED: return "DISABLED";
    case STEPPER_IDLE: return "IDLE";
    case STEPPER_MOVING: return "MOVING";
    case STEPPER_BREATHING: return "BREATH";
    case STEPPER_LIMITED: return "LIMIT";
    case STEPPER_FAULT: return "FAULT";
    default: return "?";
  }
}

const char *relayStateName(uint8_t value) {
  switch (value) {
    case RELAY_OFF: return "OFF";
    case RELAY_ARMING: return "ARMING";
    case RELAY_ON: return "ON";
    case RELAY_COOLDOWN: return "COOL";
    case RELAY_FAULT: return "FAULT";
    default: return "?";
  }
}

const char *safetyStateName(uint8_t value) {
  switch (value) {
    case SAFETY_NORMAL: return "NORMAL";
    case SAFETY_SIGNAL_BAD: return "SIGNAL";
    case SAFETY_LINK_TIMEOUT: return "TIMEOUT";
    case SAFETY_ESTOP: return "ESTOP";
    case SAFETY_FAULT: return "FAULT";
    default: return "?";
  }
}

uint16_t ageColor(uint32_t ageMs, uint32_t warnMs, uint32_t badMs) {
  if (ageMs < warnMs) {
    return 0x07E0;
  }
  if (ageMs < badMs) {
    return 0xFFE0;
  }
  return 0xF800;
}

uint16_t signalColor(uint8_t poorSignal) {
  if (poorSignal <= 50) {
    return 0x07E0;
  }
  if (poorSignal <= 120) {
    return 0xFFE0;
  }
  return 0xF800;
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

bool readCsvNumber(char *&cursor, uint32_t &value) {
  while (*cursor == ' ' || *cursor == '\t' || *cursor == ',') {
    cursor++;
  }

  if (*cursor < '0' || *cursor > '9') {
    return false;
  }

  char *endPtr = cursor;
  value = strtoul(cursor, &endPtr, 10);
  cursor = endPtr;
  return true;
}

bool readCsvToken(char *&cursor, char *token, size_t tokenSize) {
  while (*cursor == ' ' || *cursor == '\t' || *cursor == ',') {
    cursor++;
  }

  if (*cursor == '\0' || tokenSize == 0) {
    return false;
  }

  size_t index = 0;
  while (*cursor != '\0' && *cursor != ',') {
    if (index + 1 < tokenSize) {
      token[index++] = *cursor;
    }
    cursor++;
  }
  token[index] = '\0';
  return index > 0;
}

uint8_t controlActionFromName(const char *actionName) {
  if (strcmp(actionName, "SYSTEM_ENABLE") == 0) {
    return CONTROL_SYSTEM_ENABLE;
  }
  if (strcmp(actionName, "SYSTEM_DISABLE") == 0) {
    return CONTROL_SYSTEM_DISABLE;
  }
  if (strcmp(actionName, "LIGHT_AUTO") == 0) {
    return CONTROL_LIGHT_AUTO;
  }
  if (strcmp(actionName, "LIGHT_COLOR") == 0) {
    return CONTROL_LIGHT_COLOR;
  }
  if (strcmp(actionName, "LIGHT_OFF") == 0) {
    return CONTROL_LIGHT_OFF;
  }
  if (strcmp(actionName, "RELAY_ON") == 0) {
    return CONTROL_RELAY_ON;
  }
  if (strcmp(actionName, "RELAY_OFF") == 0) {
    return CONTROL_RELAY_OFF;
  }
  if (strcmp(actionName, "STEPPER_FORWARD") == 0) {
    return CONTROL_STEPPER_FORWARD;
  }
  if (strcmp(actionName, "STEPPER_BACKWARD") == 0) {
    return CONTROL_STEPPER_BACKWARD;
  }
  if (strcmp(actionName, "STEPPER_STOP") == 0) {
    return CONTROL_STEPPER_STOP;
  }
  if (strcmp(actionName, "ALL_STOP") == 0) {
    return CONTROL_ALL_STOP;
  }
  return CONTROL_NONE;
}

bool parseEegLine(char *line, DreamEegEspNowPacket &packet) {
  if (strncmp(line, "EEG,", 4) != 0) {
    return false;
  }

  char *cursor = line + 4;
  uint32_t values[13] = {};
  for (uint8_t i = 0; i < 13; i++) {
    if (!readCsvNumber(cursor, values[i])) {
      return false;
    }
  }

  memset(&packet, 0, sizeof(packet));
  packet.magic = DREAM_PACKET_MAGIC;
  packet.version = DREAM_PROTOCOL_VERSION;
  packet.type = DREAM_PACKET_EEG;
  packet.seq = values[0];
  packet.pcTimeMs = values[1];
  packet.m5UptimeMs = millis();
  packet.poorSignal = static_cast<uint8_t>(constrain(static_cast<int>(values[2]), 0, 255));
  packet.attention = static_cast<uint8_t>(constrain(static_cast<int>(values[3]), 0, 100));
  packet.meditation = static_cast<uint8_t>(constrain(static_cast<int>(values[4]), 0, 100));
  packet.flags = EEG_FLAG_CHECKSUM_OK;
  if (packet.poorSignal <= 120) {
    packet.flags |= EEG_FLAG_SIGNAL_OK;
  }

  for (uint8_t i = 0; i < 8; i++) {
    packet.eegPower[i] = values[5 + i];
  }

  packet.checksum = calculatePacketChecksum(packet);
  return true;
}

bool parseControlLine(char *line, DreamControlEspNowPacket &packet) {
  if (strncmp(line, "CMD,", 4) != 0) {
    return false;
  }

  char *cursor = line + 4;
  uint32_t seq = 0;
  uint32_t pcTimeMs = 0;
  char actionName[28] = {};
  uint32_t args[4] = {};

  if (!readCsvNumber(cursor, seq) ||
      !readCsvNumber(cursor, pcTimeMs) ||
      !readCsvToken(cursor, actionName, sizeof(actionName))) {
    return false;
  }

  const uint8_t action = controlActionFromName(actionName);
  if (action == CONTROL_NONE) {
    return false;
  }

  for (uint8_t i = 0; i < 4; i++) {
    if (!readCsvNumber(cursor, args[i])) {
      args[i] = 0;
    }
  }

  memset(&packet, 0, sizeof(packet));
  packet.magic = DREAM_PACKET_MAGIC;
  packet.version = DREAM_PROTOCOL_VERSION;
  packet.type = DREAM_PACKET_CONTROL;
  packet.seq = seq;
  packet.pcTimeMs = pcTimeMs;
  packet.m5UptimeMs = millis();
  packet.action = action;
  packet.arg1 = static_cast<uint16_t>(constrain(static_cast<int>(args[0]), 0, 65535));
  packet.arg2 = static_cast<uint16_t>(constrain(static_cast<int>(args[1]), 0, 65535));
  packet.arg3 = static_cast<uint16_t>(constrain(static_cast<int>(args[2]), 0, 65535));
  packet.arg4 = static_cast<uint16_t>(constrain(static_cast<int>(args[3]), 0, 65535));
  packet.checksum = calculatePacketChecksum(packet);
  return true;
}

void sendEegToMicroduino() {
  if (!espNowReady || !haveEeg) {
    return;
  }

  latestEegPacket.m5UptimeMs = millis();
  latestEegPacket.checksum = 0;
  latestEegPacket.checksum = calculatePacketChecksum(latestEegPacket);

  const esp_err_t result = esp_now_send(BROADCAST_MAC,
                                        reinterpret_cast<const uint8_t *>(&latestEegPacket),
                                        sizeof(latestEegPacket));
  if (result == ESP_OK) {
    espNowSendCount++;
    espNowLastSendMs = millis();
  } else {
    espNowSendFailCount++;
  }
}

void sendControlToMicroduino(DreamControlEspNowPacket &packet) {
  if (!espNowReady) {
    controlSendFailCount++;
    return;
  }

  packet.m5UptimeMs = millis();
  packet.checksum = 0;
  packet.checksum = calculatePacketChecksum(packet);

  const esp_err_t result = esp_now_send(BROADCAST_MAC,
                                        reinterpret_cast<const uint8_t *>(&packet),
                                        sizeof(packet));
  if (result == ESP_OK) {
    controlSendCount++;
    espNowSendCount++;
    espNowLastSendMs = millis();
    Serial.print("EVENT=CONTROL_TX SEQ=");
    Serial.print(packet.seq);
    Serial.print(" ACTION=");
    Serial.println(packet.action);
  } else {
    controlSendFailCount++;
    espNowSendFailCount++;
    Serial.print("EVENT=CONTROL_TX_FAIL SEQ=");
    Serial.print(packet.seq);
    Serial.print(" ERR=");
    Serial.println(result);
  }
}

void sendLocalControl(uint8_t action, uint16_t arg1 = 0, uint16_t arg2 = 0, uint16_t arg3 = 0, uint16_t arg4 = 0) {
  DreamControlEspNowPacket packet = {};
  packet.magic = DREAM_PACKET_MAGIC;
  packet.version = DREAM_PROTOCOL_VERSION;
  packet.type = DREAM_PACKET_CONTROL;
  packet.seq = localControlSeq++;
  packet.pcTimeMs = millis();
  packet.m5UptimeMs = millis();
  packet.action = action;
  packet.arg1 = arg1;
  packet.arg2 = arg2;
  packet.arg3 = arg3;
  packet.arg4 = arg4;
  packet.checksum = calculatePacketChecksum(packet);
  controlRxCount++;
  sendControlToMicroduino(packet);
}

void handleButtons() {
  if (M5.BtnA.wasPressed()) {
    sendLocalControl(CONTROL_SYSTEM_ENABLE);
    Serial.println("EVENT=LOCAL_CONTROL ACTION=SYSTEM_ENABLE SOURCE=BTN_A");
  }
  if (M5.BtnC.wasPressed()) {
    sendLocalControl(CONTROL_SYSTEM_DISABLE);
    Serial.println("EVENT=LOCAL_CONTROL ACTION=SYSTEM_DISABLE SOURCE=BTN_C");
  }
  if (M5.BtnB.wasPressed()) {
    sendLocalControl(CONTROL_ALL_STOP);
    Serial.println("EVENT=LOCAL_CONTROL ACTION=ALL_STOP SOURCE=BTN_B");
  }
}

void processSerialLine() {
  serialLine[serialLineIndex] = '\0';
  if (serialLineIndex == 0) {
    return;
  }

  DreamEegEspNowPacket packet = {};
  if (parseEegLine(serialLine, packet)) {
    latestEegPacket = packet;
    serialFrameCount++;
    lastSerialFrameMs = millis();
    haveEeg = true;

    Serial.print("EVENT=SERIAL_EEG_RX SEQ=");
    Serial.print(latestEegPacket.seq);
    Serial.print(" ATTENTION=");
    Serial.print(latestEegPacket.attention);
    Serial.print(" MEDITATION=");
    Serial.print(latestEegPacket.meditation);
    Serial.print(" POOR_SIGNAL=");
    Serial.println(latestEegPacket.poorSignal);

    sendEegToMicroduino();
    return;
  }

  DreamControlEspNowPacket controlPacket = {};
  if (parseControlLine(serialLine, controlPacket)) {
    controlRxCount++;
    Serial.print("EVENT=SERIAL_CMD_RX SEQ=");
    Serial.print(controlPacket.seq);
    Serial.print(" ACTION=");
    Serial.println(controlPacket.action);
    sendControlToMicroduino(controlPacket);
    return;
  }

  serialErrorCount++;
  Serial.print("EVENT=SERIAL_PARSE_FAIL TEXT=");
  Serial.println(serialLine);
}

void handleSerialInput() {
  while (Serial.available() > 0) {
    const char value = static_cast<char>(Serial.read());
    if (value == '\r') {
      continue;
    }

    if (value == '\n') {
      processSerialLine();
      serialLineIndex = 0;
      continue;
    }

    if (value < 0x20 || value > 0x7E) {
      continue;
    }

    if (serialLineIndex < SERIAL_LINE_BUFFER_SIZE - 1) {
      serialLine[serialLineIndex++] = value;
    } else {
      serialLineIndex = 0;
      serialErrorCount++;
      Serial.println("EVENT=SERIAL_LINE_TOO_LONG");
    }
  }
}

void onEspNowSent(const uint8_t *macAddress, esp_now_send_status_t status) {
  lastSendStatus = status;
  if (status != ESP_NOW_SEND_SUCCESS) {
    espNowSendFailCount++;
  }
}

void handleEspNowReceive(const uint8_t *macAddress, const uint8_t *data, int dataLength) {
  if (dataLength != static_cast<int>(sizeof(DreamMicStatusPacket))) {
    return;
  }

  DreamMicStatusPacket packet = {};
  memcpy(&packet, data, sizeof(packet));
  if (packet.magic != DREAM_PACKET_MAGIC ||
      packet.version != DREAM_PROTOCOL_VERSION ||
      packet.type != DREAM_PACKET_MIC_STATUS) {
    return;
  }

  const uint16_t expectedChecksum = calculatePacketChecksum(packet);
  if (packet.checksum != expectedChecksum) {
    return;
  }

  latestMicStatus = packet;
  haveMicStatus = true;
  lastMicStatusMs = millis();
}

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void onEspNowReceive(const esp_now_recv_info_t *recvInfo, const uint8_t *data, int dataLength) {
  const uint8_t *macAddress = recvInfo == nullptr ? nullptr : recvInfo->src_addr;
  handleEspNowReceive(macAddress, data, dataLength);
}
#else
void onEspNowReceive(const uint8_t *macAddress, const uint8_t *data, int dataLength) {
  handleEspNowReceive(macAddress, data, dataLength);
}
#endif

void initEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  Serial.print("EVENT=WIFI_STA_READY MAC=");
  Serial.println(WiFi.macAddress());
  Serial.print("EVENT=ESPNOW_CHANNEL VALUE=");
  Serial.println(ESPNOW_CHANNEL);

  const esp_err_t initResult = esp_now_init();
  if (initResult != ESP_OK) {
    Serial.print("EVENT=ESPNOW_INIT_FAIL ERR=");
    Serial.println(initResult);
    espNowReady = false;
    return;
  }

  esp_now_register_send_cb(onEspNowSent);
  esp_now_register_recv_cb(onEspNowReceive);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, BROADCAST_MAC, sizeof(BROADCAST_MAC));
  peerInfo.channel = ESPNOW_CHANNEL;
  peerInfo.ifidx = WIFI_IF_STA;
  peerInfo.encrypt = false;

  const esp_err_t peerResult = esp_now_add_peer(&peerInfo);
  if (peerResult != ESP_OK && peerResult != ESP_ERR_ESPNOW_EXIST) {
    Serial.print("EVENT=ESPNOW_ADD_PEER_FAIL ERR=");
    Serial.println(peerResult);
    espNowReady = false;
    return;
  }

  espNowReady = true;
  Serial.println("EVENT=ESPNOW_READY MODE=BROADCAST");
}

void drawStaticScreen() {
  M5.Lcd.fillScreen(0x0000);
  M5.Lcd.fillRect(0, 0, 320, 28, 0x18E3);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(0xFFFF, 0x18E3);
  M5.Lcd.setCursor(8, 6);
  M5.Lcd.print("Dream M5 Gateway");
}

template <typename Canvas>
void drawDashboardContent(Canvas &canvas, int16_t yShift) {
  const uint32_t now = millis();
  const uint32_t serialAgeMs = haveEeg ? now - lastSerialFrameMs : 0xFFFFFFFFUL;
  const uint32_t micAgeMs = haveMicStatus ? now - lastMicStatusMs : 0xFFFFFFFFUL;
  const uint8_t displayedPoorSignal = haveEeg ? latestEegPacket.poorSignal : 255;
  const uint32_t alphaPower = latestEegPacket.eegPower[2] + latestEegPacket.eegPower[3];
  const uint32_t betaPower = latestEegPacket.eegPower[4] + latestEegPacket.eegPower[5];
  const uint32_t gammaPower = latestEegPacket.eegPower[6] + latestEegPacket.eegPower[7];

  canvas.setTextSize(1);

  canvas.fillRect(0, 34 + yShift, 320, 14, 0x0000);
  canvas.setTextColor(ageColor(serialAgeMs, 500, SERIAL_TIMEOUT_MS), 0x0000);
  canvas.setCursor(8, 36 + yShift);
  canvas.printf("USB:%s age:%lums frames:%lu err:%lu",
                haveEeg && serialAgeMs < SERIAL_TIMEOUT_MS ? "OK" : "WAIT",
                haveEeg ? static_cast<unsigned long>(serialAgeMs) : 0,
                static_cast<unsigned long>(serialFrameCount),
                static_cast<unsigned long>(serialErrorCount));

  canvas.fillRect(0, 50 + yShift, 320, 14, 0x0000);
  canvas.setTextColor(lastSendStatus == ESP_NOW_SEND_SUCCESS ? 0x07E0 : 0xFFE0, 0x0000);
  canvas.setCursor(8, 52 + yShift);
  canvas.printf("ESP:%s tx:%lu fail:%lu ch:%u",
                espNowReady ? (lastSendStatus == ESP_NOW_SEND_SUCCESS ? "OK" : "SEND") : "OFF",
                static_cast<unsigned long>(espNowSendCount),
                static_cast<unsigned long>(espNowSendFailCount),
                ESPNOW_CHANNEL);

  canvas.fillRect(0, 72 + yShift, 320, 24, 0x0000);
  canvas.setTextColor(signalColor(displayedPoorSignal), 0x0000);
  canvas.setTextSize(2);
  canvas.setCursor(8, 76 + yShift);
  canvas.printf("Signal:%3u", displayedPoorSignal);

  canvas.fillRect(0, 100 + yShift, 320, 24, 0x0000);
  canvas.setTextColor(0xFFFF, 0x0000);
  canvas.setCursor(8, 104 + yShift);
  canvas.printf("Att:%3u  Med:%3u",
                haveEeg ? latestEegPacket.attention : 0,
                haveEeg ? latestEegPacket.meditation : 0);

  canvas.setTextSize(1);
  canvas.fillRect(0, 134 + yShift, 320, 14, 0x0000);
  canvas.setCursor(8, 136 + yShift);
  canvas.printf("seq:%lu pc:%lums",
                static_cast<unsigned long>(latestEegPacket.seq),
                static_cast<unsigned long>(latestEegPacket.pcTimeMs));
  canvas.fillRect(0, 150 + yShift, 320, 14, 0x0000);
  canvas.setCursor(8, 152 + yShift);
  canvas.printf("delta:%lu theta:%lu alpha:%lu",
                static_cast<unsigned long>(latestEegPacket.eegPower[0]),
                static_cast<unsigned long>(latestEegPacket.eegPower[1]),
                static_cast<unsigned long>(alphaPower));
  canvas.fillRect(0, 166 + yShift, 320, 14, 0x0000);
  canvas.setCursor(8, 168 + yShift);
  canvas.printf("beta:%lu gamma:%lu",
                static_cast<unsigned long>(betaPower),
                static_cast<unsigned long>(gammaPower));

  canvas.fillRect(0, 188 + yShift, 320, 14, 0x0000);
  canvas.setTextColor(ageColor(micAgeMs, 1000, MIC_STATUS_TIMEOUT_MS), 0x0000);
  canvas.setCursor(8, 190 + yShift);
  canvas.printf("MIC:%s age:%lums rx:%lu drop:%lu",
                haveMicStatus && micAgeMs < MIC_STATUS_TIMEOUT_MS ? "OK" : "WAIT",
                haveMicStatus ? static_cast<unsigned long>(micAgeMs) : 0,
                static_cast<unsigned long>(latestMicStatus.rxCount),
                static_cast<unsigned long>(latestMicStatus.dropCount));

  canvas.fillRect(0, 204 + yShift, 320, 14, 0x0000);
  canvas.setTextColor(0xFFFF, 0x0000);
  canvas.setCursor(8, 206 + yShift);
  if (haveMicStatus && micAgeMs < MIC_STATUS_TIMEOUT_MS) {
    canvas.printf("Light:%s %u  Step:%s",
                  lightModeName(latestMicStatus.lightMode),
                  latestMicStatus.lightLevel,
                  stepperStateName(latestMicStatus.stepperState));
  } else {
    canvas.print("Light:WAIT 0  Step:WAIT");
  }
  canvas.fillRect(0, 220 + yShift, 320, 14, 0x0000);
  canvas.setCursor(8, 222 + yShift);
  if (haveMicStatus && micAgeMs < MIC_STATUS_TIMEOUT_MS) {
    canvas.printf("Relay:%s  Safety:%s",
                  relayStateName(latestMicStatus.relayState),
                  safetyStateName(latestMicStatus.safetyState));
  } else {
    canvas.print("Relay:WAIT  Safety:WAIT");
  }

  canvas.setCursor(212, 222 + yShift);
  canvas.setTextColor(haveMicStatus && latestMicStatus.systemEnabled ? 0x07E0 : 0xF800, 0x0000);
  canvas.printf("SYS:%s", haveMicStatus && latestMicStatus.systemEnabled ? "ON" : "OFF");
}

void drawDashboard() {
  drawDashboardContent(M5.Lcd, 0);
}

void printStatus() {
  const uint32_t now = millis();
  Serial.print("EVENT=M5_STATUS SERIAL_FRAMES=");
  Serial.print(serialFrameCount);
  Serial.print(" SERIAL_ERRORS=");
  Serial.print(serialErrorCount);
  Serial.print(" ESPNOW_TX=");
  Serial.print(espNowSendCount);
  Serial.print(" ESPNOW_FAIL=");
  Serial.print(espNowSendFailCount);
  Serial.print(" CMD_RX=");
  Serial.print(controlRxCount);
  Serial.print(" CMD_TX=");
  Serial.print(controlSendCount);
  Serial.print(" CMD_FAIL=");
  Serial.print(controlSendFailCount);
  Serial.print(" MIC_STATUS=");
  Serial.print(haveMicStatus ? "YES" : "NO");
  Serial.print(" SERIAL_AGE_MS=");
  Serial.print(haveEeg ? now - lastSerialFrameMs : 0);
  Serial.print(" MIC_AGE_MS=");
  Serial.print(haveMicStatus ? now - lastMicStatusMs : 0);
  Serial.print(" EEG_SEQ=");
  Serial.print(haveEeg ? latestEegPacket.seq : 0);
  Serial.print(" POOR_SIGNAL=");
  Serial.print(haveEeg ? latestEegPacket.poorSignal : 255);
  Serial.print(" ATTENTION=");
  Serial.print(haveEeg ? latestEegPacket.attention : 0);
  Serial.print(" MEDITATION=");
  Serial.print(haveEeg ? latestEegPacket.meditation : 0);
  Serial.print(" MIC_RX=");
  Serial.print(latestMicStatus.rxCount);
  Serial.print(" MIC_DROP=");
  Serial.print(latestMicStatus.dropCount);
  Serial.print(" MIC_TIMEOUT=");
  Serial.print(latestMicStatus.timeoutCount);
  Serial.print(" MIC_LIGHT_MODE=");
  Serial.print(latestMicStatus.lightMode);
  Serial.print(" MIC_LIGHT_LEVEL=");
  Serial.print(latestMicStatus.lightLevel);
  Serial.print(" MIC_STEPPER=");
  Serial.print(latestMicStatus.stepperState);
  Serial.print(" MIC_RELAY=");
  Serial.print(latestMicStatus.relayState);
  Serial.print(" MIC_SAFETY=");
  Serial.print(latestMicStatus.safetyState);
  Serial.print(" MIC_CONTROL_RX=");
  Serial.print(latestMicStatus.controlRxCount);
  Serial.print(" MIC_LAST_ACTION=");
  Serial.print(latestMicStatus.lastControlAction);
  Serial.print(" MIC_MANUAL_LIGHT=");
  Serial.print(latestMicStatus.manualLightEnabled);
  Serial.print(" MIC_RELAY_ENABLED=");
  Serial.print(latestMicStatus.relayOutputEnabled);
  Serial.print(" MIC_STEPPER_ENABLED=");
  Serial.print(latestMicStatus.stepperOutputEnabled);
  Serial.print(" MIC_SYSTEM_ENABLED=");
  Serial.println(latestMicStatus.systemEnabled);
}

void setup() {
  M5.begin();
  M5.Power.begin();
  Serial.begin(SERIAL_BAUD);
  delay(300);

  M5.Lcd.setBrightness(160);
  drawStaticScreen();

  Serial.println();
  Serial.print("EVENT=DREAM_BOOT BOARD=");
  Serial.print(BOARD_NAME);
  Serial.print(" ROLE=");
  Serial.println(DEVICE_ROLE);
  Serial.println("EVENT=SERIAL_READY INPUT=EEG_CSV TARGET=M5STACK");

  initEspNow();
  drawDashboard();
}

void loop() {
  M5.update();
  handleButtons();
  handleSerialInput();

  const uint32_t now = millis();
  if (now - lastDisplayRefreshMs >= DISPLAY_REFRESH_MS) {
    lastDisplayRefreshMs = now;
    drawDashboard();
  }

  if (now - lastStatusPrintMs >= STATUS_PRINT_MS) {
    lastStatusPrintMs = now;
    printStatus();
  }
}
