# Dream 电脑端与前端使用说明

更新时间：2026-05-28

适用脚本：`tools/dream_eeg_serial_bridge.py`

## 1. 电脑端职责

电脑端程序同时负责三件事：

- 从脑电设备蓝牙 COM 口读取 ThinkGear 原始数据。
- 解析脑电数据，并通过 USB Serial 发送给 M5Stack。
- 启动本地浏览器前端，用于实时监测和发送控制命令。

当前前端是浅色分栏实时控制台，只显示真实串口、M5Stack 和 Microduino 回传状态。没有收到真实数据时，对应字段显示 `--` 或等待，不用模拟值填充。

前端指令不会直接发给 Microduino，而是走：

```text
浏览器前端 -> Python 本地服务 -> USB Serial -> M5Stack -> ESP-NOW -> Microduino
```

## 2. 第一次使用前准备

安装 Python 依赖：

```powershell
python -m pip install pyserial
```

确认电脑能识别两个串口：

- 脑电蓝牙 COM 口，例如当前现场脚本默认 `COM10`。
- M5Stack USB 串口，例如 `COM6`。

查看串口：

```powershell
pio device list
```

如果 `pio` 命令不可用，可以使用完整路径：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device list
```

## 3. 启动电脑端程序

推荐使用启动脚本。当前脚本默认使用脑电 `COM10@9600`、M5Stack `COM6@115200`：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\start_dream_frontend.ps1
```

也可以直接运行 Python 脚本：

```powershell
python tools\dream_eeg_serial_bridge.py --source COM10 --target COM6 --source-baud 9600 --target-baud 115200 --send-rate 20
```

参数说明：

| 参数 | 说明 |
| --- | --- |
| `--source` | 脑电设备蓝牙 COM 口 |
| `--source-baud` | 脑电串口波特率，Python 脚本默认 `57600`，当前现场启动脚本默认 `9600` |
| `--target` | M5Stack USB 串口 |
| `--target-baud` | M5Stack 串口波特率，默认 `115200` |
| `--send-rate` | 发送给 M5Stack 的 EEG 帧率，默认 `20Hz` |
| `--web-host` | 前端服务地址，默认 `127.0.0.1` |
| `--web-port` | 前端服务端口，默认 `8765` |
| `--log-file` | 可选，保存 EEG CSV 日志 |

保存日志示例：

```powershell
python tools\dream_eeg_serial_bridge.py --source COM10 --target COM6 --source-baud 9600 --log-file logs\eeg-session.csv
```

## 4. 打开前端

脚本启动后，浏览器打开：

```text
http://127.0.0.1:8765/
```

前端显示内容：

- 脑电实时数据：`poorSignal / attention / meditation`。
- EEG 频段：`delta / theta / lowAlpha / highAlpha / lowBeta / highBeta / lowGamma / midGamma`。
- 链路状态：电脑串口、M5Stack、ESP-NOW、Microduino。
- 执行状态：灯光、雾机继电器、步进电机、安全状态。
- 事件日志：控制命令和关键状态。

真实状态规则：

| 数据来源 | 没有真实数据时 |
| --- | --- |
| EEG | 脑电数值和频段显示 `--` |
| M5Stack | M5 / ESP-NOW 统计显示等待 |
| Microduino | 灯光、继电器、电机、安全状态显示 `--` |
| Microduino 掉线 | 不继续显示旧执行状态 |

## 5. 前端控制按钮

所有控制按钮都有即时反馈：

| 反馈 | 含义 |
| --- | --- |
| `发送中` | 前端已收到点击，正在请求本地 Python 服务 |
| `已发送` | Python 服务已把 `CMD` 放入 M5Stack 串口发送队列 |
| `失败` | 命令没有进入发送队列，需要查看提示原因 |

如果 M5Stack 串口没有打开，前端会提示：

```text
target serial is not open
```

这表示命令没有发出，也不会控制真实机器。

### 5.1 系统总开关

| 按钮 | 作用 |
| --- | --- |
| `系统开启` | 允许 Microduino 执行自动联动和继电器状态机 |
| `系统关闭` | 关闭系统授权，所有机器回到关闭状态 |
| `全部停止` | 立即关闭系统授权，并停止灯光、继电器和电机 |

Microduino 上电默认关闭。自动 EEG 联动、手动灯光、继电器和步进电机动作都需要点击 `系统开启`，或按 M5Stack A 键。接入真实机械负载前必须先确认硬件安全。

### 5.2 灯光控制

| 按钮 | 作用 |
| --- | --- |
| `自动脑电` | 灯光回到 EEG 自动流水模式 |
| `发送颜色` | 发送当前颜色选择器的 RGBW 值 |
| `灯光关闭` | 手动关闭灯光 |

当前双灯为 RGBW 4 通道，地址为 `001` 和 `005`。自动 EEG 模式下两盏灯使用同一条色轮流水，灯 2 相对灯 1 错开 `96 / 256` 圈；手动颜色模式会把前端 RGBW 转成手动渐变流水。系统关闭时灯光保持关闭；手动灯光和自动 EEG 灯光都需要系统开启。

### 5.3 雾机 / 继电器控制

| 按钮 | 作用 |
| --- | --- |
| `开启` | 请求打开继电器 |
| `停止` | 请求关闭继电器 |

当前固件中继电器物理输出默认关闭。需要确认硬件后，把 Microduino 固件中的：

```cpp
#define DREAM_ENABLE_RELAY_OUTPUT 0
```

改为：

```cpp
#define DREAM_ENABLE_RELAY_OUTPUT 1
```

并确认 `RELAY_OUTPUT_PIN` 和 `RELAY_ACTIVE_LEVEL` 正确。

### 5.4 步进电机控制

| 按钮 | 作用 |
| --- | --- |
| `正向触发` | 请求电机正向移动指定步数 |
| `反向触发` | 请求电机反向移动指定步数 |
| `停止电机` | 请求停止当前电机动作 |

步进电机控制区有目标选择：

| 目标 | 发送含义 |
| --- | --- |
| `左右` | 左右两个电机同时动作，目标掩码 `3` |
| `左` | 只动左电机，目标掩码 `1` |
| `右` | 只动右电机，目标掩码 `2` |

当前固件中步进电机物理输出已启用用于台架调试：

```cpp
#define DREAM_ENABLE_STEPPER_OUTPUT 1
```

当前引脚：

| 对象 | STEP | DIR |
| --- | ---: | ---: |
| 左电机 | `GPIO27` | `GPIO26` |
| 右电机 | `GPIO25` | `GPIO14` |

注意：当前 DMX 使用 `GPIO5`。正式接入机械负载前必须确认驱动器电流、方向、限位、行程、供电和硬件急停。

## 6. 数据刷新速度

| 链路 | 默认速度 |
| --- | --- |
| Python -> M5Stack EEG 帧 | `20Hz` |
| M5Stack 屏幕刷新 | `5Hz` |
| 前端状态推送 | 约 `5Hz` |
| 前端控制指令延迟 | 通常 `10-50ms` |

前端适合发“模式 / 目标 / 触发”指令，不适合直接发高频电机脉冲。电机脉冲必须由 Microduino 本地状态机生成。按钮显示 `已发送` 只表示命令已进入电脑到 M5Stack 的发送队列，最终执行状态仍以 Microduino 回传为准。

## 7. 换电脑注意事项

换电脑一般不影响固件，但会影响：

- Python 是否安装。
- `pyserial` 是否安装。
- 脑电蓝牙是否重新配对。
- 脑电 COM 口和 M5Stack COM 口是否变化。
- USB 转串口驱动是否安装。

换电脑后建议先运行：

```powershell
pio device list
```

然后用新的 COM 口启动脚本。
