# 测试用例 · 2026-09-09 · Phase 1 Wi-Fi 语音闭环

> **文档编号**：TC-20260909-P1-WIFI-VOICE
> **版本**：v1.1
> **适用代码**：Phase 1（`env:esp32-s3-n16r8-wifi`）
> **测试目标**：验证端到端语音闭环——`MAX9814 → ESP32 ADC → VAD → TCP RECM → Server (ASR+LLM+TTS) → TCP PLAY → ESP32 I2S → MAX98357A → Speaker`
>
> **测试时长**：全流程走一遍约 20–30 分钟；每个 TC 独立完成，可按编号跳测。

---

## 0. 目录

| 编号 | 测试项 | 时长 | 依赖 |
|------|--------|------|------|
| TC-01 | 硬件接线与供电检查 | 5 min | 无 |
| TC-02 | 固件编译烧录（Wi-Fi 版） | 5 min | TC-01 |
| TC-03 | Wi-Fi STA 连接路由器 | 3 min | TC-02 |
| TC-04 | TCP 长连接到 Server | 3 min | TC-03 + Server 起 |
| TC-05 | MAX9814 ADC 采集与预处理 | 5 min | TC-04 |
| TC-06 | Energy VAD 触发与静音判定 | 5 min | TC-05 |
| TC-07 | RECM / RPTF 上行帧 | 5 min | TC-06 |
| TC-08 | Server 端 ASR+LLM+TTS 流水线 | 3 min | TC-07 |
| TC-09 | PLAY 下行 + ACK 分块 | 5 min | TC-08 |
| TC-10 | **端到端语音闭环** | 10 min | TC-01~09 |
| TC-11 | 断线自动重连 | 5 min | TC-10 |
| TC-12 | USB Serial 回退链路 | 5 min | TC-01 |

**附录**：A 参数速查 · B 诊断命令 · C 常见故障 · D 参考文档 · E 执行记录模板 · **F WSL2 NAT 端口转发 + Windows 防火墙**（**若 Server 跑在 WSL2 内必读**）

---

## 通用前置条件

```text
[ ] ESP32-S3 N16R8 板子（本次测试用主板）
[ ] MAX9814 麦克风模块
[ ] MAX98357A I2S 功放模块
[ ] 3W 扬声器
[ ] USB 数据线（能传数据，不是仅充电线）
[ ] 电脑已装 PlatformIO（VS Code 插件）
[ ] 电脑已装 Python 3.9+
[ ] Wi-Fi 路由器可用
[ ] .env 已配置 API Key（用于 LLM）
[ ] Whisper.cpp 模型已下载
```

---

## TC-01 · 硬件接线与供电检查

**目的**：确保所有硬件在电路上正确连接，避免后续测试反复排查。

### 步骤

1. 用万用表电压档分别量：
   - ESP32 3V3 引脚对 GND 应为 3.2–3.4 V
   - 扬声器电源对 GND 应为 5.0–5.3 V（USB 供电）
2. 万用表蜂鸣档量所有 GND：ESP32 GND、MAX9814 GND、MAX98357A GND 应全部通（<0.1 Ω）
3. 断电，检查接线表（参考 [`docs/wiring.md`](./wiring.md)）：

   | 信号 | MAX9814 引脚 | 连接目标 | 备注 |
   |------|-------------|---------|------|
   | 电源地 | GND | ESP32 GND | 必须共地 |
   | 电源 | VDD | ESP32 3V3 | 加 100 nF 电容到 GND |
   | 音频输出 | Out | ESP32 GPIO1 | ADC1_CH0 |
   | 增益选择 | GAIN | **悬空** | +50 dB（默认推荐） |
   | 耦合选择 | AR | **悬空** | DC-coupled（≈VDD/2 偏置） |

   | 信号 | MAX98357A | ESP32 GPIO |
   |------|-----------|-----------|
   | SD (I2S DIN) | SD | GPIO15 |
   | SCK (BCLK) | SCK | GPIO16 |
   | LRC (LRCLK) | LRC | GPIO17 |
   | VCC | 5V | 5V |
   | GND | GND | GND |
   | OUT | 扬声器 | 扬声器 |

### 期望结果

```text
- ESP32 3V3 电压 3.2–3.4 V
- 扬声器电源电压 5.0–5.3 V
- MAX9814 Out 引脚静态电压 ≈ 1.65 V（3V3 供电，VDD/2，DC-coupled）
- 所有 GND 之间电阻 < 0.1 Ω
- 接线表完全匹配（GAIN / AR 均悬空）
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| ESP32 3V3 = 0 V | USB 线仅充电 / ESP32 供电脚虚焊 |
| 扬声器电源 = 0 | MAX98357A 未接到 5V 引脚 |
| MAX9814 Out ≈ 0 V | AR 接到了 VDD（切到 AC-coupled）—— 按接线表把 AR 悬空 |
| MAX9814 Out ≈ 3.3 V 或大幅波动 | GAIN 错接到 VDD 且输入过强，或 VDD/GND 之间未加 100 nF 电容 |
| GND 不通 | 未共地，音频会有很大噪声 |
| 接线错误 | 断电后按表重接 |

---

## TC-02 · 固件编译烧录（Wi-Fi 版）

**目的**：验证 PlatformIO 能编出 Wi-Fi 变体固件并烧录成功。

### 前置条件

- 项目根目录下 `config.local.json` 已存在（首次从 `config.local.json.example` 复制）
  且填写了正确的 `wifi_ssid` / `wifi_pass` / `pc_host`
  （PIO pre-build 会自动调用 `scripts/gen_secrets.py` 生成 `secrets.local.h`）

### 步骤

1. VS Code 打开项目根目录 `/home/hqb/projects/esp32-voice-ai`
2. 首次配置网络（只需一次）：
   ```bash
   cd ~/projects/esp32-voice-ai
   cp config.local.json.example config.local.json
   # 用编辑器打开 config.local.json，填 wifi_ssid / wifi_pass / pc_host / pc_port / esp32_hostname
   ```
3. 打开固件目录 `firmware/esp32`
4. PlatformIO 侧边栏选 env：`esp32-s3-n16r8-wifi`
5. 终端运行：
   ```bash
   cd ~/projects/esp32-voice-ai/firmware/esp32
   export PATH="$HOME/.platformio/penv/bin:$PATH"
   pio run -e esp32-s3-n16r8-wifi
   ```
   > PIO pre-build 会先跑 `scripts/gen_secrets.py`，把 `config.local.json` 转成
   > `firmware/esp32/src/secrets.local.h`，日志里能看到 `[gen_secrets] output : ...`。
6. 等编译完成，然后：
   ```bash
   pio run -e esp32-s3-n16r8-wifi --target upload
   ```
7. 等上传完成，然后：
   ```bash
   python3 -m serial.tools.miniterm /dev/ttyACM0 921600
   ```

### 期望结果

```text
[ ] 编译：SUCCESS (耗时约 60-120 s)
[ ] 烧录：Writing at 0x... / Writing at 0x...
[ ] 烧录：Successfully uploaded 488xxxx bytes
[ ] ESP32 自动重启，串口输出包含：
    - "Wi-Fi mode enabled (TCP client + ADC mic)."
    - "ESP32-S3 WAV AUDIO PLAYER"
    - "I2S initialized."
    - "READY"
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 编译失败 | 检查 `platformio.ini` 的 `lib_deps`；`FIRMWARE_MODE_WIFI` 是否定义 |
| 烧录失败 `No serial ports found` | ESP32 未连 USB / 驱动未装（Ch340 / CP210x） |
| 烧录后串口无输出 | USB CDC 未开：`-DARDUINO_USB_CDC_ON_BOOT=1`；或波特率不对 |
| `secrets.h` 编译报错 | 检查 `scripts/gen_secrets.py` 是否正常执行；确认 `config.local.json` JSON 语法有效；`secrets.local.h` 中引号闭合 |

---

## TC-03 · Wi-Fi STA 连接路由器

**目的**：验证 ESP32 能正确连上路由器。

### 步骤

1. 用 USB 线连 ESP32，打开串口监视：
   ```bash
   pio device monitor -b 921600
   ```
2. 观察串口输出

### 期望结果

```text
[wifi] ssid=<你的SSID>
[wifi] server=<你的PC_HOST>:8888
[wifi] connecting <你的SSID> ...
[wifi] ip=192.168.x.x        ← 拿到 IP 说明 STA 连上
[wifi] mDNS ready
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| `connecting ... timeout` | SSID 或密码错；路由器 5GHz-only（ESP32 只支持 2.4GHz） |
| `connecting` 一直 `.` 不返回 | 路由器 AP isolation 打开；信号弱（RSSI < -70 dBm） |
| 拿到 IP 但 `[wifi] mDNS ready` 不出现 | mDNS 端口冲突；非致命，不影响主链路 |
| `ip=` 输出的是 `0.0.0.0` | DHCP 没响应；检查路由器 DHCP 是否开启 |

---

## TC-04 · TCP 长连接到 Server

**目的**：验证 ESP32 能主动连到 PC Server 的 TCP 端口 8888。

### 前置条件

- TC-03 通过，ESP32 拿到 IP
- `config.local.json` 里 `pc_host` 指向可路由到的地址（PIO pre-build 生成到 `secrets.local.h`）

### 步骤

1. PC 侧起 Server：
   ```bash
   cd ~/projects/esp32-voice-ai/pc
   python wifi_server.py --port 8888
   ```
2. 观察 Server 输出
3. 观察 ESP32 串口

### 期望结果

**PC Server 侧**：
```text
[server] listening on 0.0.0.0:8888 (engine=sensenova)
[server] waiting for ESP32 clients...
[server] client connected: ('192.168.x.x', 54xxx)   ← ESP32 IP
```

**ESP32 串口侧**：
```text
[wifi] tcp connected
```

（当前代码未打印这条日志，但可以通过 Server 侧"client connected"确认；如需明确日志，可给 `WifiClient::tryConnectTcp()` 加 `Serial.println("[wifi] tcp connected")`。）

### 排查思路

| 现象 | 可能原因 | 排查/修复 |
|------|----------|----------|
| Server 端无 `client connected` | PC_HOST 写错 | 检查 `config.local.json` 的 `pc_host` |
| Server 端无 `client connected` | Windows 防火墙挡了 8888 | `New-NetFirewallRule ... -LocalPort 8888 -Action Allow`（见附录 F） |
| Server 端无 `client connected` | 不同网段 | ESP32 和 PC 必须在同一 VLAN |
| Server 端无 `client connected` | **WSL2 NAT 模式，ESP32 打的是 Windows IP，但 `wifi_server.py` 跑在 WSL 内** | 加 portproxy 转发 + 防火墙规则（见附录 F） |
| ESP32 反复打印 `tryConnect` 但不成功 | Server 没起 / 端口被占用 | `lsof -i:8888` 查 |
| Server 报 `Address already in use` | 端口 8888 被其他进程占 | `lsof -i:8888` 找到并 kill |
| `Test-NetConnection <IP> -Port 8888` 返回 `TcpTestSucceeded: False` | 该 IP:8888 无监听 | 见附录 F 定位 IP 属于 Windows 还是 WSL |

**WSL2 NAT 模式的典型故障模式**（本次 TC-04 实际遇到）：

```text
ESP32 ──TCP──> 192.168.0.3:8888 (Windows 宿主)  ❌ 无监听
                    │
                    └─── WSL2 (172.25.219.111:8888) ✅ wifi_server.py 在这里
```

修复方式见 **附录 F · WSL2 NAT 模式下端口转发 + Windows 防火墙**。

---

## TC-05 · MAX9814 ADC 采集与预处理

**目的**：验证 ADC 采集链路工作，信号被正确重采样和滤波。

### 前置条件

- TC-04 通过（Wi-Fi + TCP 链路 OK）
- 已烧录独立的麦克风测试固件 [`firmware/esp32-mic-test`](../firmware/esp32-mic-test/platformio.ini)（**推荐**）

### 步骤

**推荐路径：烧录独立测试固件（最直观，能直接看波形）**

1. 编译并烧录独立测试固件：
   ```bash
   cd ~/projects/esp32-voice-ai/firmware/esp32-mic-test
   pio run -e esp32-s3-mic-test
   pio run -e esp32-s3-mic-test --target upload
   ```
2. 打开串口监视：
   ```bash
   pio device monitor -b 921600   #115200
   ```
3. 观察启动信息：
   ```text
   ==========================================
     MAX9814 Mic Test
   ==========================================
   ADC pin        : GPIO1 (ADC1_CH0)
   Expected silent: raw ~= 2048 (dc ~= 0)
   ```
4. 保持安静 5 秒 → 每 100 ms 一行 `raw≈2048 / rms 小 / bar 全是点`
5. 对着麦克风大声说几句话（贴近 10–30 cm）→ 观察 `raw` 偏离 2048、`rms` 变大、`bar` 出现 `#`
6. 松开后 1–2 秒，`rms` 应回落到静默水平

**回退路径：主固件**（暂时不想烧另一个固件时）

1. 主固件已连上 Server（TC-04 通过）
2. 在 [`firmware/esp32/src/audio/mic_uploader.cpp`](../firmware/esp32/src/audio/mic_uploader.cpp) 的 VAD 状态切换处加一行：
   ```cpp
   Serial.printf("[vad] %d\n", vad.getState());
   ```
3. 重新 `pio run -e esp32-s3-n16r8-wifi --target upload`
4. 对着 MAX9814 说几句话
5. 观察 Server 端有没有 `RECM` 日志

### 期望结果

**独立测试固件（推荐）**：

```text
[ ] 启动信息显示 GPIO1 (ADC1_CH0)、Expected silent raw ~= 2048
[ ] 静默时：raw 在 2040–2056 之间抖动，rms < 30，bar 全 `.`
[ ] 说话时：raw 明显偏离 2048（±几百以上），rms > 200，bar 出现 `#`
[ ] 松开后：rms 在 1–2 秒内回落
```

**主固件（回退路径）**：

- 环境安静 → 没有 RECM 帧（VAD 处于 IDLE）
- 环境嘈杂 / 对麦说话 → Server 端有 RECM 日志

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 独立固件：静默时 raw 稳定在 0 或 4095 | 未共地 / VDD 没接 3V3 / Out 线断 |
| 独立固件：静默时 raw 稳定在 ~1650 附近 | AR 接到了 VDD（切到 AC-coupled），把 AR 悬空 |
| 独立固件：静默时 raw 稳定在 ~2048 但说话没反应 | 麦克风损坏 / 距离太远 / GAIN 错接到 GND（+40 dB 太弱） |
| 独立固件：说话时 rms 变化 < 30 | 100 nF 电容缺失（Wi-Fi 电流干扰吃掉信号）/ 麦克风离口太远 |
| 独立固件：rms 一直很大 / bar 满 | GAIN 接到 VDD（+60 dB 过强）/ MAX98357A 5V 电源串扰 |
| 主固件：一直 IDLE | VAD 阈值过高 / MAX9814 未接线 / GPIO1 接错 |
| 主固件：一直 SPEAK（噪声触发） | 阈值过低 / 麦克风接地不良 |
| 主固件：声音极小 | MAX9814 VDD 电压不稳 / 距离太远 |
| 主固件：声音巨大 / 削波 | LP 滤波增益过高；检查 `mic_adc.cpp` 中 `gain ×2` |

---

## TC-06 · Energy VAD 触发与静音判定

**目的**：验证 VAD 状态机能正确识别语音开始和结束。

### 步骤

1. 保持安静 5 秒 → 观察 VAD 应为 `IDLE`
2. 大声说 "Hello"（持续 2 秒）→ 观察 VAD 应进入 `SPEAK`
3. 停止说话，保持安静 700 ms（默认 `VAD_SILENCE_MS`）→ 观察 VAD 应进入 `END`
4. 观察 Server 端

### 期望结果

**当前日志（Server 侧观察）**：
```text
（安静期）无日志
（说话期）Server 端看到 RECM chunk 涌入
（静音 700 ms 后）Server 端收到 RPTF，pipeline 触发
```

**VAD 阈值参考**（`config` 中默认）：
- `VAD_RMS_THRESHOLD`：RMS 阈值
- `VAD_MIN_VOICE_MS`：最小语音持续
- `VAD_SILENCE_MS`：静音判定窗口（默认 700 ms）

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 说了话但 VAD 不触发 | 阈值过高；调低 `VAD_RMS_THRESHOLD` |
| 一直触发，不说话也 RPTF | 阈值过低；调高 |
| 说话 1 秒就 END | `VAD_SILENCE_MS` 太短；调到 800–1000 ms |
| 说完要 2 秒才 END | 静音窗口太长；调到 500 ms |

---

## TC-07 · RECM / RPTF 上行帧

**目的**：验证协议帧格式符合 [`docs/protocol.md`](./protocol.md) v2 定义。

### 步骤

1. 起 Server
2. 对着麦克风说一段话，然后保持安静
3. 用 tcpdump / wireshark 抓 ESP32 → PC 的 8888 端口流量（可选，进阶）：
   ```bash
   sudo tcpdump -i any -X 'tcp port 8888'
   ```

### 期望结果

**协议帧**（十六进制）：

上行 RECM（第一 chunk）：
```text
52 45 43 4D       ← "RECM" magic
00 03             ← flags = FIRST | VAD_TRIGGER
00 00 10 00       ← chunk_size = 4096
<4096 bytes of PCM 16-bit mono 16kHz>
```

上行 RECM（后续 chunk）：
```text
52 45 43 4D
00 00             ← flags = 0
00 00 10 00       ← chunk_size = 4096
<PCM>
```

上行 RPTF（段末）：
```text
52 50 54 46       ← "RPTF" magic
00 10 00 00       ← total_size 例 65536 字节
```

**tcpdump 期望**：
```text
IP 192.168.x.x.54xxx > 192.168.x.x.8888: tcp ...
  0000  5245 434d 0003 0000 1000 ...
```

**Server 端日志**（当前实现）：
```text
[server] REC chunk #1 (4096 bytes, flags=FIRST|VAD_TRIGGER)
[server] REC chunk #2 (4096 bytes)
...
[server] RPTF total_size=XXXXX, running pipeline...
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 只收到 RECM 无 RPTF | VAD 未触发 END / 单段录音超 4 MB 上限 |
| RECM 帧大小不是 4096 | chunk_size 定义改动；检查 `frame.h` |
| flags 位错乱 | VAD 状态机时序问题 |
| 帧头不是 `RECM` | 协议 magic 定义改动；两端必须一致 |

---

## TC-08 · Server 端 ASR+LLM+TTS 流水线

**目的**：验证收到 RPTF 后，Server 能正确调用 Whisper → LLM → Edge TTS。

### 步骤

1. ESP32 已连上 Server，Server 已能收 RECM/RPTF
2. 对着 ESP32 的 MAX9814 清晰说一句：`你好，你是谁？`
3. 保持安静 1 秒（触发 VAD_END）
4. 观察 Server 端输出

### 期望结果

```text
[server] REC chunk #1 ...
[server] RPTF total_size=XXXXX
[server] ASR start
[server] ASR done: "你好，你是谁？"      ← Whisper 结果
[server] LLM start (engine=sensenova)
[server] LLM done: "我是 ESP32 Voice AI，..."  ← LLM 回复
[server] TTS start
[server] TTS done, size=XXXXX bytes
[server] Sending PLAY size=XXXXX
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| ASR 返回空 | Whisper 模型未下载 / 采样率不匹配 |
| LLM 超时 | API Key 无效 / 网络问题；查 `.env` |
| TTS 返回 None | edge-tts 网络问题；换网络重试 |
| 整条链失败 | Server 日志末尾有异常堆栈；直接看栈 |

---

## TC-09 · PLAY 下行 + ACK 分块

**目的**：验证 TTS 输出能被 ESP32 正确接收、按 chunk 发送并 ACK。

### 步骤

1. TC-08 触发一次完整 pipeline
2. 观察 Server 与 ESP32 双向日志

### 期望结果

**Server 端**：
```text
[server] Sending PLAY size=XXXXX bytes
[server] PLAY chunk 1/5 sent, ack ok
[server] PLAY chunk 2/5 sent, ack ok
...
[server] PLAY complete
```

**ESP32 串口**（如果开了 DEBUG）：
```text
[play] data size: XXXXX
[play] chunk 1/5
[play] chunk 2/5
...
[play] done
```

**扬声器**：听到 LLM 的回复。

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| PLAY 发送卡住 | ESP32 ACK 丢失；TCP_NODELAY 未开；检查 `wifi_client.cpp` 的 `mutex` |
| 声音有爆音 / 断续 | I2S 播放速度不匹配；`SAMPLE_RATE` 两端不一致 |
| 只发了一半 | 单 chunk 超时；`TCP_IO_TIMEOUT` 太短 |
| 无声 | MAX98357A 未接扬声器 / PA_EN 未使能 / 静音 pin 电平错 |

---

## TC-10 · 端到端语音闭环（**核心测试**）

**目的**：完整跑一次语音对话，验证所有环节串联。

### 步骤

1. ESP32 上电，Server 已启动
2. 保持安静，确认 Server 无输出
3. 对 MAX9814 清晰说话：
   ```
   "你好，请告诉我一个笑话。"
   ```
4. 说完停 1 秒
5. 等待回复
6. 回复播完后，重复步骤 3（换个话题）

### 期望结果

| 时间点 | 观察 |
|--------|------|
| t=0 | 安静，Server 无输出 |
| t=1s（开始说话） | ESP32 VAD → SPEAK，RECM chunk 上行 |
| t=3s（继续说话） | 更多 RECM chunk |
| t=4s（说完停 1s） | Server 收 RPTF，日志出现 `ASR start` |
| t=6s | `ASR done` |
| t=8-15s | `LLM done`，`TTS done` |
| t=15s+ | `Sending PLAY`，扬声器播回复 |

**核心指标**：

| 指标 | 目标值 |
|------|--------|
| 首字延迟（说完到播放开始） | ≤ 5 s（含 ASR + LLM + TTS） |
| 语音可辨识度 | Whisper 识别正确率 ≥ 90% |
| 回复可听度 | 无明显爆音、无断续 |
| 连续对话 | 至少 3 轮不崩溃 |

### 排查思路

- **无声**：先按 TC-09 排查播放链路
- **识别错**：按 TC-08 排查 ASR；检查麦克风质量、距离、音量
- **回复慢**：按 TC-08 排查 LLM；考虑换更快的引擎（Ollama 本地）
- **回复快但无声音**：按 TC-09 排查
- **崩溃**：看 Server 端异常堆栈

---

## TC-11 · 断线自动重连

**目的**：验证 ESP32 与 Server 任一侧断线后能自动恢复。

### 步骤

1. TC-10 跑通一次后保持连接
2. **场景 A**：Server 侧关掉 wifi_server.py（Ctrl+C）
3. 观察 ESP32 串口：应有 TCP 断开日志
4. 重新启动 Server
5. 观察 ESP32 是否自动重连

6. **场景 B**：物理拔网线（如 ESP32 是插网线的）或关掉 Wi-Fi 30 秒再打开
7. 观察是否恢复

### 期望结果

**场景 A**：
```text
[ESP32 串口] tcp disconnected
[ESP32 串口] try reconnect...
（Server 重启后）
[ESP32 串口] tcp connected
[Server] client connected: ('192.168.x.x', 54xxx)
```

**场景 B**：
```text
[ESP32 串口] wifi lost
[ESP32 串口] try reconnect...
（Wi-Fi 恢复后）
[wifi] ip=192.168.x.x
[wifi] tcp connected
```

**核心指标**：

| 指标 | 目标值 |
|------|--------|
| 重连时间 | ≤ 30 s |
| 重连后功能 | 完整可用（RECM + PLAY） |

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 断线后不重连 | `WifiClient::run()` 未在主循环调用；检查 `main.cpp` loop |
| 重连但无 RECM | VAD 状态机卡在 SPEAK；重启 ESP32 |
| Server 收到重连但会话错乱 | `ClientSession` 未清理旧状态；查 `wifi_server.py` |

---

## TC-12 · USB Serial 回退链路

**目的**：验证 Wi-Fi 不可用时能退回串口路径。

### 步骤

1. 用串口版 env 烧录固件（不是 wifi 版）：
   ```bash
   cd firmware/esp32
   pio run -e esp32-s3-n16r8 --target upload
   ```
2. PC 侧用旧入口：
   ```bash
   cd pc
   python voice_chat.py --transport serial --engine sensenova
   ```
3. 输入 `你好` 回车

### 期望结果

```text
[text-input mode] 在 > 后直接输入文字，回车发送；q/exit 退出
> 你好
[bot] 嗨，我在这呢，有啥事儿？
（扬声器播回复）
>
```

### 排查思路

- 参考 [`docs/troubleshooting.md`](./troubleshooting.md)
- USB 回退链路仅用于验证链路，不作为长期架构

---

## 附录 A · 关键参数速查

| 常量 | 位置 | 默认值 | 说明 |
|------|------|--------|------|
| `WIFI_TCP_PORT` | `pc/config.py` | 8888 | TCP 端口 |
| `WIFI_HOST_BIND` | `pc/config.py` | `0.0.0.0` | Server 监听地址 |
| `TCP_IO_TIMEOUT` | `pc/config.py` | — | TCP 读写超时 |
| `VAD_RMS_THRESHOLD` | 固件常量 | — | VAD RMS 阈值 |
| `VAD_SILENCE_MS` | 固件常量 | 700 | 静音判定窗口 |
| `VAD_MIN_VOICE_MS` | 固件常量 | — | 最小语音持续 |
| `MIC_GPIO` | 固件常量 | 1 | MAX9814 Out 引脚（ADC1_CH0） |
| `WIFI_CONNECT_TIMEOUT_MS` | `wifi_client.h` | 10000 | Wi-Fi 连接超时 |
| `TCP_READ_TIMEOUT_MS` | `wifi_client.h` | 200 | 单次 read 超时 |
| `ESP32_HOSTNAME` | `config.local.json` → `secrets.local.h` | `esp32-voice` | mDNS 主机名 |
| `PC_HOST` / `PC_PORT` | `config.local.json` → `secrets.local.h` | 用户填 | Server 地址 |

---

## 附录 B · 快速诊断命令

```bash
# 1. 查看 ESP32 串口设备
ls /dev/ttyUSB* /dev/ttyACM*

# 2. 抓 ESP32 与 PC 的 TCP 流量
sudo tcpdump -i any -X -n 'tcp port 8888'

# 3. 抓 mDNS 流量
sudo tcpdump -i any -X -n 'port 5353'

# 4. 检查端口占用
lsof -i:8888
ss -tlnp | grep 8888

# 5. 检查 Wi-Fi RSSI（串口日志里会有）
# [wifi] rssi=-52 dBm  ← 信号强度

# 6. Server 端进程检查
ps aux | grep wifi_server.py

# 7. WSL2 场景：查看 WSL2 IP
ip -4 addr show eth0

# 8. Windows PowerShell：检查端口可达（ESP32 视角模拟）
Test-NetConnection <PC_HOST> -Port 8888
# TcpTestSucceeded: True  → 通；False → 见附录 F

# 9. Windows PowerShell：查看 portproxy 转发规则
netsh interface portproxy show v4tov4
```

---

## 附录 C · 常见故障速查

| 现象 | 优先排查 |
|------|----------|
| ESP32 上电无输出 | USB CDC、波特率 921600、`setup()` 前 crash |
| ESP32 连不上 Wi-Fi | SSID/密码、2.4GHz、AP isolation、RSSI |
| Wi-Fi 通但 TCP 不通 | PC_HOST 错、防火墙、不同网段、端口占用 |
| TCP 通但无 RECM | MAX9814 未接线、GPIO1 错、VAD 阈值过高 |
| RECM 通但 Server 无响应 | 帧 magic 不匹配、`wifi_server.py` 版本过旧 |
| Server 有响应但无声音 | MAX98357A 接线、PA_EN、Speaker 电源 |
| 声音断续 / 爆音 | I2S 时钟、`SAMPLE_RATE` 两端不一致 |
| 崩溃 / 死机 | 串口日志末条错误、堆栈、NVS 数据损坏（`Preferences p.begin(...)` 报错） |

---

## 附录 D · 参考文档

- 系统架构：[`architecture.md`](./architecture.md)
- 通信协议：[`protocol.md`](./protocol.md)
- 硬件接线：[`wiring.md`](./wiring.md)
- 硬件介绍：[`hardware.md`](./hardware.md)
- 固件编译：[`firmware.md`](./firmware.md)
- 网络配置：[`network-config.md`](./network-config.md)
- 阶段路线：[`roadmap.md`](./roadmap.md)

---

## 附录 E · 测试执行记录模板

| TC | 日期 | 执行人 | 结果 | 备注 |
|----|------|--------|------|------|
| TC-01 |  |  | ☐ 通过 ☐ 失败 |  |
| TC-02 |  |  | ☐ 通过 ☐ 失败 |  |
| TC-03 |  |  | ☐ 通过 ☐ 失败 |  |
| TC-04 |  |  | ☐ 通过 ☐ 失败 |  |
| TC-05 |  |  | ☐ 通过 ☐ 失败 |  |
| TC-06 |  |  | ☐ 通过 ☐ 失败 |  |
| TC-07 |  |  | ☐ 通过 ☐ 失败 |  |
| TC-08 |  |  | ☐ 通过 ☐ 失败 |  |
| TC-09 |  |  | ☐ 通过 ☐ 失败 |  |
| TC-10 |  |  | ☐ 通过 ☐ 失败 |  |
| TC-11 |  |  | ☐ 通过 ☐ 失败 |  |
| TC-12 |  |  | ☐ 通过 ☐ 失败 |  |

**结论**：☐ 全部通过 ☐ 部分失败（列出 TC 编号）

**签名**：______________  **日期**：______________

---

## 附录 F · WSL2 NAT 模式下端口转发 + Windows 防火墙

> **适用场景**：`wifi_server.py` 跑在 WSL2 内，ESP32 需要连接。WSL2 默认 NAT 模式下，ESP32 **不能**直接访问 WSL2 的 IP（`172.25.x.x`），必须通过 Windows 宿主做端口转发。
>
> **不适用**：桥接模式（见 [`network-config.md`](./network-config.md) §6.1）下 ESP32 可直接访问 WSL2 的局域网 IP，无需 portproxy。

### F.1 拓扑回顾

```text
    [ESP32 192.168.0.x]
              │
              │ TCP 8888 (走 Windows 宿主 IP)
              ▼
    [Windows 192.168.0.3 :8888]  ──portproxy──>  [WSL2 172.25.219.111 :8888]  ← wifi_server.py
```

### F.2 排查诊断（PowerShell）

```powershell
# ① 检查 Windows 宿主 IP 上的 8888 是否有监听
Test-NetConnection 192.168.0.3 -Port 8888
# TcpTestSucceeded: False  → 需要 portproxy（下一步）

# ② 检查 WSL 内 IP 上的 8888 是否真的起服务（从 Windows 侧测 WSL IP）
wsl -e ip -4 addr show eth0 | grep inet
# 例如 172.25.219.111

Test-NetConnection 172.25.219.111 -Port 8888
# TcpTestSucceeded: True   → 说明 wifi_server.py 已启动 OK
```

如果 ① 是 False 而 ② 是 True，就是典型的 NAT 隔离问题，需 portproxy。

### F.3 添加 WSL2 端口转发（管理员 PowerShell）

**必须以管理员身份打开 PowerShell**（右键 → 以管理员身份运行）。

```powershell
# 参数解释：
#   listenaddress / listenport   → Windows 宿主要接收的入口地址/端口（ESP32 打到这里）
#   connectaddress / connectport → 转发到的目标（WSL2 内 wifi_server.py）

netsh interface portproxy add v4tov4 `
    listenaddress=192.168.0.3   `
    listenport=8888           `
    connectaddress=172.25.219.111 `
    connectport=8888
```

**如果 192.168.0.3 有多个网卡，或希望任意 Windows IP 都能进来，把 `listenaddress` 改成 `0.0.0.0`**：

```powershell
netsh interface portproxy add v4tov4 `
    listenaddress=0.0.0.0   `
    listenport=8888           `
    connectaddress=172.25.219.111 `
    connectport=8888
```

### F.4 验证转发已生效

```powershell
# 列出所有 v4tov4 转发规则
netsh interface portproxy show v4tov4
```

期望输出：

```text
监听 v4             连接 v4
-----------------   -----------------
192.168.0.3:8888    172.25.219.111:8888
```

再次从 Windows 侧测：

```powershell
Test-NetConnection 192.168.0.3 -Port 8888
# TcpTestSucceeded: True  ← 转发已生效
```

### F.5 添加 Windows 防火墙规则

portproxy 转发后，Windows 会接收外部流量但默认防火墙会拦截。需要放行入站 8888：

```powershell
# 管理员 PowerShell
New-NetFirewallRule `
    -DisplayName "ESP32 Voice AI TCP 8888" `
    -Direction Inbound `
    -Protocol TCP `
    -LocalPort 8888 `
    -Action Allow
```

验证：

```powershell
Get-NetFirewallRule -DisplayName "ESP32 Voice AI TCP 8888"
Get-NetFirewallPortFilter -Name "ESP32 Voice AI TCP 8888"
```

### F.6 端到端验证

1. WSL2 内起服务：
   ```bash
   cd ~/projects/esp32-voice-ai/pc
   python wifi_server.py --port 8888
   ```
2. `config.local.json` 里 `pc_host` 应指向 **Windows 宿主 IP**（不是 WSL IP）：
   ```json
   { "pc_host": "192.168.0.3", "pc_port": 8888 }
   ```
3. 重新编译烧录 ESP32（`pio run -e esp32-s3-n16r8-wifi && pio run -t upload -e esp32-s3-n16r8-wifi`）
4. 观察 WSL2 内 `wifi_server.py` 输出应有：
   ```text
   [server] client connected: ('192.168.0.x', 54xxx)   ← ESP32 真实 LAN IP
   ```

### F.7 WSL IP 变了的应对

WSL2 NAT 模式下每次 `wsl --shutdown` 或重启 Windows 后 WSL IP **可能变化**。

**临时处理**：查新 IP → 删除旧规则 → 加新规则：

```powershell
# 查新 IP
wsl -e ip -4 addr show eth0 | grep inet

# 删除旧规则
netsh interface portproxy delete v4tov4 listenport=8888

# 加新规则
netsh interface portproxy add v4tov4 `
    listenaddress=0.0.0.0 listenport=8888 `
    connectaddress=<NEW_WSL_IP> connectport=8888
```

**长期处理**：任选其一：

1. **桥接模式**（推荐）：见 [`network-config.md`](./network-config.md) §6.1「桥接模式」，WSL2 拿到真实 LAN IP，不再需要 portproxy。
2. **自动化脚本**：写一个 PowerShell 脚本，用 Task Scheduler 定时（如每 60 s）跑一次 portproxy 刷新；或直接监听 WSL 启动事件自动执行（Phase 3 待做）。

### F.8 清理（不需要时执行）

```powershell
# 删除 portproxy 规则
netsh interface portproxy delete v4tov4 listenport=8888

# 删除防火墙规则
Remove-NetFirewallRule -DisplayName "ESP32 Voice AI TCP 8888"
```

---

## 版本

| 版本 | 日期 | 变更 |
|------|------|------|
| v1.0 | 2026-09-09 | 初版：Phase 1 Wi-Fi 语音闭环测试用例（TC-01~12） |
| v1.1 | 2026-09-09 | 新增附录 F：WSL2 NAT 模式端口转发 + Windows 防火墙；TC-04 排查思路补 WSL2 场景 |
