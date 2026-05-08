# A-dream

`A-dream` 是“梦境泡泡光影装置”的 ESP32 固件仓库。当前仓库处在硬件链路验证阶段，重点是把多块 ESP32 的编译、上传、串口、RGB 灯珠、M5Stack 屏幕按钮和 ESP-NOW 无线通讯跑通。

需求基线来自：[梦境泡泡光影装置需求规划文档 V3.0](docs/梦境泡泡光影装置_需求规划文档_V3.0(1).docx)。当前实现不是最终展出程序，而是为后续主控 / 从控固件做验证。

## 当前功能

目前已经跑通一条最小无线控制链路：

```text
M5Stack 按钮
  -> M5Stack 屏幕实时显示
  -> M5Stack 通过 ESP-NOW 广播控制包
  -> Microduino 接收 ESP-NOW
  -> Microduino 串口打印接收数据
  -> Microduino IO4 RGB 灯珠同步变色
```

当前按键映射：

| M5Stack 按键 | 动作 | Microduino IO4 灯珠 |
| --- | --- | --- |
| A | 上一个颜色 | 同步变色 |
| B | 下一个颜色 | 同步变色 |
| C | 熄灭 | 熄灭 |

颜色顺序：

```text
red -> green -> blue -> white -> off
```

## 板卡与端口

当前本机识别到的端口：

| 板子 | PlatformIO 环境 | 串口 | USB 串口芯片 | 固件入口 |
| --- | --- | --- | --- | --- |
| Microduino Core ESP32 | `microduino-core-esp32` | `COM5` | CH340K | `src/microduino_core_esp32_test/main.cpp` |
| M5Stack Core ESP32 | `m5stack-core-esp32` | `COM6` | CH9102 | `src/m5stack_core_esp32_test/main.cpp` |
| ESP32-WROOM-32 | `esp32-wroom-32` | 未固定 | 待确认 | `src/esp32_wroom_32_test/main.cpp` |

端口号是 Windows 分配的，换 USB 口或换线后可能变化。用下面命令查看当前串口：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device list
```

## 当前固件

### Microduino

Microduino 当前作为 ESP-NOW 接收端。

功能：

- 使用 WiFi STA 模式和 ESP-NOW channel 1
- 接收 M5Stack 广播控制包
- 串口打印来源 MAC、原始 HEX、ASCII、按钮、动作、颜色和序号
- 将 IO4 上的 WS2812B / NeoPixel 灯珠设置为收到的颜色
- 每 3 秒输出一次 heartbeat，显示接收包数量和当前颜色

接线：

| RGB 灯珠 | Microduino |
| --- | --- |
| DIN | IO4 |
| 5V | 外部 5V 或板载 5V，按灯珠供电情况选择 |
| GND | 与 Microduino GND 共地 |

### M5Stack

M5Stack 当前作为交互发送端。

功能：

- 使用 M5Stack 屏幕实时显示颜色、色块、按键状态、事件次数和运行时间
- A/B/C 按键控制本地 IO4 RGB 灯珠
- 每次按键通过 ESP-NOW 广播控制包
- 屏幕使用局部刷新，避免整屏闪烁

接线：

| RGB 灯珠 | M5Stack |
| --- | --- |
| DIN | IO4 |
| 5V | 外部 5V 或板载 5V，按灯珠供电情况选择 |
| GND | 与 M5Stack GND 共地 |

## 编译与上传

如果系统找不到 `pio` 命令，使用完整路径：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e m5stack-core-esp32
```

编译全部当前环境：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run
```

上传 M5Stack：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e m5stack-core-esp32 -t upload
```

上传 Microduino：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e microduino-core-esp32 -t upload
```

打开 Microduino 串口监视器：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device monitor -p COM5 -b 115200
```

打开 M5Stack 串口监视器：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device monitor -p COM6 -b 115200
```

上传失败并提示 `port is busy` 时，先关闭对应端口的串口监视器。上传和监视不能同时占用同一个 COM 口。

## PlatformIO 配置

`platformio.ini` 当前使用三个独立环境，并通过 `build_src_filter` 限定每个环境只编译自己的目录。

当前默认环境是：

```ini
[platformio]
default_envs = m5stack-core-esp32
```

如果在 VS Code 的 PlatformIO 面板中手动上传，建议展开具体环境后点击：

```text
PROJECT TASKS
-> m5stack-core-esp32
-> General
-> Upload
```

或：

```text
PROJECT TASKS
-> microduino-core-esp32
-> General
-> Upload
```

不要混淆环境和端口：环境决定编译哪份代码，端口决定烧到哪块板。

## 目标方向

后续装置目标仍是 V3.0 文档中的双 ESP32 架构：

```text
脑电头环 --BLE--> 主控 ESP32
主控 ESP32 --DMX512/MAX485--> 两盏 RGB 射灯
主控 ESP32 --ESP-NOW--> 从控 ESP32
从控 ESP32 --> 步进电机 / 造雾机 / 传感器
```

当前 M5Stack + Microduino 的 ESP-NOW 按键控灯只是验证无线控制链路。下一步可以把它整理成正式的 `master` / `slave` 固件入口，再逐步接入 DMX、BLE、步进电机和继电器。
