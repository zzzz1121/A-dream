# A-dream

`A-dream` 是 Dream 装置的 ESP32 固件与技术文档仓库。当前阶段仍是原型验证和技术路线确认，不是最终展出固件。

Dream 的目标是把体验者的脑电状态转化为现场光影和机械反馈。最新通信方案中，电脑通过 USB 串口直连 M5Stack，M5Stack 作为“网关 + 监测屏”，再通过 ESP-NOW 把脑电状态转发给 Microduino Core ESP32。Microduino 负责控制 DMX 灯光、步进电机和继电器，并保留所有本地安全状态机。

## 最新技术判断

当前脑电数据路径已经确认：

```text
脑电设备 --Bluetooth--> 电脑
电脑从 COM 口读取 ThinkGear 原始脑电数据
```

电脑收到的数据样例为 ThinkGear 原始二进制包，例如：

```text
AA AA 20 02 C8 83 18 ... 04 00 05 00 79
```

后续推荐的控制链路优先级：

```text
首选：电脑 -> USB 串口 -> M5Stack -> ESP-NOW -> Microduino
备选有线：电脑 -> USB-RS485 -> Microduino
仅调试：电脑 -> Wi-Fi UDP -> M5Stack / Microduino
```

原因：电脑到 M5Stack 使用 USB 串口，避开现场 Wi-Fi 干扰；M5Stack 到 Microduino 使用 ESP-NOW 短包通信，不依赖路由器和热点，比电脑 Wi-Fi UDP 更可控。ESP-NOW 仍然是 2.4GHz，因此 Microduino 端必须保留超时保护，M5Stack 掉线或 ESP-NOW 丢包时，Microduino 必须进入安全状态。

## 当前进度

已完成或已规划清楚：

- 已建立 PlatformIO 多环境工程。
- 已拆分 Microduino、M5Stack、ESP32-WROOM 的原型固件入口。
- 已完成早期 M5Stack 与 Microduino 的 ESP-NOW 通信验证。
- 已完成 Microduino DMX512 / MAX485 双 RGBW 灯测试原型。
- 已确认电脑端能读取脑电蓝牙 COM 口的 ThinkGear 原始数据。
- 已解析 ThinkGear 包字段：`poorSignal`、`attention`、`meditation`、8 个 EEG Power 频段。
- 已新增电脑端串口桥接脚本原型：`tools/eeg_serial_bridge.py`。
- 已建立 Dream 完整技术设计、无线专项方案和命名规范文档。
- 已确定 M5Stack 的新角色：电脑直连网关 + 现场监测屏，通过 ESP-NOW 转发给 Microduino。

仍未完成：

- 正式电脑端脑电解析与分发程序。
- 正式 Microduino 执行控制器固件。
- 正式 M5Stack 监测屏固件。
- M5Stack USB 串口接收 EEG 帧并 ESP-NOW 转发的正式固件。
- Microduino ESP-NOW 接收 EEG 帧并执行控制的正式固件。
- ESP-NOW 固定 peer MAC、固定 channel、丢包统计和超时验证。
- 步进电机状态机。
- 继电器安全状态机。
- 整机现场老化测试。

## 文档索引

| 文档 | 说明 |
| --- | --- |
| [Dream完整技术设计文档](docs/Dream完整技术设计文档.md) | 整套装置的技术架构、电源、安全、灯光、电机、继电器和测试方案 |
| [Dream脑电无线转发Microduino技术方案](docs/Dream脑电无线转发Microduino技术方案.md) | 电脑直连 M5Stack，M5Stack 通过 ESP-NOW 转发到 Microduino 的专项通信方案 |
| [Dream命名规范](docs/Dream命名规范.md) | 统一变量名、文件名、协议字段、日志事件和状态名 |
| [Dream仓库状态说明](docs/Dream仓库状态说明.md) | 仓库当前状态、历史验证内容和常用命令 |
| [Dream需求规划文档 V3.0](docs/Dream需求规划文档_V3.0.docx) | 需求规划原始文档 |

## 当前工程结构

```text
.
├── README.md
├── platformio.ini
├── docs/
│   ├── Dream仓库状态说明.md
│   ├── Dream完整技术设计文档.md
│   ├── Dream脑电无线转发Microduino技术方案.md
│   ├── Dream命名规范.md
│   └── Dream需求规划文档_V3.0.docx
├── tools/
│   └── eeg_serial_bridge.py
└── src/
    ├── microduino_core_esp32_test/
    ├── microduino_core_esp32_dmx_spotlight_test/
    ├── m5stack_core_esp32_test/
    └── esp32_wroom_32_test/
```

## 板卡与环境

| 板卡 | PlatformIO 环境 | 当前用途 | 固件入口 |
| --- | --- | --- | --- |
| Microduino Core ESP32 | `microduino-core-esp32` | 执行控制器原型，后续接收 ESP-NOW EEG 帧并控制 DMX / 电机 / 继电器 | `src/microduino_core_esp32_test/main.cpp` |
| M5Stack Core ESP32 | `m5stack-core-esp32` | 电脑 USB 串口网关 + 监测屏，后续通过 ESP-NOW 转发 EEG 帧 | `src/m5stack_core_esp32_test/main.cpp` |
| ESP32-WROOM-32 | `esp32-wroom-32` | 预留测试板 | `src/esp32_wroom_32_test/main.cpp` |

端口号由 Windows 分配，换 USB 口或换线后可能变化。查看当前端口：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device list
```

## 常用命令

编译 Microduino：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e microduino-core-esp32
```

上传 Microduino：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e microduino-core-esp32 -t upload
```

编译 M5Stack：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e m5stack-core-esp32
```

上传 M5Stack：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e m5stack-core-esp32 -t upload
```

打开串口监视器：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device monitor -p COM7 -b 115200
```

`COM7` 需要替换为实际端口。上传和串口监视不能同时占用同一个 COM 口。

## 脑电数据统一格式

后续电脑端推荐把 ThinkGear 原始包解析为统一文本帧，并通过 USB 串口发送给 M5Stack：

```text
EEG,seq,timeMs,poorSignal,attention,meditation,delta,theta,lowAlpha,highAlpha,lowBeta,highBeta,lowGamma,midGamma
```

示例：

```text
EEG,1024,35120,200,0,0,27,27,22,36,37,56,42,33
```

字段命名以 [Dream命名规范](docs/Dream命名规范.md) 为准。

## 推荐实施顺序

1. 电脑端稳定读取脑电 COM 口，并解析 ThinkGear 包。
2. 电脑端输出统一 `EEG` 文本帧。
3. 用 USB 串口把 `EEG` 帧发给 M5Stack。
4. M5Stack 解析并显示 `EEG` 帧。
5. M5Stack 通过 ESP-NOW 把结构化 EEG 帧转发给 Microduino。
6. Microduino 接收 ESP-NOW EEG 帧并打印/统计丢包。
7. Microduino 根据 `attention / meditation / poorSignal` 控制 DMX 灯光。
8. 加入继电器安全状态机。
9. 加入步进电机非阻塞状态机。
10. 做 30-60 分钟整机老化测试、ESP-NOW 丢包测试和断线安全测试。

## 安全原则

- 继电器和步进电机不能由单个脑电包直接触发。
- Microduino 本地必须实现超时保护，不能依赖 M5Stack 保证安全。
- `poorSignal` 长时间过高时应忽略脑电控制更新。
- 电脑串口中断、M5Stack 掉线或 ESP-NOW 中断 2-3 秒内必须进入安全状态。
- 继电器上电默认关闭。
- 步进电机上电默认禁用。
- 正式现场建议保留硬件急停。
