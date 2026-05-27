#include <Arduino.h>
#include <esp_dmx.h>

// Microduino Core ESP32 射灯测试固件。
// 通过 DMX512 / MAX485 测试两盏 RGBW 射灯。

#define DMX_PORT DMX_NUM_1

// 你现在接的是 ESP32 的 IO27
const int DMX_TX_PIN = 27;

// RX 和 EN 暂时不用
const int DMX_RX_PIN = -1;
const int DMX_EN_PIN = -1;

byte dmxData[DMX_PACKET_SIZE];

// 设置某一盏 RGBW 灯的颜色
// startAddress 是灯的地址，比如 1 或 5
void setLightColor(int startAddress, byte r, byte g, byte b, byte w) {
  dmxData[startAddress]     = r;
  dmxData[startAddress + 1] = g;
  dmxData[startAddress + 2] = b;
  dmxData[startAddress + 3] = w;
}

// 发送 DMX 数据
void sendDMX() {
  dmx_write(DMX_PORT, dmxData, DMX_PACKET_SIZE);
  dmx_send(DMX_PORT);
  dmx_wait_sent(DMX_PORT, DMX_TIMEOUT_TICK);
}

// 同时设置两盏灯
void setTwoLights(
  byte r1, byte g1, byte b1, byte w1,
  byte r2, byte g2, byte b2, byte w2
) {
  memset(dmxData, 0, DMX_PACKET_SIZE);

  // 灯1：地址 001
  setLightColor(1, r1, g1, b1, w1);

  // 灯2：地址 005
  setLightColor(5, r2, g2, b2, w2);

  sendDMX();
}

// 持续显示某个画面
void holdTwoLights(
  byte r1, byte g1, byte b1, byte w1,
  byte r2, byte g2, byte b2, byte w2,
  int durationMs
) {
  unsigned long startTime = millis();

  while (millis() - startTime < durationMs) {
    setTwoLights(r1, g1, b1, w1, r2, g2, b2, w2);
    delay(25);
  }
}

void setup() {
  Serial.begin(115200);

  dmx_config_t config = DMX_CONFIG_DEFAULT;

  dmx_personality_t personalities[] = {
    {1, "Two RGBW Lights"}
  };
  int personality_count = 1;

  dmx_driver_install(DMX_PORT, &config, personalities, personality_count);
  dmx_set_pin(DMX_PORT, DMX_TX_PIN, DMX_RX_PIN, DMX_EN_PIN);

  memset(dmxData, 0, DMX_PACKET_SIZE);

  Serial.println("Two DMX RGBW lights test start");
}

void loop() {
  Serial.println("Test 1: Light1 Red, Light2 Blue");
  holdTwoLights(
    255, 0, 0, 0,     // 灯1 红
    0, 0, 255, 0,     // 灯2 蓝
    2000
  );

  Serial.println("Test 2: Light1 Green, Light2 Purple");
  holdTwoLights(
    0, 255, 0, 0,     // 灯1 绿
    120, 0, 255, 0,   // 灯2 紫
    2000
  );

  Serial.println("Test 3: Light1 White, Light2 Red");
  holdTwoLights(
    0, 0, 0, 255,     // 灯1 白
    255, 0, 0, 0,     // 灯2 红
    2000
  );

  Serial.println("Test 4: Both Cyan");
  holdTwoLights(
    0, 180, 255, 0,   // 灯1 青蓝
    0, 180, 255, 0,   // 灯2 青蓝
    2000
  );

  Serial.println("Test 5: Dream Bubble Color");
  holdTwoLights(
    120, 60, 255, 0,  // 灯1 蓝紫
    255, 120, 180, 0, // 灯2 粉紫
    3000
  );

  Serial.println("Test 6: Off");
  holdTwoLights(
    0, 0, 0, 0,
    0, 0, 0, 0,
    1000
  );
}
