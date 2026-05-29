# Dream 接口与指令协议说明

更新时间：2026-05-28

## 1. 协议总览

当前有三层协议：

```text
电脑 -> M5Stack：USB Serial 文本帧
M5Stack -> Microduino：ESP-NOW 结构体包
Microduino -> M5Stack：ESP-NOW 状态结构体包
```

电脑端同时提供浏览器前端和本地 HTTP 接口：

```text
浏览器前端 -> Python 本地 HTTP -> USB Serial CMD -> M5Stack -> ESP-NOW -> Microduino
```

前端只显示真实状态。没有收到真实 EEG、M5Stack 状态或 Microduino 状态时，对应字段显示 `--` 或等待。

电脑到 M5Stack 的文本帧包括：

| 帧类型 | 方向 | 作用 |
| --- | --- | --- |
| `EEG` | Python -> M5Stack | 脑电实时数据 |
| `CMD` | Python -> M5Stack | 前端控制指令 |

## 2. EEG 文本帧

格式：

```text
EEG,seq,timeMs,poorSignal,attention,meditation,delta,theta,lowAlpha,highAlpha,lowBeta,highBeta,lowGamma,midGamma
```

示例：

```text
EEG,1024,35120,0,67,43,123,456,78,90,111,222,33,44
```

字段：

| 字段 | 说明 |
| --- | --- |
| `seq` | 电脑端递增序号 |
| `timeMs` | 电脑端脚本启动后的毫秒数 |
| `poorSignal` | 信号质量，0 最好，200 很差 |
| `attention` | 专注度，通常 0-100 |
| `meditation` | 放松度，通常 0-100 |
| `delta` | Delta 频段 |
| `theta` | Theta 频段 |
| `lowAlpha` | 低 Alpha |
| `highAlpha` | 高 Alpha |
| `lowBeta` | 低 Beta |
| `highBeta` | 高 Beta |
| `lowGamma` | 低 Gamma |
| `midGamma` | 中 Gamma |

## 3. CMD 文本帧

格式：

```text
CMD,seq,timeMs,action,arg1,arg2,arg3,arg4
```

示例：

```text
CMD,3,12800,SYSTEM_ENABLE,0,0,0,0
CMD,4,13000,LIGHT_COLOR,255,80,120,0
CMD,5,15000,RELAY_ON,0,0,0,0
CMD,6,17000,STEPPER_FORWARD,1600,3,0,0
CMD,7,17500,STEPPER_STOP,0,1,0,0
CMD,8,18000,ALL_STOP,0,0,0,0
```

支持的 `action`：

| action | 说明 | 参数 |
| --- | --- | --- |
| `SYSTEM_ENABLE` | 系统开启，允许自动联动和继电器状态机动作 | 无 |
| `SYSTEM_DISABLE` | 系统关闭，机器回到默认关闭 | 无 |
| `LIGHT_AUTO` | 灯光回到脑电自动模式 | 无 |
| `LIGHT_COLOR` | 手动设置灯光颜色 | `arg1=R`，`arg2=G`，`arg3=B`，`arg4=W` |
| `LIGHT_OFF` | 手动关闭灯光 | 无 |
| `RELAY_ON` | 请求继电器开启 | 无 |
| `RELAY_OFF` | 请求继电器关闭 | 无 |
| `STEPPER_FORWARD` | 请求电机正向移动 | `arg1=步数`，`arg2=目标掩码` |
| `STEPPER_BACKWARD` | 请求电机反向移动 | `arg1=步数`，`arg2=目标掩码` |
| `STEPPER_STOP` | 请求电机停止 | `arg2=目标掩码` |
| `ALL_STOP` | 全部停止，并关闭系统授权 | 无 |

步进电机目标掩码：

| `arg2` | 目标 |
| ---: | --- |
| `0` | 默认目标 |
| `1` | 左目标 |
| `2` | 右目标 |
| `3` | 左右目标 |

当前 Microduino 硬件简化为一个步进驱动板，以上目标都会映射到同一个 `GPIO25` STEP / `GPIO14` DIR 输出。

步数规则：

- `arg1=0` 时，Microduino 按一圈 `1600` 步处理。
- 单次命令最大限制为 `64000` 步。
- STEP 脉冲由 Microduino 本地状态机生成，前端不发送高频脉冲。

### 3.1 前端命令反馈

浏览器按钮点击后会请求 Python 本地接口：

```text
POST /api/command
```

请求体：

```json
{"action":"system_enable","args":[0,0,0,0]}
```

Python 会把 action 转换为 `CMD` 文本帧并放入 M5Stack 串口发送队列。成功返回：

```json
{"ok":true,"command":"CMD,0,1234,SYSTEM_ENABLE,0,0,0,0"}
```

如果 M5Stack 串口未打开，返回失败，例如：

```json
{"error":"target serial is not open"}
```

按钮反馈含义：

| 反馈 | 含义 |
| --- | --- |
| `发送中` | 前端正在请求 Python 本地服务 |
| `已发送` | 命令已进入 M5Stack 串口发送队列 |
| `失败` | 命令没有进入发送队列 |

`已发送` 不是 Microduino 已执行的确认。最终状态以 Microduino 回传为准。

## 4. 系统总开关

Microduino 内部有：

```text
systemEnabled
```

默认：

```text
false
```

自动 EEG 灯光、手动灯光、继电器和步进电机动作都需要收到 `SYSTEM_ENABLE` 后才允许。系统关闭时，输出类命令不会驱动物理输出；接入真实机械负载前必须先确认硬件安全。

收到以下任一指令后，`systemEnabled` 会变为 `false`：

```text
SYSTEM_DISABLE
ALL_STOP
```

系统关闭时：

- 灯光目标为 0。
- 继电器关闭。
- 电机停止。

## 5. M5Stack 按键指令

M5Stack 可以本地生成控制包，不经过电脑前端。

| 按键 | 生成指令 |
| --- | --- |
| A | `SYSTEM_ENABLE` |
| B | `ALL_STOP` |
| C | `SYSTEM_DISABLE` |

## 6. ESP-NOW 包基础字段

所有 ESP-NOW 包都包含：

| 字段 | 说明 |
| --- | --- |
| `magic` | 固定 `0x44524541` |
| `version` | 当前为 `1` |
| `type` | 包类型 |
| `checksum` | 简单 16-bit 累加校验 |

包类型：

| type | 说明 |
| ---: | --- |
| `1` | EEG 包 |
| `2` | Microduino 状态包 |
| `3` | 控制包 |

## 7. Microduino 状态回传

Microduino 会回传：

| 字段 | 说明 |
| --- | --- |
| `lastEegSeq` | 最近收到的 EEG 序号 |
| `eegAgeMs` | EEG 数据年龄 |
| `rxCount` | EEG 收包数 |
| `dropCount` | 丢包或校验错误计数 |
| `timeoutCount` | 超时次数 |
| `poorSignal` | 最近脑电信号质量 |
| `attention` | 最近专注度 |
| `meditation` | 最近放松度 |
| `lightLevel` | 当前灯光亮度 |
| `lightMode` | 当前灯光模式 |
| `light1R / light1G / light1B / light1W` | 灯 1 当前 RGBW |
| `light2R / light2G / light2B / light2W` | 灯 2 当前 RGBW |
| `stepperState` | 当前电机状态 |
| `relayState` | 当前继电器状态 |
| `safetyState` | 当前安全状态 |
| `controlRxCount` | 收到控制包数量 |
| `lastControlAction` | 最近控制动作 |
| `manualLightEnabled` | 是否手动灯光模式 |
| `relayOutputEnabled` | 继电器物理输出是否启用 |
| `stepperOutputEnabled` | 电机物理输出是否启用 |
| `systemEnabled` | 系统总开关是否开启 |

## 8. Python 前端状态接口

前端通过 Server-Sent Events 实时接收状态：

```text
GET /events
```

也可以读取一次快照：

```text
GET /api/state
```

关键状态字段：

| 字段 | 说明 |
| --- | --- |
| `eeg.seen` | 是否收到过真实 EEG 包 |
| `eeg.ageMs` | 最近 EEG 包距现在的毫秒数，未收到时为 `-1` |
| `m5.seen` | 是否收到过 M5Stack 的 `EVENT=M5_STATUS` |
| `m5.ageMs` | 最近 M5 状态距现在的毫秒数，未收到时为 `-1` |
| `mic.seen` | 当前是否有 Microduino 状态回传 |
| `mic.ageMs` | 最近 Microduino 状态距现在的毫秒数，未收到时为 `-1` |
| `bridge.sourceOpen` | 脑电蓝牙串口是否打开 |
| `bridge.targetOpen` | M5Stack USB 串口是否打开 |
| `mic.light1Rgbw` | 前端解析后的灯 1 RGBW 数组 |
| `mic.light2Rgbw` | 前端解析后的灯 2 RGBW 数组 |

前端渲染规则：

- `eeg.seen = false` 时，脑电数值显示 `--`。
- `m5.seen = false` 时，M5 / ESP-NOW 统计显示等待。
- `mic.seen = false` 时，灯光、继电器、电机、安全状态显示 `--`。
- Microduino 掉线后，不继续显示旧执行状态。

## 9. 状态枚举

灯光：

```text
0 LIGHT_OFF
1 LIGHT_IDLE
2 LIGHT_EEG_BLEND
3 LIGHT_SIGNAL_BAD
4 LIGHT_TIMEOUT
5 LIGHT_MANUAL
```

步进电机：

```text
0 STEPPER_DISABLED
1 STEPPER_IDLE
2 STEPPER_MOVING
3 STEPPER_BREATHING
4 STEPPER_LIMITED
5 STEPPER_FAULT
```

继电器：

```text
0 RELAY_OFF
1 RELAY_ARMING
2 RELAY_ON
3 RELAY_COOLDOWN
4 RELAY_FAULT
```

安全：

```text
0 SAFETY_NORMAL
1 SAFETY_SIGNAL_BAD
2 SAFETY_LINK_TIMEOUT
3 SAFETY_ESTOP
4 SAFETY_FAULT
```

## 10. 设计原则

- 前端只发目标和模式，不直接发高频脉冲。
- M5Stack 只负责显示和转发，不直接控制机器。
- Microduino 是最终执行和安全判断位置。
- 所有机器默认关闭。
- 无开启指令不动作。
- 超时或全部停止时回到关闭状态。
