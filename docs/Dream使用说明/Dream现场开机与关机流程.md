# Dream 现场开机与关机流程

更新时间：2026-05-28

## 1. 现场开机前检查

### 1.1 硬件检查

- M5Stack 供电正常。
- Microduino 供电正常。
- M5Stack 和 Microduino 距离建议控制在 1-3 米。
- Microduino 天线附近不要紧贴金属结构。
- MAX485 / DMX 接线正确。
- DMX 灯具地址为 001 和 005。
- 继电器负载电源未直接暴露。
- 步进电机驱动器、电流、方向、限位和机械结构已确认无卡死。
- 急停或断电方案可用。

### 1.2 电脑检查

- Python 已安装。
- `pyserial` 已安装。
- 脑电设备已蓝牙配对。
- M5Stack USB 串口可识别。
- 浏览器可打开本地地址。

查看端口：

```powershell
pio device list
```

## 2. 推荐开机顺序

1. 打开电脑。
2. 给 M5Stack 上电。
3. 给 Microduino 上电。
4. 接通 DMX 灯具电源。
5. 确认继电器 / 雾机 / 电机负载仍处于安全关闭状态。
6. 连接脑电设备蓝牙。
7. 运行电脑端脚本。
8. 打开浏览器前端。
9. 确认前端、M5Stack、Microduino 状态正常。
10. 点击 `系统开启` 或按 M5Stack A 键。

运行脚本示例：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\start_dream_frontend.ps1
```

当前启动脚本默认使用脑电 `COM10@9600`、M5Stack `COM6@115200`。如果现场 COM 号变化，可给脚本传入 `-Source`、`-Target`、`-SourceBaud`、`-TargetBaud`。

打开前端：

```text
http://127.0.0.1:8765/
```

## 3. 运行前状态确认

前端应显示：

| 项目 | 正常状态 |
| --- | --- |
| 网页 | 已连接 |
| 电脑串口 | 串口在线 |
| EEG | 真实数据或等待脑电 |
| M5Stack | 在线 |
| Microduino | 在线 |
| Safety | `NORMAL`、`SIGNAL` 或 `TIMEOUT` 中可解释的状态 |
| System | 默认 `系统关闭` |

如果还没有真实 EEG / M5Stack / Microduino 回传，对应字段会显示 `--` 或等待。这是正常的空状态，不代表假数据。

M5Stack 应显示：

```text
USB:OK
ESP:OK 或 SEND
MIC:OK
SYS:OFF
```

如果 `SYS:OFF`，这是正常默认状态。点击 `系统开启` 后才允许自动联动、手动灯光、继电器和步进电机动作；手动台架调试仍需按现场安全条件单独确认。

## 4. 展示运行步骤

1. 体验者佩戴脑电设备。
2. 观察 `poorSignal` 是否下降。
3. 如果 `poorSignal > 120`，先调整佩戴，不要急着开启。
4. 前端点击 `系统开启`，或按 M5Stack A 键。
5. 确认按钮反馈显示 `已发送`，再观察 Microduino 状态是否变化。
6. 测试灯光是否响应。
7. 测试步进电机时先选择 `左`、`右` 或 `左右`，确认方向和停止都正确。
8. 确认雾机和电机输出是否符合当天硬件启用状态。
9. 展示期间持续观察前端和 M5Stack。

## 5. 暂停和停止

临时暂停：

- 前端点击 `系统关闭`。
- 或按 M5Stack C 键。

紧急停止：

- 前端点击 `全部停止`。
- 或按 M5Stack B 键。
- 必要时使用硬件急停或直接切断负载电源。

`系统关闭` 和 `全部停止` 都会让 Microduino 回到未授权状态，机器默认关闭。

## 6. 关机顺序

1. 前端点击 `全部停止`。
2. 确认灯光关闭、继电器关闭、电机停止。
3. 关闭雾机 / 电机 / 灯具等负载电源。
4. 停止 Python 脚本：`Ctrl+C`。
5. 关闭脑电设备或断开蓝牙。
6. 断开 M5Stack 和 Microduino 电源。
7. 关闭电脑。

## 7. 换电脑流程

换电脑时固件不用重刷，但需要重新确认：

- Python 和 `pyserial`。
- PlatformIO 或串口工具。
- 脑电蓝牙配对。
- USB 串口驱动。
- `--source` 和 `--target` COM 号。

最短检查：

```powershell
python -m pip install pyserial
pio device list
python tools\dream_eeg_serial_bridge.py --source 脑电COM --target M5COM --source-baud 脑电波特率
```

## 8. 现场老化测试建议

正式开展前至少测试：

- 连续运行 30 分钟。
- 连续运行 60 分钟。
- 拔掉脑电设备，看系统是否超时安全。
- 停止 Python 脚本，看 Microduino 是否进入安全状态。
- 拉远 M5Stack 和 Microduino，看 ESP-NOW 丢包情况。
- 点击 `全部停止`，确认所有机器关闭。
