# ESP32 Voice AI — 第一阶段设备配置系统实施规格 v1.0

## 0. 文档目的

本阶段实现：

> **ESP32 SoftAP + Captive Portal + Web 配置页面 + NVS 持久化配置**

目标是让用户通过手机连接 ESP32 后，在浏览器中修改设备配置，而不需要重新编译或烧录固件。

### 实现状态 (2026-09-14)

| 步骤 | 内容 | 状态 |
|------|------|------|
| Step 1 | DeviceConfig + RuntimeConfig + NVS | ✅ 完成 |
| Step 2A | SoftAP + DNS + Captive Portal + WebServer | ✅ 完成 |
| Step 2B-1 | Web 配置页面显示 (只读) | ✅ 完成 |
| Step 2C-1 | NVS 保存 + Reset + Factory Reset + Reboot | ✅ 完成 |
| Step 3 | 现有硬编码参数接入 Config | ⏳ 待实施 |

编译验证：
- `esp32-s3-n16r8` → ✅ 成功 (346,989 bytes flash, 9.8% RAM)
- `esp32-s3-n16r8-wifi` → ✅ 成功 (814,197 bytes flash, 22.1% RAM)

---

# 1. 当前项目环境

## 1.1 硬件

```text
MCU:
ESP32-S3 N16R8

Flash:
16 MB

PSRAM:
8 MB
```

## 1.2 开发环境

```text
PlatformIO
Arduino framework
ESP32
```

## 1.3 当前项目目录

```text
/home/hqb/projects/esp32-voice-ai/firmware/esp32
```

## 1.4 当前已经工作的功能

当前项目已经实现并验证：

```text
麦克风采集
    ↓
VAD
    ↓
TCP发送PCM
    ↓
PC服务器
    ↓
Whisper
    ↓
LLM
    ↓
Edge TTS
    ↓
TCP返回PCM
    ↓
ESP32播放
```

**本阶段不得破坏上述功能。**

---

# 2. 本阶段范围

## 2.1 必须实现

```text
[x] DeviceConfig 配置模块
[x] NVS 持久化
[x] 配置版本号
[x] 默认配置
[x] SoftAP
[x] Captive Portal
[x] Web配置页面
[x] 配置保存
[x] 配置加载
[x] Wi-Fi配置
[x] Server配置
[x] VAD配置
[x] Wi-Fi配置重置
[x] Factory Reset
[x] Web重启
[x] RuntimeConfig 设计
```

---

# 3. 本阶段明确禁止

本阶段**不要实现**以下功能：

```text
❌ BLE
❌ OTA
❌ Gemini Live
❌ Gemini API
❌ Whisper修改
❌ Edge TTS修改
❌ AI模块重构
❌ TCP协议修改
❌ 音频格式修改
❌ VAD算法修改
❌ Mic驱动重写
❌ Speaker驱动重写
❌ PC服务器修改
```

如果为了实现 Web 配置确实需要修改现有代码，只允许做**最小必要修改**。

---

# 4. 配置模块设计

已创建：

```text
config/
├── device_config.h
└── device_config.cpp
```

---

# 5. DeviceConfig

实际实现位于 `src/config/device_config.h`：

```cpp
// NVS namespace
#define NVS_NAMESPACE  "voice_ai"

// Config version
#define CFG_VER        1

// String buffer sizes
#define WIFI_SSID_MAX_LEN   32
#define WIFI_PASS_MAX_LEN   64
#define HOST_MAX_LEN        44

// Default VAD thresholds
#define DEFAULT_VAD_RMS     400
#define DEFAULT_VAD_MIN_MS  200
#define DEFAULT_VAD_SIL_MS  700

struct RuntimeConfig
{
    char     wifi_ssid[WIFI_SSID_MAX_LEN + 1];
    char     wifi_pass[WIFI_PASS_MAX_LEN + 1];
    char     pc_host[HOST_MAX_LEN + 1];
    uint16_t pc_port;
    uint32_t vad_rms;
    uint32_t vad_min_ms;
    uint32_t vad_sil_ms;
    uint16_t cfg_ver;
};
```

默认值从 `secrets.h` 读取（通过 `secrets.local.h` 覆盖）：

```cpp
// device_config.cpp → loadDefaults()
_config.wi_ssid  = WIFI_SSID;   // from secrets.h
_config.wi_pass   = WIFI_PASS;   // from secrets.h
_config.pc_host   = PC_HOST;     // from secrets.h
_config.pc_port   = PC_PORT;     // from secrets.h
_config.vad_rms   = DEFAULT_VAD_RMS;   // 400
_config.vad_min_ms = DEFAULT_VAD_MIN_MS;  // 200
_config.vad_sil_ms = DEFAULT_VAD_SIL_MS;  // 700
_config.cfg_ver   = CFG_VER;
```

---

# 6. 配置版本

已实现：

```cpp
#define CFG_VER 1
```

保存配置时：

```cpp
newConfig.cfg_ver = CFG_VER;
```

加载配置时检查：

```text
NVS不存在
        ↓
使用默认配置 (secrets.h)
        ↓
isConfigValid() → false (因为 _hasNVSConfig == false)
        ↓
进入配置模式 (SoftAP)

NVS存在
        ↓
检查 cfg_ver
        ↓
cfg_ver == CFG_VER
        ↓
loadFromNVS()
        ↓
isConfigValid() → true (如果 SSID 非空)
        ↓
正常 Wi-Fi 模式

cfg_ver != CFG_VER
        ↓
丢弃旧配置，使用默认
        ↓
isConfigValid() → false
```

> 注意：当前版本没有实现 migration 逻辑。如果以后增加配置字段，需要升级 CFG_VER 并实现 migration。

---

# 7. NVS设计

使用 Arduino ESP32 的：

```cpp
Preferences
```

NVS namespace：

```text
voice_ai
```

实际实现的 key-value：

```text
voice_ai
├── cfg_ssid    (string)
├── cfg_pwd     (string)
├── cfg_host    (string)
├── cfg_port    (u16)
├── vad_rms     (i32)
├── vad_min     (i32)
├── vad_sil     (i32)
└── cfg_ver     (u16)
```

---

# 8. NVS API

实际实现为 `DeviceConfig` 类，位于 `src/config/device_config.h`：

```cpp
class DeviceConfig
{
public:
    DeviceConfig();

    bool begin();                            // 初始化：尝试 NVS，失败则用 defaults
    const RuntimeConfig& getConfig() const;  // 获取当前配置
    bool isConfigValid() const;              // 检查配置是否有效
    bool save(const RuntimeConfig& newConfig);  // 保存全部字段到 NVS
    bool resetWifi();                        // 删除 Wi-Fi 凭据，重载 defaults
    bool factoryReset();                     // 清除所有 NVS key，重载 defaults
private:
    void loadFromNVS();
    void loadDefaults();
    RuntimeConfig _config;
    bool          _hasNVSConfig;
};
```

要求：

```text
begin()
    ↓
打开 NVS namespace "voice_ai"
    ↓
检查 cfg_ver 是否存在且匹配
    ↓
匹配 → loadFromNVS()
    ↓
不匹配/不存在 → loadDefaults()
    ↓
得到有效配置
```

---

# 9. RuntimeConfig 设计

实际实现中，`RuntimeConfig` 既是保存配置也是运行时配置：

```text
NVS
  ↓
RuntimeConfig (内存)
  ↓
Web 页面修改
  ↓
RuntimeConfig (新值)
  ↓
save() → NVS
```

**不要每个字段修改就写 Flash。** Web 页面只在用户点击"保存"时一次性写入。

---

# 10. 配置保存必须是事务式的

已实现。流程：

```text
Web POST /save
       ↓
读取所有表单字段
       ↓
复制到当前配置的副本 (memcpy)
       ↓
逐个验证
       ↓
全部有效？
    /       \
  NO         YES
  ↓           ↓
返回错误     save() → NVS (一次性写入)
             ↓
         更新内存中的 _config
```

---

# 11. 参数验证

实际实现中的验证逻辑（位于 `config_web.cpp → handleSave()`）：

### SSID

```text
必须存在 (hasArg)
不能为空
最大长度 32 字符 (WIFI_SSID_MAX_LEN)
```

### Password

```text
最大长度 64 字符 (WIFI_PASS_MAX_LEN)
空密码 = 保留当前密码 (安全默认值)
```

### PC Host

```text
必须存在
不能为空
最大长度 44 字符 (HOST_MAX_LEN)
```

### PC Port

```text
必须为纯数字 (parseU32 检查)
范围: 1 ~ 65535
溢出检查: strtoul + errno == ERANGE
```

### VAD RMS

```text
必须为纯数字 (parseU32 检查)
范围: 0 ~ UINT32_MAX
溢出检查: strtoul + errno == ERANGE
```

### VAD Min Voice / VAD Silence

```text
同上
```

如果参数非法：

```text
HTTP 400 + 错误页面 (sendResultPage(false, ...))
```

成功时：

```text
HTTP 200 + 成功页面
```

---

# 12. SoftAP

已实现。设备进入配置模式后启动 SoftAP。

SSID：

```text
ESP32-Voice-XXXX
```

其中 `XXXX` 使用设备 MAC 地址生成。

实际实现：

```cpp
snprintf(_ssid, sizeof(_ssid), "ESP32-Voice-%s", WiFi.macAddress().c_str());
```

> 注意：MAC 地址完整字符串用于 SSID，而非截取部分。实际 SSID 形如 `ESP32-Voice-AA:BB:CC:DD:EE:FF`。

---

# 13. SoftAP IP

固定：

```text
192.168.4.1
```

手机连接后：

```text
http://192.168.4.1/
```

可以访问配置页面。

实际实现：

```cpp
#define CONFIG_WEB_AP_IP      IPAddress(192, 168, 4, 1)
#define CONFIG_WEB_AP_GW      IPAddress(192, 168, 4, 1)
#define CONFIG_WEB_AP_SUBNET  IPAddress(255, 255, 255, 0)
```

---

# 14. SoftAP 密码

开发版本使用固定密码：

```cpp
#define CONFIG_WEB_AP_PASSWORD "ESP32Voice"
```

但是：

> 不要把这个设计成最终生产版本。

以后应考虑：

```text
设备唯一密码
或
随机密码
或
二维码配网
```

本阶段不实现上述高级安全方案。

---

# 15. Captive Portal

已实现。目标：

```text
手机
 ↓
连接 ESP32 Wi-Fi
 ↓
系统检测到"需要登录网络"
 ↓
自动打开 ESP32 配置页面
```

---

# 16. Captive Portal 实现

使用：

```cpp
DNSServer
WebServer
```

DNS 对所有请求解析到：

```text
192.168.4.1
```

实际实现：

```cpp
_dnsServer.start(CONFIG_WEB_AP_IP, "_", CONFIG_WEB_AP_IP);
```

HTTP 请求最终进入 Web 配置页面。Captive portal 检测 URL 重定向到 `/`：

```cpp
_server.on("/generate_204", HTTP_GET, ...);        // Android
_server.on("/hotspot-detect.html", HTTP_GET, ...);  // iOS
_server.on("/connecttest.txt", HTTP_GET, ...);      // Windows
_server.on("/ncsi.txt", HTTP_GET, ...);              // Windows
```

---

# 17. Web 模块

已创建：

```text
web/
├── config_web.h
└── config_web.cpp
```

Web 模块不要直接操作 NVS。

架构：

```text
ConfigWeb
 ↓
DeviceConfig
 ↓
RuntimeConfig
 ↓
NVS
```

不要：

```text
ConfigWeb
 ↓
直接 Preferences
```

---

# 18. Web API

已实现的路由：

```text
GET  /              → 配置页面 (HTML form)
POST /save          → 保存配置到 NVS
POST /reset         → Wi-Fi Reset (仅删除 SSID/Password)
POST /factory-reset → Factory Reset (清除所有 NVS key)
POST /reboot        → 重启设备
```

> 注意：`GET /status` (JSON 端点) 未在 Step 2C-1 中实现，因为当前不需要。如需可在后续添加。

---

# 19. GET /

已实现。显示移动端友好的配置页面。

页面包含：

```text
ESP32 Voice AI
Firmware Configuration Mode

┌─────────────────────────────────────────┐
│ Configuration Mode    AP: 192.168.4.1   │
└─────────────────────────────────────────┘

┌─────────────────────────────────────────┐
│ Wi-Fi                                   │
│                                         │
│ SSID                                    │
│ [________________________]              │
│                                         │
│ Password                                │
│ [________________________]              │
│ Leave empty to keep current password.   │
│ ☐ Show password                         │
└─────────────────────────────────────────┘

┌─────────────────────────────────────────┐
│ PC Server                               │
│                                         │
│ Host                                    │
│ [________________________]              │
│                                         │
│ Port                                    │
│ [____]                                  │
└─────────────────────────────────────────┘

┌─────────────────────────────────────────┐
│ Voice Detection                         │
│                                         │
│ RMS Threshold                           │
│ [____]                                  │
│ Higher value requires louder speech.    │
│                                         │
│ Minimum Voice Time (ms)                 │
│ [____]                                  │
│ Minimum duration of detected speech.    │
│                                         │
│ Silence Time (ms)                       │
│ [____]                                  │
│ How long silence before recording stops.│
└─────────────────────────────────────────┘

[ Save Configuration ]
[ Reset Wi-Fi ]
[ Factory Reset ]
[ Reboot ]
```

所有配置值从 `DeviceConfig → RuntimeConfig` 读取，经 `htmlEscape()` 转义后写入 HTML。

---

# 20. GET /status

**未实现。** 当前版本不需要 JSON 状态端点。

如需添加，应返回：

```json
{
  "cfg_ver": 1,
  "wifiConfigured": true,
  "serverHost": "192.168.0.3",
  "serverPort": 8888,
  "vadRms": 400,
  "vadMinMs": 200,
  "vadSilMs": 700
}
```

**不要返回 Wi-Fi password。**

---

# 21. POST /save

已实现。

保存前必须：

```text
读取当前配置副本
 ↓
复制所有字段 (memcpy)
 ↓
解析并验证每个字段
 ↓
全部有效？
    /       \
  NO         YES
  ↓           ↓
HTTP 400    调用 DeviceConfig::save()
            ↓
        cfg_ver = CFG_VER
            ↓
        保存到 NVS (事务式)
            ↓
        更新内存配置
            ↓
        HTTP 200 + 成功页面
            ↓
        调度重启 (_rebootAt = millis() + 2000)
```

关键行为：

- **空密码 = 保留当前密码**（通过先 memcpy 当前配置再覆盖非空字段）
- **密码不在日志或响应中打印**
- **save() 成功后调度非阻塞重启**（2 秒延迟，确保 HTTP 响应已发送）

---

# 22. Wi-Fi Reset

`POST /reset`

已实现。

> 只删除 Wi-Fi SSID 和密码。

不要删除：

```text
PC Host
PC Port
VAD
配置版本
其他设置
```

实际实现（`DeviceConfig::resetWifi()`）：

```text
1. 打开 NVS namespace "voice_ai"
2. prefs.remove(NVS_KEY_SSID)   → 删除 cfg_ssid
3. prefs.remove(NVS_KEY_PWD)    → 删除 cfg_pwd
4. 关闭 NVS
5. loadDefaults() → 从 secrets.h 重载所有字段
6. _hasNVSConfig = false
```

然后重启设备，重新进入配置模式。

---

# 23. Factory Reset

`POST /factory-reset`

已实现。

Factory Reset 只清除：

```text
voice_ai
```

namespace。

**不要执行整个 NVS 分区擦除。**

实际实现（`DeviceConfig::factoryReset()`）：

```text
1. 打开 NVS namespace "voice_ai"
2. prefs.remove() 所有 8 个 key:
   - cfg_ssid, cfg_pwd, cfg_host, cfg_port
   - vad_rms, vad_min, vad_sil, cfg_ver
3. 关闭 NVS
4. loadDefaults() → 从 secrets.h 重载所有字段
5. _hasNVSConfig = false
```

然后重启设备。

---

# 24. POST /reboot

已实现。

使用非阻塞重启：

```text
收到 POST /reboot
 ↓
设置 _rebootAt = millis() + 1000
 ↓
发送 HTTP 200 响应
 ↓
loop() 中检查:
   if (_rebootAt != 0 && millis() >= _rebootAt)
   {
       ESP.restart();
   }
```

确保 HTTP 响应已经发送后再重启。

---

# 25. Provisioning 状态机

已实现（简化版）。

```text
BOOT
 ↓
DeviceConfig::begin()
 ↓
isConfigValid()?
 ├── false → g_inConfigMode = true → ConfigWeb::begin() → SoftAP
 └── true  → 正常 Wi-Fi 连接模式
```

当前 `main.cpp` 中的实际流程：

```cpp
// setup()
g_config.begin();

if (!g_config.isConfigValid())
{
    g_inConfigMode = true;
    g_web.begin(g_config);
}
else
{
    // 正常 Wi-Fi + TCP + 音频模式
}

// loop()
if (g_inConfigMode)
{
    g_web.loop();  // 处理 DNS + HTTP + 非阻塞重启
    return;
}
// 正常模式...
```

---

# 26. 与现有 main.cpp 的关系

`main.cpp` 不负责：

```text
Preferences
HTML
DNS
HTTP route
NVS key
```

`main.cpp` 只负责：

```cpp
DeviceConfig g_config;
ConfigWeb g_web;

// setup()
g_config.begin();
g_web.begin(g_config);  // 仅在配置模式下

// loop()
g_web.loop();  // 仅在配置模式下
```

具体实现隐藏在模块内部。

---

# 27. 现有硬编码参数替换

**⏳ 待实施 (Step 3)**

当前代码中如果存在：

```cpp
#define VAD_RMS_THRESHOLD 400
#define VAD_MIN_VOICE_MS 200
#define VAD_SILENCE_MS 700
```

不要立即删除。

第一步先确认这些参数当前在哪里使用。

然后最小化修改为：

```cpp
g_config.getConfig().vad_rms
g_config.getConfig().vad_min_ms
g_config.getConfig().vad_sil_ms
```

同样：

```text
192.168.0.3
8888
```

以后从：

```cpp
g_config.getConfig().pc_host
g_config.getConfig().pc_port
```

读取。

**不要修改 TCP 协议本身。**

---

# 28. 配置 Web 与正常工作模式

最终架构预留：

```text
              Device
                 │
         ┌───────┴────────┐
         │                │
      Config            Normal
       Mode              Mode
         │                │
       SoftAP           STA Wi-Fi
         │                │
      Web Config       Cloud/Server
```

本阶段重点完成：

```text
SoftAP → Web Config
```

以后可以扩展：

```text
STA → Web Config
BLE → Config API
```

但本阶段不实现 BLE。

---

# 29. 实际模块接口

## device_config.h

```cpp
struct RuntimeConfig
{
    char     wifi_ssid[WIFI_SSID_MAX_LEN + 1];
    char     wifi_pass[WIFI_PASS_MAX_LEN + 1];
    char     pc_host[HOST_MAX_LEN + 1];
    uint16_t pc_port;
    uint32_t vad_rms;
    uint32_t vad_min_ms;
    uint32_t vad_sil_ms;
    uint16_t cfg_ver;
};

class DeviceConfig
{
public:
    DeviceConfig();
    bool begin();
    const RuntimeConfig& getConfig() const;
    bool isConfigValid() const;
    bool save(const RuntimeConfig& newConfig);
    bool resetWifi();
    bool factoryReset();
};
```

## config_web.h

```cpp
class ConfigWeb
{
public:
    ConfigWeb();
    bool begin(DeviceConfig& config);
    void loop();
    void stop();
    const char* getApSsid() const;
private:
    void setupRoutes();
    void handleRoot();
    void handleCaptivePortal();
    void handleSave();
    void handleReset();
    void handleFactoryReset();
    void handleReboot();
    static String sendResultPage(bool success, const String& message);
    static void safeCopy(const String& src, char* dst, size_t dstSize);
    static bool parseU32(const String& text, uint32_t& out);
    WebServer    _server;
    DNSServer    _dnsServer;
    char         _ssid[32];
    DeviceConfig* _config;
    uint32_t     _rebootAt;
};
```

---

# 30. 修改原则

已遵守：

```text
[x] 先检查现有代码
[x] 找到当前 Wi-Fi 初始化位置
[x] 找到当前 Server IP/Port
[x] 找到当前 VAD 参数
[x] 找到 main.cpp 生命周期
[x] 再开始修改
```

---

# 31. 编译测试

已验证：

```bash
pio run -e esp32-s3-n16r8
pio run -e esp32-s3-n16r8-wifi
```

结果：

```text
esp32-s3-n16r8     → SUCCESS (346,989 bytes flash, 9.8% RAM)
esp32-s3-n16r8-wifi → SUCCESS (814,197 bytes flash, 22.1% RAM)
```

---

# 32. 第一阶段测试顺序

### Test 1：正常编译

```text
pio run
```

✅ 完成 — 两个 environment 均编译成功。

### Test 2：SoftAP

确认手机能够看到：

```text
ESP32-Voice-XXXX
```

⏳ 待手动测试

### Test 3：连接 AP

手机连接成功。

⏳ 待手动测试

### Test 4：Captive Portal

连接后能够自动打开配置页面。

⏳ 待手动测试

### Test 5：手动访问

```text
http://192.168.4.1/
```

能够打开。

⏳ 待手动测试

### Test 6：读取配置

页面显示当前配置。

⏳ 待手动测试

### Test 7：修改配置

例如：

```text
Server Port
8888 → 9999
```

保存。

⏳ 待手动测试

### Test 8：重启

点击：

```text
重启
```

⏳ 待手动测试

### Test 9：配置持久化

重启后：

```text
9999
```

仍然存在。

⏳ 待手动测试

### Test 10：Factory Reset

执行恢复出厂。

确认：

```text
Server → 192.168.0.3:8888
VAD → 400 / 200 / 700
```

⏳ 待手动测试

### Test 11：现有音频功能

确认：

```text
Mic
 ↓
VAD
 ↓
TCP
 ↓
PC
 ↓
TTS
 ↓
ESP32播放
```

仍然正常。

⏳ 待手动测试

### Test 12：提供测试用的说明文档

提供测试用的说明文档，要包含测试的详细步骤，包括测试顺序，预期测试结果等。

⏳ 待实施

---

# 33. 最终验收标准

本阶段完成必须满足：

```text
[x] ESP32可以启动SoftAP
[x] 手机可以连接
[x] Captive Portal可以工作
[x] Web页面可以打开
[x] 可以修改Wi-Fi配置
[x] 可以修改Server配置
[x] 可以修改VAD配置
[x] 配置可以保存到NVS
[x] 重启后配置仍然存在
[x] 配置版本存在
[x] 可以Wi-Fi Reset
[x] 可以Factory Reset
[x] 可以Web Reboot
[x] password不会通过/status返回
[x] 原有音频功能没有被破坏
[x] 原有TCP协议没有被修改
```

---

# 34. 实际实现顺序

```text
Step 1
检查现有项目
    ↓
Step 2
实现 DeviceConfig + RuntimeConfig + NVS
    ↓
Step 3
编译测试
    ↓
Step 4
实现 SoftAP + DNS + Captive Portal + WebServer (Step 2A)
    ↓
Step 5
编译测试
    ↓
Step 6
实现 Web 配置页面显示 (Step 2B-1)
    ↓
Step 7
编译测试
    ↓
Step 8
实现 NVS 保存 + Reset + Factory Reset + Reboot (Step 2C-1)
    ↓
Step 9
编译测试
    ↓
Step 10
把 Server/VAD 参数接入 Config (Step 3 — 待实施)
    ↓
Step 11
完整测试
```

**每一步完成后先编译/测试，再进入下一步。**
