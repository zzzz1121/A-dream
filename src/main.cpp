// main.cpp
#include <Arduino.h>
#include <FastLED.h> // 包含 FastLED 库

#define LED_PIN     4    // 信号线连接的 GPIO 引脚
#define NUM_LEDS    30   // 灯珠数量

CRGB leds[NUM_LEDS]; // 定义灯珠数组

void setup() {
  Serial.begin(115200); // 初始化串口监视器，方便调试
  Serial.println("Starting LED Test...");

  // 初始化 LED 控制器
  // WS2812B 是一种常用的灯珠型号，GRB 表示颜色顺序
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(50); // 设置亮度 (0-255)，降低功耗和发热
  Serial.println("LED Controller initialized.");
}

void loop() {
  // 清空所有灯珠
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show(); // 将颜色数据发送到灯珠
  Serial.println("All LEDs off.");
  delay(1000);

  // 点亮第一个灯珠为红色
  leds[0] = CRGB::Red;
  FastLED.show();
  Serial.println("First LED Red.");
  delay(1000);

  // 点亮第二个灯珠为绿色
  leds[1] = CRGB::Green;
  FastLED.show();
  Serial.println("Second LED Green.");
  delay(1000);

  // 点亮第三个灯珠为蓝色
  leds[2] = CRGB::Blue;
  FastLED.show();
  Serial.println("Third LED Blue.");
  delay(1000);
}