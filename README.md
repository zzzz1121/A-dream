# A-dream

`A-dream` 是一个基于 ESP32 和灯光控制的装置项目。当前仓库还处在早期硬件测试阶段，主要用于验证 ESP32 开发板、WS2812B 灯带、FastLED 库和 PlatformIO 开发环境是否工作正常。

更完整的阶段说明见：[仓库现状说明](docs/repository-status.md)。

## 当前阶段

当前程序会依次执行：

- 清空所有灯珠
- 点亮第 1 颗灯珠为红色
- 点亮第 2 颗灯珠为绿色
- 点亮第 3 颗灯珠为蓝色

这段代码只是装置开发前的基础测试，不代表最终装置效果。

## 当前测试硬件

- Microduino Core ESP32、ESP32-WROOM-32 或 M5Stack Core ESP32 开发板
- WS2812B / NeoPixel 兼容灯带
- 外部 5V 电源，按灯珠数量和亮度选择足够电流
- 杜邦线

## 支持的开发板

| PlatformIO 环境 | 板子 |
| --- | --- |
| `microduino-core-esp32` | Microduino Core ESP32 |
| `esp32-wroom-32` | ESP32-WROOM-32 / ESP32 Dev Module |
| `m5stack-core-esp32` | M5Stack Core ESP32 |

## 接线

| WS2812B 灯带 | ESP32 |
| --- | --- |
| DIN | GPIO 4 |
| 5V | 5V 电源正极 |
| GND | ESP32 GND 和电源负极共地 |

注意：如果灯珠数量较多，不建议直接使用开发板 5V 引脚给整条灯带供电。请使用外部 5V 电源，并确保 ESP32 和灯带电源共地。

## 软件环境

推荐使用：

- VS Code
- PlatformIO IDE 扩展
- Git

项目依赖写在 `platformio.ini` 中，PlatformIO 编译时会自动安装：

```ini
lib_deps =
    fastled/FastLED
```

## 快速开始

克隆项目：

```powershell
git clone https://github.com/zzzz1121/A-dream.git
cd A-dream
```

编译：

```powershell
pio run
```

只编译指定板子：

```powershell
pio run -e microduino-core-esp32
pio run -e esp32-wroom-32
pio run -e m5stack-core-esp32
```

上传到开发板：

```powershell
pio run --target upload
```

上传到指定板子：

```powershell
pio run -e microduino-core-esp32 --target upload
pio run -e esp32-wroom-32 --target upload
pio run -e m5stack-core-esp32 --target upload
```

打开串口监视器：

```powershell
pio device monitor
```

默认串口波特率是 `115200`。

如果系统找不到 `pio` 命令，可以在 VS Code 的 PlatformIO 插件里直接点击 Build / Upload / Monitor。

## 常用配置

主要参数在 `src/main.cpp`：

```cpp
#define LED_PIN 4
#define NUM_LEDS 30
```

- `LED_PIN`：灯带数据线连接的 ESP32 引脚
- `NUM_LEDS`：灯带上的灯珠数量
- `FastLED.setBrightness(50)`：灯带亮度，范围 0-255

如果你的灯带颜色显示不对，可以尝试把 `GRB` 改成 `RGB`：

```cpp
FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
```

## 项目结构

```text
.
├── platformio.ini
├── src/
│   └── main.cpp
├── include/
├── lib/
└── test/
```

## 协作流程

开始写代码前先拉取最新版本：

```powershell
git pull
```

修改后提交：

```powershell
git add .
git commit -m "描述这次修改"
git push
```

如果多人同时开发，建议新建分支：

```powershell
git checkout -b feature/your-feature-name
git push -u origin feature/your-feature-name
```

然后在 GitHub 上创建 Pull Request，确认没问题后合并到 `main`。

## 故障排查

### 找不到 `FastLED.h`

确认 `platformio.ini` 中有：

```ini
lib_deps =
    fastled/FastLED
```

然后重新编译：

```powershell
pio run
```

### 灯带不亮

- 检查 DIN 是否连接到 GPIO 4
- 检查 ESP32 和灯带电源是否共地
- 检查灯带方向，数据线要接到输入端 DIN
- 检查灯珠数量是否和 `NUM_LEDS` 一致
- 尝试降低亮度，避免供电不足

### 颜色顺序不对

尝试把 `GRB` 改成 `RGB` 或其他颜色顺序。
