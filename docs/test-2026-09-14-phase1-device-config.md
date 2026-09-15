# 测试用例 · 2026-09-14 · Phase 1 设备配置系统实机验收

> **文档编号**：TC-20260914-P1-DEVCFG
> **版本**：v1.0
> **适用代码**：Phase 1 设备配置系统（`env:esp32-s3-n16r8-wifi`）
> **测试目标**：验证 ESP32 设备配置闭环——`首次启动 → SoftAP → Web 配置 → NVS 保存 → 重启 → 正常模式 → RuntimeConfig 驱动 Wi-Fi/TCP → 完整语音链路`，以及 `Wi-Fi Reset / Factory Reset / 断电持久化`
>
> **测试时长**：全流程约 40–60 分钟；每个测试独立完成，可按编号顺序执行，**禁止跳测导致状态错乱**。

---

## 0. 目录

| 编号 | 测试项 | 时长 | 依赖 |
|------|--------|------|------|
| TC-01 | 编译检查 + 串口确认 | 5 min | 无 |
| TC-02 | 烧录 Wi-Fi 固件 | 5 min | TC-01 |
| TC-03 | 首次启动 / Config Mode | 3 min | TC-02 |
| TC-04 | 连接 SoftAP + Captive Portal | 3 min | TC-03 |
| TC-05 | 配置页面字段检查 | 3 min | TC-04 |
| TC-06 | 保存 Wi-Fi 配置（Web → NVS → Reboot） | 5 min | TC-05 |
| TC-07 | 重启后 NVS 持久化 + Normal Mode | 3 min | TC-06 |
| TC-08 | Wi-Fi 使用 RuntimeConfig | 3 min | TC-07 |
| TC-09 | PC Host/Port 使用 RuntimeConfig | 3 min | TC-08 |
| TC-10 | **完整语音链路回归** | 10 min | TC-09 |
| TC-11 | 修改 PC Host/Port | 5 min | TC-10 |
| TC-12 | Wi-Fi Reset | 5 min | TC-11 |
| TC-13 | Wi-Fi Reset 后重新保存 | 5 min | TC-12 |
| TC-14 | Factory Reset | 5 min | TC-13 |
| TC-15 | **断电重启配置持久化** | 5 min | TC-14 |

**附录**：A 关键日志速查 · B 诊断命令 · C 常见故障 · D 执行记录模板

---

## 通用前置条件

```text
[ ] ESP32-S3 N16R8 板子
[ ] MAX9814 麦克风模块（TC-10 需要）
[ ] MAX98357A I2S 功放模块（TC-10 需要）
[ ] 3W 扬声器（TC-10 需要）
[ ] USB 数据线（能传数据）
[ ] 手机（能连 Wi-Fi + 打开浏览器）
[ ] 一个真实可用的 2.4GHz Wi-Fi（用于保存测试配置）
[ ] PC Server（`wifi_server.py` / 等价物，TC-10 需要）
```

> **重要安全规则**：
> - 整个测试过程中**禁止**执行 `nvs_flash_erase()` 或 `erase_flash`。只使用设备自带的功能按钮：
>   `Reset Wi-Fi` / `Factory Reset` / `Reboot`。
> - 测试文档中**禁止记录 Wi-Fi 密码**。日志中出现的 `password=ESP32Voice` 是 **SoftAP 固定开发密码**，不是 Wi-Fi 密码，属正常输出。

---

## TC-01 · 编译检查 + 串口确认

**目的**：确认当前代码能编译，且 ESP32 已通过 USB 连接。

### 步骤

1. 检查代码状态：
   ```bash
   cd /home/hqb/projects/esp32-voice-ai/firmware/esp32
   git status --short
   ```
   期望看到 Step 1~3 的修改文件（`src/config/`、`src/web/`、`src/network/wifi_client.*`、`src/main.cpp`）。

2. 编译 Wi-Fi 环境：
   ```bash
   pio run -e esp32-s3-n16r8-wifi
   ```
   > PIO pre-build 会自动运行 `scripts/gen_secrets.py`，日志会出现 `[gen_secrets] output : ...`。

3. 确认串口设备存在：
   ```bash
   ls -l /dev/ttyACM* /dev/ttyUSB*
   ```

### 期望结果

```text
[ ] 编译：SUCCESS
[ ] /dev/ttyACM0 或 /dev/ttyUSB0 存在
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 编译失败 | 检查 `platformio.ini` 的 `build_flags`（`FIRMWARE_MODE_WIFI`） |
| 无串口设备 | ESP32 未连 USB / 驱动未装 / USB 线仅充电 |

---

## TC-02 · 烧录 Wi-Fi 固件

**目的**：将当前 Wi-Fi 版固件烧录到 ESP32。

### 步骤

1. 烧录：
   ```bash
   pio run -e esp32-s3-n16r8-wifi --target upload
   ```
2. 打开串口监视器：
   ```bash
   pio device monitor -e esp32-s3-n16r8-wifi -b 921600
   ```
   如果自动选择的串口不对，用实际设备：
   ```bash
   python3 -m serial.tools.miniterm /dev/ttyACM0 921600
   ```

### 期望结果

```text
[ ] 烧录：Successfully uploaded ...
[ ] ESP32 自动重启
[ ] 串口出现启动横幅 "ESP32-S3 WAV AUDIO PLAYER"
[ ] 串口出现 "READY"
```

---

## TC-03 · 首次启动 / Config Mode

**目的**：验证无有效 NVS 配置时进入配置模式（SoftAP + DNS + WebServer）。

### 步骤

1. **保证设备处于首次状态**：如果设备此前已保存过配置，先通过 Web 页面的 `Factory Reset` 按钮清空（不要擦除整个 NVS，见 TC-14 流程），然后观察重启。
2. 观察串口日志，确认进入 Config Mode（见下方期望结果）。

### 访问网址（本阶段可选）

TC-03 的重点是**串口日志确认进入 Config Mode**。如果要同时用浏览器确认，请先连上 SoftAP 再访问：

```text
1. 手机 Wi-Fi 设置 → 连接热点  ESP32-Voice-<MAC>  （密码 ESP32Voice）
2. 浏览器访问配置页面：
   http://192.168.4.1
```

| 访问方式 | 网址 | 说明 |
|----------|------|------|
| 标准入口（推荐） | `http://192.168.4.1` | SoftAP 固定 IP，路由 `GET /`，总是可用 |
| Captive Portal 自动跳转 | 连接热点后手机自动弹窗 | 手机系统请求 `generate_204` / `hotspot-detect.html` 等检测 URL，被重定向到 `http://192.168.4.1/` |
| 任意域名 | `http://anything.com` | DNS 通配解析到 `192.168.4.1`，也回到配置页（用于测试 Captive Portal） |

> 网页访问在 TC-04 会正式验证；此处若不便操作手机，可只观察串口日志。

### 期望结果

串口应依次出现：

```text
[config] loaded defaults: ssid=<secrets默认> host=<secrets默认> port=<secrets默认> vad_rms=400
Configuration mode enabled (SoftAP + captive portal).
[config-web] SoftAP ready: ssid=ESP32-Voice-<MAC> ip=192.168.4.1 password=ESP32Voice
[config-web] DNS captive portal start failed / started
```

> `[config] loaded defaults` 说明 NVS 无有效配置 → `isConfigValid() == false` → 进入 Config Mode。

**关键确认链**：

```text
DeviceConfig::begin()
    ↓
没有有效 NVS 配置
    ↓
isConfigValid() == false
    ↓
进入 Config Mode
    ↓
SoftAP 启动
    ↓
WebServer 启动
    ↓
DNS Captive Portal 启动
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 出现 `loaded from NVS` 而非 `loaded defaults` | 设备有旧配置，先 Factory Reset |
| SoftAP ready 不出现 | `WiFi.softAP()` 失败；检查日志 `[config-web] SoftAP start failed` |
| DNS 启动失败 | `DNSServer.start()` 失败；非致命，可手动访问 IP |
| 重启后仍回 Config Mode（已保存过配置） | 属于 TC-06 之后要检查的问题，先记录 |

---

## TC-04 · 连接 SoftAP + Captive Portal

**目的**：验证手机能连接 ESP32 的 SoftAP，并验证 Captive Portal 弹出。

### 步骤

1. 打开手机 Wi-Fi 列表，找到：
   ```text
   ESP32-Voice-<MAC>
   ```
2. 连接，密码输入：`ESP32Voice`
3. 观察手机是否自动弹出配置页面（Captive Portal）。
4. 如果没有自动弹出，手动访问：`http://192.168.4.1`

### 期望结果

```text
[ ] 手机能看到 ESP32-Voice-<MAC> 热点
[ ] 连接成功（密码 ESP32Voice）
[ ] Captive Portal 自动弹出配置页面（部分手机系统需要点击"登录/连接"通知）
[ ] 手动访问 http://192.168.4.1 可打开页面
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 看不到热点 | SoftAP 未启动（回 TC-03 查日志） |
| 连接失败 | 密码错误；SSID 带 MAC 冒号字符（部分手机可连） |
| 不弹 Captive Portal | 手机系统策略；手动访问 192.168.4.1 即可 |

---

## TC-05 · 配置页面字段检查

**目的**：验证页面包含全部配置字段，且 Password 默认隐藏。

### 步骤

1. 打开配置页面 `http://192.168.4.1`
2. 检查页面内容。

### 期望结果

页面必须包含：

```text
[ ] Wi-Fi 分组：
    - SSID 输入框
    - Password 输入框（type=password，默认隐藏，有 "Show password" 复选框）
    - 提示 "Leave empty to keep current password."
[ ] PC Server 分组：
    - Host 输入框
    - Port 输入框
[ ] Voice Detection 分组：
    - RMS Threshold
    - Minimum Voice Time (ms)
    - Silence Time (ms)
[ ] 按钮：Save Configuration / Reset Wi-Fi / Factory Reset / Reboot
```

**不记录、不报告实际的 Wi-Fi password 值。**

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 字段缺失 | 固件版本不对；重新烧录 TC-02 |
| Password 明文显示 | `config_web.cpp` 中 `input type` 被改；检查 `handleRoot()` |

---

## TC-06 · 保存 Wi-Fi 配置（Web → NVS → Reboot）

**目的**：验证 Web 保存流程完整闭环。

### 步骤

1. 在配置页面填写（示例值，使用真实可用的 Wi-Fi）：
   ```text
   SSID     = <真实测试 Wi-Fi SSID>
   Password = <该 Wi-Fi 密码>        ← 不要在报告中显示
   PC Host  = 192.168.0.3           ← 示例；可先用当前 PC 实际 IP
   PC Port  = 8888
   VAD RMS  = 400                   ← 保持默认，不要改
   VAD Min  = 200                   ← 保持默认
   VAD Sil  = 700                   ← 保持默认
   ```
2. 点击 `Save Configuration`
3. 观察串口日志和页面响应。

### 期望结果

**Web 侧**：返回成功页面（"Configuration saved"）。

**串口侧**：

```text
[config-web] config saved: ssid=<SSID> host=192.168.0.3 port=8888 vad_rms=400
[config] saved to NVS
[config-web] rebooting
```

> 保存成功后 2 秒自动重启（`_rebootAt` 非阻塞调度）。

**确认链**：

```text
Web Save
    ↓
DeviceConfig::save()
    ↓
NVS
    ↓
HTTP 响应
    ↓
ESP.restart()
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 返回 400 | 字段验证失败；检查 Host/Port 格式 |
| 无 `saved to NVS` | `Preferences` 打开失败；查看 `[config] NVS save open failed` |
| 保存后不重启 | `_rebootAt` 调度问题；可手动点 Reboot |

---

## TC-07 · 重启后 NVS 持久化 + Normal Mode

**目的**：验证重启后从 NVS 读取配置并进入 Normal Mode。

### 步骤

1. TC-06 保存后设备已自动重启。
2. 观察串口日志。

### 期望结果

串口应出现：

```text
[config] loaded from NVS: ssid=<保存的SSID> host=192.168.0.3 port=8888 vad_rms=400
Wi-Fi mode enabled (TCP client + ADC mic).
```

**关键确认**：

```text
DeviceConfig::begin()
    ↓
读取 NVS
    ↓
配置有效
    ↓
isConfigValid() == true
    ↓
进入 NORMAL MODE（不再进入 Config Mode）
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 又出现 `Configuration mode enabled` | **停止测试，分析原因**：NVS 保存失败 / cfg_ver 不匹配 / SSID 为空 |
| 出现 `NVS version mismatch` | `cfg_ver` 与 `CFG_VER` 不一致（代码版本问题） |
| 出现 `loaded defaults` | NVS 没保存成功，回 TC-06 重试并抓完整日志 |

---

## TC-08 · Wi-Fi 使用 RuntimeConfig

**目的**：验证 ESP32 连接的是 Web 保存的 SSID，而非 `secrets.local.h` 的默认值。

### 步骤

1. TC-07 已进入 Normal Mode。
2. 观察串口 Wi-Fi 连接日志。

### 期望结果

```text
[wifi] initialized: ssid=<Web保存的SSID> pc=192.168.0.3:8888
[wifi] connecting to <Web保存的SSID>
[wifi] connected
[wifi] IP: 192.168.x.x
[wifi] RSSI: -xx dBm
```

**关键确认**：

- `ssid=` 显示的必须是 Web 页面保存的 SSID（**不是** `secrets.local.h` 中的默认 SSID）。
- 日志中**不出现** Wi-Fi 密码（代码只在 `WiFi.begin(ssid, pass)` 内部使用，不打印）。

> 如果日志无 SSID，可通过路由器/AP 管理页确认实际连接的设备名称。

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| `connecting` 一直超时 | 密码错 / SSID 错 / 5GHz-only AP（ESP32 仅 2.4GHz） |
| 连接的不是保存的 SSID | `main.cpp` 未把 RuntimeConfig 传给 `g_wifi.begin()`；检查 Step 3 修改 |
| 日志打印密码 | 代码回归；检查 `wifi_client.cpp` 的 `Serial.printf` |

---

## TC-09 · PC Host/Port 使用 RuntimeConfig

**目的**：验证 `_pcIp` / `_pcPort` 来自 RuntimeConfig 而非 secrets。

### 步骤

1. PC 侧启动 Server：
   ```bash
   cd ~/projects/esp32-voice-ai/pc
   python wifi_server.py --port 8888
   ```
2. 观察 ESP32 串口 TCP 日志 + Server 日志。

### 期望结果

**ESP32 串口**：

```text
[tcp] connecting to 192.168.0.3:8888
[tcp] connected
```

**PC Server 侧**：

```text
[server] client connected: ('192.168.x.x', 54xxx)
```

**关键确认**：`connecting to` 的目标必须是 Web 保存的 Host:Port（`192.168.0.3:8888`），而非 secrets 默认值。

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| TCP 连不上 | Host 不可达 / 防火墙 / 端口未监听（参考旧文档附录 F WSL2 portproxy） |
| 连的是旧地址 | `main.cpp` 未传 `cfg.pc_host` / `cfg.pc_port`；检查 Step 3 |
| `resolving host` 后失败 | Host 是域名且 DNS 解析失败；改填 IP |

---

## TC-10 · 完整语音链路回归（核心）

**目的**：验证 Step 1~3 未破坏原有语音系统。

### 步骤

1. 确保 TC-09 的 TCP 已连接。
2. 保持安静 5 秒 → 确认无异常 RECM。
3. 对 MAX9814 清晰说话（如 "你好，请告诉我一个笑话"），说完停 1 秒。
4. 等待 PC Server 处理（Whisper → LLM → Edge TTS）。
5. 确认扬声器播放回复。
6. 重复一轮对话（换个话题）。

### 期望结果

```text
[ ] 说话时串口出现 [vad] SPEAK / [mic] SEND chunk=... 
[ ] 说完出现 [mic] RPTF total=... B
[ ] PC Server 出现 ASR → LLM → TTS 日志
[ ] ESP32 串口出现 [play] PLAY received, %u bytes
[ ] 扬声器听到回复
[ ] 播放结束后串口出现 [mic] playback done, cooldown ... ms
[ ] 下一轮录音正常（VAD 回到监听状态）
[ ] 至少 3 轮对话不崩溃
```

**确认链**：

```text
ESP32 麦克风 → VAD → 录音 → TCP → PC Whisper → LLM → Edge TTS → ESP32 → MAX98357A → 扬声器 → 下一轮录音
```

### 排查思路

| 现象 | 可能原因 | 处理 |
|------|----------|------|
| 链路任何一环失败 | — | **不要修改音频代码**，记录失败位置（`[vad]` / `[mic]` / `[play]` / Server 侧） |
| 无 RECM | 麦克风未接 / VAD 阈值过高 | 记录，不修代码 |
| Server 无响应 | Server 未起 / 端口错 | 检查 TC-09 |

---

## TC-11 · 修改 PC Host / Port

**目的**：验证修改 Server 配置后，RuntimeConfig 驱动新的连接目标。

### 步骤

1. 进入配置模式：**长按开发板 `BOOT` 键 (GPIO0) 3 秒** → 串口出现 `[config] BOOT long-press: entering config mode` → 设备切换到 SoftAP Config Mode。
   > 注意：须在设备**启动完成、正常运行于 Normal Mode** 后长按；启动瞬间按住 BOOT 会进入下载模式（烧录），不是配置模式。
2. 手机连接 SoftAP `ESP32-Voice-<MAC>`（密码 `ESP32Voice`），打开 `http://192.168.4.1`。
3. 填写：
   ```text
   SSID     = <真实 Wi-Fi>
   Password = <该 Wi-Fi 密码>
   PC Host  = <当前 PC 实际 IP>      ← 与 TC-06 不同
   PC Port  = 8888（或按需 9999）
   VAD      = 保持默认
   ```
4. 保存 → 自动重启。
5. 观察串口。

### 期望结果

```text
[wifi] initialized: ssid=<SSID> pc=<新IP>:<新端口>
[tcp] connecting to <新IP>:<新端口>
[tcp] connected
```

**确认链**：

```text
RuntimeConfig → WifiClient → _pcIp / _pcPort → _client.connect()
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 仍是旧地址 | NVS 未更新 / `main.cpp` 未读新值 |
| 长按 3 秒无 `entering config mode` 日志 | BOOT 键接触不良 / 未用 INPUT_PULLUP / 触发后立即松手（需持续按住满 3 秒） |
| 启动时按 BOOT 进的是下载模式 | strapping 行为（GPIO0 上电低电平=下载模式）；须启动完成后再长按 |

---

## TC-12 · Wi-Fi Reset

**目的**：验证 Reset Wi-Fi 只删除 SSID/密码，保留其他配置。

### 步骤

1. 进入配置模式（Factory Reset → Config Mode）。
2. 打开页面，点击 `Reset Wi-Fi`。
3. 观察串口和页面响应。

### 期望结果

**串口**：

```text
[config] WiFi credentials reset
[config-web] Wi-Fi credentials reset
[config-web] rebooting
```

**重启后**：

```text
[config] loaded defaults: ssid=<secrets默认> host=<保留的Host> port=<保留的Port> vad_rms=<保留的VAD>
Configuration mode enabled (SoftAP + captive portal).
```

> 注意：`resetWifi()` 内部 `loadDefaults()` 会重载**所有**字段（Host/Port/VAD 回到 secrets 默认），
> 但 NVS 中仅删除 `cfg_ssid` / `cfg_pwd`。重启后因 SSID 无效 → 进入 Config Mode。
> **重点确认**：`cfg_host` / `cfg_port` / `vad_*` 的 NVS 值未被删除（可通过重新保存验证，见 TC-13）。

**确认链**：

```text
POST /reset
    ↓
DeviceConfig::resetWifi()
    ↓
NVS 中 Wi-Fi 配置删除（仅 cfg_ssid / cfg_pwd）
    ↓
reboot
    ↓
isConfigValid() == false → Config Mode
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| Host/Port 也被清掉 | `resetWifi()` 误删 key；检查 `device_config.cpp` |
| 不重启 | `_rebootAt` 调度问题 |

---

## TC-13 · Wi-Fi Reset 后重新保存

**目的**：验证 Reset Wi-Fi 后重新配置即可恢复。

### 步骤

1. TC-12 后设备在 Config Mode。
2. 打开页面，重新填写：
   ```text
   SSID     = <真实 Wi-Fi>
   Password = <该 Wi-Fi 密码>
   PC Host  = <保持 TC-11 的值或重新填>
   PC Port  = <保持>
   ```
3. 保存 → 自动重启。
4. 观察串口。

### 期望结果

```text
[config] loaded from NVS: ssid=<新SSID> host=<Host> port=<Port> vad_rms=<VAD>
Wi-Fi mode enabled (TCP client + ADC mic).
[wifi] connecting to <新SSID>
[tcp] connected
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 回到 Config Mode | 保存失败 / SSID 空；抓完整日志 |

---

## TC-14 · Factory Reset

**目的**：验证 Factory Reset 清除 `voice_ai` namespace 全部 key，回到默认配置。

### 步骤

1. 当前在 Normal Mode（TC-13 完成后）。
2. 进入配置模式：通过 Web `Reboot` 后确认回 Config Mode（或使用 `Reset Wi-Fi` 后重启）。
   > 若无法回 Config Mode，说明 NVS 仍有效——这是预期行为；此时先执行一次 `Reset Wi-Fi` 进入 Config Mode。
3. 打开页面，点击 `Factory Reset`。
4. 观察串口。

### 期望结果

**串口**：

```text
[config] factory reset complete
[config-web] factory reset complete
[config-web] rebooting
```

**重启后**：

```text
[config] loaded defaults: ssid=<secrets默认> host=192.168.0.3 port=8888 vad_rms=400
Configuration mode enabled (SoftAP + captive portal).
```

**确认链**：

```text
voice_ai namespace 中 8 个 key 全部删除
    ↓
cfg_ssid, cfg_pwd, cfg_host, cfg_port
vad_rms, vad_min, vad_sil, cfg_ver
    ↓
loadDefaults() → secrets 默认值
    ↓
isConfigValid() == false → Config Mode
```

**再打开页面确认**：SSID / PC Host / PC Port / VAD 全部回到首次默认状态。

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 重启后还有旧值 | `factoryReset()` 未删完 key；检查 8 个 key |
| 仍 Normal Mode | `_hasNVSConfig` 未置 false；检查代码 |

---

## TC-15 · 断电重启配置持久化（重点）

**目的**：证明配置真正持久化在 NVS，而非仅 RAM。

### 步骤

1. 从 TC-14 的 Config Mode 重新保存配置（真实 Wi-Fi + PC Host/Port），确认进入 Normal Mode 且 TCP 连接成功。
2. **拔掉 USB 电源**，等待 10 秒。
3. **重新上电**，观察串口。

### 期望结果

```text
[config] loaded from NVS: ssid=<保存的SSID> host=<保存的Host> port=<保存的Port> vad_rms=<保存的VAD>
Wi-Fi mode enabled (TCP client + ADC mic).
[wifi] connecting to <保存的SSID>
[wifi] connected
[tcp] connecting to <保存的Host>:<保存的Port>
[tcp] connected
```

**关键确认**：断电重启后：

```text
NVS → RuntimeConfig → WifiClient → 自动连接
```

**不再进入 Config Mode。**

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 断电后回 Config Mode | NVS 写入失败 / 断电时正在写 NVS（等待保存后重启完成再断电） |
| 配置正确但连不上 Wi-Fi | 路由器侧问题，与持久化无关 |

---

## 附录 A · 关键日志速查

| 阶段 | 日志 | 含义 |
|------|------|------|
| 配置加载 | `[config] loaded defaults: ...` | NVS 无有效配置，用 secrets 默认 |
| 配置加载 | `[config] loaded from NVS: ...` | NVS 有有效配置 |
| 配置加载 | `[config] NVS version mismatch (x vs y)` | cfg_ver 不匹配，回退默认 |
| 配置模式 | `Configuration mode enabled (SoftAP + captive portal).` | 进入 Config Mode |
| 配置模式 | `[config-web] SoftAP ready: ssid=... ip=192.168.4.1 password=ESP32Voice` | SoftAP 启动（password 为 AP 固定密码） |
| 保存 | `[config-web] config saved: ssid=... host=... port=... vad_rms=...` | Web 保存成功 |
| 保存 | `[config] saved to NVS` | 写入 NVS |
| Reset | `[config] WiFi credentials reset` | 仅删 SSID/密码 |
| Factory | `[config] factory reset complete` | 清空 voice_ai namespace |
| 重启 | `[config-web] rebooting` | 调度 ESP.restart() |
| Wi-Fi | `[wifi] initialized: ssid=... pc=...:...` | RuntimeConfig 传入成功 |
| Wi-Fi | `[wifi] connecting to <SSID>` | 正在连接（SSID 应为 Web 保存值） |
| Wi-Fi | `[wifi] connected` / `[wifi] IP: ...` | 连接成功 |
| TCP | `[tcp] connecting to <Host>:<Port>` | 连接目标来自 RuntimeConfig |
| TCP | `[tcp] connected` | TCP 建立 |

---

## 附录 B · 快速诊断命令

```bash
# 1. 查看串口
ls /dev/ttyACM* /dev/ttyUSB*

# 2. 编译（不烧录）
pio run -e esp32-s3-n16r8-wifi

# 3. 烧录
pio run -e esp32-s3-n16r8-wifi --target upload

# 4. 串口监视
pio device monitor -e esp32-s3-n16r8-wifi -b 921600

# 5. 抓 ESP32 与 PC 的 TCP 流量
sudo tcpdump -i any -X -n 'tcp port 8888'

# 6. 检查端口占用
lsof -i:8888

# 7. 查看 NVS 工具（仅诊断，不要 erase）
python3 -m esp_idf_nvs_partition_gen  # 或使用设备日志确认

# 8. PC Server 启动
cd ~/projects/esp32-voice-ai/pc && python wifi_server.py --port 8888
```

---

## 附录 C · 常见故障速查

| 现象 | 优先排查 |
|------|----------|
| 上电无输出 | USB CDC、波特率 921600 |
| 重复进入 Config Mode（已保存） | NVS 写入失败；看 `[config] NVS save open failed` |
| 保存后重启仍是旧配置 | cfg_ver 不匹配；看 `NVS version mismatch` |
| 连的不是保存的 SSID | `main.cpp` 未传 RuntimeConfig；查 Step 3 |
| TCP 连的是旧地址 | `main.cpp` 未传 `pc_host/pc_port`；查 Step 3 |
| Wi-Fi Reset 后 Host/Port 也丢 | `resetWifi()` 误删 key；查 `device_config.cpp` |
| Factory Reset 后仍有旧值 | `factoryReset()` 未删完 8 个 key |
| 语音链路异常 | **不要改音频代码**；记录 `[vad]/[mic]/[play]` 或 Server 侧日志 |

---

## 附录 D · 测试执行记录模板

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
| TC-13 |  |  | ☐ 通过 ☐ 失败 |  |
| TC-14 |  |  | ☐ 通过 ☐ 失败 |  |
| TC-15 |  |  | ☐ 通过 ☐ 失败 |  |

**结论**：☐ 全部通过 ☐ 部分失败（列出 TC 编号）

**签名**：______________  **日期**：______________

---

## 版本

| 版本 | 日期 | 变更 |
|------|------|------|
| v1.0 | 2026-09-14 | 初版：Phase 1 设备配置系统实机验收测试（TC-01~15） |
