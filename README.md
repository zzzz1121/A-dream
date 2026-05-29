# A-dream

`A-dream` 是 Dream 装置的 ESP32 固件、电脑端桥接程序、浏览器前端和技术文档仓库。

Dream 的目标是把体验者的脑电状态转化为现场光影和机械反馈。当前原型链路为：脑电设备通过蓝牙把 ThinkGear 数据发给电脑，电脑端 Python 程序解析后通过 USB 串口发给 M5Stack，M5Stack 作为“网关 + 监测屏”通过 ESP-NOW 转发给 Microduino，Microduino 负责 DMX 灯光、步进电机、继电器和本地安全状态机。

当前版本已经具备电脑端脑电桥接、浏览器实时前端、M5Stack 网关监测、M5Stack 到 Microduino 的 ESP-NOW 转发、Microduino DMX 双灯控制、单步进驱动板台架输出、Microduino 状态回传与安全总开关。浏览器前端只显示真实串口、M5Stack 和 Microduino 回传状态；没有收到真实数据时显示 `--` / 等待，不使用假数据。继电器物理输出仍默认关闭；步进电机物理输出当前已启用用于台架调试，正式负载接入前仍必须确认电源、驱动器、限位、行程和急停。

## 当前链路

```text
脑电设备 --Bluetooth--> 电脑 Python 桥接程序
电脑 Python 桥接程序 --USB Serial--> M5Stack
M5Stack --ESP-NOW--> Microduino
Microduino --> DMX 灯光 / 步进电机 / 继电器
```

浏览器前端也由 Python 桥接程序提供：

```text
浏览器前端 -> Python 本地服务 -> USB Serial -> M5Stack -> ESP-NOW -> Microduino
```

## 当前进度

已完成：

- PlatformIO 多环境工程。
- M5Stack、Microduino、ESP32-WROOM 原型固件入口拆分。
- 电脑端读取脑电蓝牙 COM 口 ThinkGear 原始数据。
- ThinkGear 字段解析：`poorSignal`、`attention`、`meditation`、8 个 EEG Power 频段。
- 电脑端脑电桥接与浏览器前端：`tools/dream_eeg_serial_bridge.py`。
- M5Stack USB 串口接收 `EEG` / `CMD`，屏幕显示实时状态，并通过 ESP-NOW 转发。
- Microduino ESP-NOW 接收、DMX 灯光控制、安全总开关和状态回传。
- Microduino 双 DMX RGBW 灯控制：DMX TX 使用 `GPIO5`，灯具地址为 `001` 和 `005`。
- 两灯流水光效：灯 1 使用当前色轮相位，灯 2 固定错开 `96 / 256` 圈，约 `3.36s` 相位差。
- 前端真实状态监测、系统开启 / 关闭、灯光颜色、继电器、步进电机控制入口。
- 前端步进电机固定控制单驱动板 `GPIO25/GPIO14`，不再显示左 / 右 / 左右目标选择。
- 前端所有控制按钮都有反馈：发送中、已发送、失败；M5Stack 串口未打开时不会假装发送成功。
- M5Stack A/B/C 按键控制系统开启、全部停止、系统关闭。
- Dream 完整技术设计、无线专项方案、命名规范和使用说明文档。

仍需现场确认：

- ESP-NOW 固定 peer MAC 和现场丢包测试。
- 继电器物理输出引脚、触发电平和负载安全。
- 步进电机机械限位、方向、行程、驱动器电流和负载安全。
- 整机 30-60 分钟老化测试。

## 文档索引

| 文档 | 说明 |
| --- | --- |
| [Dream使用说明总览](docs/Dream使用说明/Dream使用说明总览.md) | 当前原型的使用说明入口 |
| [Dream电脑端与前端使用说明](docs/Dream使用说明/Dream电脑端与前端使用说明.md) | 电脑端 Python 桥接、浏览器前端和控制按钮 |
| [Dream板卡固件烧录使用说明](docs/Dream使用说明/Dream板卡固件烧录使用说明.md) | M5Stack 和 Microduino 编译、上传、串口监视 |
| [DreamM5Stack屏幕界面说明](docs/Dream使用说明/DreamM5Stack屏幕界面说明.md) | M5Stack 屏幕字段、按键和现场读屏判断 |
| [Dream现场开机与关机流程](docs/Dream使用说明/Dream现场开机与关机流程.md) | 展示现场开机、测试、运行、关机顺序 |
| [Dream故障排查手册](docs/Dream使用说明/Dream故障排查手册.md) | 常见串口、前端、灯光、ESP-NOW、机器控制问题 |
| [Dream接口与指令协议说明](docs/Dream使用说明/Dream接口与指令协议说明.md) | EEG、CMD、ESP-NOW 包和状态回传字段 |
| [Dream完整技术设计文档](docs/Dream技术文档/Dream完整技术设计文档.md) | 整套装置的技术架构、电源、安全、灯光、电机、继电器和测试方案 |
| [Dream脑电无线转发Microduino技术方案](docs/Dream技术文档/Dream脑电无线转发Microduino技术方案.md) | 电脑直连 M5Stack，M5Stack 通过 ESP-NOW 转发到 Microduino 的专项通信方案 |
| [Dream命名规范](docs/Dream技术文档/Dream命名规范.md) | 统一变量名、文件名、协议字段、日志事件和状态名 |
| [Dream仓库状态说明](docs/Dream仓库状态说明.md) | 仓库当前状态、历史验证内容和常用命令 |
| [Dream需求规划文档 V3.0](docs/Dream需求规划文档_V3.0.docx) | 需求规划原始文档 |

## 工程结构

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

## 板卡与环境

| 板卡 | PlatformIO 环境 | 当前用途 | 固件入口 |
| --- | --- | --- | --- |
| Microduino Core ESP32 | `microduino-core-esp32` | 执行控制器，接收 ESP-NOW EEG / CMD，控制双 DMX 灯、单步进驱动板并管理安全状态 | `src/microduino_core_esp32_test/main.cpp` |
| M5Stack Core ESP32 | `m5stack-core-esp32` | 电脑 USB 串口网关 + 监测屏，通过 ESP-NOW 转发 EEG / CMD | `src/m5stack_core_esp32_test/main.cpp` |
| Microduino DMX 测试 | `microduino-core-esp32-dmx-spotlight-test` | 单项测试两盏 RGBW DMX 灯流水状态 | `src/microduino_core_esp32_dmx_spotlight_test/main.cpp` |
| Microduino 步进引脚诊断 | `microduino-core-esp32-stepper-pin-diagnostic` | 单项诊断左右步进 STEP / DIR 输出 | `src/microduino_core_esp32_stepper_pin_diagnostic/main.cpp` |
| ESP32-WROOM-32 | `esp32-wroom-32` | 预留测试板 | `src/esp32_wroom_32_test/main.cpp` |

查看当前串口：

```powershell
pio device list
```

如果 `pio` 命令不可用：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device list
```

## 常用命令

编译和上传 M5Stack：

```powershell
pio run -e m5stack-core-esp32
pio run -e m5stack-core-esp32 -t upload
```

编译和上传 Microduino：

```powershell
pio run -e microduino-core-esp32
pio run -e microduino-core-esp32 -t upload
```

打开串口监视器：

```powershell
pio device monitor -p COM5 -b 115200
```

`COM5` 需要替换为实际端口。上传、串口监视器和 Python 脚本不能同时占用同一个 COM 口。

## 电脑端前端

安装依赖：

```powershell
python -m pip install pyserial
```

启动电脑端脑电桥接和浏览器前端。当前现场脚本默认使用脑电 `COM10@9600`、M5Stack `COM6@115200`：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\start_dream_frontend.ps1
```

也可以直接运行 Python 桥接程序：

```powershell
python tools\dream_eeg_serial_bridge.py --source COM10 --target COM6 --source-baud 9600 --target-baud 115200 --send-rate 20
```

打开前端：

```text
http://127.0.0.1:8765/
```

前端可以查看真实实时数据，并发送系统开启、系统关闭、灯光颜色、继电器、步进电机和全部停止指令。步进电机控制区固定控制单驱动板，可发送正向、反向或停止命令。没有收到真实 EEG / M5Stack / Microduino 状态时，界面显示 `--` 或等待；所有控制按钮都有发送中、已发送、失败反馈。若 M5Stack 串口未打开，控制请求会返回失败。

## 数据格式

电脑端把 ThinkGear 原始包解析为统一文本帧，并通过 USB 串口发送给 M5Stack：

```text
EEG,seq,timeMs,poorSignal,attention,meditation,delta,theta,lowAlpha,highAlpha,lowBeta,highBeta,lowGamma,midGamma
```

控制指令格式：

```text
CMD,seq,timeMs,action,arg1,arg2,arg3,arg4
```

详细协议见 [Dream接口与指令协议说明](docs/Dream使用说明/Dream接口与指令协议说明.md)。

## 安全原则

- Microduino 上电默认 `systemEnabled = false`，没有收到系统开启指令前灯光、继电器和步进电机都保持关闭 / 停止；手动灯光和步进电机台架调试也必须先系统开启。
- 系统开启可由前端 `系统开启` 按钮或 M5Stack A 键发出。
- 系统关闭可由前端 `系统关闭`、前端 `全部停止`、M5Stack C 键或 M5Stack B 键发出。
- 继电器和步进电机不能由单个脑电包直接触发。
- Microduino 本地必须实现超时保护，不能依赖 M5Stack 保证安全。
- `poorSignal` 长时间过高时应忽略脑电控制更新。
- 电脑串口中断、M5Stack 掉线或 ESP-NOW 中断 2-3 秒内必须进入安全状态。
- 继电器上电默认关闭。
- 步进电机物理输出当前已启用用于台架调试；接真实机械负载前必须确认限位、行程、电流、方向和急停。
- 正式现场建议保留硬件急停。
