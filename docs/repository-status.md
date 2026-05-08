# 仓库现状说明

更新时间：2026-05-08

## 项目定位

`A-dream` 是一个装置项目仓库。当前仓库还处在早期硬件验证阶段，不是最终装置程序。

现阶段的重点是确认基础硬件链路是否可用：

- ESP32 开发板能否正常编译和上传程序
- WS2812B 灯带能否被 FastLED 控制
- 灯带数据引脚、供电和共地是否工作正常
- 多种 ESP32 板型是否都能使用同一份测试代码

## 当前实现内容

当前 `src/main.cpp` 是一个 LED 灯带测试程序。程序启动后会：

- 初始化串口，波特率为 `115200`
- 初始化 FastLED
- 清空所有灯珠
- 依次点亮第 1、2、3 颗灯珠为红、绿、蓝

这个程序的目的不是表现最终装置效果，而是验证灯带、电源、接线和开发环境。

## 当前硬件假设

当前代码默认：

- 灯带类型：WS2812B / NeoPixel 兼容灯带
- 数据引脚：GPIO 4
- 灯珠数量：30
- 颜色顺序：GRB
- 亮度：50 / 255

对应代码位置：

```cpp
#define LED_PIN 4
#define NUM_LEDS 30
FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
FastLED.setBrightness(50);
```

如果装置后续更换灯带长度、接线引脚、颜色顺序或亮度，需要同步修改这些参数。

## 支持的开发板

仓库当前配置了两个 PlatformIO 环境：

| 环境名 | PlatformIO board | 用途 |
| --- | --- | --- |
| `microduino-core-esp32` | `microduino-core-esp32` | Microduino Core ESP32 测试 |
| `esp32-wroom-32` | `esp32dev` | ESP32-WROOM-32 / 通用 ESP32 Dev Module 测试 |
| `m5stack-core-esp32` | `m5stack-core-esp32` | M5Stack Core ESP32 测试 |

两个环境共享：

- `espressif32` 平台
- Arduino framework
- `fastled/FastLED` 依赖
- `115200` 串口波特率

## 当前仓库结构

```text
.
├── README.md
├── platformio.ini
├── docs/
│   └── repository-status.md
├── src/
│   └── main.cpp
├── include/
├── lib/
└── test/
```

说明：

- `README.md`：给协作者快速了解和运行项目
- `docs/repository-status.md`：说明仓库当前阶段、已完成内容和后续方向
- `platformio.ini`：PlatformIO 板型、框架和库依赖配置
- `src/main.cpp`：当前 LED 硬件测试代码
- `include/`：预留头文件目录
- `lib/`：预留项目本地库目录
- `test/`：预留测试目录

## 已验证内容

已经完成的基础配置：

- PlatformIO 项目结构已建立
- FastLED 已加入 `lib_deps`
- Microduino Core ESP32 环境可编译
- ESP32-WROOM-32 环境可编译
- M5Stack Core ESP32 环境已加入配置
- `.pio` 构建缓存已通过 `.gitignore` 忽略
- GitHub 远程仓库已配置为 `https://github.com/zzzz1121/A-dream.git`

## 尚未完成内容

后续装置开发还需要继续明确：

- 装置最终交互方式
- 传感器或输入设备
- 灯光动画规则
- 供电方案和安全余量
- 外壳、结构或安装方式
- 多人协作的分支和合并流程
- 是否需要保存配置、联网或蓝牙控制

## 推荐下一步

建议按这个顺序继续推进：

1. 确认最终装置需要多少颗灯珠，以及实际供电方案。
2. 用当前测试程序验证两块板子都能控制灯带。
3. 把测试代码拆成更清晰的灯光函数，例如 `showColorTest()`、`clearStrip()`。
4. 根据装置概念设计第一版灯光效果。
5. 如果加入传感器，再为输入逻辑单独建模块。

## 协作提醒

这个仓库目前适合用作硬件验证和早期原型协作。协作者开始修改前应先运行：

```powershell
git pull
pio run
```

如果只测试某一块板子，可以指定环境：

```powershell
pio run -e microduino-core-esp32
pio run -e esp32-wroom-32
pio run -e m5stack-core-esp32
```

上传前请确认连接的是对应的开发板，并优先使用外部 5V 电源给灯带供电。
