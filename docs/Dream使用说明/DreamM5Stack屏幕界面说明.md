# Dream M5Stack 屏幕界面说明

更新时间：2026-05-28

适用对象：M5Stack Core ESP32 网关屏幕  
对应固件：`src/m5stack_core_esp32_test/main.cpp`

## 1. M5Stack 在系统里的作用

M5Stack 是当前原型链路里的“USB 串口网关 + ESP-NOW 转发器 + 现场状态屏”。

它负责：

- 从电脑端 Python 桥接程序接收 `EEG` 脑电数据和 `CMD` 控制命令。
- 把脑电数据和控制命令通过 ESP-NOW 转发给 Microduino。
- 接收 Microduino 回传的执行状态。
- 在屏幕上显示电脑、ESP-NOW、Microduino 和系统安全状态。
- 用 A / B / C 三个实体按键发送系统开启、全部停止、系统关闭命令。

它不直接控制 DMX 灯光、继电器、雾机或步进电机。真正执行动作的是 Microduino。

## 2. 屏幕整体布局

M5Stack 屏幕大致按下面顺序显示：

```text
Dream M5 Gateway
USB:... age:... frames:... err:...
ESP:... tx:... fail:... ch:...
Signal:...
Att:... Med:...
seq:... pc:...ms
delta:... theta:... alpha:...
beta:... gamma:...
MIC:... age:... rx:... drop:...
Light:... ... Step:...
Relay:... Safety:... SYS:...
```

现场调试时，最关键先看三项：

- `USB` 是否为 `OK`。
- `MIC` 是否为 `OK`。
- `SYS` 是否能从 `OFF` 变成 `ON`。

## 3. 顶部标题

```text
Dream M5 Gateway
```

表示当前烧录的是 Dream M5Stack 网关固件。

如果屏幕没有出现这个标题，先检查：

- M5Stack 是否上电。
- 是否烧录了 `m5stack-core-esp32` 环境。
- 烧录后是否自动复位成功。

## 4. USB 行

示例：

```text
USB:OK age:120ms frames:356 err:0
```

这一行表示 M5Stack 从电脑 USB 串口接收脑电数据的状态。

| 字段 | 含义 |
| --- | --- |
| `USB:OK` | 最近 3 秒内收到电脑发来的 EEG 数据 |
| `USB:WAIT` | 还没有收到 EEG，或已经超过 3 秒没有新数据 |
| `age` | 距离上一帧电脑数据过去的时间，单位毫秒 |
| `frames` | M5Stack 已收到的 EEG 帧数量 |
| `err` | 串口文本解析失败次数 |

常见判断：

- `USB:WAIT` 且 `frames:0`：电脑端桥接程序可能没有启动，或目标串口不是 M5Stack。
- `USB:OK` 但 `err` 不断增加：电脑端发来的文本格式可能不符合 `EEG` / `CMD` 协议。
- `USB:OK` 但网页没有脑电值：可能脑电源端没有有效 ThinkGear 包，电脑端没有产生真实 EEG 帧。

## 5. ESP 行

示例：

```text
ESP:OK tx:356 fail:0 ch:1
```

这一行表示 M5Stack 的 ESP-NOW 转发状态。

| 字段 | 含义 |
| --- | --- |
| `ESP:OK` | ESP-NOW 已初始化，并且最近发送成功 |
| `ESP:SEND` | ESP-NOW 已打开，但最近一次发送状态不是成功 |
| `ESP:OFF` | ESP-NOW 没有准备好或初始化失败 |
| `tx` | M5Stack 已发送的 ESP-NOW 包数量，包括 EEG 和控制命令 |
| `fail` | ESP-NOW 发送失败次数 |
| `ch` | ESP-NOW 信道，当前为 `1` |

常见判断：

- `ESP:OFF`：M5Stack 的 ESP-NOW 初始化失败，需要重启 M5Stack 后再看串口日志。
- `fail` 持续增加：Microduino 可能未上电、未烧录当前固件、距离太远，或信道不一致。
- `tx` 增加但 `MIC:WAIT`：M5Stack 有发送，但没有收到 Microduino 状态回传。

## 6. Signal 行

示例：

```text
Signal: 35
```

`Signal` 显示 ThinkGear 的 `poorSignal`，数值越低越好。

| 数值范围 | 屏幕颜色 | 含义 |
| --- | --- | --- |
| `0-50` | 绿色 | 信号较好 |
| `51-120` | 黄色 | 信号一般 |
| `>120` | 红色 | 信号差 |
| `255` | 红色 | 还没有真实 EEG 数据 |

注意：`poorSignal` 是“差的程度”，不是信号强度，所以数值越小越好。

## 7. Att / Med 行

示例：

```text
Att: 72  Med: 44
```

| 字段 | 含义 |
| --- | --- |
| `Att` | attention，专注度，范围 `0-100` |
| `Med` | meditation，放松度，范围 `0-100` |

当前 Microduino 可根据这些值生成双灯自动流水。步进电机物理输出已启用于台架调试；继电器物理输出仍默认关闭。

## 8. seq / pc 行

示例：

```text
seq:356 pc:18240ms
```

| 字段 | 含义 |
| --- | --- |
| `seq` | 电脑端 Python 桥接程序生成的 EEG 帧序号 |
| `pc` | 电脑端运行时间戳，单位毫秒 |

`seq` 持续增加，说明电脑端正在持续向 M5Stack 发送 EEG 帧。

## 9. EEG Power 频段行

示例：

```text
delta:120 theta:88 alpha:340
beta:210 gamma:95
```

这些字段来自 ThinkGear 的 EEG Power 数据。

| 字段 | 含义 |
| --- | --- |
| `delta` | Delta 频段 |
| `theta` | Theta 频段 |
| `alpha` | `lowAlpha + highAlpha` |
| `beta` | `lowBeta + highBeta` |
| `gamma` | `lowGamma + midGamma` |

这些值主要用于观察脑电原始频段趋势，当前屏幕只做汇总显示。

## 10. MIC 行

示例：

```text
MIC:OK age:180ms rx:356 drop:0
```

这一行表示 M5Stack 有没有收到 Microduino 回传状态。

| 字段 | 含义 |
| --- | --- |
| `MIC:OK` | 最近 3 秒内收到 Microduino 状态回传 |
| `MIC:WAIT` | 还没收到，或 Microduino 状态已经超时 |
| `age` | 距离上一条 Microduino 状态过去的时间，单位毫秒 |
| `rx` | Microduino 收到的 EEG 包数量 |
| `drop` | Microduino 统计的丢包或校验错误数量 |

常见判断：

- `USB:OK` 但 `MIC:WAIT`：M5Stack 收到了电脑数据，但 Microduino 没有回传状态。
- `MIC:OK` 但 `rx:0`：Microduino 在线，但还没收到 EEG 包，可能电脑端还没有发送有效 EEG。
- `drop` 持续增加：ESP-NOW 包序号不连续，或校验失败，需要检查无线距离、供电和信道。

## 11. Light 行

示例：

```text
Light:EEG 120  Step:DISABLED
```

`Light` 显示 Microduino 当前灯光模式和亮度。

| 模式 | 含义 |
| --- | --- |
| `OFF` | 灯光关闭 |
| `IDLE` | 空闲状态 |
| `EEG` | 根据脑电数据自动控制灯光 |
| `BAD` | 脑电信号差，进入信号差灯光状态 |
| `TIMEOUT` | 长时间没有 EEG，进入超时安全状态 |
| `MANUAL` | 浏览器前端手动设置颜色 |
| `WAIT` | 没有收到有效 Microduino 状态 |

`Light` 后面的数字是当前灯光亮度级别，来自 Microduino 的 `lightLevel`。

## 12. Step 字段

同一行里的 `Step` 表示步进电机状态。

| 状态 | 含义 |
| --- | --- |
| `DISABLED` | 步进电机输出未启用 |
| `IDLE` | 空闲 |
| `MOVING` | 正在运动 |
| `BREATH` | 呼吸式运动 |
| `LIMIT` | 触发限位或到达限制 |
| `FAULT` | 故障 |
| `WAIT` | 没有收到有效 Microduino 状态 |

当前固件中 Microduino 的步进电机物理输出已启用于台架调试：

```cpp
#define DREAM_ENABLE_STEPPER_OUTPUT 1
```

系统关闭时，现场应看到 `Step:DISABLED`，步进电机保持停止 / 禁用。系统开启后，前端可选择 `左`、`右`、`左右` 目标发送步进命令；正式接入真实机械负载前，需要确认引脚、驱动器、限位、电源、方向、行程和机械安全。

## 13. Relay / Safety / SYS 行

示例：

```text
Relay:OFF  Safety:TIMEOUT  SYS:OFF
```

### Relay

`Relay` 表示继电器或雾机状态。

| 状态 | 含义 |
| --- | --- |
| `OFF` | 继电器关闭 |
| `ARMING` | 预备触发 |
| `ON` | 已打开 |
| `COOL` | 冷却中 |
| `FAULT` | 故障 |
| `WAIT` | 没有收到有效 Microduino 状态 |

当前固件中 Microduino 的继电器物理输出默认关闭：

```cpp
#define DREAM_ENABLE_RELAY_OUTPUT 0
```

因此现场大多数时候会看到 `Relay:OFF`。

### Safety

`Safety` 是 Microduino 本地安全状态。

| 状态 | 含义 |
| --- | --- |
| `NORMAL` | 正常 |
| `SIGNAL` | 脑电信号差 |
| `TIMEOUT` | 长时间没有收到 EEG，进入安全状态 |
| `ESTOP` | 急停 |
| `FAULT` | 故障 |
| `WAIT` | 没有收到有效 Microduino 状态 |

如果看到 `Safety:TIMEOUT`，说明 Microduino 在线，但没有持续收到有效 EEG 数据。

### SYS

`SYS` 是 Microduino 的系统总开关。

| 状态 | 含义 |
| --- | --- |
| `SYS:ON` | 系统已开启，允许 Microduino 执行自动 EEG 灯光、继电器状态机和自动机构逻辑 |
| `SYS:OFF` | 系统关闭，Microduino 保持安全输出 |

Microduino 上电后默认 `SYS:OFF`。必须通过前端点击 `系统开启`，或按 M5Stack A 键，才会进入 `SYS:ON`。

## 14. M5Stack 按键

| 按键 | 发送命令 | 含义 |
| --- | --- | --- |
| A | `SYSTEM_ENABLE` | 系统开启 |
| B | `ALL_STOP` | 全部停止 |
| C | `SYSTEM_DISABLE` | 系统关闭 |

按键命令同样通过 ESP-NOW 发给 Microduino。M5Stack 自己不直接控制灯光、电机或继电器。

## 15. 常见现场读屏结论

| 屏幕现象 | 可能原因 | 优先检查 |
| --- | --- | --- |
| `USB:WAIT` | 电脑端桥接没启动，或 M5 串口不对 | 前端服务、`COM6`、USB 线 |
| `USB:OK`，`MIC:WAIT` | Microduino 没有回传 | Microduino 上电、固件、ESP-NOW 信道、距离 |
| `MIC:OK`，`SYS:OFF` | Microduino 在线但系统未开启 | 前端点 `系统开启`，或按 M5 A 键 |
| 点系统开启失败 | 电脑端失去 M5 串口，命令没有发到 M5 | 重启前端服务，确认没有串口监视器占用 `COM6` |
| `SYS:ON`，`Safety:TIMEOUT` | 系统已开启但没有持续 EEG | 检查脑电 COM、ThinkGear 数据、电脑端 `VALID` / `SENT` |
| `Signal:255` | 还没有真实脑电数据 | 脑电设备是否连接，电脑端是否解析到有效包 |
| `fail` 持续增加 | ESP-NOW 发送失败 | M5 / Microduino 供电、距离、信道、固件 |

## 16. 调试优先顺序

现场排查建议按下面顺序看：

1. M5 顶部是否显示 `Dream M5 Gateway`。
2. `USB` 是否为 `OK`。
3. `ESP` 是否不是 `OFF`。
4. `MIC` 是否为 `OK`。
5. `SYS` 是否能变成 `ON`。
6. `Safety` 是否为 `NORMAL`。
7. 再看 `Light`、`Relay`、`Step` 等执行状态。
