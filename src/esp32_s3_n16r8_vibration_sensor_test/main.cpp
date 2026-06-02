#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_arduino_version.h>

#define BOARD_NAME "ESP32-S3 N16R8"
#define DEVICE_ROLE "vibrationSensor"
#define VIBRATION_SENSOR_VERSION "0.1.0"

#define SERIAL_BAUD 115200
#define ESPNOW_CHANNEL 1
#define DREAM_PACKET_MAGIC 0x44524541UL
#define DREAM_PROTOCOL_VERSION 1

// Vibration sensor configuration
#define VIBRATION_SENSOR_PIN 4
#define VIBRATION_SENSOR_TYPE 1  // 0: Analog, 1: Digital
#define VIBRATION_SENSOR_ACTIVE_LEVEL LOW
#define VIBRATION_THRESHOLD 500
#define VIBRATION_DEBOUNCE_MS 20
#define VIBRATION_SAMPLE_INTERVAL_MS 5
#define VIBRATION_TRIGGER_SAMPLES 1
#define VIBRATION_RELEASE_SAMPLES 60
#define VIBRATION_HOLD_MS 300
#define VIBRATION_RETRIGGER_LOCKOUT_MS 1500

// Bubble trigger relay configuration
#define PILLOW_BUBBLE_RELAY_PIN 5
#define BUBBLE_TRIGGER_DURATION_MS 200
#define BUBBLE_TRIGGER_RELAY_ACTIVE_LEVEL HIGH

#define STATUS_SEND_MS 100
#define STATUS_PRINT_MS 1000

const uint8_t BROADCAST_MAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

enum DreamPacketType : uint8_t {
  DREAM_PACKET_EEG = 1,
  DREAM_PACKET_MIC_STATUS = 2,
  DREAM_PACKET_CONTROL = 3,
  DREAM_PACKET_VIBRATION_STATUS = 4,
};

enum DreamPacketFlags : uint8_t {
  VIBRATION_FLAG_SIGNAL_OK = 1 << 0,
  VIBRATION_FLAG_SOURCE_TIMEOUT = 1 << 1,
  VIBRATION_FLAG_CHECKSUM_OK = 1 << 2,
};

enum DreamBubbleState : uint8_t {
  BUBBLE_IDLE,
  BUBBLE_TRIGGERING,
  BUBBLE_COOLDOWN,
};

struct __attribute__((packed)) DreamVibrationEspNowPacket {
  uint32_t magic;
  uint16_t version;
  uint8_t type;
  uint8_t flags;
  uint16_t sensorValue;
  uint8_t vibrationDetected;
  uint8_t bubbleState;
  uint32_t timestamp;
  uint8_t checksum;
};

// Global variables
unsigned long lastVibrationTime = 0;
unsigned long lastBubbleTriggerTime = 0;
unsigned long lastStatusSendTime = 0;
unsigned long lastStatusPrintTime = 0;
unsigned long lastSampleTime = 0;

bool vibrationDetected = false;
uint16_t lastSensorValue = 0;
uint8_t vibrationActiveSamples = 0;
uint8_t vibrationReleaseSamples = 0;
volatile bool vibrationInterruptPending = false;
DreamBubbleState bubbleState = BUBBLE_IDLE;

// Function declarations
void setupPins();
void setupESPNow();
void onESPNowSend(const uint8_t *mac_addr, esp_now_send_status_t status);
void onESPNowReceive(const uint8_t *mac_addr, const uint8_t *data, int data_len);
void IRAM_ATTR onVibrationInterrupt();
void checkVibration();
void triggerBubble();
void updateBubbleState();
void sendStatus();
void printStatus();
uint8_t calculateChecksum(const DreamVibrationEspNowPacket *packet);

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(500);

  Serial.println("\n\n=== Dream Vibration Sensor Controller ===");
  Serial.printf("Board: %s\n", BOARD_NAME);
  Serial.printf("Version: %s\n", VIBRATION_SENSOR_VERSION);
  Serial.printf("Device Role: %s\n", DEVICE_ROLE);
  Serial.printf("ESP-IDF Version: %s\n", esp_get_idf_version());

  setupPins();
  
  // Initialize WiFi and ESP-NOW
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  setupESPNow();

  Serial.println("Setup completed!");
}

void loop() {
  unsigned long currentTime = millis();

  // Sample vibration sensor
  if (currentTime - lastSampleTime >= VIBRATION_SAMPLE_INTERVAL_MS) {
    lastSampleTime = currentTime;
    checkVibration();
  }

  // Update bubble relay state
  updateBubbleState();

  // Send status periodically
  if (currentTime - lastStatusSendTime >= STATUS_SEND_MS) {
    lastStatusSendTime = currentTime;
    sendStatus();
  }

  // Print status information
  if (currentTime - lastStatusPrintTime >= STATUS_PRINT_MS) {
    lastStatusPrintTime = currentTime;
    printStatus();
  }

  delay(10);
}

void setupPins() {
  if (VIBRATION_SENSOR_TYPE == 1) {
    pinMode(VIBRATION_SENSOR_PIN, INPUT_PULLUP);
    const int interruptMode = VIBRATION_SENSOR_ACTIVE_LEVEL == LOW ? FALLING : RISING;
    attachInterrupt(digitalPinToInterrupt(VIBRATION_SENSOR_PIN), onVibrationInterrupt, interruptMode);
  } else {
    pinMode(VIBRATION_SENSOR_PIN, INPUT);
  }
  pinMode(PILLOW_BUBBLE_RELAY_PIN, OUTPUT);

  if (BUBBLE_TRIGGER_RELAY_ACTIVE_LEVEL == HIGH) {
    digitalWrite(PILLOW_BUBBLE_RELAY_PIN, LOW);
  } else {
    digitalWrite(PILLOW_BUBBLE_RELAY_PIN, HIGH);
  }

  Serial.printf("Vibration sensor pin: %d\n", VIBRATION_SENSOR_PIN);
  Serial.printf("Vibration sensor mode: %s\n", VIBRATION_SENSOR_TYPE == 1 ? "digital active-edge" : "analog threshold");
  Serial.printf("Vibration trigger samples: %d\n", VIBRATION_TRIGGER_SAMPLES);
  Serial.printf("Vibration release samples: %d\n", VIBRATION_RELEASE_SAMPLES);
  Serial.printf("Bubble trigger relay pin: %d\n", PILLOW_BUBBLE_RELAY_PIN);
}

void IRAM_ATTR onVibrationInterrupt() {
  vibrationInterruptPending = true;
}

void setupESPNow() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: Failed to initialize ESP-NOW");
    return;
  }

  Serial.println("ESP-NOW initialized");

  // Set WiFi power save mode and channel
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  // Register send callback
  esp_now_register_send_cb(onESPNowSend);

  // Register receive callback
  esp_now_register_recv_cb(onESPNowReceive);

  // Add broadcast peer
  esp_now_peer_info_t peerInfo = {};
  memcpy(&peerInfo.peer_addr, BROADCAST_MAC, 6);
  peerInfo.channel = ESPNOW_CHANNEL;
  peerInfo.encrypt = false;

  if (!esp_now_is_peer_exist(BROADCAST_MAC)) {
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.println("ERROR: Failed to add broadcast peer");
      return;
    }
  }

  Serial.printf("ESP-NOW Channel: %d\n", ESPNOW_CHANNEL);
}

void onESPNowSend(const uint8_t *mac_addr, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    // Serial.println("ESP-NOW send successful");
  } else {
    Serial.println("ESP-NOW send failed");
  }
}

void onESPNowReceive(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
  if (data_len < 4) return;

  uint32_t magic = *(uint32_t *)data;
  if (magic != DREAM_PACKET_MAGIC) return;

  // Process Dream protocol packet
  if (data_len >= sizeof(DreamVibrationEspNowPacket)) {
    const DreamVibrationEspNowPacket *packet = (const DreamVibrationEspNowPacket *)data;

    Serial.printf("Received packet: type=%d, flags=0x%02x\n", packet->type, packet->flags);
  }
}

void checkVibration() {
  unsigned long currentTime = millis();

  uint16_t sensorValue = 0;
  bool sensorActive = false;

  if (VIBRATION_SENSOR_TYPE == 1) {
    int digitalValue = digitalRead(VIBRATION_SENSOR_PIN);
    noInterrupts();
    bool interruptPending = vibrationInterruptPending;
    vibrationInterruptPending = false;
    interrupts();

    sensorActive = interruptPending || digitalValue == VIBRATION_SENSOR_ACTIVE_LEVEL;
    sensorValue = sensorActive ? 0 : 1;
  } else {
    sensorValue = analogRead(VIBRATION_SENSOR_PIN);
    sensorActive = sensorValue > VIBRATION_THRESHOLD;
  }

  lastSensorValue = sensorValue;
  if (sensorActive) {
    if (vibrationActiveSamples < 255) vibrationActiveSamples++;
    vibrationReleaseSamples = 0;
  } else {
    if (vibrationReleaseSamples < 255) vibrationReleaseSamples++;
    vibrationActiveSamples = 0;
  }

  const bool triggerReady = vibrationActiveSamples >= VIBRATION_TRIGGER_SAMPLES;
  const bool releaseReady = vibrationReleaseSamples >= VIBRATION_RELEASE_SAMPLES;

  // Detect vibration
  if (triggerReady && !vibrationDetected) {
    const bool retriggerReady = lastVibrationTime == 0 ||
                                currentTime - lastVibrationTime >= VIBRATION_RETRIGGER_LOCKOUT_MS;
    if (retriggerReady && currentTime - lastVibrationTime >= VIBRATION_DEBOUNCE_MS) {
      vibrationDetected = true;
      lastVibrationTime = currentTime;
      Serial.printf("EVENT=VIBRATION_DETECTED value=%d\n", sensorValue);
      triggerBubble();
    }
  } else if (releaseReady && vibrationDetected) {
    if (currentTime - lastVibrationTime >= VIBRATION_HOLD_MS) {
      vibrationDetected = false;
      Serial.printf("EVENT=VIBRATION_STOPPED value=%d\n", sensorValue);
    }
  }
}

void triggerBubble() {
  if (bubbleState == BUBBLE_IDLE) {
    bubbleState = BUBBLE_TRIGGERING;
    lastBubbleTriggerTime = millis();

    // Activate relay
    if (BUBBLE_TRIGGER_RELAY_ACTIVE_LEVEL == HIGH) {
      digitalWrite(PILLOW_BUBBLE_RELAY_PIN, HIGH);
    } else {
      digitalWrite(PILLOW_BUBBLE_RELAY_PIN, LOW);
    }

    Serial.println("EVENT=BUBBLE_TRIGGER_ACTIVATED");
  }
}

void updateBubbleState() {
  unsigned long currentTime = millis();

  switch (bubbleState) {
    case BUBBLE_TRIGGERING:
      if (currentTime - lastBubbleTriggerTime >= BUBBLE_TRIGGER_DURATION_MS) {
        bubbleState = BUBBLE_COOLDOWN;
        lastBubbleTriggerTime = currentTime;

        // Deactivate relay
        if (BUBBLE_TRIGGER_RELAY_ACTIVE_LEVEL == HIGH) {
          digitalWrite(PILLOW_BUBBLE_RELAY_PIN, LOW);
        } else {
          digitalWrite(PILLOW_BUBBLE_RELAY_PIN, HIGH);
        }

        Serial.println("EVENT=BUBBLE_TRIGGER_DEACTIVATED");
      }
      break;

    case BUBBLE_COOLDOWN:
      if (currentTime - lastBubbleTriggerTime >= 500) {
        bubbleState = BUBBLE_IDLE;
        Serial.println("EVENT=BUBBLE_STATE_IDLE");
      }
      break;

    case BUBBLE_IDLE:
      // Stay idle
      break;
  }
}

void sendStatus() {
  DreamVibrationEspNowPacket packet = {};

  packet.magic = DREAM_PACKET_MAGIC;
  packet.version = DREAM_PROTOCOL_VERSION;
  packet.type = DREAM_PACKET_VIBRATION_STATUS;
  packet.flags = VIBRATION_FLAG_SIGNAL_OK;
  packet.sensorValue = lastSensorValue;
  packet.vibrationDetected = vibrationDetected ? 1 : 0;
  packet.bubbleState = bubbleState;
  packet.timestamp = millis();
  packet.checksum = calculateChecksum(&packet);

  esp_now_send(BROADCAST_MAC, (uint8_t *)&packet, sizeof(packet));
}

void printStatus() {
  Serial.printf("[%lu] Sensor=%d, Vibration=%s, Bubble=%d\n",
                millis(),
                lastSensorValue,
                vibrationDetected ? "YES" : "NO",
                bubbleState);
}

uint8_t calculateChecksum(const DreamVibrationEspNowPacket *packet) {
  uint8_t sum = 0;
  const uint8_t *data = (const uint8_t *)packet;
  for (size_t i = 0; i < sizeof(DreamVibrationEspNowPacket) - 1; i++) {
    sum += data[i];
  }
  return sum;
}
