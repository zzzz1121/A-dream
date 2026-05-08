# 仓库现状说明

更新时间：2026-05-08

## 当前定位

`A-dream` 是“梦境泡泡光影装置”的 ESP32 固件与文档仓库。当前阶段不是最终展出程序，而是硬件验证和通讯链路打样。

项目需求基线为 `docs/梦境泡泡光影装置_需求规划文档_V3.0(1).docx`。需求中的最终方向仍是：

- BLE 接收脑电头环数据
- DMX512 控制体验者附近两盏 RGB 射灯
- ESP-NOW 实现主控和从控通讯
- 从控驱动步进电机、造雾机继电器和触发传感器

## 当前已跑通内容

当前仓库已经从单板灯带测试推进到双板 ESP-NOW 联动验证：

```text
M5Stack 按键输入
  -> M5Stack 屏幕实时状态显示
  -> M5Stack ESP-NOW 广播控制包
  -> Microduino ESP-NOW 接收
  -> Microduino 串口打印接收数据
  -> Microduino IO4 RGB 灯珠同步变色
```

M5Stack 按键映射：

| 按键 | 动作 | ESP-NOW action | 颜色效果 |
| --- | --- | --- | --- |
| A | 上一个颜色 | `PREVIOUS` | 循环到上一个颜色 |
| B | 下一个颜色 | `NEXT` | 循环到下一个颜色 |
| C | 熄灭 | `OFF` | 切到 `off` |

颜色集合：

```text
red / green / blue / white / off
```

## 当前板卡配置

| 板子 | 环境名 | 当前端口 | 当前用途 | 固件入口 |
| --- | --- | --- | --- | --- |
| Microduino Core ESP32 | `microduino-core-esp32` | `COM5` | ESP-NOW 接收端 + IO4 RGB 输出 | `src/microduino_core_esp32_test/main.cpp` |
| M5Stack Core ESP32 | `m5stack-core-esp32` | `COM6` | 屏幕按钮交互 + ESP-NOW 发送端 + IO4 RGB 输出 | `src/m5stack_core_esp32_test/main.cpp` |
| ESP32-WROOM-32 | `esp32-wroom-32` | 未固定 | 预留测试入口 | `src/esp32_wroom_32_test/main.cpp` |

`platformio.ini` 当前固定：

```ini
[platformio]
default_envs = m5stack-core-esp32

[env:microduino-core-esp32]
upload_port = COM5
monitor_port = COM5

[env:m5stack-core-esp32]
upload_port = COM6
monitor_port = COM6
```

注意：COM 号由 Windows 分配，换线或换 USB 口后可能变化。端口变化时需要同步修改 `platformio.ini`。

## 当前源码结构

```text
.
├── README.md
├── platformio.ini
├── docs/
│   ├── repository-status.md
│   └── 梦境泡泡光影装置_需求规划文档_V3.0(1).docx
├── src/
│   ├── microduino_core_esp32_test/
│   │   └── main.cpp
│   ├── m5stack_core_esp32_test/
│   │   └── main.cpp
│   └── esp32_wroom_32_test/
│       └── main.cpp
├── include/
├── lib/
└── test/
```

## Microduino 固件说明

入口：`src/microduino_core_esp32_test/main.cpp`

职责：

- 初始化 ESP-NOW 接收端
- 使用 `ESPNOW_CHANNEL = 1`
- 接收 M5Stack 广播控制包
- 打印原始数据：
  - 来源 MAC
  - 数据长度
  - HEX 内容
  - ASCII 内容
- 解析结构化控制包：
  - `board`
  - `event`
  - `button`
  - `action`
  - `color`
  - `sequence`
  - `uptimeMs`
- 根据 `color` 控制 IO4 上的 WS2812B / NeoPixel RGB 灯珠
- 每 3 秒打印 heartbeat

关键串口输出：

```text
EVENT=ESPNOW_RECEIVER_READY
EVENT=ESPNOW_CONTROL_RX FROM_BOARD=M5STACK EVENT_TYPE=BUTTON_PRESS BUTTON=B ACTION=NEXT COLOR=green SEQ=1
EVENT=RGB_APPLIED COLOR=green
EVENT=HEARTBEAT BOARD=MICRODUINO RX_COUNT=1 COLOR=green UPTIME_MS=...
```

## M5Stack 固件说明

入口：`src/m5stack_core_esp32_test/main.cpp`

职责：

- 初始化 M5Stack 屏幕和按钮
- 初始化 IO4 上的 WS2812B / NeoPixel RGB 灯珠
- 初始化 ESP-NOW 发送端
- 使用 `ESPNOW_CHANNEL = 1`
- A/B/C 按键控制本地灯珠颜色
- 按键事件通过 ESP-NOW 广播到 Microduino
- 屏幕实时显示：
  - 当前颜色
  - 当前颜色色块
  - 按键事件次数
  - 运行时间
  - 最近事件
  - A/B/C 按键状态

屏幕已改为局部刷新，避免整屏闪烁。

关键串口输出：

```text
EVENT=ESPNOW_SENDER_READY MODE=BROADCAST
EVENT=BUTTON_PRESS BUTTON=B ACTION=NEXT COLOR=green
EVENT=ESPNOW_TX BUTTON=B ACTION=NEXT COLOR=green SEQ=1 RESULT=QUEUED
EVENT=ESPNOW_SEND_RESULT TARGET=FF:FF:FF:FF:FF:FF STATUS=OK
```

## ESP-NOW 数据包

当前控制包结构两端保持一致：

```cpp
struct EspNowControlPacket {
  uint32_t magic;
  char board[16];
  char event[16];
  char button[8];
  char action[16];
  char color[16];
  uint32_t sequence;
  uint32_t uptimeMs;
};
```

当前 magic：

```cpp
0xAD202606
```

当前发送方式为 broadcast：

```text
FF:FF:FF:FF:FF:FF
```

这样做方便早期测试，不需要手动填写对方 MAC。进入正式固件阶段后，可以改成固定 peer MAC，提高可控性。

## 常用命令

查看串口：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device list
```

编译 M5Stack：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e m5stack-core-esp32
```

上传 M5Stack：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e m5stack-core-esp32 -t upload
```

编译 Microduino：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e microduino-core-esp32
```

上传 Microduino：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e microduino-core-esp32 -t upload
```

监视 Microduino：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device monitor -p COM5 -b 115200
```

监视 M5Stack：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device monitor -p COM6 -b 115200
```

## 已完成

- PlatformIO 多环境配置
- 三块开发板源码入口拆分
- Microduino 固定上传 / 监视端口为 `COM5`
- M5Stack 固定上传 / 监视端口为 `COM6`
- M5Stack 屏幕按钮测试
- M5Stack 屏幕局部刷新
- ESP-NOW 广播发送
- ESP-NOW 接收监听
- Microduino 收到 M5 控制包后同步 IO4 RGB 灯色

## 仍未完成

- 正式 `master` / `slave` 固件目录
- BLE 脑电数据接入
- DMX512 / MAX485 射灯输出
- 两盏射灯地址和通道定义
- 步进电机控制
- 造雾机继电器控制
- 触发传感器输入
- 现场参数配置和故障恢复

## 下一步建议

1. 将当前 M5Stack 发送端整理为正式主控原型。
2. 将当前 Microduino 接收端整理为正式从控原型。
3. 把 ESP-NOW broadcast 改为固定 peer MAC。
4. 在控制包中加入设备模式、心跳、错误码和执行确认。
5. 增加 DMX512 单灯测试环境。
6. 增加步进电机与继电器的最小测试环境。
