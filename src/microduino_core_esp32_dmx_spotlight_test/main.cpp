#include <Arduino.h>
#include <esp_dmx.h>

#define DMX_PORT DMX_NUM_1

const int DMX_TX_PIN = 5;
const int DMX_RX_PIN = -1;
const int DMX_EN_PIN = -1;

const int DMX_LIGHT_1_ADDRESS = 1;
const int DMX_LIGHT_2_ADDRESS = 5;
const int FLOW_STEP_MS = 35;
const byte FLOW_OFFSET = 96;

byte dmxData[DMX_PACKET_SIZE];

void setLightColor(int startAddress, byte r, byte g, byte b, byte w) {
  dmxData[startAddress] = r;
  dmxData[startAddress + 1] = g;
  dmxData[startAddress + 2] = b;
  dmxData[startAddress + 3] = w;
}

void sendDmx() {
  dmx_write(DMX_PORT, dmxData, DMX_PACKET_SIZE);
  dmx_send(DMX_PORT);
  dmx_wait_sent(DMX_PORT, DMX_TIMEOUT_TICK);
}

void setTwoLights(byte r1, byte g1, byte b1, byte w1,
                  byte r2, byte g2, byte b2, byte w2) {
  memset(dmxData, 0, DMX_PACKET_SIZE);
  setLightColor(DMX_LIGHT_1_ADDRESS, r1, g1, b1, w1);
  setLightColor(DMX_LIGHT_2_ADDRESS, r2, g2, b2, w2);
  sendDmx();
}

void wheelColor(byte position, byte brightness, byte &r, byte &g, byte &b) {
  position = 255 - position;
  if (position < 85) {
    r = 255 - position * 3;
    g = 0;
    b = position * 3;
  } else if (position < 170) {
    position -= 85;
    r = 0;
    g = position * 3;
    b = 255 - position * 3;
  } else {
    position -= 170;
    r = position * 3;
    g = 255 - position * 3;
    b = 0;
  }

  r = static_cast<byte>(static_cast<uint16_t>(r) * brightness / 255);
  g = static_cast<byte>(static_cast<uint16_t>(g) * brightness / 255);
  b = static_cast<byte>(static_cast<uint16_t>(b) * brightness / 255);
}

void showFlow(byte brightness = 255) {
  byte r1 = 0;
  byte g1 = 0;
  byte b1 = 0;
  byte r2 = 0;
  byte g2 = 0;
  byte b2 = 0;
  const byte phase = static_cast<byte>((millis() / FLOW_STEP_MS) & 0xFF);

  wheelColor(phase, brightness, r1, g1, b1);
  wheelColor(static_cast<byte>(phase + FLOW_OFFSET), brightness, r2, g2, b2);
  setTwoLights(r1, g1, b1, 0, r2, g2, b2, 0);
}

void setup() {
  Serial.begin(115200);

  dmx_config_t config = DMX_CONFIG_DEFAULT;
  dmx_personality_t personalities[] = {
    {1, "Two RGBW Flow Lights"}
  };

  dmx_driver_install(DMX_PORT, &config, personalities, 1);
  dmx_set_pin(DMX_PORT, DMX_TX_PIN, DMX_RX_PIN, DMX_EN_PIN);
  memset(dmxData, 0, DMX_PACKET_SIZE);

  Serial.println("Two DMX RGBW flow test start");
}

void loop() {
  showFlow();
  delay(25);
}
