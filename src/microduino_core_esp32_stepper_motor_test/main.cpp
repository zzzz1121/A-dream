#include <Arduino.h>

// Microduino Core ESP32 双步进电机测试固件。
//
// 当前接线方式：
// - 两个驱动器的 PUL+ 都接 3V3。
// - 两个驱动器的 PUL- 分别接 ESP32 GPIO。
// - 因为 PUL+ 固定接高电平，所以 GPIO 拉低时产生有效脉冲。
//
// 适用对象：
// - 使用 PUL/DIR 或 STEP/DIR 输入的步进驱动器。
// - 例如 TB6600、DM542、A4988、DRV8825 等类似驱动器。

// 电机 1 引脚
const int stepPin1 = 25;  // Driver 1 PUL-
const int dirPin1 = 14;   // Driver 1 DIR-

// 电机 2 引脚
const int stepPin2 = 27;  // Driver 2 PUL-
const int dirPin2 = 26;   // Driver 2 DIR-

// 如果驱动器细分设置为 1600，那么 1600 个脉冲就是一圈。
// 如果你改了驱动器拨码细分，这个值也要同步修改。
const int stepsPerRev = 1600;

// 速度控制参数，单位是微秒。
// 数字越大，脉冲间隔越长，电机转得越慢。
// 当前一次完整脉冲包含 LOW 延时 + HIGH 延时，所以周期约为 pulseDelay * 2。
const int pulseDelay = 800;  // microseconds

// 同时给两个电机发送相同数量的步进脉冲。
// 当前两个电机共用同一个节奏，所以会同步启动、同步停止。
void stepTwoMotors(int steps) {
  for (int i = 0; i < steps; i++) {
    // PUL- 拉低，产生有效脉冲的低电平阶段。
    digitalWrite(stepPin1, LOW);
    digitalWrite(stepPin2, LOW);
    delayMicroseconds(pulseDelay);

    // PUL- 拉高，结束本次脉冲，并为下一次脉冲做准备。
    digitalWrite(stepPin1, HIGH);
    digitalWrite(stepPin2, HIGH);
    delayMicroseconds(pulseDelay);
  }
}

void setup() {
  // 配置两个电机的 STEP/PUL 和 DIR 引脚为输出。
  pinMode(stepPin1, OUTPUT);
  pinMode(dirPin1, OUTPUT);

  pinMode(stepPin2, OUTPUT);
  pinMode(dirPin2, OUTPUT);

  // 初始状态将 PUL- 拉高。
  // 在当前接线方式下，高电平表示没有触发脉冲。
  digitalWrite(stepPin1, HIGH);
  digitalWrite(stepPin2, HIGH);

  // 初始方向设为 HIGH。
  // 如果实际电机方向和预期相反，可以交换 HIGH/LOW，或调整电机相线。
  digitalWrite(dirPin1, HIGH);
  digitalWrite(dirPin2, HIGH);

  // 串口用于观察当前测试阶段。
  Serial.begin(115200);
  Serial.println("Dual stepper motor test start");
}

void loop() {
  Serial.println("Both motors forward");

  // 两个电机同方向正转。
  digitalWrite(dirPin1, HIGH);
  digitalWrite(dirPin2, HIGH);

  // 改变方向后稍微等待，让驱动器稳定识别 DIR 电平。
  delay(100);

  // 两个电机同步转动一圈。
  stepTwoMotors(stepsPerRev);

  // 正转完成后停 1 秒，方便观察。
  delay(1000);

  Serial.println("Both motors backward");

  // 两个电机同方向反转。
  digitalWrite(dirPin1, LOW);
  digitalWrite(dirPin2, LOW);

  // 改变方向后等待驱动器识别方向。
  delay(100);

  // 两个电机同步反向转动一圈。
  stepTwoMotors(stepsPerRev);

  // 反转完成后停 1 秒，然后循环继续正转。
  delay(1000);
}
