# ESP32 Voice AI - 固件开发

> PlatformIO 配置、编译烧录流程、串口监视，以及 PC 端 Python 环境与系统依赖。

---


# 33. PlatformIO

固件使用 PlatformIO，两个 env：

| env | 说明 |
|-----|------|
| `env:esp32-s3-n16r8` | 串口版（回退 / 烧录），**不**启用 Wi-Fi |
| `env:esp32-s3-n16r8-wifi` | Wi-Fi 版（主链路），开启 `-DFIRMWARE_MODE_WIFI` |

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

编译 Wi-Fi 版本：

```bash
# 方式 1：直接指定 env
pio run -e esp32-s3-n16r8-wifi
pio run -e esp32-s3-n16r8-wifi --target upload

# 方式 2：临时加 build flag
pio run -DFIRMWARE_MODE_WIFI
pio run -DFIRMWARE_MODE_WIFI --target upload
```

> Wi-Fi 版本需要先编辑 [`config.local.json`](../config.local.json.example) 填入你的 SSID / 密码 / PC_HOST，
> PIO pre-build 会自动生成 `secrets.local.h`。详见第 38 节。

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

# 38. Wi-Fi 配置（config.local.json）

Wi-Fi / 网络配置**统一真相源**位于项目根 [`config.local.json`](../config.local.json)。
固件与 PC 端共享同一份配置，避免两处硬编码不同步。

工作流：

```text
config.local.json.example   ← 模板（入库）
  ↓  cp config.local.json.example config.local.json
config.local.json           ← 你实际填的（.gitignore，不入库）
  ↓  scripts/gen_secrets.py（PIO pre-build 自动跑，或手动）
firmware/esp32/src/secrets.local.h   ← 生成物（.gitignore，不入库）
  ↓  #include 到 wifi_client.cpp
```

> `.env` 只管 LLM API Key 等敏感信息，**不**再放网络配置。
> 需要改网络参数：只编辑 `config.local.json`，别去碰 secrets.h / config.py 的硬编码。

## 38.1 首次配置

```bash
cd ~/projects/esp32-voice-ai
cp config.local.json.example config.local.json
# 用你喜欢的编辑器打开 config.local.json，填 5 个字段
vim config.local.json
```

`config.local.json` 内容：

```json
{
  "wifi_ssid": "你的路由器名称",
  "wifi_pass": "你的路由器密码",
  "pc_host": "192.168.1.20",
  "pc_port": 8888,
  "esp32_hostname": "esp32-voice"
}
```

## 38.2 字段说明

| JSON 字段 | 对应 C 宏 | 说明 |
|----------|----------|------|
| `wifi_ssid` | `WIFI_SSID` | 路由器 SSID，ESP32-S3 仅支持 2.4 GHz |
| `wifi_pass` | `WIFI_PASS` | 路由器密码，WPA/WPA2 |
| `pc_host` | `PC_HOST` | Server 静态 IP 或 `my-pc.local`（mDNS 主机名） |
| `pc_port` | `PC_PORT` | Server 端口，默认 8888 |
| `esp32_hostname` | `ESP32_HOSTNAME` | mDNS 主机名，PC 侧可 `ping esp32-voice.local` |

## 38.3 编译 / 烧录

PIO 每次编译 `env:esp32-s3-n16r8-wifi` 前会自动跑 [`scripts/gen_secrets.py`](../scripts/gen_secrets.py)，
把 JSON 转成 [`firmware/esp32/src/secrets.local.h`](../firmware/esp32/src/secrets.local.h)：

```bash
cd ~/projects/esp32-voice-ai/firmware/esp32
pio run -e esp32-s3-n16r8-wifi                # 编译（自动先跑 gen_secrets.py）
pio run -e esp32-s3-n16r8-wifi --target upload # 烧录
```

日志会看到：

```text
[gen_secrets] source : config.local.json
[gen_secrets] output : firmware/esp32/src/secrets.local.h
[gen_secrets] wifi_ssid      = YourSSID
[gen_secrets] pc_host        = 192.168.1.20:8888
```

如需手动跑（例如 pre-script 失败时）：

```bash
cd ~/projects/esp32-voice-ai
python3 scripts/gen_secrets.py
```

## 38.4 PC 端读取

[`pc/config.py`](../pc/config.py) 启动时会读同一份 `config.local.json`，映射到：

| Python 常量 | 来源 |
|------------|------|
| `WIFI_TCP_PORT` | `pc_port` |
| `ESP32_HOSTNAME` | `esp32_hostname` |
| `PC_HOST` | `pc_host` |
| `WIFI_SSID` / `WIFI_PASS` | `wifi_ssid` / `wifi_pass`（供日志/调试用） |

`config.local.json` 不存在或字段缺失时，Python 端**静默回退到硬编码默认值**，不影响非 Wi-Fi 路径。

## 38.5 启用 Wi-Fi 模式

固件按 `FIRMWARE_MODE_WIFI` 宏分派：

* 未定义 → 走 USB Serial 回退链路（`env:esp32-s3-n16r8`）
* 已定义 → 走 Wi-Fi / TCP 主链路（`env:esp32-s3-n16r8-wifi`，见 [`platformio.ini`](../firmware/esp32/platformio.ini)）

## 38.6 常见故障

| 现象 | 可能原因 | 排查动作 |
|------|---------|---------|
| 编译期 `[gen_secrets] NOTE: 未找到 config.local.json` | 未复制模板 | `cp config.local.json.example config.local.json` |
| 编译期 pre-script 失败 | Python 未装 / 路径不对 | 手动 `python3 scripts/gen_secrets.py` 定位；或临时注释 platformio.ini 的 `pre_script_hooks` |
| 连不上 Wi-Fi | SSID/密码错、2.4 GHz、AP isolation | 串口日志 `[wifi] connecting ...` 阶段会打印失败原因 |
| 连上 Wi-Fi 但收不到 PLAY | `ping PC_HOST` 不通 / 防火墙 | 检查 pc_host 与防火墙 |
| 上行 RECM 无响应 | 状态机或端口错 | 确认 mic_uploader 进入 REC、Server 端口是 8888 |

参考：
- [`firmware/esp32/src/secrets.h`](../firmware/esp32/src/secrets.h)（C 宏模板与回退）
- [`scripts/gen_secrets.py`](../scripts/gen_secrets.py)（JSON → C 头文件生成器）
- [`scripts/piopre.py`](../scripts/piopre.py)（PlatformIO pre-build 钩子）
- [`pc/config.py`](../pc/config.py)（Python 端加载逻辑）

---

# 39. aidlux 部署提示（Server 侧）

> 网络配置与配对方案（mDNS / NVS / Web UI / SoftAP）见 [`docs/network-config.md`](./network-config.md)。

Server 端 `wifi_server.py` 只用 Python 标准库（`socket` / `threading` / `struct` / `argparse` / `wave` / `wave` / `io`），**不使用 asyncio，不用第三方框架**，方便日后迁移到 aidlux（安卓 slim Python）。

在 aidlux 上运行时：

* 只保留 `wifi_server.py` + 精简后的 `voice_pipeline.py`
* `config.py` 全部改为常量或读文件，不依赖 `.env` / `pydantic-settings`
* ASR / TTS 用设备端可执行的推理（例如 `whisper.cpp` 二进制 + `curl`，或本地 TTS）
* 详细方案见 [`docs/architecture.md`](./architecture.md#105-aidlux-迁移预留) 与 [`docs/protocol.md`](./protocol.md#10-aidlux-兼容性)

---

# 40. 版本

| 版本 | 日期 | 说明 |
|------|------|------|
| v1 | 2026-09-01 | 初版：PlatformIO、USB Serial、PC Python 环境 |
| v2 | 2026-09-09 | 新增 Wi-Fi env、secrets.h 配置、aidlux 部署提示 |
| v3 | 2026-09-10 | 网络配置单一真相源：`config.local.json` + `scripts/gen_secrets.py` + PIO pre-build hook |

---
