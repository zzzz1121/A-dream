#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_dmx.h>
#include <esp_arduino_version.h>

#define BOARD_NAME "Microduino Core ESP32"
#define DEVICE_ROLE "micController"
#define MIC_CONTROLLER_VERSION "0.1.0"

#define SERIAL_BAUD 115200
#define ESPNOW_CHANNEL 1
#define DREAM_PACKET_MAGIC 0x44524541UL
#define DREAM_PROTOCOL_VERSION 1

#define EEG_TIMEOUT_MS 3000
#define POOR_SIGNAL_BAD_THRESHOLD 120
#define DMX_REFRESH_MS 50
#define STATUS_SEND_MS 500
#define STATUS_PRINT_MS 1000

#define DMX_PORT DMX_NUM_1
#define DMX_TX_PIN 5
#define DMX_RX_PIN -1
#define DMX_EN_PIN -1
#define DMX_LIGHT_1_ADDRESS 1
#define DMX_LIGHT_2_ADDRESS 5
#define DREAM_DMX_BOOT_SELF_TEST 1
#define LIGHT_FLOW_STEP_MS 35
#define LIGHT_FLOW_OFFSET 96

// Stepper output is enabled for bench tuning. Keep relay output disabled until safety hardware is confirmed.
#define DREAM_ENABLE_STEPPER_OUTPUT 1
#define DREAM_ENABLE_RELAY_OUTPUT 0

#if DREAM_ENABLE_STEPPER_OUTPUT
#define STEPPER_LEFT_STEP_PIN 27
#define STEPPER_LEFT_DIR_PIN 26
#define STEPPER_RIGHT_STEP_PIN 25
#define STEPPER_RIGHT_DIR_PIN 14
#define STEPPER_RIGHT_DIR_INVERT 0
#define STEPPER_TARGET_LEFT 0x01
#define STEPPER_TARGET_RIGHT 0x02
#define STEPPER_TARGET_BOTH (STEPPER_TARGET_LEFT | STEPPER_TARGET_RIGHT)
#define STEPPER_ENABLE_PIN -1
#define STEPPER_ENABLE_ACTIVE_LEVEL LOW
#define STEPPER_STEPS_PER_REV 1600
#define STEPPER_PULSE_HALF_PERIOD_US 1000
#define STEPPER_MIN_POSITION_STEPS -320000
#define STEPPER_MAX_POSITION_STEPS 320000
#define STEPPER_MAX_COMMAND_STEPS 64000
#define STEPPER_ATTENTION_THRESHOLD 70
#define STEPPER_MEDITATION_THRESHOLD 65
#endif

#if DREAM_ENABLE_RELAY_OUTPUT
#define RELAY_OUTPUT_PIN 33
#define RELAY_ACTIVE_LEVEL HIGH
#define RELAY_ARM_MS 3000
#define RELAY_ON_MS 2000
#define RELAY_MANUAL_MAX_ON_MS 5000
#define RELAY_COOLDOWN_MS 10000
#endif

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
  uint8_t light1R;
  uint8_t light1G;
  uint8_t light1B;
  uint8_t light1W;
  uint8_t light2R;
  uint8_t light2G;
  uint8_t light2B;
  uint8_t light2W;
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

struct RgbwColor {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t w;
};

DreamEegEspNowPacket latestEegPacket = {};
byte dmxData[DMX_PACKET_SIZE] = {};

RgbwColor light1Current = {};
RgbwColor light2Current = {};
RgbwColor light1Target = {};
RgbwColor light2Target = {};

uint32_t eegRxCount = 0;
uint32_t eegDropCount = 0;
uint32_t eegChecksumErrorCount = 0;
uint32_t eegTimeoutCount = 0;
uint32_t controlRxCount = 0;
uint32_t controlChecksumErrorCount = 0;
uint32_t statusSeq = 0;
uint32_t lastEegFrameMs = 0;
uint32_t lastDmxRefreshMs = 0;
uint32_t lastStatusSendMs = 0;
uint32_t lastStatusPrintMs = 0;
uint32_t dmxFrameCount = 0;
uint32_t dmxSendFailCount = 0;
uint32_t dmxWaitFailCount = 0;
size_t lastDmxWriteSize = 0;
size_t lastDmxSendSize = 0;
uint8_t lightMode = LIGHT_OFF;
uint8_t lightLevel = 0;
uint8_t stepperState = STEPPER_DISABLED;
uint8_t relayState = RELAY_OFF;
uint8_t safetyState = SAFETY_LINK_TIMEOUT;
uint8_t lastControlAction = CONTROL_NONE;
uint8_t lastAppliedControlActionForDedup = CONTROL_NONE;
uint32_t lastAppliedControlSeq = 0;
uint32_t lastAppliedControlPcTimeMs = 0;
bool espNowReady = false;
bool haveEeg = false;
bool haveAppliedControlPacket = false;
bool timeoutWasActive = false;
bool systemEnabled = false;
bool manualLightEnabled = false;
RgbwColor manualLightColor = {};

#if DREAM_ENABLE_RELAY_OUTPUT
uint32_t relayStateStartMs = 0;
uint32_t relayArmStartMs = 0;
bool manualRelayRequested = false;
#endif

#if DREAM_ENABLE_STEPPER_OUTPUT
int32_t stepperLeftPositionSteps = 0;
int32_t stepperRightPositionSteps = 0;
int32_t stepperLeftTargetSteps = 0;
int32_t stepperRightTargetSteps = 0;
int32_t manualStepperLeftTargetSteps = 0;
int32_t manualStepperRightTargetSteps = 0;
uint32_t lastLeftStepperPulseUs = 0;
uint32_t lastRightStepperPulseUs = 0;
bool leftStepperPulseActive = false;
bool rightStepperPulseActive = false;
bool stepperBreathForward = true;
bool manualStepperActive = false;
uint8_t manualStepperMask = 0;
#endif

uint16_t packetChecksum(const uint8_t *data, size_t length) {
  uint32_t sum = 0;
  for (size_t i = 0; i < length; i++) {
    sum += data[i];
  }
  return static_cast<uint16_t>(sum & 0xFFFF);
}

template <typename Packet>
uint16_t calculatePacketChecksum(const Packet &packet) {
  return packetChecksum(reinterpret_cast<const uint8_t *>(&packet), sizeof(Packet) - sizeof(uint16_t));
}

uint8_t scalePercentToByte(uint8_t value) {
  return static_cast<uint8_t>(constrain(static_cast<int>(value), 0, 100) * 255 / 100);
}

uint8_t max4(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  return max(max(a, b), max(c, d));
}

uint8_t smoothChannel(uint8_t current, uint8_t target) {
  return static_cast<uint8_t>((static_cast<uint16_t>(current) * 85 + static_cast<uint16_t>(target) * 15 + 50) / 100);
}

RgbwColor smoothColor(const RgbwColor &current, const RgbwColor &target) {
  return {
    smoothChannel(current.r, target.r),
    smoothChannel(current.g, target.g),
    smoothChannel(current.b, target.b),
    smoothChannel(current.w, target.w),
  };
}

bool isColorOff(const RgbwColor &color) {
  return color.r == 0 && color.g == 0 && color.b == 0 && color.w == 0;
}

RgbwColor scaleRgbColor(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness) {
  return {
    static_cast<uint8_t>(static_cast<uint16_t>(r) * brightness / 255),
    static_cast<uint8_t>(static_cast<uint16_t>(g) * brightness / 255),
    static_cast<uint8_t>(static_cast<uint16_t>(b) * brightness / 255),
    0,
  };
}

RgbwColor colorWheel(uint8_t position, uint8_t brightness) {
  position = 255 - position;
  if (position < 85) {
    return scaleRgbColor(255 - position * 3, 0, position * 3, brightness);
  }
  if (position < 170) {
    position -= 85;
    return scaleRgbColor(0, position * 3, 255 - position * 3, brightness);
  }
  position -= 170;
  return scaleRgbColor(position * 3, 255 - position * 3, 0, brightness);
}

void setFlowTargets(uint8_t brightness, uint8_t phaseOffset = 0) {
  const uint8_t phase = static_cast<uint8_t>((millis() / LIGHT_FLOW_STEP_MS + phaseOffset) & 0xFF);
  light1Target = colorWheel(phase, brightness);
  light2Target = colorWheel(static_cast<uint8_t>(phase + LIGHT_FLOW_OFFSET), brightness);
}

uint8_t triangleWave(uint8_t phase) {
  return phase < 128 ? static_cast<uint8_t>(phase * 2) : static_cast<uint8_t>((255 - phase) * 2);
}

uint8_t mixChannel(uint8_t a, uint8_t b, uint8_t amount) {
  return static_cast<uint8_t>((static_cast<uint16_t>(a) * (255 - amount) + static_cast<uint16_t>(b) * amount + 127) / 255);
}

RgbwColor blendColor(const RgbwColor &a, const RgbwColor &b, uint8_t amount) {
  return {
    mixChannel(a.r, b.r, amount),
    mixChannel(a.g, b.g, amount),
    mixChannel(a.b, b.b, amount),
    mixChannel(a.w, b.w, amount),
  };
}

RgbwColor scaleColor(const RgbwColor &color, uint8_t brightness) {
  return {
    static_cast<uint8_t>(static_cast<uint16_t>(color.r) * brightness / 255),
    static_cast<uint8_t>(static_cast<uint16_t>(color.g) * brightness / 255),
    static_cast<uint8_t>(static_cast<uint16_t>(color.b) * brightness / 255),
    static_cast<uint8_t>(static_cast<uint16_t>(color.w) * brightness / 255),
  };
}

uint8_t manualFlowBrightness() {
  const uint8_t level = max4(manualLightColor.r, manualLightColor.g, manualLightColor.b, manualLightColor.w);
  return static_cast<uint8_t>(constrain(static_cast<int>(level), 32, 255));
}

RgbwColor manualAccentColor() {
  const uint8_t r = manualLightColor.r;
  const uint8_t g = manualLightColor.g;
  const uint8_t b = manualLightColor.b;

  if (r > 120 && b > 100 && g + 40 < max(r, b)) {
    return {
      static_cast<uint8_t>(constrain((static_cast<int>(r) + static_cast<int>(b)) / 2, 0, 255)),
      static_cast<uint8_t>(g / 3),
      255,
      manualLightColor.w,
    };
  }

  if (g > 100 && b > 100 && r < 120) {
    return {
      static_cast<uint8_t>(r / 3),
      static_cast<uint8_t>(constrain(static_cast<int>(g) + static_cast<int>(b) / 4, 0, 255)),
      static_cast<uint8_t>(constrain(static_cast<int>(b) + 32, 0, 255)),
      manualLightColor.w,
    };
  }

  if (b > r && b > g) {
    return {
      static_cast<uint8_t>(constrain(static_cast<int>(r) / 2 + static_cast<int>(b) / 4, 0, 255)),
      static_cast<uint8_t>(g / 2),
      255,
      manualLightColor.w,
    };
  }

  if (r > 150 && g > 80) {
    return {
      255,
      static_cast<uint8_t>(g / 2),
      static_cast<uint8_t>(constrain(static_cast<int>(b) + 96, 0, 255)),
      manualLightColor.w,
    };
  }

  return {
    static_cast<uint8_t>(b / 2),
    static_cast<uint8_t>(constrain(static_cast<int>(r) / 3 + static_cast<int>(g) / 2, 0, 255)),
    static_cast<uint8_t>(max(r, b)),
    manualLightColor.w,
  };
}

void setManualGradientTargets() {
  const uint8_t brightness = manualFlowBrightness();
  const uint8_t phase = static_cast<uint8_t>((millis() / LIGHT_FLOW_STEP_MS) & 0xFF);
  const RgbwColor base = scaleColor({manualLightColor.r, manualLightColor.g, manualLightColor.b, 0}, brightness);
  const RgbwColor accent = scaleColor(manualAccentColor(), brightness);
  const uint8_t glowBrightness = static_cast<uint8_t>(max(static_cast<int>(brightness) / 2, 32));
  const RgbwColor glow = scaleColor({
    static_cast<uint8_t>(constrain(static_cast<int>(manualLightColor.r) + 42, 0, 255)),
    static_cast<uint8_t>(constrain(static_cast<int>(manualLightColor.g) + 42, 0, 255)),
    static_cast<uint8_t>(constrain(static_cast<int>(manualLightColor.b) + 42, 0, 255)),
    manualLightColor.w,
  }, glowBrightness);

  light1Target = blendColor(base, accent, triangleWave(phase));
  light2Target = blendColor(accent, glow, triangleWave(static_cast<uint8_t>(phase + LIGHT_FLOW_OFFSET)));
}

void setLightColor(int startAddress, const RgbwColor &color) {
  dmxData[startAddress] = color.r;
  dmxData[startAddress + 1] = color.g;
  dmxData[startAddress + 2] = color.b;
  dmxData[startAddress + 3] = color.w;
}

void sendDmxFrame() {
  memset(dmxData, 0, DMX_PACKET_SIZE);
  setLightColor(DMX_LIGHT_1_ADDRESS, light1Current);
  setLightColor(DMX_LIGHT_2_ADDRESS, light2Current);
  lastDmxWriteSize = dmx_write(DMX_PORT, dmxData, DMX_PACKET_SIZE);
  lastDmxSendSize = dmx_send(DMX_PORT);
  dmxFrameCount++;
  if (lastDmxWriteSize != DMX_PACKET_SIZE || lastDmxSendSize == 0) {
    dmxSendFailCount++;
  }
  if (!dmx_wait_sent(DMX_PORT, DMX_TIMEOUT_TICK)) {
    dmxWaitFailCount++;
  }
}

void setTwoLightsImmediate(const RgbwColor &light1, const RgbwColor &light2) {
  light1Current = light1;
  light2Current = light2;
  lightLevel = max(max4(light1Current.r, light1Current.g, light1Current.b, light1Current.w),
                   max4(light2Current.r, light2Current.g, light2Current.b, light2Current.w));
  sendDmxFrame();
}

void holdTwoLightsImmediate(const RgbwColor &light1, const RgbwColor &light2, uint16_t durationMs) {
  const uint32_t startMs = millis();
  while (millis() - startMs < durationMs) {
    setTwoLightsImmediate(light1, light2);
    delay(25);
  }
}

void runDmxBootSelfTest() {
#if DREAM_DMX_BOOT_SELF_TEST
  Serial.println("EVENT=DMX_SELF_TEST STEP=RED_BLUE");
  holdTwoLightsImmediate({255, 0, 0, 0}, {0, 0, 255, 0}, 1200);

  Serial.println("EVENT=DMX_SELF_TEST STEP=GREEN_PURPLE");
  holdTwoLightsImmediate({0, 255, 0, 0}, {120, 0, 255, 0}, 1200);

  Serial.println("EVENT=DMX_SELF_TEST STEP=WHITE_RED");
  holdTwoLightsImmediate({0, 0, 0, 255}, {255, 0, 0, 0}, 1200);

  Serial.println("EVENT=DMX_SELF_TEST STEP=OFF");
  holdTwoLightsImmediate({0, 0, 0, 0}, {0, 0, 0, 0}, 500);
  Serial.println("EVENT=DMX_SELF_TEST DONE");
#endif
}

void printMacAddress(const uint8_t *macAddress) {
  for (uint8_t i = 0; i < 6; i++) {
    if (macAddress[i] < 0x10) {
      Serial.print("0");
    }
    Serial.print(macAddress[i], HEX);
    if (i < 5) {
      Serial.print(":");
    }
  }
}

bool validateEegPacket(DreamEegEspNowPacket &packet) {
  if (packet.magic != DREAM_PACKET_MAGIC ||
      packet.version != DREAM_PROTOCOL_VERSION ||
      packet.type != DREAM_PACKET_EEG) {
    return false;
  }

  const uint16_t expectedChecksum = calculatePacketChecksum(packet);
  if (packet.checksum != expectedChecksum) {
    eegChecksumErrorCount++;
    return false;
  }

  packet.poorSignal = static_cast<uint8_t>(constrain(static_cast<int>(packet.poorSignal), 0, 255));
  packet.attention = static_cast<uint8_t>(constrain(static_cast<int>(packet.attention), 0, 100));
  packet.meditation = static_cast<uint8_t>(constrain(static_cast<int>(packet.meditation), 0, 100));
  return true;
}

bool validateControlPacket(DreamControlEspNowPacket &packet) {
  if (packet.magic != DREAM_PACKET_MAGIC ||
      packet.version != DREAM_PROTOCOL_VERSION ||
      packet.type != DREAM_PACKET_CONTROL) {
    return false;
  }

  const uint16_t expectedChecksum = calculatePacketChecksum(packet);
  if (packet.checksum != expectedChecksum) {
    controlChecksumErrorCount++;
    return false;
  }

  return packet.action > CONTROL_NONE && packet.action <= CONTROL_ALL_STOP;
}

void updateSequenceCounters(uint32_t incomingSeq) {
  if (!haveEeg) {
    return;
  }

  const uint32_t lastSeq = latestEegPacket.seq;
  if (incomingSeq > lastSeq + 1) {
    eegDropCount += incomingSeq - lastSeq - 1;
  } else if (incomingSeq <= lastSeq) {
    eegDropCount++;
  }
}

void handleEegPacket(const DreamEegEspNowPacket &packet, const uint8_t *macAddress) {
  updateSequenceCounters(packet.seq);
  latestEegPacket = packet;
  lastEegFrameMs = millis();
  eegRxCount++;
  haveEeg = true;

  Serial.print("EVENT=ESPNOW_EEG_RX SEQ=");
  Serial.print(latestEegPacket.seq);
  Serial.print(" POOR_SIGNAL=");
  Serial.print(latestEegPacket.poorSignal);
  Serial.print(" ATTENTION=");
  Serial.print(latestEegPacket.attention);
  Serial.print(" MEDITATION=");
  Serial.print(latestEegPacket.meditation);
  Serial.print(" SRC=");
  if (macAddress != nullptr) {
    printMacAddress(macAddress);
  } else {
    Serial.print("UNKNOWN");
  }
  Serial.println();
}

uint8_t clampByteArg(uint16_t value) {
  return static_cast<uint8_t>(constrain(static_cast<int>(value), 0, 255));
}

#if DREAM_ENABLE_STEPPER_OUTPUT
uint16_t requestedStepperSteps(uint16_t value) {
  const uint16_t requested = value == 0 ? STEPPER_STEPS_PER_REV : value;
  return static_cast<uint16_t>(constrain(static_cast<int>(requested), 1, STEPPER_MAX_COMMAND_STEPS));
}

uint8_t requestedStepperTargetMask(uint16_t value) {
  const uint8_t mask = static_cast<uint8_t>(value) & STEPPER_TARGET_BOTH;
  return mask == 0 ? STEPPER_TARGET_BOTH : mask;
}

int32_t clampStepperTarget(int32_t target) {
  return constrain(target, static_cast<int32_t>(STEPPER_MIN_POSITION_STEPS), static_cast<int32_t>(STEPPER_MAX_POSITION_STEPS));
}
#endif

bool isDuplicateControlPacket(const DreamControlEspNowPacket &packet) {
  return haveAppliedControlPacket &&
         packet.seq == lastAppliedControlSeq &&
         packet.pcTimeMs == lastAppliedControlPcTimeMs &&
         packet.action == lastAppliedControlActionForDedup;
}

void rememberControlPacket(const DreamControlEspNowPacket &packet) {
  haveAppliedControlPacket = true;
  lastAppliedControlSeq = packet.seq;
  lastAppliedControlPcTimeMs = packet.pcTimeMs;
  lastAppliedControlActionForDedup = packet.action;
}

void applyControlPacket(const DreamControlEspNowPacket &packet, const uint8_t *macAddress) {
  if (isDuplicateControlPacket(packet)) {
    Serial.print("EVENT=CONTROL_DUPLICATE SEQ=");
    Serial.print(packet.seq);
    Serial.print(" ACTION=");
    Serial.println(packet.action);
    return;
  }

  rememberControlPacket(packet);
  controlRxCount++;
  lastControlAction = packet.action;

  Serial.print("EVENT=CONTROL_RX SEQ=");
  Serial.print(packet.seq);
  Serial.print(" ACTION=");
  Serial.print(packet.action);
  Serial.print(" ARG1=");
  Serial.print(packet.arg1);
  Serial.print(" ARG2=");
  Serial.print(packet.arg2);
  Serial.print(" ARG3=");
  Serial.print(packet.arg3);
  Serial.print(" ARG4=");
  Serial.print(packet.arg4);
  Serial.print(" SRC=");
  if (macAddress != nullptr) {
    printMacAddress(macAddress);
  } else {
    Serial.print("UNKNOWN");
  }
  Serial.println();

  switch (packet.action) {
    case CONTROL_SYSTEM_ENABLE:
      systemEnabled = true;
      break;
    case CONTROL_SYSTEM_DISABLE:
      systemEnabled = false;
      manualLightEnabled = false;
      manualLightColor = {0, 0, 0, 0};
#if DREAM_ENABLE_RELAY_OUTPUT
      manualRelayRequested = false;
#endif
#if DREAM_ENABLE_STEPPER_OUTPUT
      manualStepperActive = false;
      manualStepperMask = 0;
      stepperLeftTargetSteps = stepperLeftPositionSteps;
      stepperRightTargetSteps = stepperRightPositionSteps;
      leftStepperPulseActive = false;
      rightStepperPulseActive = false;
      digitalWrite(STEPPER_LEFT_STEP_PIN, HIGH);
      digitalWrite(STEPPER_RIGHT_STEP_PIN, HIGH);
#endif
      break;
    case CONTROL_LIGHT_AUTO:
      manualLightEnabled = false;
      break;
    case CONTROL_LIGHT_COLOR:
      if (!systemEnabled) {
        Serial.println("EVENT=CONTROL_IGNORED SYSTEM=OFF ACTION=LIGHT_COLOR");
        break;
      }
      manualLightEnabled = true;
      manualLightColor = {
        clampByteArg(packet.arg1),
        clampByteArg(packet.arg2),
        clampByteArg(packet.arg3),
        clampByteArg(packet.arg4),
      };
      break;
    case CONTROL_LIGHT_OFF:
      manualLightEnabled = true;
      manualLightColor = {0, 0, 0, 0};
      break;
    case CONTROL_RELAY_ON:
      if (!systemEnabled) {
        Serial.println("EVENT=CONTROL_IGNORED SYSTEM=OFF ACTION=RELAY_ON");
        break;
      }
#if DREAM_ENABLE_RELAY_OUTPUT
      manualRelayRequested = true;
#endif
      break;
    case CONTROL_RELAY_OFF:
#if DREAM_ENABLE_RELAY_OUTPUT
      manualRelayRequested = false;
#endif
      break;
    case CONTROL_STEPPER_FORWARD:
#if DREAM_ENABLE_STEPPER_OUTPUT
    {
      if (!systemEnabled) {
        Serial.println("EVENT=CONTROL_IGNORED SYSTEM=OFF ACTION=STEPPER_FORWARD");
        break;
      }
      const uint8_t targetMask = requestedStepperTargetMask(packet.arg2);
      const uint8_t startMask = targetMask & ~manualStepperMask;
      const int32_t steps = static_cast<int32_t>(requestedStepperSteps(packet.arg1));
      if (startMask != targetMask) {
        Serial.print("EVENT=STEPPER_BUSY_IGNORED ACTION=FORWARD MASK=");
        Serial.println(targetMask & manualStepperMask);
      }
      if (startMask == 0) {
        break;
      }
      manualStepperActive = true;
      manualStepperMask |= startMask;
      if (startMask & STEPPER_TARGET_LEFT) {
        manualStepperLeftTargetSteps = clampStepperTarget(stepperLeftPositionSteps + steps);
      }
      if (startMask & STEPPER_TARGET_RIGHT) {
        manualStepperRightTargetSteps = clampStepperTarget(stepperRightPositionSteps + steps);
      }
    }
#endif
      break;
    case CONTROL_STEPPER_BACKWARD:
#if DREAM_ENABLE_STEPPER_OUTPUT
    {
      if (!systemEnabled) {
        Serial.println("EVENT=CONTROL_IGNORED SYSTEM=OFF ACTION=STEPPER_BACKWARD");
        break;
      }
      const uint8_t targetMask = requestedStepperTargetMask(packet.arg2);
      const uint8_t startMask = targetMask & ~manualStepperMask;
      const int32_t steps = static_cast<int32_t>(requestedStepperSteps(packet.arg1));
      if (startMask != targetMask) {
        Serial.print("EVENT=STEPPER_BUSY_IGNORED ACTION=BACKWARD MASK=");
        Serial.println(targetMask & manualStepperMask);
      }
      if (startMask == 0) {
        break;
      }
      manualStepperActive = true;
      manualStepperMask |= startMask;
      if (startMask & STEPPER_TARGET_LEFT) {
        manualStepperLeftTargetSteps = clampStepperTarget(stepperLeftPositionSteps - steps);
      }
      if (startMask & STEPPER_TARGET_RIGHT) {
        manualStepperRightTargetSteps = clampStepperTarget(stepperRightPositionSteps - steps);
      }
    }
#endif
      break;
    case CONTROL_STEPPER_STOP:
#if DREAM_ENABLE_STEPPER_OUTPUT
    {
      const uint8_t targetMask = requestedStepperTargetMask(packet.arg2);
      const uint8_t stopMask = targetMask & ~manualStepperMask;
      if (stopMask != targetMask) {
        Serial.print("EVENT=STEPPER_BUSY_IGNORED ACTION=STOP MASK=");
        Serial.println(targetMask & manualStepperMask);
      }
      if (stopMask & STEPPER_TARGET_LEFT) {
        manualStepperMask &= ~STEPPER_TARGET_LEFT;
        stepperLeftTargetSteps = stepperLeftPositionSteps;
        manualStepperLeftTargetSteps = stepperLeftPositionSteps;
        leftStepperPulseActive = false;
        digitalWrite(STEPPER_LEFT_STEP_PIN, HIGH);
      }
      if (stopMask & STEPPER_TARGET_RIGHT) {
        manualStepperMask &= ~STEPPER_TARGET_RIGHT;
        stepperRightTargetSteps = stepperRightPositionSteps;
        manualStepperRightTargetSteps = stepperRightPositionSteps;
        rightStepperPulseActive = false;
        digitalWrite(STEPPER_RIGHT_STEP_PIN, HIGH);
      }
      manualStepperActive = manualStepperMask != 0;
    }
#endif
      break;
    case CONTROL_ALL_STOP:
      systemEnabled = false;
      manualLightEnabled = false;
      manualLightColor = {0, 0, 0, 0};
#if DREAM_ENABLE_RELAY_OUTPUT
      manualRelayRequested = false;
#endif
#if DREAM_ENABLE_STEPPER_OUTPUT
      manualStepperActive = false;
      manualStepperMask = 0;
      stepperLeftTargetSteps = stepperLeftPositionSteps;
      stepperRightTargetSteps = stepperRightPositionSteps;
      leftStepperPulseActive = false;
      rightStepperPulseActive = false;
      digitalWrite(STEPPER_LEFT_STEP_PIN, HIGH);
      digitalWrite(STEPPER_RIGHT_STEP_PIN, HIGH);
#endif
      break;
    default:
      break;
  }
}

void handleEspNowReceive(const uint8_t *macAddress, const uint8_t *data, int dataLength) {
  if (dataLength == static_cast<int>(sizeof(DreamEegEspNowPacket))) {
    DreamEegEspNowPacket packet = {};
    memcpy(&packet, data, sizeof(packet));
    if (!validateEegPacket(packet)) {
      eegDropCount++;
      return;
    }

    handleEegPacket(packet, macAddress);
    return;
  }

  if (dataLength == static_cast<int>(sizeof(DreamControlEspNowPacket))) {
    DreamControlEspNowPacket packet = {};
    memcpy(&packet, data, sizeof(packet));
    if (validateControlPacket(packet)) {
      applyControlPacket(packet, macAddress);
    }
    return;
  }
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

void initDmx() {
  dmx_config_t config = DMX_CONFIG_DEFAULT;
  dmx_personality_t personalities[] = {
    {1, "Two RGBW Lights"}
  };

  const bool driverInstalled = dmx_driver_install(DMX_PORT, &config, personalities, 1);
  const bool pinConfigured = dmx_set_pin(DMX_PORT, DMX_TX_PIN, DMX_RX_PIN, DMX_EN_PIN);
  sendDmxFrame();

  Serial.print("EVENT=DMX_READY TX_PIN=");
  Serial.print(DMX_TX_PIN);
  Serial.print(" LIGHT1=");
  Serial.print(DMX_LIGHT_1_ADDRESS);
  Serial.print(" LIGHT2=");
  Serial.print(DMX_LIGHT_2_ADDRESS);
  Serial.print(" DRIVER=");
  Serial.print(driverInstalled ? "OK" : "FAIL");
  Serial.print(" PIN=");
  Serial.println(pinConfigured ? "OK" : "FAIL");
  runDmxBootSelfTest();
}

void initStepper() {
#if DREAM_ENABLE_STEPPER_OUTPUT
  pinMode(STEPPER_LEFT_STEP_PIN, OUTPUT);
  pinMode(STEPPER_LEFT_DIR_PIN, OUTPUT);
  pinMode(STEPPER_RIGHT_STEP_PIN, OUTPUT);
  pinMode(STEPPER_RIGHT_DIR_PIN, OUTPUT);
  digitalWrite(STEPPER_LEFT_STEP_PIN, HIGH);
  digitalWrite(STEPPER_RIGHT_STEP_PIN, HIGH);
  digitalWrite(STEPPER_LEFT_DIR_PIN, HIGH);
  digitalWrite(STEPPER_RIGHT_DIR_PIN, STEPPER_RIGHT_DIR_INVERT ? LOW : HIGH);
  if (STEPPER_ENABLE_PIN >= 0) {
    pinMode(STEPPER_ENABLE_PIN, OUTPUT);
    digitalWrite(STEPPER_ENABLE_PIN, STEPPER_ENABLE_ACTIVE_LEVEL);
  }
  stepperState = STEPPER_IDLE;
  Serial.print("EVENT=STEPPER_READY MODE=OUTPUT_ENABLED LEFT_STEP=");
  Serial.print(STEPPER_LEFT_STEP_PIN);
  Serial.print(" LEFT_DIR=");
  Serial.print(STEPPER_LEFT_DIR_PIN);
  Serial.print(" RIGHT_STEP=");
  Serial.print(STEPPER_RIGHT_STEP_PIN);
  Serial.print(" RIGHT_DIR=");
  Serial.print(STEPPER_RIGHT_DIR_PIN);
  Serial.print(" STEPS_PER_REV=");
  Serial.println(STEPPER_STEPS_PER_REV);
#else
  stepperState = STEPPER_DISABLED;
  Serial.println("EVENT=STEPPER_READY MODE=DISABLED");
#endif
}

void initRelay() {
#if DREAM_ENABLE_RELAY_OUTPUT
  pinMode(RELAY_OUTPUT_PIN, OUTPUT);
  digitalWrite(RELAY_OUTPUT_PIN, !RELAY_ACTIVE_LEVEL);
  relayState = RELAY_OFF;
  relayStateStartMs = millis();
  Serial.println("EVENT=RELAY_READY MODE=OUTPUT_ENABLED");
#else
  relayState = RELAY_OFF;
  Serial.println("EVENT=RELAY_READY MODE=DISABLED");
#endif
}

uint32_t eegAgeMs() {
  if (!haveEeg) {
    return 0xFFFFFFFFUL;
  }
  return millis() - lastEegFrameMs;
}

void updateSafetyState() {
  const bool timedOut = !haveEeg || eegAgeMs() > EEG_TIMEOUT_MS;
  if (timedOut) {
    safetyState = SAFETY_LINK_TIMEOUT;
    if (!timeoutWasActive) {
      eegTimeoutCount++;
      timeoutWasActive = true;
      Serial.println("EVENT=EEG_TIMEOUT ACTION=SAFE_STATE");
    }
    return;
  }

  timeoutWasActive = false;
  safetyState = latestEegPacket.poorSignal > POOR_SIGNAL_BAD_THRESHOLD ? SAFETY_SIGNAL_BAD : SAFETY_NORMAL;
}

void updateLightTargets() {
  if (!systemEnabled) {
    lightMode = LIGHT_OFF;
    light1Target = {0, 0, 0, 0};
    light2Target = {0, 0, 0, 0};
    return;
  }

  if (manualLightEnabled) {
    lightMode = LIGHT_MANUAL;
    if (isColorOff(manualLightColor)) {
      light1Target = {0, 0, 0, 0};
      light2Target = {0, 0, 0, 0};
    } else {
      setManualGradientTargets();
    }
    return;
  }

  if (safetyState == SAFETY_LINK_TIMEOUT) {
    lightMode = LIGHT_TIMEOUT;
    light1Target = {0, 0, 0, 0};
    light2Target = {0, 0, 0, 0};
    return;
  }

  if (safetyState == SAFETY_SIGNAL_BAD) {
    lightMode = LIGHT_SIGNAL_BAD;
    light1Target = {8, 0, 24, 0};
    light2Target = {0, 8, 18, 0};
    return;
  }

  const uint8_t attention = scalePercentToByte(latestEegPacket.attention);
  const uint8_t meditation = scalePercentToByte(latestEegPacket.meditation);
  const uint8_t flowBrightness = static_cast<uint8_t>(constrain(static_cast<int>(max(attention, meditation)), 64, 255));

  lightMode = LIGHT_EEG_BLEND;
  setFlowTargets(flowBrightness, latestEegPacket.attention);
}

void updateLightController() {
  updateLightTargets();

  const uint32_t now = millis();
  if (now - lastDmxRefreshMs < DMX_REFRESH_MS) {
    return;
  }

  lastDmxRefreshMs = now;
  if (manualLightEnabled || !systemEnabled) {
    light1Current = light1Target;
    light2Current = light2Target;
  } else {
    light1Current = smoothColor(light1Current, light1Target);
    light2Current = smoothColor(light2Current, light2Target);
  }
  lightLevel = max(max4(light1Current.r, light1Current.g, light1Current.b, light1Current.w),
                   max4(light2Current.r, light2Current.g, light2Current.b, light2Current.w));
  sendDmxFrame();
}

#if DREAM_ENABLE_STEPPER_OUTPUT
void writeStepperStepPin(uint8_t targetMask, uint8_t level) {
  if (targetMask & STEPPER_TARGET_LEFT) {
    digitalWrite(STEPPER_LEFT_STEP_PIN, level);
  }
  if (targetMask & STEPPER_TARGET_RIGHT) {
    digitalWrite(STEPPER_RIGHT_STEP_PIN, level);
  }
}

void writeStepperDirectionPin(uint8_t targetMask, bool forward) {
  if (targetMask & STEPPER_TARGET_LEFT) {
    digitalWrite(STEPPER_LEFT_DIR_PIN, forward ? HIGH : LOW);
  }
  if (targetMask & STEPPER_TARGET_RIGHT) {
    const bool rightForward = STEPPER_RIGHT_DIR_INVERT ? !forward : forward;
    digitalWrite(STEPPER_RIGHT_DIR_PIN, rightForward ? HIGH : LOW);
  }
}

bool updateSingleStepper(uint8_t targetMask,
                         int32_t &positionSteps,
                         int32_t targetSteps,
                         uint32_t &lastPulseUs,
                         bool &pulseActive,
                         uint32_t nowUs) {
  if (targetSteps == positionSteps) {
    pulseActive = false;
    writeStepperStepPin(targetMask, HIGH);
    return false;
  }

  const int8_t direction = targetSteps > positionSteps ? 1 : -1;
  writeStepperDirectionPin(targetMask, direction > 0);

  if (static_cast<uint32_t>(nowUs - lastPulseUs) < STEPPER_PULSE_HALF_PERIOD_US) {
    return true;
  }

  lastPulseUs = nowUs;
  if (pulseActive) {
    writeStepperStepPin(targetMask, HIGH);
    pulseActive = false;
    positionSteps += direction;
  } else {
    writeStepperStepPin(targetMask, LOW);
    pulseActive = true;
  }
  return true;
}

bool stepperTargetsReached() {
  return stepperLeftTargetSteps == stepperLeftPositionSteps &&
         stepperRightTargetSteps == stepperRightPositionSteps &&
         !leftStepperPulseActive &&
         !rightStepperPulseActive;
}
#endif

void updateStepperController() {
#if DREAM_ENABLE_STEPPER_OUTPUT
  if (!systemEnabled) {
    manualStepperActive = false;
    manualStepperMask = 0;
    stepperLeftTargetSteps = stepperLeftPositionSteps;
    stepperRightTargetSteps = stepperRightPositionSteps;
    leftStepperPulseActive = false;
    rightStepperPulseActive = false;
    writeStepperStepPin(STEPPER_TARGET_BOTH, HIGH);
    if (STEPPER_ENABLE_PIN >= 0) {
      digitalWrite(STEPPER_ENABLE_PIN, !STEPPER_ENABLE_ACTIVE_LEVEL);
    }
    stepperState = STEPPER_DISABLED;
    return;
  }

  const bool allowAutoStepper = systemEnabled && safetyState == SAFETY_NORMAL;
  if (!manualStepperActive && !allowAutoStepper) {
    stepperLeftTargetSteps = stepperLeftPositionSteps;
    stepperRightTargetSteps = stepperRightPositionSteps;
    leftStepperPulseActive = false;
    rightStepperPulseActive = false;
    writeStepperStepPin(STEPPER_TARGET_BOTH, HIGH);
    if (STEPPER_ENABLE_PIN >= 0) {
      digitalWrite(STEPPER_ENABLE_PIN, !STEPPER_ENABLE_ACTIVE_LEVEL);
    }
    stepperState = STEPPER_DISABLED;
    return;
  }

  if (STEPPER_ENABLE_PIN >= 0) {
    digitalWrite(STEPPER_ENABLE_PIN, STEPPER_ENABLE_ACTIVE_LEVEL);
  }

  if (manualStepperActive) {
    stepperState = STEPPER_MOVING;
    if (manualStepperMask & STEPPER_TARGET_LEFT) {
      stepperLeftTargetSteps = manualStepperLeftTargetSteps;
    }
    if (manualStepperMask & STEPPER_TARGET_RIGHT) {
      stepperRightTargetSteps = manualStepperRightTargetSteps;
    }
  } else if (latestEegPacket.meditation >= STEPPER_MEDITATION_THRESHOLD) {
    stepperState = STEPPER_BREATHING;
    const int32_t averagePositionSteps = (stepperLeftPositionSteps + stepperRightPositionSteps) / 2;
    if (averagePositionSteps >= STEPPER_MAX_POSITION_STEPS) {
      stepperBreathForward = false;
    } else if (averagePositionSteps <= STEPPER_MIN_POSITION_STEPS) {
      stepperBreathForward = true;
    }
    stepperLeftTargetSteps = stepperBreathForward ? STEPPER_MAX_POSITION_STEPS : STEPPER_MIN_POSITION_STEPS;
    stepperRightTargetSteps = stepperLeftTargetSteps;
  } else if (latestEegPacket.attention >= STEPPER_ATTENTION_THRESHOLD) {
    stepperState = STEPPER_MOVING;
    stepperLeftTargetSteps = STEPPER_MAX_POSITION_STEPS;
    stepperRightTargetSteps = STEPPER_MAX_POSITION_STEPS;
  } else {
    stepperLeftTargetSteps = 0;
    stepperRightTargetSteps = 0;
    stepperState = stepperTargetsReached() ? STEPPER_IDLE : STEPPER_MOVING;
  }

  const uint32_t nowUs = micros();
  const bool leftMoving = updateSingleStepper(STEPPER_TARGET_LEFT,
                                              stepperLeftPositionSteps,
                                              stepperLeftTargetSteps,
                                              lastLeftStepperPulseUs,
                                              leftStepperPulseActive,
                                              nowUs);
  const bool rightMoving = updateSingleStepper(STEPPER_TARGET_RIGHT,
                                               stepperRightPositionSteps,
                                               stepperRightTargetSteps,
                                               lastRightStepperPulseUs,
                                               rightStepperPulseActive,
                                               nowUs);

  if (manualStepperActive) {
    if ((manualStepperMask & STEPPER_TARGET_LEFT) &&
        stepperLeftPositionSteps == manualStepperLeftTargetSteps &&
        !leftStepperPulseActive) {
      manualStepperMask &= ~STEPPER_TARGET_LEFT;
    }
    if ((manualStepperMask & STEPPER_TARGET_RIGHT) &&
        stepperRightPositionSteps == manualStepperRightTargetSteps &&
        !rightStepperPulseActive) {
      manualStepperMask &= ~STEPPER_TARGET_RIGHT;
    }
    manualStepperActive = manualStepperMask != 0;
  }

  if (!leftMoving && !rightMoving) {
    stepperState = STEPPER_IDLE;
  }
#else
  stepperState = STEPPER_DISABLED;
#endif
}

void updateRelayController() {
#if DREAM_ENABLE_RELAY_OUTPUT
  const uint32_t now = millis();
  if (!systemEnabled || safetyState != SAFETY_NORMAL) {
    digitalWrite(RELAY_OUTPUT_PIN, !RELAY_ACTIVE_LEVEL);
    relayState = RELAY_OFF;
    relayStateStartMs = now;
    relayArmStartMs = 0;
    return;
  }

  switch (relayState) {
    case RELAY_OFF:
      digitalWrite(RELAY_OUTPUT_PIN, !RELAY_ACTIVE_LEVEL);
      if (manualRelayRequested) {
        relayState = RELAY_ON;
        relayStateStartMs = now;
        digitalWrite(RELAY_OUTPUT_PIN, RELAY_ACTIVE_LEVEL);
      } else if (latestEegPacket.attention > 75) {
        relayState = RELAY_ARMING;
        relayArmStartMs = now;
        relayStateStartMs = now;
      }
      break;
    case RELAY_ARMING:
      digitalWrite(RELAY_OUTPUT_PIN, !RELAY_ACTIVE_LEVEL);
      if (manualRelayRequested) {
        relayState = RELAY_ON;
        relayStateStartMs = now;
        digitalWrite(RELAY_OUTPUT_PIN, RELAY_ACTIVE_LEVEL);
      } else if (latestEegPacket.attention <= 75) {
        relayState = RELAY_OFF;
        relayStateStartMs = now;
      } else if (now - relayArmStartMs >= RELAY_ARM_MS) {
        relayState = RELAY_ON;
        relayStateStartMs = now;
        digitalWrite(RELAY_OUTPUT_PIN, RELAY_ACTIVE_LEVEL);
      }
      break;
    case RELAY_ON:
      digitalWrite(RELAY_OUTPUT_PIN, RELAY_ACTIVE_LEVEL);
      if ((manualRelayRequested && now - relayStateStartMs >= RELAY_MANUAL_MAX_ON_MS) ||
          (!manualRelayRequested && now - relayStateStartMs >= RELAY_ON_MS)) {
        manualRelayRequested = false;
        relayState = RELAY_COOLDOWN;
        relayStateStartMs = now;
        digitalWrite(RELAY_OUTPUT_PIN, !RELAY_ACTIVE_LEVEL);
      }
      break;
    case RELAY_COOLDOWN:
      digitalWrite(RELAY_OUTPUT_PIN, !RELAY_ACTIVE_LEVEL);
      if (now - relayStateStartMs >= RELAY_COOLDOWN_MS) {
        relayState = RELAY_OFF;
        relayStateStartMs = now;
      }
      break;
    default:
      relayState = RELAY_FAULT;
      digitalWrite(RELAY_OUTPUT_PIN, !RELAY_ACTIVE_LEVEL);
      break;
  }
#else
  relayState = RELAY_OFF;
#endif
}

void sendMicStatus() {
  if (!espNowReady) {
    return;
  }

  DreamMicStatusPacket packet = {};
  packet.magic = DREAM_PACKET_MAGIC;
  packet.version = DREAM_PROTOCOL_VERSION;
  packet.type = DREAM_PACKET_MIC_STATUS;
  packet.seq = statusSeq++;
  packet.uptimeMs = millis();
  packet.lastEegSeq = haveEeg ? latestEegPacket.seq : 0;
  packet.eegAgeMs = haveEeg ? eegAgeMs() : 0xFFFFFFFFUL;
  packet.rxCount = eegRxCount;
  packet.dropCount = eegDropCount + eegChecksumErrorCount;
  packet.timeoutCount = eegTimeoutCount;
  packet.poorSignal = haveEeg ? latestEegPacket.poorSignal : 255;
  packet.attention = haveEeg ? latestEegPacket.attention : 0;
  packet.meditation = haveEeg ? latestEegPacket.meditation : 0;
  packet.lightLevel = lightLevel;
  packet.lightMode = lightMode;
  packet.light1R = light1Current.r;
  packet.light1G = light1Current.g;
  packet.light1B = light1Current.b;
  packet.light1W = light1Current.w;
  packet.light2R = light2Current.r;
  packet.light2G = light2Current.g;
  packet.light2B = light2Current.b;
  packet.light2W = light2Current.w;
  packet.stepperState = stepperState;
  packet.relayState = relayState;
  packet.safetyState = safetyState;
  packet.controlRxCount = controlRxCount;
  packet.lastControlAction = lastControlAction;
  packet.manualLightEnabled = manualLightEnabled ? 1 : 0;
  packet.relayOutputEnabled = DREAM_ENABLE_RELAY_OUTPUT ? 1 : 0;
  packet.stepperOutputEnabled = DREAM_ENABLE_STEPPER_OUTPUT ? 1 : 0;
  packet.systemEnabled = systemEnabled ? 1 : 0;
  packet.checksum = calculatePacketChecksum(packet);

  const esp_err_t result = esp_now_send(BROADCAST_MAC, reinterpret_cast<const uint8_t *>(&packet), sizeof(packet));
  if (result != ESP_OK) {
    Serial.print("EVENT=MIC_STATUS_SEND_FAIL ERR=");
    Serial.println(result);
  }
}

void printStatus() {
  Serial.print("EVENT=MIC_STATUS RX=");
  Serial.print(eegRxCount);
  Serial.print(" DROP=");
  Serial.print(eegDropCount);
  Serial.print(" CHECKSUM_ERR=");
  Serial.print(eegChecksumErrorCount);
  Serial.print(" TIMEOUT=");
  Serial.print(eegTimeoutCount);
  Serial.print(" CONTROL_RX=");
  Serial.print(controlRxCount);
  Serial.print(" CONTROL_CHECKSUM_ERR=");
  Serial.print(controlChecksumErrorCount);
  Serial.print(" EEG_AGE_MS=");
  Serial.print(haveEeg ? eegAgeMs() : 0);
  Serial.print(" LIGHT_MODE=");
  Serial.print(lightMode);
  Serial.print(" LIGHT_LEVEL=");
  Serial.print(lightLevel);
  Serial.print(" DMX_FRAMES=");
  Serial.print(dmxFrameCount);
  Serial.print(" DMX_WRITE=");
  Serial.print(lastDmxWriteSize);
  Serial.print(" DMX_SEND=");
  Serial.print(lastDmxSendSize);
  Serial.print(" DMX_SEND_FAIL=");
  Serial.print(dmxSendFailCount);
  Serial.print(" DMX_WAIT_FAIL=");
  Serial.print(dmxWaitFailCount);
  Serial.print(" L1_RGBW=");
  Serial.print(light1Current.r);
  Serial.print(",");
  Serial.print(light1Current.g);
  Serial.print(",");
  Serial.print(light1Current.b);
  Serial.print(",");
  Serial.print(light1Current.w);
  Serial.print(" L2_RGBW=");
  Serial.print(light2Current.r);
  Serial.print(",");
  Serial.print(light2Current.g);
  Serial.print(",");
  Serial.print(light2Current.b);
  Serial.print(",");
  Serial.print(light2Current.w);
  Serial.print(" STEPPER=");
  Serial.print(stepperState);
#if DREAM_ENABLE_STEPPER_OUTPUT
  Serial.print(" STEPPER_L_POS=");
  Serial.print(stepperLeftPositionSteps);
  Serial.print(" STEPPER_R_POS=");
  Serial.print(stepperRightPositionSteps);
  Serial.print(" STEPPER_L_TARGET=");
  Serial.print(stepperLeftTargetSteps);
  Serial.print(" STEPPER_R_TARGET=");
  Serial.print(stepperRightTargetSteps);
  Serial.print(" STEPPER_MANUAL_MASK=");
  Serial.print(manualStepperMask);
#endif
  Serial.print(" RELAY=");
  Serial.print(relayState);
  Serial.print(" SAFETY=");
  Serial.print(safetyState);
  Serial.print(" LAST_ACTION=");
  Serial.print(lastControlAction);
  Serial.print(" MANUAL_LIGHT=");
  Serial.print(manualLightEnabled ? 1 : 0);
  Serial.print(" RELAY_OUTPUT_ENABLED=");
  Serial.print(DREAM_ENABLE_RELAY_OUTPUT ? 1 : 0);
  Serial.print(" STEPPER_OUTPUT_ENABLED=");
  Serial.print(DREAM_ENABLE_STEPPER_OUTPUT ? 1 : 0);
  Serial.print(" SYSTEM_ENABLED=");
  Serial.println(systemEnabled ? 1 : 0);
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(300);

  latestEegPacket.poorSignal = 255;

  Serial.println();
  Serial.print("EVENT=DREAM_BOOT BOARD=");
  Serial.print(BOARD_NAME);
  Serial.print(" ROLE=");
  Serial.print(DEVICE_ROLE);
  Serial.print(" VERSION=");
  Serial.println(MIC_CONTROLLER_VERSION);

  initDmx();
  initStepper();
  initRelay();
  initEspNow();
}

void loop() {
  updateSafetyState();
  updateStepperController();
  updateLightController();
  updateRelayController();

  const uint32_t now = millis();
  if (now - lastStatusSendMs >= STATUS_SEND_MS) {
    lastStatusSendMs = now;
    sendMicStatus();
  }

  if (now - lastStatusPrintMs >= STATUS_PRINT_MS) {
    lastStatusPrintMs = now;
    printStatus();
  }
}
