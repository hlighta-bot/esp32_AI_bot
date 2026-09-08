# ESP32 Voice AI - 固件开发

> PlatformIO 配置、编译烧录流程、串口监视，以及 PC 端 Python 环境与系统依赖。

---


# 33. PlatformIO

固件使用 PlatformIO。

安装：

```text
VS Code
+
PlatformIO
```

进入：

```bash
cd firmware/esp32
```

编译：

```bash
pio run
```

烧录：

```bash
pio run --target upload
```

---

# 34. ESP32 固件编译流程

第一次使用：

```bash
cd firmware/esp32
```

然后：

```bash
pio run
```

确认：

```text
SUCCESS
```

再：

```bash
pio run --target upload
```

确认：

```text
SUCCESS
```

---

# 35. 查看 ESP32 串口

可以使用：

```bash
pio device list
```

查看设备。

串口监视：

```bash
pio device monitor -b 921600
```

退出：

```text
Ctrl + C
```

---

# 36. PC Python 环境

进入：

```bash
cd pc
```

创建虚拟环境：

```bash
python3 -m venv esp32_voice_ai_env
```

激活：

```bash
source esp32_voice_ai_env/bin/activate
```

Windows PowerShell：

```powershell
.\esp32_voice_ai_env\Scripts\Activate.ps1
```

安装依赖：

```bash
pip install -r requirements.txt
```

---

# 37. 系统依赖

Linux 下可能需要：

```text
ffmpeg
portaudio
ALSA
```

例如：

```bash
sudo apt install ffmpeg
```

PortAudio：

```bash
sudo apt install portaudio19-dev
```

ALSA：

```bash
sudo apt install libasound2-dev
```

---
