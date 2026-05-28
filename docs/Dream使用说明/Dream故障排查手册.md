# Dream 故障排查手册

更新时间：2026-05-28

## 1. 前端打不开

现象：

```text
http://127.0.0.1:8765/ 无法打开
```

检查：

- Python 脚本是否正在运行。
- 终端是否打印 `EVENT=WEB_READY`。
- `--web-port` 是否被其他程序占用。

换端口示例：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\start_dream_frontend.ps1 -WebPort 8766
```

然后打开：

```text
http://127.0.0.1:8766/
```

## 2. Python 提示没有 pyserial

现象：

```text
pyserial is required
```

解决：

```powershell
python -m pip install pyserial
```

## 3. 串口打不开

常见原因：

- COM 号写错。
- 串口被 PlatformIO monitor 占用。
- 串口被另一个 Python 程序占用。
- M5Stack 没插好。
- USB 线只能充电不能传数据。
- 新电脑缺 USB 转串口驱动。

检查：

```powershell
pio device list
```

处理：

1. 关闭串口监视器。
2. 停止 Python 脚本。
3. 重新插拔 USB。
4. 换数据线。
5. 修改 `--source` 和 `--target` 参数。

## 4. 前端有页面但没有 EEG 数据

检查：

- 脑电设备是否开机。
- 脑电设备是否蓝牙连接到电脑。
- `--source` 是否是脑电蓝牙 COM 口。
- `--source-baud` 是否正确。Python 脚本默认 `57600`，当前现场启动脚本默认 `9600`。
- 脑电设备是否正在发送 ThinkGear 原始包。

前端没有收到真实 EEG 包时，脑电字段会显示 `--`，这是正常空状态，不是模拟数据。前端中的 `validPackets` 应持续增加。如果 `rawBytes` 增加但 `validPackets` 不增加，可能是波特率错误或协议不匹配。

## 5. EEG 有数据但 M5Stack 不显示

检查：

- `--target` 是否是 M5Stack 的 COM 口。
- M5Stack 固件是否已上传。
- Python 脚本是否能打开 M5Stack 串口。
- M5Stack 屏幕是否显示 `Dream M5 Gateway`。

M5Stack 正常时前端应显示：

```text
M5Stack 在线
M5 ESP-NOW tx 增加
```

## 6. M5Stack 有数据但 Microduino 不在线

检查：

- Microduino 是否上电。
- Microduino 固件是否已上传。
- M5Stack 和 Microduino 是否距离太远。
- 两块板是否使用同一个 `ESPNOW_CHANNEL`。
- Microduino 是否重启或供电不稳。

M5Stack 屏幕正常时应看到：

```text
MIC:OK
```

如果一直是：

```text
MIC:WAIT
```

说明 M5Stack 没有收到 Microduino 的状态回传。

## 7. 前端点击按钮但机器不动

先看按钮反馈：

| 前端反馈 | 含义 | 处理 |
| --- | --- | --- |
| `发送中` 后变 `已发送` | 命令已进入电脑到 M5Stack 的发送队列 | 继续看 M5Stack 和 Microduino 状态 |
| `失败` | 命令没有进入发送队列 | 查看页面提示和 Python 终端错误 |
| `target serial is not open` | M5Stack 串口没有打开 | 检查 `--target` COM 口，关闭串口监视器，重启脚本 |

`已发送` 不等于机器已经执行。最终是否执行，以 Microduino 状态回传和现场设备动作为准。

先确认系统总开关：

- 前端是否显示 `系统已开启`。
- M5Stack 是否显示 `SYS:ON`。
- 如果是 `SYS:OFF`，点击前端 `系统开启` 或按 M5Stack A 键。

再确认具体输出：

| 输出 | 检查 |
| --- | --- |
| 灯光 | DMX 接线、灯具地址、MAX485、电源 |
| 继电器 / 雾机 | `DREAM_ENABLE_RELAY_OUTPUT` 是否为 `1` |
| 步进电机 | `DREAM_ENABLE_STEPPER_OUTPUT` 是否为 `1`，目标是否选对左 / 右 / 左右 |

当前继电器默认物理输出关闭。步进电机物理输出已经启用用于台架调试；如果接了真实机械负载，先确认驱动器、电流、方向、限位、行程和急停。

## 8. 前端状态一直显示 `--`

这是前端的真实状态保护逻辑：没有收到真实数据时不显示假值。

| 显示 `--` 的区域 | 可能原因 |
| --- | --- |
| EEG 数据 | 脑电蓝牙未连接、`--source` COM 错、波特率错 |
| M5 / ESP-NOW 统计 | M5Stack 串口未打开、`--target` COM 错、M5 固件未运行 |
| Microduino 执行状态 | Microduino 未上电、ESP-NOW 不通、M5 没收到状态回传 |

如果 Microduino 曾经在线后又掉线，前端会清空执行状态，不继续显示旧状态。

## 9. 灯光不亮

检查：

- 系统是否开启。
- 前端是否点击了 `发送颜色` 或 `自动脑电`。
- Microduino 是否在线。
- DMX 灯地址是否为 001 和 005。
- 灯具是否处于 RGBW 4 通道模式。
- MAX485 DI 是否接到 `GPIO5`。
- DMX+ / DMX- 是否接反。
- 灯具电源是否正常。

## 10. 灯光颜色不对

可能原因：

- 灯具通道顺序不是 RGBW。
- 灯具不是 4 通道模式。
- 起始地址不正确。

处理：

1. 先运行 DMX 单项测试固件。
2. 确认每个通道对应颜色。
3. 再回到主固件。

DMX 测试入口：

```text
src/microduino_core_esp32_dmx_spotlight_test/main.cpp
```

## 11. 继电器 / 雾机不动作

检查：

- 是否已经系统开启。
- `DREAM_ENABLE_RELAY_OUTPUT` 是否改为 `1`。
- `RELAY_OUTPUT_PIN` 是否接对。
- `RELAY_ACTIVE_LEVEL` 是否符合继电器模块。
- 继电器模块控制侧是否与 Microduino 共地。
- 负载侧电源是否打开。

安全提醒：

继电器负载如果是高压交流设备，必须有外壳、绝缘、急停和独立电源保护。

## 12. 步进电机不动作

检查：

- 是否已经系统开启。
- `DREAM_ENABLE_STEPPER_OUTPUT` 是否改为 `1`。
- STEP / DIR 引脚是否接对。
- 驱动器是否有电机电源。
- 驱动器 EN 是否需要接线。
- 驱动器细分和电流限流是否正确。
- Microduino GND 是否与驱动器控制地相连。
- 前端目标是否选择了正确的 `左`、`右` 或 `左右`。
- `arg2` 目标掩码是否为 `1`、`2` 或 `3`。

注意：

当前 DMX 使用 `GPIO5`，左电机 STEP/DIR 为 `GPIO27/GPIO26`，右电机 STEP/DIR 为 `GPIO25/GPIO14`。接入真实机械负载前仍需要确认限位、方向、行程和硬件急停。

## 13. 系统运行一段时间后自动关闭

可能原因：

- EEG 超时。
- Python 脚本退出。
- M5Stack 掉线。
- ESP-NOW 丢包严重。
- Microduino 供电不稳重启。

这是安全逻辑，不是坏事。Microduino 设计上会在超时或未授权时关闭机器。

## 14. 上传失败

检查：

- COM 口是否正确。
- Python 脚本是否正在占用 M5Stack 串口。
- PlatformIO monitor 是否正在占用串口。
- 板子是否进入下载模式。
- USB 线是否支持数据。

处理顺序：

1. 停止 Python 脚本。
2. 关闭串口监视器。
3. 重新插拔 USB。
4. 再执行上传命令。

## 15. 现场紧急处理

优先级：

1. 前端点击 `全部停止`。
2. 按 M5Stack B 键。
3. 使用硬件急停。
4. 切断继电器 / 电机 / 灯具负载电源。

不要只依赖软件作为最终安全措施。
