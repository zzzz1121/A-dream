# Dream 前端启动与停止命令

本文只记录电脑端前端和桥接服务的启动、停止命令。

## 启动前端

在仓库根目录运行：

```powershell
powershell -ExecutionPolicy Bypass -File tools\start_dream_frontend.ps1
```

启动后打开：

```text
http://127.0.0.1:8765/
```

默认参数：

```text
脑电串口: COM10@9600
M5Stack 串口: COM6@115200
前端端口: 8765
```

## COM 口变化时

先查看当前电脑识别到的串口：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device list
```

如果脑电或 M5Stack 的 COM 口变化，不需要改代码，启动时覆盖参数即可：

```powershell
powershell -ExecutionPolicy Bypass -File tools\start_dream_frontend.ps1 -Source COM3 -Target COM6
```

其中：

```text
-Source: 脑电蓝牙串口
-Target: M5Stack USB 串口
```

如果波特率也需要调整：

```powershell
powershell -ExecutionPolicy Bypass -File tools\start_dream_frontend.ps1 -Source COM3 -SourceBaud 9600 -Target COM6 -TargetBaud 115200
```

如果前端端口 `8765` 被占用：

```powershell
powershell -ExecutionPolicy Bypass -File tools\start_dream_frontend.ps1 -WebPort 8766
```

对应打开：

```text
http://127.0.0.1:8766/
```

## 停止前端

在仓库根目录运行：

```powershell
powershell -ExecutionPolicy Bypass -File tools\stop_dream_frontend.ps1
```

默认会停止 `127.0.0.1:8765` 上的 Dream 前端/桥接进程。

如果启动时使用了其他端口，停止时也要指定相同端口：

```powershell
powershell -ExecutionPolicy Bypass -File tools\stop_dream_frontend.ps1 -WebPort 8766
```

如果脚本提示该端口有监听进程，但不像 Dream 前端，不会默认停止。确认就是要停这个端口时，再运行：

```powershell
powershell -ExecutionPolicy Bypass -File tools\stop_dream_frontend.ps1 -AllowUnknownProcess
```
