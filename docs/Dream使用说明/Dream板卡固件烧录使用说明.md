# Dream 板卡固件烧录使用说明

更新时间：2026-05-20  
适用工程：PlatformIO

## 1. 板卡与环境

| 板卡 | PlatformIO 环境 | 固件入口 | 当前职责 |
| --- | --- | --- | --- |
| M5Stack Core ESP32 | `m5stack-core-esp32` | `src/m5stack_core_esp32_test/main.cpp` | USB 串口网关、屏幕监测、ESP-NOW 转发 |
| Microduino Core ESP32 | `microduino-core-esp32` | `src/microduino_core_esp32_test/main.cpp` | ESP-NOW 接收、DMX 灯光、机器安全状态机 |

当前 `platformio.ini` 中默认端口：

| 环境 | 上传端口 | 监视端口 |
| --- | --- | --- |
| `m5stack-core-esp32` | `COM6` | `COM6` |
| `microduino-core-esp32` | `COM7` | `COM7` |

端口会随电脑、USB 口、线材变化。实际使用前先查看：

```powershell
pio device list
```

## 2. 编译固件

编译 M5Stack：

```powershell
pio run -e m5stack-core-esp32
```

编译 Microduino：

```powershell
pio run -e microduino-core-esp32
```

如果 `pio` 命令不可用，使用完整路径：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e m5stack-core-esp32
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e microduino-core-esp32
```

## 3. 上传固件

上传 M5Stack：

```powershell
pio run -e m5stack-core-esp32 -t upload
```

上传 Microduino：

```powershell
pio run -e microduino-core-esp32 -t upload
```

如果上传失败：

- 确认串口号是否正确。
- 关闭串口监视器。
- 关闭正在运行的 Python 桥接脚本。
- 重新插拔 USB。
- 检查驱动是否安装。

## 4. 串口监视

监视 M5Stack：

```powershell
pio device monitor -p COM6 -b 115200
```

监视 Microduino：

```powershell
pio device monitor -p COM7 -b 115200
```

注意：同一个 COM 口不能同时被上传、监视器、Python 脚本占用。

## 5. M5Stack 使用说明

M5Stack 是电脑和 Microduino 之间的网关。

职责：

- 从电脑 USB 串口接收 `EEG` 脑电帧。
- 从电脑 USB 串口接收 `CMD` 控制帧。
- 屏幕显示 EEG、USB、ESP-NOW、Microduino 状态。
- 通过 ESP-NOW 转发 EEG 和控制指令到 Microduino。
- 接收 Microduino 状态回传。
- 通过串口输出 `EVENT=M5_STATUS ...`，供电脑端前端显示真实链路和执行状态。

按键：

| 按键 | 作用 |
| --- | --- |
| A | 系统开启 |
| B | 全部停止 |
| C | 系统关闭 |

屏幕关键字段：

| 字段 | 说明 |
| --- | --- |
| `USB` | 电脑到 M5Stack 串口状态 |
| `ESP` | M5Stack 到 Microduino ESP-NOW 发送状态 |
| `Signal` | 脑电 `poorSignal` |
| `Att` | 专注度 |
| `Med` | 放松度 |
| `MIC` | Microduino 状态回传 |
| `Light` | 灯光状态 |
| `Step` | 步进电机状态 |
| `Relay` | 继电器状态 |
| `Safety` | 安全状态 |
| `SYS` | 系统总开关状态 |

## 6. Microduino 使用说明

Microduino 是执行控制器。

职责：

- 接收 M5Stack 发来的 ESP-NOW EEG 包。
- 接收 M5Stack 发来的 ESP-NOW 控制包。
- 本地判断系统是否开启、脑电是否超时、信号是否过差。
- 控制 DMX 灯光。
- 预留继电器和步进电机状态机。
- 回传执行状态给 M5Stack。
- Microduino 掉线或没有状态回传时，电脑前端不会继续显示旧执行状态。

上电默认状态：

```text
systemEnabled = false
灯光关闭
继电器关闭
步进电机停止
```

只有收到 `SYSTEM_ENABLE` 后才允许动作。

## 7. 启用继电器输出

当前默认：

```cpp
#define DREAM_ENABLE_RELAY_OUTPUT 0
```

确认以下内容后再改为 `1`：

- 继电器控制引脚正确。
- 继电器是高电平触发还是低电平触发。
- 负载电源和继电器额定参数匹配。
- 雾机或负载有独立电源保护。
- 有硬件急停或断电方案。

相关参数：

```cpp
#define RELAY_OUTPUT_PIN 33
#define RELAY_ACTIVE_LEVEL HIGH
#define RELAY_MANUAL_MAX_ON_MS 5000
```

手动开启继电器也有最大开启时长保护，避免长时间吸合。

## 8. 启用步进电机输出

当前默认：

```cpp
#define DREAM_ENABLE_STEPPER_OUTPUT 0
```

确认以下内容后再改为 `1`：

- STEP / DIR / EN 引脚不与 DMX 冲突。
- 驱动器电流限流设置正确。
- 电机电源独立且稳定。
- 有机械限位或安全行程限制。
- 观众无法触碰运动危险区域。

当前 DMX 使用：

```cpp
#define DMX_TX_PIN 27
```

正式启用前必须再次核对 DMX、步进和继电器的引脚分配，避免多个输出复用同一个 GPIO。

## 9. 烧录后最小测试

1. 先只接 M5Stack 和 Microduino，不接雾机和电机负载。
2. 上传两块板固件。
3. 启动电脑端脚本和前端。
4. 确认 M5Stack 显示 `MIC:OK`。
5. 前端点击 `系统开启`。
6. 确认前端按钮反馈为 `已发送`。
7. 测试灯光颜色切换，并观察 Microduino 回传状态。
8. 点击 `全部停止`，确认灯光关闭。
9. 再接入继电器和电机进行单项测试。
