# Dream 脑电转发到 Microduino 技术方案

更新时间：2026-05-19  
当前决策：电脑通过 USB 串口直连 M5Stack，M5Stack 通过 ESP-NOW 转发给 Microduino。  
适用范围：脑电数据从电脑进入装置、M5Stack 监测显示、Microduino 执行控制。

## 1. 当前结论

推荐主链路：

```text
脑电设备
  -- Bluetooth -->
电脑
  -- COM 口读取 ThinkGear 原始数据 -->
电脑端桥接程序
  -- USB Serial -->
M5Stack Core ESP32
  -- ESP-NOW -->
Microduino Core ESP32
  --> DMX512 灯光
  --> 步进电机
  --> 继电器
```

M5Stack 同时承担两个角色：

```text
1. 电脑 USB 串口网关
2. 现场监测屏
```

Microduino 只承担执行控制角色：

```text
接收 ESP-NOW EEG 帧
维护本地安全状态
控制灯光 / 步进电机 / 继电器
```

这个方案比电脑直接 Wi-Fi UDP 到 Microduino 更适合现场人多的情况，因为电脑到 M5Stack 是有线 USB，M5Stack 到 Microduino 使用 ESP-NOW 短包，不依赖热点、路由器或电脑无线网卡。

## 2. 方案可行性

该方案可行，而且适合当前硬件条件。

优点：

- 电脑到 M5Stack 是 USB 串口，稳定、低延迟、抗现场无线干扰。
- M5Stack 有屏幕，可以直接显示脑电、数据年龄、ESP-NOW 状态和丢包计数。
- ESP-NOW 适合 ESP32 与 ESP32 之间的小包低延迟通信。
- Microduino 端可以专注执行控制，不需要处理电脑串口和显示界面。
- 电脑不需要连接 Microduino 热点，也不需要现场 Wi-Fi。

限制：

- ESP-NOW 仍然工作在 2.4GHz，现场极端干扰下仍可能丢包。
- M5Stack 成为关键网关，M5Stack 掉线时 Microduino 会收不到数据。
- M5Stack 需要同时处理串口、显示和 ESP-NOW，屏幕刷新必须避免阻塞。
- Microduino 必须有本地超时保护，不能依赖 M5Stack 保证安全。

安全结论：

```text
M5Stack 负责转发和显示
Microduino 负责安全
```

## 3. 设备角色

| 设备 | 统一角色名 | 职责 |
| --- | --- | --- |
| 脑电设备 | `eegDevice` | 蓝牙发送 ThinkGear 原始脑电数据到电脑 |
| 电脑 | `pcBridge` | 读取脑电 COM，解析 ThinkGear，输出统一 EEG 帧到 M5Stack |
| M5Stack | `m5GatewayMonitor` | USB 串口接收 EEG，屏幕显示，ESP-NOW 转发给 Microduino |
| Microduino | `micController` | ESP-NOW 接收 EEG，控制灯光、电机、继电器 |
| DMX 灯具 | `dmxLights` | 光效输出 |
| 步进电机 | `stepperMotor` | 运动输出 |
| 继电器 | `relayOutput` | 开关输出 |

## 4. 数据路径

### 4.1 脑电到电脑

电脑从蓝牙 COM 口读取 ThinkGear 原始包。

包结构：

```text
AA AA LEN PAYLOAD CHECKSUM
```

用户提供的样例：

```text
AA AA 20 02 C8 83 18 00 00 1B 00 00 1B 00 00 16 00 00 24 00 00 25 00 00 38 00 00 2A 00 00 21 04 00 05 00 79
```

该包可解析出：

```text
poorSignal = 200
attention = 0
meditation = 0
delta = 27
theta = 27
lowAlpha = 22
highAlpha = 36
lowBeta = 37
highBeta = 56
lowGamma = 42
midGamma = 33
```

### 4.2 电脑到 M5Stack

电脑端把 ThinkGear 原始包解析成统一文本帧，通过 USB Serial 发给 M5Stack。

推荐串口参数：

```text
baud: 115200
line ending: \n
encoding: ASCII / UTF-8 safe subset
```

统一 EEG 文本帧：

```text
EEG,seq,timeMs,poorSignal,attention,meditation,delta,theta,lowAlpha,highAlpha,lowBeta,highBeta,lowGamma,midGamma
```

示例：

```text
EEG,1024,35120,200,0,0,27,27,22,36,37,56,42,33
```

电脑端职责：

- 读取脑电 COM。
- 解析 ThinkGear。
- 校验 checksum。
- 生成 `EEG` 文本帧。
- 定时发送给 M5Stack。
- 可选保存 CSV 日志。

电脑端不负责：

- 直接控制灯光。
- 直接控制继电器。
- 直接控制步进电机。

### 4.3 M5Stack 到 Microduino

M5Stack 收到 `EEG` 文本帧后：

```text
解析 EEG 文本帧
更新本地显示状态
转换为 ESP-NOW 二进制结构体
发送给 Microduino
```

ESP-NOW 推荐使用固定 peer MAC，不建议长期使用 broadcast。

推荐：

```text
ESP-NOW channel: 固定，例如 1 或现场测试后选择
peer: Microduino Wi-Fi MAC
encrypt: 原型阶段 false，正式阶段可评估
send mode: fixed peer unicast
```

### 4.4 Microduino 执行控制

Microduino 收到 ESP-NOW EEG 包后：

```text
校验 magic / version / length
检查 seq
更新 latestEegFrame
更新 lastEegFrameMs
更新 dropCount
刷新安全状态
驱动灯光 / 电机 / 继电器
```

Microduino 如果超过 2-3 秒没有收到有效 EEG 包，必须进入安全状态。

## 5. 协议设计

### 5.1 PC 到 M5Stack 串口文本帧

文本帧：

```text
EEG,seq,timeMs,poorSignal,attention,meditation,delta,theta,lowAlpha,highAlpha,lowBeta,highBeta,lowGamma,midGamma
```

字段：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `seq` | uint32 | 递增序号 |
| `timeMs` | uint32 | 电脑端相对时间 |
| `poorSignal` | uint8 | 0 最好，200 很差 |
| `attention` | uint8 | 专注度 0-100 |
| `meditation` | uint8 | 放松度 0-100 |
| `delta` | uint32 | Delta |
| `theta` | uint32 | Theta |
| `lowAlpha` | uint32 | 低 Alpha |
| `highAlpha` | uint32 | 高 Alpha |
| `lowBeta` | uint32 | 低 Beta |
| `highBeta` | uint32 | 高 Beta |
| `lowGamma` | uint32 | 低 Gamma |
| `midGamma` | uint32 | 中 Gamma |

### 5.2 M5Stack 到 Microduino ESP-NOW 包

推荐结构：

```cpp
struct DreamEegEspNowPacket {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint32_t seq;
  uint32_t timeMs;
  uint32_t m5UptimeMs;
  uint8_t poorSignal;
  uint8_t attention;
  uint8_t meditation;
  uint8_t flags;
  uint32_t eegPower[8];
  uint16_t checksum;
};
```

推荐 magic：

```text
0x44524541
```

含义：

```text
DREA
```

`flags` 建议：

| bit | 名称 | 说明 |
| --- | --- | --- |
| 0 | `EEG_FLAG_SIGNAL_OK` | `poorSignal` 可接受 |
| 1 | `EEG_FLAG_SOURCE_TIMEOUT` | M5Stack 长时间没有收到电脑数据 |
| 2 | `EEG_FLAG_CHECKSUM_OK` | 电脑端 ThinkGear 校验通过 |

### 5.3 M5Stack 本地显示状态

M5Stack 不需要从 Microduino 回传才能显示基础脑电状态。

第一阶段显示：

```text
USB serial connected / receiving
last EEG age
seq
poorSignal
attention
meditation
delta / theta / alpha / beta / gamma
ESP-NOW send ok / fail
send count
fail count
```

第二阶段可让 Microduino 通过 ESP-NOW 回传执行状态给 M5Stack：

```text
lightMode
lightLevel
stepperState
relayState
safetyState
micRxCount
micDropCount
```

## 6. 频率与延迟

建议电脑端向 M5Stack 发送频率：

```text
10 Hz 到 20 Hz
```

建议 M5Stack 向 Microduino 转发频率：

```text
10 Hz 到 20 Hz
```

屏幕刷新频率：

```text
5 Hz 到 10 Hz
```

原则：

- 串口接收和 ESP-NOW 转发优先于屏幕刷新。
- 屏幕使用局部刷新，不能整屏高频刷新。
- Microduino 控制灯光可以 25-40 ms 刷新一次，但 EEG 目标值用最近一次有效数据。

## 7. 丢包与超时策略

### 7.1 M5Stack 端

M5Stack 需要统计：

```text
serialFrameCount
serialParseErrorCount
espNowSendCount
espNowSendFailCount
lastSerialFrameMs
lastEspNowSendMs
```

如果电脑串口超过 2 秒没有新 EEG：

```text
M5Stack 显示 SOURCE TIMEOUT
M5Stack 可以继续发送带 timeout flag 的包
Microduino 收到 timeout flag 后进入安全状态
```

### 7.2 Microduino 端

Microduino 需要统计：

```text
espNowRxCount
espNowDropCount
lastEegFrameMs
lastSeq
eegTimedOut
```

如果超过 2-3 秒没有有效 EEG 包：

```text
灯光进入待机或渐暗
步进电机停止
继电器关闭或进入冷却
串口输出 EVENT=EEG_TIMEOUT
```

## 8. M5Stack 屏幕布局

建议布局：

```text
顶部：USB / ESP-NOW / 数据年龄 / 发送失败数
中部：poorSignal / attention / meditation
中下：delta / theta / alpha / beta / gamma
底部：Microduino 状态 / 灯光 / 电机 / 继电器 / 安全状态
```

颜色规则：

| 条件 | 显示 |
| --- | --- |
| 数据年龄 < 500 ms | 绿色 |
| 数据年龄 500-2000 ms | 黄色 |
| 数据年龄 > 2000 ms | 红色 |
| `poorSignal <= 50` | 绿色 |
| `poorSignal 51-120` | 黄色 |
| `poorSignal > 120` | 红色 |
| ESP-NOW 连续发送失败 | 红色提示 |
| 继电器 ON | 高可见警示 |

## 9. Microduino 控制策略

Microduino 端原则：

- 不信任单个数据包。
- 继电器和电机都必须走状态机。
- `poorSignal` 差时忽略控制更新。
- 数据超时进入安全状态。
- 不能因为 M5Stack 掉线而保持危险动作。

灯光：

```text
attention -> 亮度 / 暖色
meditation -> 蓝紫色 / 柔和程度
poorSignal -> 是否接受当前帧
```

继电器：

```text
OFF -> ARMING -> ON -> COOLDOWN -> OFF
```

步进电机：

```text
DISABLED -> IDLE -> MOVING / BREATHING -> STOPPING -> IDLE
```

## 10. 实施阶段

### 阶段 A：电脑到 M5Stack 串口

目标：

```text
脑电 COM -> 电脑 Python -> USB Serial -> M5Stack 串口打印
```

完成标志：

```text
M5Stack 能显示 poorSignal / attention / meditation
```

### 阶段 B：M5Stack ESP-NOW 到 Microduino

目标：

```text
M5Stack -> ESP-NOW -> Microduino 串口打印
```

完成标志：

```text
Microduino 能收到 seq 连续的 EEG 包
```

### 阶段 C：链路监测

目标：

```text
M5Stack 显示串口状态、ESP-NOW 发送状态、失败数、数据年龄
Microduino 输出接收数、丢包数、超时状态
```

### 阶段 D：DMX 灯光

目标：

```text
EEG -> M5Stack -> ESP-NOW -> Microduino -> DMX 灯光
```

### 阶段 E：继电器与步进电机

目标：

```text
在链路稳定后加入继电器安全状态机和步进电机非阻塞状态机
```

## 11. 测试清单

必须测试：

- 电脑停止发送串口数据。
- M5Stack 拔掉 USB。
- M5Stack 与 Microduino 拉远距离。
- ESP-NOW 连续发送失败。
- Microduino 收到重复 seq。
- Microduino 收到跳号 seq。
- `poorSignal` 长时间大于 120。
- M5Stack 屏幕高频刷新时是否影响 ESP-NOW。
- 运行 30-60 分钟是否稳定。

预期：

```text
灯光不会乱闪
继电器不会误触发
步进电机不会失控
Microduino 2-3 秒内进入安全状态
M5Stack 明确显示链路异常
```

## 12. 备选方案

如果 ESP-NOW 在现场仍然不稳定：

| 方案 | 说明 |
| --- | --- |
| 电脑 USB 直连 Microduino | 最稳，但 M5Stack 只能旁路显示 |
| 电脑 USB-RS485 到 Microduino | 适合距离更远、干扰更强的场地 |
| 独立 ESP32 网关 | 与当前 M5Stack 网关类似，但不承担显示 |
| Wi-Fi UDP | 保留为调试方案，不作为关键执行链路首选 |

## 13. 当前最终决策

当前通信主方案确定为：

```text
脑电蓝牙 -> 电脑
电脑解析 ThinkGear
电脑 USB Serial -> M5Stack
M5Stack 显示 EEG 和链路状态
M5Stack ESP-NOW -> Microduino
Microduino 本地安全状态机
Microduino 控制 DMX / 步进 / 继电器
```

核心原则：

```text
M5Stack 是网关和仪表盘
Microduino 是执行器和安全控制器
ESP-NOW 只传状态，不直接触发危险动作
```
