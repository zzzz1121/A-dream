# Dream 命名规范

更新时间：2026-05-20  
用途：统一 Dream 装置后续文档、代码、协议、变量、日志和文件命名。  
适用范围：`docs/` 文档、`src/` 固件、`tools/` 电脑端脚本、USB 串口、ESP-NOW 协议、调试日志。

## 1. 总原则

- 装置名称统一为 `Dream`。
- `README.md` 是仓库入口文件，文件名不改。
- 文档类文件放在 `docs/`，文件名使用中文，前缀统一为 `Dream`。
- 代码目录、代码文件、Python 脚本使用英文 `snake_case`。
- C++ 常量使用 `UPPER_SNAKE_CASE`。
- C++ 变量和函数使用 `lowerCamelCase`。
- C++ 类型、结构体、类使用 `PascalCase`。
- Python 变量和函数使用 `snake_case`。
- 协议字段使用 `lowerCamelCase`，保持电脑端、Microduino、M5Stack 一致。
- 日志事件使用 `EVENT=UPPER_SNAKE_CASE`。

当前已有原型代码可以暂时保留旧命名。后续重构、正式固件、新脚本、新文档都按本规范执行。

## 2. 项目与设备命名

| 对象 | 统一名称 | 说明 |
| --- | --- | --- |
| 装置名 | `Dream` | 对外和文档中的装置名称 |
| 仓库入口 | `README.md` | 不改名 |
| 电脑端 | `pcBridge` | 电脑上的脑电读取与转发程序 |
| 脑电设备 | `eegDevice` | 通过蓝牙向电脑发送 ThinkGear 数据 |
| Microduino | `micController` | 执行控制器，控制灯光、电机、继电器 |
| M5Stack | `m5GatewayMonitor` | 电脑 USB 串口网关 + 监测屏，通过 ESP-NOW 转发给 Microduino |
| DMX 灯光 | `dmxLights` | DMX512 / MAX485 灯光输出 |
| 步进电机 | `stepperMotor` | 运动输出 |
| 继电器 | `relayOutput` | 开关型输出 |

## 3. 文档文件命名

文档文件放在 `docs/`，使用中文名，前缀统一为 `Dream`。使用说明统一放在 `docs/Dream使用说明/`；技术设计、通信方案和命名规范统一放在 `docs/Dream技术文档/`；仓库状态和需求原始文档保留在 `docs/` 根目录。

当前文档命名：

| 文件 | 用途 |
| --- | --- |
| `docs/Dream仓库状态说明.md` | 仓库当前状态、板卡、环境、已完成内容 |
| `docs/Dream技术文档/Dream完整技术设计文档.md` | 整套装置技术设计总文档 |
| `docs/Dream技术文档/Dream脑电无线转发Microduino技术方案.md` | 脑电到 Microduino 的通信专项方案 |
| `docs/Dream技术文档/Dream命名规范.md` | 本命名规范 |
| `docs/Dream需求规划文档_V3.0.docx` | 需求规划原始文档 |
| `docs/Dream使用说明/Dream使用说明总览.md` | 使用说明入口 |
| `docs/Dream使用说明/Dream电脑端与前端使用说明.md` | 电脑端脚本和浏览器前端使用说明 |
| `docs/Dream使用说明/Dream板卡固件烧录使用说明.md` | M5Stack 和 Microduino 固件烧录说明 |
| `docs/Dream使用说明/Dream现场开机与关机流程.md` | 现场开机、测试、运行、关机流程 |
| `docs/Dream使用说明/Dream故障排查手册.md` | 常见故障和排查步骤 |
| `docs/Dream使用说明/Dream接口与指令协议说明.md` | EEG、CMD、ESP-NOW 和状态回传协议说明 |

后续新增文档建议：

| 建议文件名 | 用途 |
| --- | --- |
| `docs/Dream现场部署检查清单.md` | 展览现场部署和开机检查 |
| `docs/Dream调试记录.md` | 每次硬件/软件联调记录 |
| `docs/Dream电气接线表.md` | 电源、DMX、电机、继电器接线 |
| `docs/Dream测试用例.md` | 数据链路、灯光、电机、继电器测试 |
| `docs/Dream版本变更记录.md` | 固件、脚本、协议版本变化 |

## 4. 代码目录命名

当前原型目录可以保留：

| 当前目录 | 说明 |
| --- | --- |
| `src/microduino_core_esp32_test/` | Microduino 原型固件 |
| `src/m5stack_core_esp32_test/` | M5Stack 原型固件 |
| `src/esp32_wroom_32_test/` | ESP32-WROOM 测试固件 |
| `src/microduino_core_esp32_dmx_spotlight_test/` | DMX 灯光测试固件 |

正式阶段建议目录：

| 建议目录 | 说明 |
| --- | --- |
| `src/dream_microduino_controller/` | Microduino 正式执行控制器 |
| `src/dream_m5_monitor/` | M5Stack 正式监测屏 |
| `src/dream_gateway_esp32/` | 可选 ESP32 无线网关 |
| `src/dream_dmx_test/` | DMX 单项测试 |
| `src/dream_stepper_test/` | 步进电机单项测试 |
| `src/dream_relay_test/` | 继电器单项测试 |

## 5. 工具脚本命名

工具脚本放在 `tools/`，使用英文 `snake_case`，前缀统一为 `dream_`。

当前脚本：

| 当前文件 | 后续建议名称 | 说明 |
| --- | --- | --- |
| `tools/eeg_serial_bridge.py` | `tools/dream_eeg_serial_bridge.py` | 电脑端脑电读取与 USB 串口发送 |

后续建议脚本：

| 文件名 | 用途 |
| --- | --- |
| `tools/dream_eeg_serial_bridge.py` | 正式脑电桥接程序，发送到 M5Stack |
| `tools/dream_serial_test_sender.py` | 发送测试串口 EEG 帧 |
| `tools/dream_eeg_log_replay.py` | 回放 EEG 日志 |
| `tools/dream_serial_probe.py` | 检查脑电串口数据 |
| `tools/dream_protocol_check.py` | 校验协议帧格式 |

当前 `tools/dream_eeg_serial_bridge.py` 同时提供浏览器前端。前端状态字段使用 `lowerCamelCase`，并保留以下真实状态标记：

| 字段 | 说明 |
| --- | --- |
| `eeg.seen` | 是否收到真实 EEG 包 |
| `m5.seen` | 是否收到真实 M5Stack 状态 |
| `mic.seen` | 当前是否有 Microduino 状态回传 |
| `bridge.sourceOpen` | 脑电串口是否打开 |
| `bridge.targetOpen` | M5Stack 串口是否打开 |

前端按钮反馈统一使用：

| 反馈文字 | 含义 |
| --- | --- |
| `发送中` | 请求已发给 Python 本地服务 |
| `已发送` | 命令已进入 M5Stack 串口发送队列 |
| `失败` | 命令没有进入发送队列 |

## 6. PlatformIO 环境命名

当前环境可以保留：

| 当前环境 | 说明 |
| --- | --- |
| `microduino-core-esp32` | Microduino 原型 |
| `m5stack-core-esp32` | M5Stack 原型 |
| `esp32-wroom-32` | ESP32-WROOM 测试 |

正式阶段建议环境：

| 建议环境 | 说明 |
| --- | --- |
| `dream-mic-controller` | Microduino 正式执行控制器 |
| `dream-m5-monitor` | M5Stack 正式监测屏 |
| `dream-dmx-test` | DMX 测试 |
| `dream-stepper-test` | 步进电机测试 |
| `dream-relay-test` | 继电器测试 |

## 7. 网络与通信命名

### 7.1 串口与 ESP-NOW

当前主方案统一使用：

| 名称 | 值 | 说明 |
| --- | --- | --- |
| `PC_TO_M5_BAUD` | `115200` | 电脑到 M5Stack USB 串口波特率 |
| `ESPNOW_CHANNEL` | `1` | M5Stack 与 Microduino 的 ESP-NOW 信道 |
| `MIC_CONTROLLER_MAC` | 待填写 | Microduino Wi-Fi MAC |
| `M5_GATEWAY_MAC` | 待填写 | M5Stack Wi-Fi MAC |
| `DREAM_PACKET_MAGIC` | `0x44524541` | ESP-NOW EEG 包 magic |
| `DREAM_PROTOCOL_VERSION` | `1` | 协议版本 |

Wi-Fi UDP 只作为调试备选，不作为当前主链路命名基准。

### 7.2 通信链路名称

| 链路名 | 说明 |
| --- | --- |
| `eegSerialLink` | 脑电设备蓝牙 COM 到电脑 |
| `pcToM5SerialLink` | 电脑到 M5Stack 的 USB 串口链路 |
| `m5ToMicEspNowLink` | M5Stack 到 Microduino 的 ESP-NOW 控制链路 |
| `micToM5EspNowStatusLink` | Microduino 到 M5Stack 的可选 ESP-NOW 状态回传 |
| `dmxOutputLink` | Microduino 到 DMX 灯具 |
| `stepperControlLink` | Microduino 到步进驱动器 |
| `relayControlLink` | Microduino 到继电器模块 |

## 8. 串口 / ESP-NOW 协议命名

### 8.1 帧类型

| 帧类型 | 方向 | 说明 |
| --- | --- | --- |
| `EEG` | `pcBridge -> m5GatewayMonitor` | 串口脑电状态文本帧 |
| `DREAM_EEG_PACKET` | `m5GatewayMonitor -> micController` | ESP-NOW 脑电结构包 |
| `MIC` | `micController -> m5GatewayMonitor` | 可选 Microduino 执行状态帧 |
| `PING` | 任意方向 | 可选心跳 |
| `ACK` | 任意方向 | 可选确认 |

### 8.2 EEG 帧字段

统一字段顺序：

```text
EEG,seq,timeMs,poorSignal,attention,meditation,delta,theta,lowAlpha,highAlpha,lowBeta,highBeta,lowGamma,midGamma
```

字段变量名：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `seq` | `uint32` | 递增序号 |
| `timeMs` | `uint32` | 电脑端相对时间 |
| `poorSignal` | `uint8` | ThinkGear 信号质量 |
| `attention` | `uint8` | 专注度 |
| `meditation` | `uint8` | 放松度 |
| `delta` | `uint32` | Delta 频段 |
| `theta` | `uint32` | Theta 频段 |
| `lowAlpha` | `uint32` | 低 Alpha |
| `highAlpha` | `uint32` | 高 Alpha |
| `lowBeta` | `uint32` | 低 Beta |
| `highBeta` | `uint32` | 高 Beta |
| `lowGamma` | `uint32` | 低 Gamma |
| `midGamma` | `uint32` | 中 Gamma |

派生字段：

| 字段 | 计算 |
| --- | --- |
| `alphaPower` | `lowAlpha + highAlpha` |
| `betaPower` | `lowBeta + highBeta` |
| `gammaPower` | `lowGamma + midGamma` |
| `eegAgeMs` | `nowMs - lastEegFrameMs` |

### 8.3 M5 网关状态字段

```text
M5,seq,timeMs,serialFrames,serialErrors,espNowSent,espNowFailed,lastErrorCode
```

### 8.4 MIC 帧字段

```text
MIC,seq,timeMs,eegAgeMs,rxCount,dropCount,lightMode,lightLevel,stepperState,relayState,timeout
```

## 9. C++ 命名规范

### 9.1 常量

使用 `UPPER_SNAKE_CASE`：

```cpp
constexpr uint32_t PC_TO_M5_BAUD = 115200;
constexpr uint8_t ESPNOW_CHANNEL = 1;
constexpr uint32_t DREAM_PACKET_MAGIC = 0x44524541;
constexpr uint32_t EEG_TIMEOUT_MS = 3000;
```

推荐常量名：

| 常量 | 说明 |
| --- | --- |
| `BOARD_NAME` | 当前板卡名称 |
| `DREAM_DEVICE_ROLE` | 当前固件角色 |
| `PC_TO_M5_BAUD` | 电脑到 M5Stack 串口波特率 |
| `ESPNOW_CHANNEL` | ESP-NOW 信道 |
| `MIC_CONTROLLER_MAC` | Microduino peer MAC |
| `M5_GATEWAY_MAC` | M5Stack MAC |
| `EEG_TIMEOUT_MS` | EEG 超时时间 |
| `DMX_TX_PIN` | DMX 发送引脚 |
| `STEPPER_STEP_PIN` | 步进 STEP 引脚 |
| `STEPPER_DIR_PIN` | 步进 DIR 引脚 |
| `STEPPER_EN_PIN` | 步进 EN 引脚 |
| `RELAY_FOG_PIN` | 造雾或继电器控制引脚 |

### 9.2 结构体和类型

使用 `PascalCase`：

```cpp
struct DreamEegFrame;
struct DreamBridgeStatus;
struct DreamMicStatus;
struct DmxLightState;
struct StepperControllerState;
struct RelayControllerState;
```

### 9.3 变量

使用 `lowerCamelCase`：

```cpp
DreamEegFrame latestEegFrame;
uint32_t lastEegFrameMs;
uint32_t eegFrameCount;
uint32_t eegDropCount;
bool eegTimedOut;
```

推荐变量名：

| 变量 | 说明 |
| --- | --- |
| `latestEegFrame` | 最近一帧有效 EEG |
| `lastEegFrameMs` | 最近有效 EEG 时间 |
| `eegFrameCount` | EEG 接收帧数 |
| `eegDropCount` | EEG 丢包计数 |
| `eegChecksumErrorCount` | ThinkGear 校验错误数 |
| `serialReady` | M5Stack 串口是否收到数据 |
| `espNowReady` | ESP-NOW 是否就绪 |
| `espNowSendFailCount` | ESP-NOW 发送失败数 |
| `lightMode` | 灯光模式 |
| `lightLevel` | 灯光亮度 |
| `stepperState` | 步进电机状态 |
| `relayState` | 继电器状态 |

### 9.4 函数

使用 `lowerCamelCase`：

```cpp
void setupWifi();
void handleUdpInput();
bool parseEegCsvFrame(const char *line, DreamEegFrame &frame);
void updateLightController();
void updateStepperController();
void updateRelayController();
void sendMicStatusFrame();
void drawMonitorScreen();
```

### 9.5 枚举

枚举类型使用 `PascalCase`，枚举值使用 `UPPER_SNAKE_CASE`：

```cpp
enum EegSignalState : uint8_t {
  EEG_SIGNAL_NO_DATA,
  EEG_SIGNAL_BAD,
  EEG_SIGNAL_OK,
  EEG_SIGNAL_TIMEOUT,
};
```

推荐枚举：

```cpp
EegSignalState
DreamNetworkState
DreamLightMode
DreamStepperState
DreamRelayState
DreamSafetyState
```

## 10. Python 命名规范

### 10.1 文件和模块

使用 `snake_case`，前缀 `dream_`：

```text
dream_eeg_serial_bridge.py
dream_serial_test_sender.py
dream_eeg_log_replay.py
```

### 10.2 变量和函数

使用 `snake_case`：

```python
source_port = "COM3"
source_baud = 57600
target_port = "COM6"
target_baud = 115200
send_rate_hz = 20
latest_eeg_frame = None
```

函数：

```python
def parse_thinkgear_packet(data):
def build_eeg_csv_frame(frame):
def send_udp_frame(sock, target, payload):
def print_bridge_status(status):
```

类：

```python
class ThinkGearParser:
class DreamEegFrame:
class UdpFrameSender:
class BridgeStatus:
```

### 10.3 命令行参数

使用 `kebab-case`：

```text
--source
--source-baud
--target
--target-baud
--send-rate
--log-file
```

## 11. 日志命名规范

统一日志格式：

```text
EVENT=EVENT_NAME KEY=VALUE KEY=VALUE
```

推荐事件：

| 事件 | 说明 |
| --- | --- |
| `DREAM_BOOT` | 固件或脚本启动 |
| `SERIAL_READY` | 串口就绪 |
| `ESPNOW_READY` | ESP-NOW 就绪 |
| `EEG_RX` | 收到有效 EEG |
| `EEG_PARSE_FAIL` | EEG 解析失败 |
| `EEG_CHECKSUM_FAIL` | ThinkGear 校验失败 |
| `EEG_TIMEOUT` | EEG 超时 |
| `SERIAL_RX` | 收到串口帧 |
| `SERIAL_TX` | 发出串口帧 |
| `ESPNOW_RX` | 收到 ESP-NOW 包 |
| `ESPNOW_TX` | 发出 ESP-NOW 包 |
| `DMX_UPDATE` | DMX 刷新 |
| `LIGHT_STATE` | 灯光状态变化 |
| `STEPPER_STATE` | 步进状态变化 |
| `RELAY_STATE` | 继电器状态变化 |
| `SAFETY_STATE` | 安全状态变化 |
| `MONITOR_UPDATE` | M5Stack 屏幕刷新 |

示例：

```text
EVENT=EEG_RX SEQ=1024 POOR_SIGNAL=0 ATTENTION=67 MEDITATION=43
EVENT=RELAY_STATE FROM=OFF TO=ARMING REASON=ATTENTION_HIGH
EVENT=EEG_TIMEOUT AGE_MS=3050 ACTION=SAFE_STATE
```

## 12. 状态命名

### 12.1 EEG 状态

| 状态 | 含义 |
| --- | --- |
| `EEG_SIGNAL_NO_DATA` | 尚未收到数据 |
| `EEG_SIGNAL_BAD` | `poorSignal` 过高 |
| `EEG_SIGNAL_OK` | 信号可用 |
| `EEG_SIGNAL_TIMEOUT` | 数据超时 |

### 12.2 灯光状态

| 状态 | 含义 |
| --- | --- |
| `LIGHT_OFF` | 关闭 |
| `LIGHT_IDLE` | 待机 |
| `LIGHT_EEG_BLEND` | EEG 混合光效 |
| `LIGHT_SIGNAL_BAD` | 信号差提示 |
| `LIGHT_TIMEOUT` | 超时安全状态 |

### 12.3 步进电机状态

| 状态 | 含义 |
| --- | --- |
| `STEPPER_DISABLED` | 电机禁用 |
| `STEPPER_IDLE` | 待机 |
| `STEPPER_HOMING` | 回零 |
| `STEPPER_MOVING` | 运动中 |
| `STEPPER_BREATHING` | 呼吸式往复 |
| `STEPPER_LIMITED` | 限位触发 |
| `STEPPER_FAULT` | 故障 |

### 12.4 继电器状态

| 状态 | 含义 |
| --- | --- |
| `RELAY_OFF` | 关闭 |
| `RELAY_ARMING` | 触发条件累计中 |
| `RELAY_ON` | 开启 |
| `RELAY_COOLDOWN` | 冷却 |
| `RELAY_FAULT` | 故障 |

### 12.5 安全状态

| 状态 | 含义 |
| --- | --- |
| `SAFETY_NORMAL` | 正常 |
| `SAFETY_SIGNAL_BAD` | 脑电信号差 |
| `SAFETY_LINK_TIMEOUT` | 通信超时 |
| `SAFETY_ESTOP` | 急停 |
| `SAFETY_FAULT` | 综合故障 |

## 13. 引脚命名

引脚常量使用功能命名，不使用模糊名称。

| 常量 | 说明 |
| --- | --- |
| `DMX_TX_PIN` | DMX 数据输出 |
| `RS485_DE_PIN` | RS485 发送使能 |
| `RS485_RE_PIN` | RS485 接收使能 |
| `STEPPER_STEP_PIN` | 步进脉冲 |
| `STEPPER_DIR_PIN` | 步进方向 |
| `STEPPER_EN_PIN` | 步进使能 |
| `LIMIT_MIN_PIN` | 最小限位 |
| `LIMIT_MAX_PIN` | 最大限位 |
| `RELAY_FOG_PIN` | 造雾继电器 |
| `RELAY_FAN_PIN` | 风扇继电器 |
| `STATUS_LED_PIN` | 状态灯 |

如果某个引脚只是原型测试使用，可以在名称中加入 `TEST`：

```cpp
constexpr uint8_t TEST_RGB_LED_PIN = 4;
```

## 14. 版本命名

协议版本：

```text
DREAM_PROTOCOL_VERSION = 1
```

固件版本：

```text
MIC_CONTROLLER_VERSION = "0.1.0"
M5_MONITOR_VERSION = "0.1.0"
```

脚本版本：

```text
PC_BRIDGE_VERSION = "0.1.0"
```

版本递增规则：

| 类型 | 规则 |
| --- | --- |
| `0.1.x` | 原型调试、小修 |
| `0.2.x` | 增加模块或协议字段 |
| `1.0.0` | 展示前稳定版本 |

## 15. 当前命名迁移建议

短期不强制改现有代码，只在新增代码中使用规范命名。

建议迁移顺序：

1. 新增文档全部使用 `Dream中文名.md`。
2. 新增 Python 脚本使用 `dream_*.py`。
3. 新增正式固件目录使用 `src/dream_*`。
4. 把协议字段统一为本规范中的 `EEG / DREAM_EEG_PACKET / MIC`。
5. 最后再逐步整理旧原型代码中的变量和事件名。

## 16. 快速对照表

| 类别 | 统一命名 |
| --- | --- |
| 装置 | `Dream` |
| 电脑桥接程序 | `pcBridge` |
| Microduino 控制器 | `micController` |
| M5Stack 网关监测屏 | `m5GatewayMonitor` |
| EEG 数据帧 | `DreamEegFrame` |
| Microduino 状态帧 | `DreamMicStatus` |
| M5 网关状态帧 | `DreamM5GatewayStatus` |
| EEG 帧类型 | `EEG` |
| ESP-NOW EEG 包类型 | `DREAM_EEG_PACKET` |
| Microduino 状态帧类型 | `MIC` |
| 电脑到 M5 串口波特率 | `PC_TO_M5_BAUD` |
| ESP-NOW 信道 | `ESPNOW_CHANNEL` |
| 超时变量 | `eegTimedOut` |
| 最近 EEG 时间 | `lastEegFrameMs` |
| 灯光模式 | `lightMode` |
| 步进状态 | `stepperState` |
| 继电器状态 | `relayState` |
