#include <Arduino.h>
#include <esp_dmx.h>

#define BOARD_NAME "Microduino Core ESP32"
#define DMX_PORT DMX_NUM_1
#define DMX_TX_PIN 26
#define DMX_RX_PIN -1
#define DMX_EN_PIN -1
#define THINKGEAR_MAX_PAYLOAD 169
#define SERIAL_READ_BUDGET 192
#define DMX_REFRESH_MS 25
#define STATUS_PRINT_MS 1000
#define LINE_BUFFER_SIZE 160

byte dmxData[DMX_PACKET_SIZE];

struct ThinkGearData {
  bool seen;
  uint8_t poorSignal;
  uint8_t attention;
  uint8_t meditation;
  uint32_t eegPower[8];
  uint32_t packets;
  uint32_t checksumErrors;
  uint32_t lengthErrors;
  uint32_t csvPackets;
  uint32_t rawBytes;
  uint32_t lastPacketMs;
};

enum ThinkGearParserState : uint8_t {
  TG_WAIT_SYNC_1,
  TG_WAIT_SYNC_2,
  TG_READ_LENGTH,
  TG_READ_PAYLOAD,
  TG_READ_CHECKSUM,
};

ThinkGearData thinkGear = {};
ThinkGearParserState thinkGearParserState = TG_WAIT_SYNC_1;
uint8_t thinkGearPayload[THINKGEAR_MAX_PAYLOAD] = {};
uint8_t thinkGearPayloadLength = 0;
uint8_t thinkGearPayloadIndex = 0;
uint8_t thinkGearPayloadSum = 0;
char serialLine[LINE_BUFFER_SIZE] = {};
uint8_t serialLineIndex = 0;
unsigned long lastDmxRefresh = 0;
unsigned long lastStatusPrint = 0;

uint32_t readUint24BE(const uint8_t *bytes) {
  return (static_cast<uint32_t>(bytes[0]) << 16) |
         (static_cast<uint32_t>(bytes[1]) << 8) |
         static_cast<uint32_t>(bytes[2]);
}

uint8_t scalePercentToByte(uint8_t value) {
  return static_cast<uint8_t>(constrain(static_cast<int>(value), 0, 100) * 255 / 100);
}

void setLightColor(int startAddress, byte r, byte g, byte b, byte w) {
  dmxData[startAddress] = r;
  dmxData[startAddress + 1] = g;
  dmxData[startAddress + 2] = b;
  dmxData[startAddress + 3] = w;
}

void sendDMX() {
  dmx_write(DMX_PORT, dmxData, DMX_PACKET_SIZE);
  dmx_send(DMX_PORT);
  dmx_wait_sent(DMX_PORT, DMX_TIMEOUT_TICK);
}

void updateDmxFromThinkGear() {
  memset(dmxData, 0, DMX_PACKET_SIZE);

  if (!thinkGear.seen || millis() - thinkGear.lastPacketMs > 3000) {
    setLightColor(1, 0, 0, 0, 0);
    setLightColor(5, 0, 0, 0, 0);
    sendDMX();
    return;
  }

  const uint8_t attention = scalePercentToByte(thinkGear.attention);
  const uint8_t meditation = scalePercentToByte(thinkGear.meditation);
  const uint8_t signalOk = thinkGear.poorSignal == 0
                              ? 255
                              : static_cast<uint8_t>(255 - constrain(static_cast<int>(thinkGear.poorSignal), 0, 200) * 255 / 200);

  setLightColor(1, attention, meditation / 3, meditation, signalOk / 5);
  setLightColor(5, meditation / 2, attention / 4, attention, signalOk / 8);
  sendDMX();
}

void printThinkGearData(const char *source) {
  Serial.print("EVENT=EEG_RX SOURCE=");
  Serial.print(source);
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

void markThinkGearPacket(const char *source) {
  thinkGear.seen = true;
  thinkGear.packets++;
  thinkGear.lastPacketMs = millis();
  printThinkGearData(source);
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

  markThinkGearPacket("RAW_THINKGEAR");
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
        Serial.print("EVENT=EEG_CHECKSUM_FAIL EXPECTED=0x");
        if (expectedChecksum < 0x10) {
          Serial.print("0");
        }
        Serial.print(expectedChecksum, HEX);
        Serial.print(" GOT=0x");
        if (value < 0x10) {
          Serial.print("0");
        }
        Serial.println(value, HEX);
      }
      thinkGearParserState = TG_WAIT_SYNC_1;
      break;
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

bool parseEegCsvLine(char *line) {
  if (strncmp(line, "EEG,", 4) != 0 && strncmp(line, "THINKGEAR,", 10) != 0) {
    return false;
  }

  char *cursor = strchr(line, ',');
  if (cursor == nullptr) {
    return false;
  }
  cursor++;

  uint32_t values[11] = {};
  for (uint8_t i = 0; i < 11; i++) {
    if (!readCsvNumber(cursor, values[i])) {
      return false;
    }
  }

  thinkGear.poorSignal = static_cast<uint8_t>(constrain(static_cast<int>(values[0]), 0, 255));
  thinkGear.attention = static_cast<uint8_t>(constrain(static_cast<int>(values[1]), 0, 100));
  thinkGear.meditation = static_cast<uint8_t>(constrain(static_cast<int>(values[2]), 0, 100));
  for (uint8_t i = 0; i < 8; i++) {
    thinkGear.eegPower[i] = values[3 + i];
  }

  thinkGear.csvPackets++;
  markThinkGearPacket("CSV");
  return true;
}

void processLineByte(uint8_t value) {
  if (value == '\r') {
    return;
  }

  if (value == '\n') {
    serialLine[serialLineIndex] = '\0';
    if (serialLineIndex > 0 && !parseEegCsvLine(serialLine)) {
      Serial.print("EVENT=SERIAL_LINE_IGNORED TEXT=");
      Serial.println(serialLine);
    }
    serialLineIndex = 0;
    return;
  }

  if (value < 0x20 || value > 0x7E) {
    return;
  }

  if (serialLineIndex < LINE_BUFFER_SIZE - 1) {
    serialLine[serialLineIndex++] = static_cast<char>(value);
  } else {
    serialLineIndex = 0;
    Serial.println("EVENT=SERIAL_LINE_TOO_LONG");
  }
}

void handleSerialInput() {
  uint16_t budget = SERIAL_READ_BUDGET;
  while (Serial.available() > 0 && budget-- > 0) {
    const uint8_t value = static_cast<uint8_t>(Serial.read());
    thinkGear.rawBytes++;
    processThinkGearByte(value);
    processLineByte(value);
  }
}

void printStatus() {
  const uint32_t ageMs = thinkGear.seen ? millis() - thinkGear.lastPacketMs : 0;
  Serial.print("EVENT=EEG_STATUS SEEN=");
  Serial.print(thinkGear.seen ? "YES" : "NO");
  Serial.print(" PACKETS=");
  Serial.print(thinkGear.packets);
  Serial.print(" CSV=");
  Serial.print(thinkGear.csvPackets);
  Serial.print(" RAW_BYTES=");
  Serial.print(thinkGear.rawBytes);
  Serial.print(" CHECKSUM_ERRORS=");
  Serial.print(thinkGear.checksumErrors);
  Serial.print(" LENGTH_ERRORS=");
  Serial.print(thinkGear.lengthErrors);
  Serial.print(" AGE_MS=");
  Serial.println(ageMs);
}

void setup() {
  Serial.begin(115200);
  delay(300);

  thinkGear.poorSignal = 255;

  dmx_config_t config = DMX_CONFIG_DEFAULT;
  dmx_personality_t personalities[] = {
    {1, "EEG RGBW Lights"}
  };
  dmx_driver_install(DMX_PORT, &config, personalities, 1);
  dmx_set_pin(DMX_PORT, DMX_TX_PIN, DMX_RX_PIN, DMX_EN_PIN);
  memset(dmxData, 0, DMX_PACKET_SIZE);
  sendDMX();

  Serial.println();
  Serial.print("Starting EEG serial receiver for ");
  Serial.println(BOARD_NAME);
  Serial.println("PC -> Microduino USB Serial: 115200 baud");
  Serial.println("Accepted input: raw ThinkGear packets or CSV lines");
  Serial.println("CSV format: EEG,poor,attention,meditation,delta,theta,lowAlpha,highAlpha,lowBeta,highBeta,lowGamma,midGamma");
  Serial.println("DMX output: TX=GPIO26, light addresses 001 and 005");
}

void loop() {
  handleSerialInput();

  const unsigned long now = millis();
  if (now - lastDmxRefresh >= DMX_REFRESH_MS) {
    lastDmxRefresh = now;
    updateDmxFromThinkGear();
  }

  if (now - lastStatusPrint >= STATUS_PRINT_MS) {
    lastStatusPrint = now;
    printStatus();
  }
}
