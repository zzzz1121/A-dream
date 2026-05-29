# Dream 脑电数据与灯光泡泡对应关系

更新时间：2026-05-30

文档目的：根据 `docs/数据格式说明.pdf` 的 ThinkGear 脑电原始数据格式，整理当前项目里脑电、灯光、泡泡机之间的数据对应关系。

## 1. 数据链路

```text
脑电设备
  -> 蓝牙串口 ThinkGear 原始包
  -> 电脑 Python 桥接程序解析
  -> EEG 文本帧
  -> M5Stack
  -> ESP-NOW EEG 结构体包
  -> Microduino
  -> DMX 灯光 / 继电器开关输出 / 状态回传
```

说明：

- “灯”当前对应两盏 DMX RGBW 灯。
- “泡泡”当前由 Microduino 上的泡泡流程状态机控制，触发来源是 M5Stack 侧按压传感器或前端调试按钮。
- 雾机和风扇均通过继电器控制，当前约定为高电平触发。

## 2. PDF 中的脑电原始包格式

PDF 描述的是 ThinkGear 原始二进制包：

```text
AA AA LEN PAYLOAD CHECKSUM
```

示例包：

```text
AA AA 20 02 50 83 18 04 96 80 08 64 4B 01 DB 44 07 DF FC 06 CC A0 04 8B 00 04 69 F9 09 A3 73 04 15 05 3D 5E
```

字段解释：

| 字节 / Code | 含义 | 长度 | 示例值 | 说明 |
| --- | --- | ---: | --- | --- |
| `AA AA` | 同步头 | 2 | `AA AA` | 每个包开头 |
| `LEN` | payload 长度 | 1 | `20` | 十六进制 `0x20` = 十进制 32 |
| `0x02` | Signal / poorSignal | 1 | `50` | 信号值；数值越小越好 |
| `0x83` | EEG Power | 24 | `18` 后 24 字节 | 8 个频段，每个频段 3 字节，大端 |
| `0x04` | Attention | 1 | `15` | 专注度，0-100 |
| `0x05` | Meditation | 1 | `3D` | 放松度，0-100 |
| `CHECKSUM` | 校验和 | 1 | `5E` | payload 字节求和后按位取反 |

EEG Power 频段顺序：

| 顺序 | 字段 | 示例 3 字节 |
| ---: | --- | --- |
| 1 | `delta` | `04 96 80` |
| 2 | `theta` | `08 64 4B` |
| 3 | `lowAlpha` | `01 DB 44` |
| 4 | `highAlpha` | `07 DF FC` |
| 5 | `lowBeta` | `06 CC A0` |
| 6 | `highBeta` | `04 8B 00` |
| 7 | `lowGamma` | `04 69 F9` |
| 8 | `midGamma` / `middleGamma` | `09 A3 73` |

PDF 还标注：如果 Signal 等于以下值，代表头戴没有戴好：

```text
0x1D, 0x36, 0x37, 0x38, 0x50, 0x51, 0x52, 0x6B, 0xC8
```

项目现有逻辑进一步使用 `poorSignal > 120` 作为信号差判断。

## 3. 项目内部统一 EEG 帧

电脑端解析 ThinkGear 后，统一发送文本帧：

```text
EEG,seq,timeMs,poorSignal,attention,meditation,delta,theta,lowAlpha,highAlpha,lowBeta,highBeta,lowGamma,midGamma
```

字段用途：

| 字段 | 范围 / 类型 | 当前用途 |
| --- | --- | --- |
| `poorSignal` | 0-255，0 最好 | 判断信号质量；未戴好或信号差时灯光和泡泡都关闭 |
| `attention` | 0-100 | 灯光色轮相位；高专注可触发电机 / 继电器状态机 |
| `meditation` | 0-100 | 灯光亮度；高放松可触发电机呼吸模式 |
| `delta` | 24-bit EEG Power | 当前主要显示 / 记录，暂未直接控制输出 |
| `theta` | 24-bit EEG Power | 当前主要显示 / 记录，暂未直接控制输出 |
| `lowAlpha` | 24-bit EEG Power | 当前主要显示 / 记录，暂未直接控制输出 |
| `highAlpha` | 24-bit EEG Power | 当前主要显示 / 记录，暂未直接控制输出 |
| `lowBeta` | 24-bit EEG Power | 当前主要显示 / 记录，暂未直接控制输出 |
| `highBeta` | 24-bit EEG Power | 当前主要显示 / 记录，暂未直接控制输出 |
| `lowGamma` | 24-bit EEG Power | 当前主要显示 / 记录，暂未直接控制输出 |
| `midGamma` | 24-bit EEG Power | 当前主要显示 / 记录，暂未直接控制输出 |

## 4. 脑电到灯光的对应关系

当前灯光硬件：

| 对象 | DMX 起始地址 | 通道 |
| --- | ---: | --- |
| 灯 1 | `001` | R / G / B / W |
| 灯 2 | `005` | R / G / B / W |

DMX 通道：

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

自动脑电灯光映射：

| 脑电字段 / 状态 | 灯光输出 | 说明 |
| --- | --- | --- |
| `systemEnabled = false` | 两灯关闭 | 系统未开启时不输出灯光 |
| EEG 超时 | 两灯关闭，`LIGHT_TIMEOUT` | 超过约 3 秒无有效 EEG |
| `poorSignal > 120` 或 PDF 标注的未戴好 Signal 值 | 两灯关闭 / 无颜色 | 未戴好时不显示灯光颜色 |
| `attention` | 色轮相位偏移 | 专注度越高，色彩相位越靠后 |
| `attention / meditation` | 流水亮度 | 两者先换算到 0-255，再取较大值；亮度最低限制约 64 |
| 灯 1 | 当前色轮相位 | 主相位 |
| 灯 2 | 灯 1 相位 + `96 / 256` 圈 | 形成双灯流水错位 |

简化公式：

```text
attentionByte = attention * 255 / 100
meditationByte = meditation * 255 / 100
flowBrightness = max(attentionByte, meditationByte), limit 64-255
phase = time / 35ms + attention
light1 = colorWheel(phase, flowBrightness)
light2 = colorWheel(phase + 96, flowBrightness)
```

颜色过渡规则：

```text
正常自动灯光：约 90 ms 推进一个色轮步进，约 23 秒完整流动一圈
每次 DMX 刷新只向目标颜色靠近约 6%，避免颜色跳变
未戴好 / EEG 超时 / 系统关闭 / 关灯指令：立即变为黑场
```

未戴好时不进入上述色轮计算，灯光目标为：

```text
light1 = RGBW(0, 0, 0, 0)
light2 = RGBW(0, 0, 0, 0)
```

手动灯光模式：

| 控制指令 | 作用 |
| --- | --- |
| `LIGHT_AUTO` | 回到脑电自动灯光 |
| `LIGHT_COLOR,R,G,B,W` | 前端指定 RGBW，Microduino 生成手动渐变流水 |
| `LIGHT_OFF` | 手动关闭灯光 |

系统关闭时前端禁用灯光手动按钮；Microduino 收到 `LIGHT_COLOR` 也会忽略，并强制保持黑场。

## 5. 脑电到泡泡的对应关系

当前项目没有独立的泡泡机协议字段。建议第一版把泡泡机作为“继电器开关输出设备”处理：

```text
泡泡机 = relayOutput
泡泡状态 = relayState
泡泡物理输出是否启用 = relayOutputEnabled
```

当前继电器 / 泡泡状态枚举：

| 值 | 状态 | 泡泡机含义 |
| ---: | --- | --- |
| `0` | `RELAY_OFF` | 泡泡关闭 |
| `1` | `RELAY_ARMING` | 自动触发准备中 |
| `2` | `RELAY_ON` | 泡泡开启 |
| `3` | `RELAY_COOLDOWN` | 泡泡冷却中，不允许连续触发 |
| `4` | `RELAY_FAULT` | 泡泡输出故障 / 异常 |

当前自动规则：

| 脑电字段 / 状态 | 泡泡输出 | 说明 |
| --- | --- | --- |
| `systemEnabled = false` | 关闭 | 系统未开启时不允许泡泡 |
| EEG 超时 | 关闭 | 链路异常时不允许泡泡 |
| `poorSignal > 120` | 关闭 | 头戴不稳时不允许泡泡 |
| `attention > 75` 持续约 3 秒 | 进入 `RELAY_ON` | 可理解为专注达到阈值后喷泡泡 |
| `RELAY_ON` 持续约 2 秒 | 进入冷却 | 自动泡泡输出时间 |
| `RELAY_COOLDOWN` 约 10 秒 | 回到关闭 | 防止连续触发 |

手动规则：

| 控制指令 | 泡泡输出 |
| --- | --- |
| `RELAY_ON` | 请求手动开启泡泡，最长约 5 秒 |
| `RELAY_OFF` | 请求关闭泡泡 |
| `ALL_STOP` | 关闭泡泡，并关闭系统授权 |

重要现状：

- 当前固件里继电器物理输出默认关闭，`relayOutputEnabled = 0`。
- 因此文档中的泡泡映射是数据和状态机对应关系；真正接泡泡机前，需要确认继电器输出引脚、电气隔离、供电、电流、急停和防连续触发策略。

## 6. 执行状态回传字段

Microduino 会把执行状态回传给 M5Stack 和电脑前端。与灯 / 泡泡有关的字段如下：

| 回传字段 | 对应对象 | 含义 |
| --- | --- | --- |
| `poorSignal` | 脑电 | 最近一次信号质量 |
| `attention` | 脑电 | 最近一次专注度 |
| `meditation` | 脑电 | 最近一次放松度 |
| `lightMode` | 灯 | 当前灯光模式 |
| `lightLevel` | 灯 | 当前灯光最大亮度 |
| `light1R/G/B/W` | 灯 1 | 当前灯 1 RGBW |
| `light2R/G/B/W` | 灯 2 | 当前灯 2 RGBW |
| `relayState` | 泡泡 / 继电器 | 当前泡泡开关状态 |
| `relayOutputEnabled` | 泡泡 / 继电器 | 物理输出是否启用 |
| `safetyState` | 全系统 | 安全状态 |
| `systemEnabled` | 全系统 | 系统总开关 |

灯光状态枚举：

| 值 | 状态 | 含义 |
| ---: | --- | --- |
| `0` | `LIGHT_OFF` | 灯光关闭 |
| `1` | `LIGHT_IDLE` | 待机 |
| `2` | `LIGHT_EEG_BLEND` | 脑电自动流水 |
| `3` | `LIGHT_SIGNAL_BAD` | 信号差 / 未戴好；现场目标为关灯无颜色 |
| `4` | `LIGHT_TIMEOUT` | EEG 超时 |
| `5` | `LIGHT_MANUAL` | 手动灯光 |

安全状态枚举：

| 值 | 状态 | 含义 |
| ---: | --- | --- |
| `0` | `SAFETY_NORMAL` | 正常 |
| `1` | `SAFETY_SIGNAL_BAD` | 脑电信号差 |
| `2` | `SAFETY_LINK_TIMEOUT` | EEG 链路超时 |
| `3` | `SAFETY_ESTOP` | 急停 |
| `4` | `SAFETY_FAULT` | 故障 |

## 7. 建议的现场对应表

| 体验状态 | EEG 条件 | 灯光 | 泡泡 |
| --- | --- | --- | --- |
| 未佩戴 / 佩戴不稳 | `poorSignal > 120` 或 PDF 标注的未戴好 Signal 值 | 无颜色 / 关灯 | 关闭 |
| 平稳放松 | `meditation` 较高 | 灯光更亮、更柔和流水 | 不自动触发 |
| 高专注 | `attention` 较高 | 色轮相位推进，亮度随 `attention` 提升 | 超过阈值并稳定后短时开启 |
| 数据中断 | EEG 超时 | 灯光关闭 | 关闭 |
| 系统关闭 / 全停 | `systemEnabled = false` | 灯光关闭 | 关闭 |

## 8. 后续如果要把“泡泡”独立出来

后续可以在不改变脑电原始格式的前提下，新增更清晰的应用层命名：

```text
bubbleState
bubbleOutputEnabled
bubbleMode
bubbleLevel
```

建议第一版仍保持开关型：

```text
0 = OFF
1 = ARMING
2 = ON
3 = COOLDOWN
4 = FAULT
```

暂不建议把 EEG 频段值直接映射成连续泡泡强度。泡泡机通常是机械 / 电机 / 风机负载，第一版用短时开关和冷却时间更安全。

## 9. 2026-05-30 当前实现覆盖说明

本节为当前实现准则，优先级高于上方旧版“脑电触发泡泡”描述。

### 9.1 控制分工

| 输入 / 控制源 | 当前作用 |
| --- | --- |
| 脑电数据 | 仅控制自动灯光，不再触发步进电机、雾机、风扇或泡泡 |
| M5Stack 按压传感器 | 触发泡泡流程；传感器 IO 尚未定，当前固件预留为 `PRESSURE_SENSOR_PIN = -1` |
| 前端手动控制 | 保留系统开关、灯光、步进电机、雾机、风扇、泡泡流程触发等调试入口 |

### 9.2 泡泡流程

| 阶段 | 默认时长 | 输出 |
| --- | ---: | --- |
| `BUBBLE_FOGGING` | 1000 ms | 雾机继电器开启，风扇关闭，步进电机等待 |
| `BUBBLE_BLOWING` | 4000 ms | 雾机继电器开启，风扇继电器开启，步进电机运行约 1600 步 |
| `BUBBLE_FINISHING` | 1000 ms | 雾机关闭，风扇继续开启 |
| `BUBBLE_IDLE` | - | 泡泡流程结束，可再次触发 |

泡泡流程一旦开始不可被前端停止、步进停止、雾机停止、风扇停止、系统关闭或全停打断。流程运行中新的按压或前端泡泡触发会被忽略；流程结束后可以再次触发。

当前 Microduino IO 约定：

| 设备 | IO | 电平 |
| --- | ---: | --- |
| 步进电机 STEP | GPIO25 | 脉冲 |
| 步进电机 DIR | GPIO14 | 方向 |
| 雾机继电器 | GPIO26 | HIGH 开，LOW 关 |
| 风扇继电器 | GPIO27 | HIGH 开，LOW 关 |
| DMX TX | GPIO5 | DMX 输出 |

### 9.3 灯光优先级

| 状态 | 灯光行为 |
| --- | --- |
| 系统关闭 | 强制黑场；前端不可点亮手动灯光，Microduino 忽略 `LIGHT_COLOR` |
| 前端手动颜色 | 仅系统开启时可亮；可覆盖自动灯光，不受泡泡流程限制 |
| 前端手动关灯 | 系统开启时可手动关灯 |
| 自动灯光 + 泡泡流程中 | 不亮 |
| 自动灯光 + 泡泡完成 + 有有效脑电 | 按脑电自动流水控制 |
| 自动灯光 + 泡泡完成 + 无脑电数据 | 使用蓝紫到蓝色的缓慢流水渐变：`#4b7dff` 到 `#245cff` |
| 自动灯光 + 佩戴不稳 / 信号差 | 不亮 |

前端“蓝紫”预设现在也使用同一套 `#4b7dff` 到 `#245cff` 的双灯错位流水渐变。

### 9.4 前端显示

前端新增按压触发显示：

| 字段 | 含义 |
| --- | --- |
| 按压状态 | `未按下 / 已按下` |
| 上次触发 | 距离 M5 上次按压触发的时间 |
| 泡泡流程 | Microduino 当前泡泡阶段和触发次数 |
