# Dream 完整技术设计文档

更新时间：2026-05-18  
文档状态：技术路线确认与实施前检查  
适用范围：脑电采集、无线传输、Microduino 执行控制、M5Stack 监测、灯光、步进电机、继电器、电源、安全与现场部署。

## 1. 项目目标

本装置的目标是把体验者的脑电状态转化为现场光影和机械反馈。

当前核心链路：

```text
脑电设备
  -- Bluetooth -->
电脑
  -- COM 口读取 -->
电脑端桥接程序
  -- USB Serial -->
M5Stack Core ESP32
  -- ESP-NOW -->
Microduino Core ESP32
  --> DMX512 / MAX485 灯光
  --> 步进电机
  --> 继电器
```

设计原则：

- 电脑负责脑电采集和解析，通过 USB 串口把统一 EEG 帧发给 M5Stack。
- M5Stack 负责 USB 串口接收、现场显示，并通过 ESP-NOW 转发给 Microduino。
- Microduino 负责所有现场执行控制和本地安全状态机。
- 所有危险动作必须在 Microduino 端有本地安全逻辑。
- 任何电脑断线、M5Stack 掉线、ESP-NOW 丢包、脑电信号差，都不能导致设备失控。

## 2. 总体系统架构

### 2.1 设备角色

| 设备 | 角色 | 主要职责 |
| --- | --- | --- |
| 脑电设备 | 数据源 | 通过蓝牙向电脑发送 ThinkGear 原始数据 |
| 电脑 | 数据桥接 | 读取 COM 口，解析脑电数据，通过 USB Serial 发送给 M5Stack |
| M5Stack Core ESP32 | 网关与监测屏 | 接收电脑 USB 串口 EEG 帧，显示状态，通过 ESP-NOW 转发给 Microduino |
| Microduino Core ESP32 | 执行控制器 | 接收 M5Stack 的 ESP-NOW EEG 包，控制灯光、步进电机和继电器 |
| DMX 灯具 | 光效输出 | 接收 DMX512 数据，输出 RGBW 光效 |
| 步进电机驱动器 | 运动输出 | 接收 STEP/DIR/EN 信号，驱动步进电机 |
| 继电器模块 | 开关输出 | 控制造雾机、风扇、电磁阀或其他开关型设备 |

### 2.2 推荐通信结构

当前主方案：

```text
电脑 -> M5Stack: USB Serial, 115200 baud, 文本 EEG 帧
M5Stack -> Microduino: ESP-NOW fixed peer, 结构化 EEG 包
```

M5Stack 需要知道 Microduino 的 Wi-Fi MAC，并使用固定 ESP-NOW peer。正式运行时不建议长期使用 broadcast。

### 2.3 为什么使用 M5Stack 网关 + ESP-NOW

电脑不能直接稳定发送 ESP-NOW，但可以稳定通过 USB 串口连接 M5Stack。M5Stack 和 Microduino 都是 ESP32，适合用 ESP-NOW 发送小型状态包。

优点：

- 电脑到 M5Stack 是有线 USB，避开现场 Wi-Fi 干扰。
- ESP-NOW 不依赖热点、路由器或电脑无线网卡。
- M5Stack 有屏幕，可同时作为现场监测仪表盘。
- Microduino 专注执行控制和安全逻辑。

限制：

- ESP-NOW 仍然是 2.4GHz，现场极端干扰下仍可能丢包。
- M5Stack 成为关键网关，掉线时 Microduino 必须进入安全状态。
- M5Stack 屏幕刷新不能阻塞串口接收和 ESP-NOW 转发。

## 3. 数据源与脑电协议

### 3.1 当前脑电数据格式

电脑从蓝牙 COM 口读到的是 ThinkGear 原始二进制包。

用户提供的样例：

```text
AA AA 20 02 C8 83 18 00 00 1B 00 00 1B 00 00 16 00 00 24 00 00 25 00 00 38 00 00 2A 00 00 21 04 00 05 00 79
```

解析结果：

| 字段 | 值 |
| --- | ---: |
| packet header | `AA AA` |
| payload length | `0x20` = 32 |
| poorSignal | `0xC8` = 200 |
| attention | 0 |
| meditation | 0 |
| delta | 27 |
| theta | 27 |
| lowAlpha | 22 |
| highAlpha | 36 |
| lowBeta | 37 |
| highBeta | 56 |
| lowGamma | 42 |
| midGamma | 33 |
| checksum | `0x79` |

其中 `poorSignal = 200` 表示信号很差，通常意味着电极接触不好或还没有戴稳。此时 `attention = 0` 和 `meditation = 0` 是合理现象。

### 3.2 ThinkGear 包解析规则

包结构：

```text
AA AA LEN PAYLOAD CHECKSUM
```

校验规则：

```text
checksum = bitwise_not(sum(payload bytes) & 0xFF)
```

常用 code：

| Code | 含义 | 长度 |
| --- | --- | --- |
| `0x02` | poorSignal | 1 |
| `0x04` | attention | 1 |
| `0x05` | meditation | 1 |
| `0x83` | EEG Power | 24 |

EEG Power 包含 8 个 24-bit 大端整数：

```text
delta, theta, lowAlpha, highAlpha, lowBeta, highBeta, lowGamma, midGamma
```

### 3.3 电脑端输出统一 EEG 帧

第一阶段推荐电脑端把 ThinkGear 原始包解析为文本 CSV，再通过 USB Serial 发送给 M5Stack。

格式：

```text
EEG,seq,timeMs,poorSignal,attention,meditation,delta,theta,lowAlpha,highAlpha,lowBeta,highBeta,lowGamma,midGamma
```

示例：

```text
EEG,1024,35120,200,0,0,27,27,22,36,37,56,42,33
```

字段意义：

- `seq`：递增序号，用于判断丢包和乱序。
- `timeMs`：电脑端程序启动后的毫秒数。
- `poorSignal`：信号质量，0 最好，200 很差。
- `attention`：专注度，通常 0-100。
- `meditation`：放松度，通常 0-100。
- 8 个频段值用于后续更丰富的灯光和运动映射。

## 4. 电脑端桥接程序设计

### 4.1 职责

电脑端程序负责：

- 打开脑电蓝牙 COM 口。
- 持续读取 ThinkGear 原始字节流。
- 解析合法 ThinkGear 包。
- 生成统一 EEG 状态。
- 以固定频率通过 USB Serial 发送给 M5Stack。
- 输出调试日志。
- 可选保存 CSV 日志文件，方便回放和分析。

电脑端不负责：

- 直接控制继电器。
- 直接生成步进电机脉冲。
- 决定任何危险动作。

危险动作必须由 Microduino 端根据本地安全规则执行。

### 4.2 推荐命令参数

```text
--source COM3
--source-baud 57600
--target COM6
--target-baud 115200
--send-rate 20
--log-file logs/eeg-session.csv
```

### 4.3 发送频率

推荐：

```text
10 Hz 到 20 Hz
```

也就是每 50-100 ms 发送一次最新 EEG 状态。

电脑端可以持续读取脑电原始字节，但不必每个字节都发给 M5Stack。推荐维护 `latestEegState`，然后按固定频率发送最新状态。

### 4.4 电脑端日志

建议打印：

```text
source port opened
target serial port
target baud
raw bytes
valid packets
checksum errors
sent serial frames
latest poorSignal / attention / meditation
last error
```

建议保存 CSV：

```text
timeMs,seq,poorSignal,attention,meditation,delta,theta,lowAlpha,highAlpha,lowBeta,highBeta,lowGamma,midGamma
```

## 5. Microduino 执行控制器设计

### 5.1 职责

Microduino 是现场执行控制器。

职责：

- 初始化 ESP-NOW 接收端。
- 接收 M5Stack 转发的 ESP-NOW EEG 包。
- 校验 EEG 包的 `magic / version / size / checksum`。
- 根据 `seq` 判断重复包、乱序包和丢包。
- 保存最近有效 EEG 状态。
- 控制 DMX 灯光。
- 控制步进电机。
- 控制继电器。
- 实现所有安全超时和保护。
- 可选向 M5Stack 发送执行状态。

### 5.2 主循环结构

Microduino 主循环必须非阻塞。

推荐结构：

```text
loop:
  receiveUdp()
  updateEegState()
  updateLightController()
  updateStepperController()
  updateRelayController()
  sendMonitorStatus()
  printDebugStatus()
```

不要在主循环里使用长时间 `delay()`。灯光、电机、继电器都用 `millis()` 定时。

### 5.3 安全超时

如果超过 2-3 秒没有收到有效 EEG 数据：

```text
Microduino 进入安全状态
灯光进入待机或渐暗
步进电机停止
继电器关闭或进入冷却
串口输出 EVENT=EEG_TIMEOUT
M5Stack 显示红色断线状态
```

### 5.4 ESP-NOW 包处理

Microduino 接收 ESP-NOW EEG 包后：

```text
检查 magic / version / size
检查 checksum
检查数值范围
检查 seq 是否重复或倒退
更新 dropCount
更新 latestEegState
更新 lastEegMs
```

无效包不能影响当前控制状态。

## 6. M5Stack 监测屏设计

### 6.1 职责

M5Stack 是 USB 串口网关和现场监测设备。

职责：

- 通过 USB 串口接收电脑发来的 `EEG` 文本帧。
- 解析并显示 EEG 数据。
- 通过 ESP-NOW fixed peer 转发 EEG 包给 Microduino。
- 显示 EEG 数据。
- 显示串口和 ESP-NOW 链路状态。
- 显示丢包和数据年龄。
- 显示 Microduino 执行状态。

M5Stack 不直接控制灯光、电机或继电器。M5Stack 掉线时，Microduino 必须依靠本地超时保护进入安全状态。

### 6.2 推荐显示内容

顶部状态栏：

```text
USB serial receiving / timeout
ESP-NOW send ok / fail
data age
```

脑电核心区：

```text
poorSignal
attention
meditation
```

频段区：

```text
delta
theta
alpha = lowAlpha + highAlpha
beta = lowBeta + highBeta
gamma = lowGamma + midGamma
```

执行状态区：

```text
light mode
light level
stepper state
relay state
timeout flag
drop count
```

### 6.3 显示颜色规则

```text
data age < 500 ms: green
500 ms <= data age < 2000 ms: yellow
data age >= 2000 ms: red
poorSignal <= 50: green
50 < poorSignal <= 120: yellow
poorSignal > 120: red
relay ON: high visibility warning color
```

### 6.4 刷新策略

M5Stack 屏幕建议 5-10 Hz 局部刷新。

避免整屏闪烁：

- 固定布局。
- 只刷新数值区域。
- 数字变化时清除旧值区域。
- 不在高频循环里整屏 `fillScreen`。

## 7. DMX512 灯光系统

### 7.1 硬件链路

```text
Microduino GPIO26 TX
  -> MAX485 / RS485 模块 DI
  -> DMX+ / DMX-
  -> DMX 灯具 1
  -> DMX 灯具 2
```

如果使用只发送 DMX 的场景：

```text
RO/RX 可以不接
DE 和 RE 根据模块要求接到发送使能
```

当前代码原型中 `DMX_TX_PIN = 26`。

### 7.2 DMX 地址规划

两盏 RGBW 灯建议：

| 灯具 | 起始地址 | 通道 |
| --- | ---: | --- |
| 灯 1 | 001 | R/G/B/W |
| 灯 2 | 005 | R/G/B/W |

通道表：

```text
001 light1 R
002 light1 G
003 light1 B
004 light1 W
005 light2 R
006 light2 G
007 light2 B
008 light2 W
```

### 7.3 灯光映射

第一版建议：

```text
attention -> 亮度 / 红色强度
meditation -> 蓝紫色强度
poorSignal -> 是否接受当前帧
alpha / beta / gamma -> 后续动态效果
```

信号规则：

```text
poorSignal > 120:
  不使用当前 EEG 更新目标值

poorSignal > 180:
  进入弱反馈或待机状态
```

### 7.4 平滑处理

避免 EEG 数值抖动造成灯光闪烁。

推荐低通滤波：

```text
current = current * 0.85 + target * 0.15
```

灯光刷新：

```text
25-40 ms 一次
```

### 7.5 DMX 技术问题

需要确认：

- DMX 灯具通道模式是否为 RGBW 4 通道。
- 灯具起始地址是否正确。
- DMX 线是否为双绞屏蔽线。
- 长线末端是否需要 120 欧终端电阻。
- MAX485 模块供电和逻辑电平是否兼容。
- Microduino、MAX485 和灯具控制地是否处理得当。

## 8. 步进电机系统

### 8.1 推荐硬件

建议使用独立步进驱动器：

```text
A4988 / DRV8825 / TB6600 / TMC 系列
```

Microduino 只输出控制信号：

```text
STEP
DIR
EN
GND
```

不要直接用 ESP32 引脚驱动电机。

### 8.2 引脚规划

建议预留：

```text
STEP_PIN
DIR_PIN
EN_PIN
LIMIT_MIN_PIN
LIMIT_MAX_PIN
```

实际引脚需要结合 Microduino 可用 GPIO、DMX 占用 GPIO26、继电器占用情况统一规划。

### 8.3 控制原则

- 非阻塞控制。
- 不在 ESP-NOW 接收回调里直接跑电机。
- 使用速度、加速度限制。
- 必须有停止状态。
- 必须有最大行程或限位开关。

### 8.4 推荐状态机

```text
IDLE
HOMING
BREATHING
MOVE_FORWARD
MOVE_BACKWARD
STOPPING
FAULT
```

### 8.5 EEG 映射建议

第一版不要让 EEG 直接映射步数。

建议映射为模式：

```text
attention 高且稳定 -> 缓慢推进
meditation 高且稳定 -> 缓慢呼吸式往复
poorSignal 差 -> 停止
数据超时 -> 停止
```

### 8.6 步进电机技术问题

必须确认：

- 电机额定电流。
- 驱动器电流限流设置。
- 电机电源电压。
- 电机运动机构是否有卡死风险。
- 是否有物理限位。
- 如果没有限位，软件行程如何标定。
- 断电后机构是否会滑动。
- 观众是否可能触碰运动部件。

## 9. 继电器系统

### 9.1 控制对象

继电器可能控制：

```text
造雾机
风扇
电磁阀
水泵
其他开关型设备
```

继电器是高风险模块，必须保守设计。

### 9.2 基本规则

- EEG 瞬时值不能直接开关继电器。
- 必须使用持续时间阈值。
- 必须设置最大开启时间。
- 必须设置冷却时间。
- 数据超时必须关闭或进入冷却。
- 上电默认关闭。

### 9.3 推荐状态机

```text
OFF
ARMING
ON
COOLDOWN
FAULT
```

示例：

```text
attention > 75 持续 3 秒:
  OFF -> ARMING -> ON

ON 超过 2 秒:
  ON -> COOLDOWN

COOLDOWN 超过 10 秒:
  COOLDOWN -> OFF

数据超时:
  ON -> COOLDOWN 或 OFF
```

### 9.4 继电器技术问题

必须确认：

- 继电器模块是高电平触发还是低电平触发。
- 继电器模块是否有光耦隔离。
- 负载电压和电流是否在继电器额定范围内。
- 感性负载是否需要续流二极管、RC 吸收或浪涌保护。
- 高压交流负载必须做好绝缘和外壳保护。
- 观众不能触碰任何高压端子。

## 10. 电源与接地设计

### 10.1 电源分区

建议分区：

```text
逻辑电源：ESP32 / M5Stack / 小模块
灯光电源：DMX 灯具
电机电源：步进电机驱动器
继电器负载电源：造雾机 / 风扇 / 电磁阀等
```

不要让电机和继电器负载与 ESP32 共用不合适的小电源。

### 10.2 共地原则

低压控制信号通常需要共地：

```text
Microduino GND
MAX485 GND
步进驱动器 GND
继电器控制侧 GND
```

但高压负载侧不能随意与低压侧混接。继电器负载侧应按电气安全规范独立处理。

### 10.3 抗干扰

建议：

- 电机线、继电器负载线远离 ESP32 天线和信号线。
- ESP32 供电加足够滤波电容。
- 电机电源和逻辑电源分开。
- 继电器控制感性负载时加吸收保护。
- ESP32 天线附近不要有金属遮挡。

### 10.4 电源技术问题

必须确认：

- 每个模块的供电电压。
- 每路电源最大电流。
- 是否有足够余量。
- 是否有保险丝或限流保护。
- 是否有统一电源开关。
- 是否有紧急断电方案。
- 上电顺序是否会导致误动作。

## 11. ESP-NOW 通信与现场干扰

### 11.1 ESP-NOW 可能受影响

ESP-NOW 仍然工作在 2.4GHz，因此现场无线环境仍可能影响链路，尤其是：

- 人多。
- 手机热点多。
- 路由器多。
- 蓝牙设备多。
- 金属结构多。
- M5Stack 和 Microduino 距离较远。
- 电机和继电器产生电磁噪声。

### 11.2 对策

推荐：

- M5Stack 与 Microduino 距离控制在 1-3 米。
- M5Stack 和 Microduino 天线远离金属和强电线。
- 使用固定发送频率 10-20 Hz。
- ESP-NOW 包带 `seq`。
- ESP-NOW 使用固定 peer MAC 和固定 channel。
- Microduino 有 2-3 秒安全超时。
- M5Stack 显示串口数据年龄、ESP-NOW 发送失败数和最近发送状态。
- 现场提前跑 30-60 分钟稳定性测试。

### 11.3 备选方案

如果现场 ESP-NOW 仍然不稳定：

| 方案 | 说明 |
| --- | --- |
| 电脑 USB 串口直连 Microduino | 最稳，但 M5Stack 只能旁路显示 |
| 电脑 USB-RS485 到 Microduino | 适合距离更远、干扰更强的现场 |
| 独立 ESP32 网关 | 与 M5Stack 网关类似，但不承担显示 |
| Wi-Fi UDP | 保留为调试方案，不作为关键执行链路首选 |

## 12. 安全策略

### 12.1 全局安全状态

全局安全状态触发条件：

```text
EEG 数据超时
poorSignal 长时间过差
ESP-NOW 包持续丢失
电脑桥接程序退出
Microduino 解析错误过多
电机限位触发
继电器超过最大开启时间
手动急停触发
```

安全状态动作：

```text
灯光进入待机或渐暗
步进电机停止
继电器关闭或冷却
M5Stack 显示红色故障状态
串口输出明确日志
```

### 12.2 急停

正式装置建议保留独立急停：

- 切断电机电源。
- 切断继电器负载电源。
- 不只依赖软件。

### 12.3 上电默认状态

上电后：

```text
灯光关闭或低亮度待机
步进电机 disabled
继电器 OFF
等待有效 EEG 数据
等待串口和 ESP-NOW 链路稳定
```

## 13. 软件状态机总览

### 13.1 EEG 状态

```text
NO_DATA
SIGNAL_BAD
SIGNAL_OK
TIMEOUT
```

### 13.2 网络状态

```text
SERIAL_WAITING
SERIAL_READY
ESPNOW_READY
RECEIVING
STALE
```

### 13.3 灯光状态

```text
LIGHT_OFF
LIGHT_IDLE
LIGHT_EEG_BLEND
LIGHT_SIGNAL_BAD
LIGHT_TIMEOUT
```

### 13.4 电机状态

```text
STEPPER_DISABLED
STEPPER_IDLE
STEPPER_MOVING
STEPPER_BREATHING
STEPPER_LIMITED
STEPPER_FAULT
```

### 13.5 继电器状态

```text
RELAY_OFF
RELAY_ARMING
RELAY_ON
RELAY_COOLDOWN
RELAY_FAULT
```

## 14. 调试与测试计划

### 14.1 阶段 A：脑电数据确认

目标：

```text
电脑能稳定从蓝牙 COM 口读取 ThinkGear 原始包
```

检查：

- 是否持续出现 `AA AA` 包头。
- checksum 是否通过。
- `poorSignal` 是否能随着佩戴改善而下降。
- `attention` 和 `meditation` 是否能出现非零值。

### 14.2 阶段 B：M5Stack 网关链路确认

目标：

```text
电脑 -> USB Serial -> M5Stack
M5Stack -> ESP-NOW -> Microduino
```

检查：

- M5Stack 是否显示 EEG 帧。
- M5Stack 是否成功发送 ESP-NOW 包。
- Microduino 是否收到 EEG 包。
- `seq` 是否连续。
- 丢包率是否可接受。

### 14.3 阶段 C：灯光确认

目标：

```text
EEG 数据影响 DMX 灯光
```

检查：

- DMX 地址正确。
- 灯具颜色通道正确。
- 灯光没有明显闪烁。
- 信号差时不乱跳。
- 超时时进入安全状态。

### 14.4 阶段 D：继电器确认

目标：

```text
继电器只在满足稳定条件时动作
```

检查：

- 上电默认 OFF。
- 阈值持续时间有效。
- 最大开启时间有效。
- 冷却时间有效。
- 超时关闭有效。

### 14.5 阶段 E：步进电机确认

目标：

```text
步进电机非阻塞、安全、可停止
```

检查：

- 速度限制有效。
- 加速度限制有效。
- 限位有效。
- 超时停止有效。
- 不影响 ESP-NOW 接收和灯光刷新。

### 14.6 整机老化测试

正式部署前建议：

```text
连续运行 30 分钟
连续运行 60 分钟
模拟电脑断线
模拟脑电断线
模拟 ESP-NOW 干扰或拉远距离
模拟 poorSignal 长时间很差
模拟继电器频繁触发条件
模拟电机卡住或限位触发
```

## 15. 现场部署检查清单

### 15.1 软件检查

- 电脑能识别脑电 COM 口。
- Python 桥接程序参数正确。
- Microduino 固件版本正确。
- M5Stack 固件版本正确。
- ESP-NOW peer MAC 和 channel 一致。
- 串口日志正常。
- M5Stack 显示数据年龄和丢包数。

### 15.2 硬件检查

- Microduino 供电稳定。
- M5Stack 电量或供电稳定。
- MAX485 接线正确。
- DMX 灯地址正确。
- 步进驱动器电流设置正确。
- 继电器负载接线安全。
- 所有低压控制地按设计连接。
- 高压部分有外壳和绝缘。

### 15.3 安全检查

- 急停有效。
- 继电器上电默认 OFF。
- 电机上电默认 disabled。
- 断开电脑到 M5Stack 串口或中断 ESP-NOW 后 2-3 秒内进入安全状态。
- 电脑程序退出后系统进入安全状态。
- 观众无法触碰高压和运动危险区域。

## 16. 风险表

| 风险 | 影响 | 对策 |
| --- | --- | --- |
| 脑电 poorSignal 长期过高 | 数据不可用 | M5 显示警告，Microduino 忽略控制更新 |
| ESP-NOW 丢包 | 控制状态更新变慢 | seq、dropCount、超时保护 |
| 电脑桥接程序退出 | 无新数据 | Microduino 超时安全状态 |
| DMX 接线错误 | 灯不亮或颜色错误 | 单灯测试、地址表、通道表 |
| 电机卡死 | 机械损坏或过热 | 限位、限流、最大运行时间 |
| 继电器误触发 | 设备突然启动 | 状态机、持续阈值、冷却时间 |
| 电源噪声 | ESP32 重启或数据异常 | 分电源、滤波、线缆隔离 |
| 现场人员触碰 | 人身风险 | 外壳、防护、急停、低压隔离 |

## 17. 推荐实施顺序

第一步：

```text
脑电 COM -> Python 解析 -> 控制台打印 EEG
```

第二步：

```text
Python -> USB Serial -> M5Stack 显示 EEG
```

第三步：

```text
M5Stack -> ESP-NOW -> Microduino 串口打印 EEG
```

第四步：

```text
Microduino -> DMX 灯光简单映射
```

第五步：

```text
Microduino -> M5Stack 回传执行状态，可选
```

第六步：

```text
继电器状态机
```

第七步：

```text
步进电机状态机
```

第八步：

```text
整机联调和现场老化测试
```

## 18. 文件规划

原型阶段：

```text
tools/eeg_serial_bridge.py
src/microduino_core_esp32_test/main.cpp
src/m5stack_core_esp32_test/main.cpp
```

正式阶段建议拆分：

```text
tools/
  eeg_udp_bridge.py
  eeg_udp_test_sender.py
  eeg_log_replay.py

src/microduino_eeg_controller/
  main.cpp
  eeg_protocol.h
  eeg_protocol.cpp
  wifi_udp_receiver.h
  wifi_udp_receiver.cpp
  dmx_lights.h
  dmx_lights.cpp
  stepper_controller.h
  stepper_controller.cpp
  relay_controller.h
  relay_controller.cpp
  safety_manager.h
  safety_manager.cpp

src/m5stack_eeg_monitor/
  main.cpp
  eeg_protocol.h
  eeg_protocol.cpp
  wifi_monitor_receiver.h
  wifi_monitor_receiver.cpp
  monitor_ui.h
  monitor_ui.cpp
```

## 19. 当前技术结论

当前最合理的技术路线：

```text
脑电设备通过蓝牙把 ThinkGear 原始数据发给电脑
电脑从 COM 口读取并解析 ThinkGear
电脑以 EEG CSV 协议通过 USB Serial 发给 M5Stack
M5Stack 显示 EEG 和链路状态，并通过 ESP-NOW 转发给 Microduino
Microduino 接收 ESP-NOW EEG 包并控制灯光、步进电机和继电器
Microduino 本地实现所有安全超时和状态机
```

第一版最小闭环：

```text
脑电 COM -> Python -> USB Serial -> M5Stack 显示
M5Stack -> ESP-NOW -> Microduino 串口打印
Microduino -> DMX 灯光简单变化
```

继电器和步进电机必须在无线链路和灯光输出稳定后再接入。
