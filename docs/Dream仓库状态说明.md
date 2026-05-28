# Dream 仓库状态说明

更新时间：2026-05-28

## 当前定位

`Dream` 是同名装置的 ESP32 固件、电脑端桥接程序、浏览器前端和技术文档仓库。当前阶段是可联调原型：脑电数据从电脑进入 M5Stack，再通过 ESP-NOW 转发给 Microduino，由 Microduino 负责双 DMX 灯光、左右步进电机台架输出、继电器状态机和本地安全状态。

项目需求基线为 `docs/Dream需求规划文档_V3.0.docx`。当前实现保留需求中的核心方向：

- 脑电设备通过蓝牙把 ThinkGear 原始数据传到电脑。
- 电脑解析脑电数据，并通过 USB 串口发给 M5Stack。
- M5Stack 作为网关和监测屏，通过 ESP-NOW 转发给 Microduino。
- Microduino 控制两盏 DMX RGBW 灯，当前步进电机输出已启用于台架调试，继电器物理输出仍默认关闭。

## 当前主链路

```text
脑电设备 --Bluetooth--> 电脑 Python 桥接程序
电脑 Python 桥接程序 --USB Serial--> M5Stack
M5Stack --ESP-NOW--> Microduino
Microduino --> DMX 灯光 / 步进电机 / 继电器
```

浏览器前端由同一个 Python 桥接程序提供：

```text
浏览器前端 -> Python 本地 HTTP -> USB Serial CMD -> M5Stack -> ESP-NOW -> Microduino
```

## 当前已完成

- PlatformIO 多环境工程。
- M5Stack、Microduino、ESP32-WROOM 原型固件入口拆分。
- 电脑端读取脑电蓝牙 COM 口 ThinkGear 原始数据。
- ThinkGear 字段解析：`poorSignal`、`attention`、`meditation`、8 个 EEG Power 频段。
- 电脑端脑电桥接与浏览器前端：`tools/dream_eeg_serial_bridge.py`。
- 兼容旧入口：`tools/eeg_serial_bridge.py`。
- M5Stack USB 串口接收 `EEG` / `CMD`，屏幕显示实时状态，并通过 ESP-NOW 转发。
- Microduino ESP-NOW 接收、DMX 灯光控制、安全总开关和状态回传。
- Microduino DMX 使用 `GPIO5` 输出，两盏灯起始地址为 `001` 和 `005`。
- 双灯流水光效已启用：灯 2 相对灯 1 错开 `96 / 256` 圈，形成前后流动感。
- 左右步进电机输出已启用于台架调试：左 STEP/DIR 为 `GPIO27/GPIO26`，右 STEP/DIR 为 `GPIO25/GPIO14`。
- 前端步进电机控制支持 `左右`、`左`、`右` 三种目标。
- 前端真实状态监测：没有真实 EEG / M5 / MIC 回传时显示 `--` 或等待。
- 前端控制按钮反馈：发送中、已发送、失败。
- M5Stack A/B/C 按键控制系统开启、全部停止、系统关闭。
- Dream 完整技术设计、无线专项方案、命名规范和使用说明文档。

## 当前安全原则

- Microduino 上电默认 `systemEnabled = false`。
- 没有收到 `SYSTEM_ENABLE` 前，灯光、继电器和步进电机都保持关闭 / 停止；手动灯光和步进电机台架调试也必须先系统开启。
- 前端和 M5Stack 都可以发系统开启 / 关闭指令。
- M5Stack 只负责显示和转发，不直接控制继电器、步进电机或灯光。
- Microduino 必须在本地判断超时、信号差和安全状态。
- 电脑串口断开、M5Stack 掉线或 ESP-NOW 丢包时，Microduino 必须在 2-3 秒内进入安全状态。
- 继电器物理输出默认关闭，等待硬件安全确认后再启用。
- 步进电机物理输出当前已启用于台架调试；接入真实机械负载前必须确认限位、方向、行程、电流和急停。

## 当前源码结构

```text
.
├── README.md
├── platformio.ini
├── docs/
│   ├── Dream使用说明/
│   │   ├── Dream使用说明总览.md
│   │   ├── Dream电脑端与前端使用说明.md
│   │   ├── Dream板卡固件烧录使用说明.md
│   │   ├── DreamM5Stack屏幕界面说明.md
│   │   ├── Dream现场开机与关机流程.md
│   │   ├── Dream故障排查手册.md
│   │   └── Dream接口与指令协议说明.md
│   ├── Dream技术文档/
│   │   ├── Dream完整技术设计文档.md
│   │   ├── Dream脑电无线转发Microduino技术方案.md
│   │   └── Dream命名规范.md
│   ├── Dream仓库状态说明.md
│   └── Dream需求规划文档_V3.0.docx
├── tools/
│   ├── dream_eeg_serial_bridge.py
│   └── eeg_serial_bridge.py
└── src/
    ├── microduino_core_esp32_test/
    ├── microduino_core_esp32_dmx_spotlight_test/
    ├── microduino_core_esp32_stepper_pin_diagnostic/
    ├── microduino_core_esp32_stepper_motor_test/
    ├── m5stack_core_esp32_test/
    └── esp32_wroom_32_test/
```

## 当前板卡配置

| 板子 | 环境名 | 当前用途 | 固件入口 |
| --- | --- | --- | --- |
| Microduino Core ESP32 | `microduino-core-esp32` | 执行控制器，接收 ESP-NOW EEG / CMD，控制双 DMX 灯、左右步进电机并管理安全状态 | `src/microduino_core_esp32_test/main.cpp` |
| M5Stack Core ESP32 | `m5stack-core-esp32` | 电脑 USB 串口网关 + 监测屏，通过 ESP-NOW 转发 EEG / CMD | `src/m5stack_core_esp32_test/main.cpp` |
| Microduino DMX 测试 | `microduino-core-esp32-dmx-spotlight-test` | 单项测试两盏 RGBW DMX 灯流水状态 | `src/microduino_core_esp32_dmx_spotlight_test/main.cpp` |
| Microduino 步进引脚诊断 | `microduino-core-esp32-stepper-pin-diagnostic` | 单项诊断左右步进 STEP / DIR 输出 | `src/microduino_core_esp32_stepper_pin_diagnostic/main.cpp` |
| ESP32-WROOM-32 | `esp32-wroom-32` | 预留测试板 | `src/esp32_wroom_32_test/main.cpp` |

端口由 Windows 分配，换线、换 USB 口或换电脑后可能变化。实际使用前先运行：

```powershell
pio device list
```

## 电脑端与前端状态

前端入口：

```text
http://127.0.0.1:8765/
```

前端显示原则：

- `eeg.seen = false` 时，脑电数值和频段显示 `--`。
- `m5.seen = false` 时，M5 / ESP-NOW 统计显示等待。
- `mic.seen = false` 时，灯光、继电器、电机、安全状态显示 `--`。
- Microduino 掉线后，不继续显示旧执行状态。
- 控制按钮点击后显示发送中、已发送或失败。
- M5Stack 串口未打开时，命令返回 `target serial is not open`，不会进入发送队列。

## 常用命令

查看串口：

```powershell
pio device list
```

编译 M5Stack：

```powershell
pio run -e m5stack-core-esp32
```

上传 M5Stack：

```powershell
pio run -e m5stack-core-esp32 -t upload
```

编译 Microduino：

```powershell
pio run -e microduino-core-esp32
```

上传 Microduino：

```powershell
pio run -e microduino-core-esp32 -t upload
```

启动电脑端桥接和前端。当前现场脚本默认使用脑电 `COM10@9600`、M5Stack `COM6@115200`：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\start_dream_frontend.ps1
```

直接运行 Python 桥接程序时：

```powershell
python tools\dream_eeg_serial_bridge.py --source COM10 --target COM6 --source-baud 9600 --target-baud 115200 --send-rate 20
```

如果系统 Python 没有 `pyserial`，可先安装：

```powershell
python -m pip install pyserial
```

## 仍需现场确认

- ESP-NOW 固定 peer MAC 和现场丢包测试。
- 继电器物理输出引脚、触发电平和负载安全。
- 步进电机机械限位、方向、行程、驱动器电流和负载安全。
- 整机 30-60 分钟老化测试。
- 正式现场硬件急停和负载断电方案。
