# ESP32 Voice AI - 网络配置与配对方案

> 本文档回答三个问题：
> 1. ESP32 怎么知道 PC Server 的 IP？
> 2. PC Server 怎么反向知道 ESP32 的 IP？
> 3. 网络变了怎么办（换 WiFi / 换机器 / 从 PC 迁到 aidlux 手机）？
>
> 覆盖：现状分析、5 种方案对比、行业最佳实践、反向发现、迁移路径与推荐落地顺序。

---

## 1. 当前状态（Phase 1 已实现）

### 1.1 ESP32 → PC 方向

配置**单一真相源**在 [`config.local.json`](../config.local.json.example)（用户填入真实值，`.gitignore`），
PIO pre-build 时由 [`scripts/gen_secrets.py`](../scripts/gen_secrets.py) 自动转换成
[`firmware/esp32/src/secrets.local.h`](../firmware/esp32/src/secrets.local.h)，
再被 [`firmware/esp32/src/secrets.h`](../firmware/esp32/src/secrets.h) `#include`。

```jsonc
// config.local.json
{
  "wifi_ssid": "your_wifi_ssid",
  "wifi_pass": "your_wifi_password",
  "pc_host": "192.168.1.20",   // PC / aidlux 静态 IP
  "pc_port": 8888,
  "esp32_hostname": "esp32-voice"
}
```

等价 C 宏（gen 后的样子）：

```c
#define WIFI_SSID       "your_wifi_ssid"
#define WIFI_PASS       "your_wifi_password"
#define PC_HOST         "192.168.1.20"
#define PC_PORT         8888
#define ESP32_HOSTNAME  "esp32-voice"
```

- 换 IP / 换 SSID → 只改 `config.local.json` → 重新 `pio run`（会自动 re-gen `secrets.local.h`）
- `WifiClient::begin()` 里读一次，之后 `_pcIp` 就不再变
- 支持 `"192.168.1.20"` / `"pc.example.com"` / `"my-pc.local"` 三种写法（先 `fromString` 再 `gethostbyname` 兜底）
- PC 侧 [`pc/config.py`](../pc/config.py) 也直接读同一个 `config.local.json`，两侧永远一致

### 1.2 ESP32 广播自己的 IP

- 已启用 mDNS：`MDNS.begin(ESP32_HOSTNAME)` + `MDNS.addService("tcp", "tcp", PC_PORT)`
- PC 侧可以 `ping esp32-voice.local` / `nc esp32-voice.local 8888` 找到 ESP32
- **但**：ESP32 自己**没有**消费 PC 侧 mDNS 服务（PC 还没广播 `wifi_server` 服务）
- 目前 mDNS 只是"ESP32 单向广播自己"，PC 侧还没反过来广播

### 1.3 PC Server 侧

- [`wifi_server.py`](../pc/wifi_server.py) `bind(0.0.0.0, 8888)` 监听所有接口
- 不做主动发现，等 ESP32 连过来
- 不做 mDNS 广播，PC 换个 IP，ESP32 就连不上

---

## 2. 五种方案对比

### 方案 A：路由器端 DHCP 保留（零代码）

**做法**：在路由器后台把 PC 的网卡 MAC 绑定到固定 IP（例如 `192.168.1.20`）。`config.local.json` 里 `pc_host` 写这个 IP。

| 项 | 说明 |
|---|---|
| 代码改动 | 0 |
| 配置改动 | 路由器后台点一下 |
| 换网络 | 换路由器 → 重新配置一次 DHCP 保留 |
| 换 PC | 换机器 → 重新做保留 |
| 换到 aidlux | 手机 IP 通常动态，**难做到真正固定**，需要手机路由器特权 |
| 优点 | 最稳，代码路径最简 |
| 缺点 | 换环境每次都要进路由器，aidlux 手机不适用 |

**结论**：**短期首选**。桌面开发期最省事，但换到手机就不灵。

---

### 方案 B：mDNS 主机名（PC 侧广播，ESP32 用 `.local`）

**做法**：
1. PC 侧装 `avahi-daemon`（Linux）/ `Bonjour`（macOS 自带）/ `dnssd`（Windows 需第三方）
2. `avahi-daemon` 广播本机为 `my-pc.local`
3. ESP32 侧 `config.local.json`：
   ```json
   { "pc_host": "my-pc.local" }
   ```
4. `WifiClient::begin()` 已经处理：`fromString()` 失败时走 `gethostbyname()`

| 项 | 说明 |
|---|---|
| 代码改动 | 0（当前 `wifi_client.cpp` 已经支持） |
| 配置改动 | PC 装 mDNS 服务 + `config.local.json` 改一次 |
| 换网络 | 自动，只要 PC 和 ESP32 在同一 mDNS 域 |
| 换 PC | 新 PC 装 avahi 即可 |
| 换到 aidlux | 手机 IP 变了也无所谓，因为按主机名解析；但手机要能收 mDNS |
| 优点 | 零烧录，跨设备 |
| 缺点 | **Windows 没原生 mDNS**（要装软件）；路由器/AP 隔离时 mDNS 广播会被拦；aidlux 手机环境对 mDNS 支持不确定 |

**avahi 是什么？**
- 一套开源的 mDNS / DNS-SD 实现，Linux 桌面/服务器标配
- 装法（Ubuntu / Debian）：
  ```bash
  sudo apt install avahi-daemon avahi-utils
  sudo systemctl enable avahi-daemon
  ```
- 让 PC 广播为本机名：`hostnamectl set-hostname my-pc`
- 验证：`avahi-browse -a` 或 `ping my-pc.local`
- macOS：系统自带 Bonjour，无需装
- Windows：需要 [dnssd](https://github.com/jasonbarnett/dnssd-win) 或 iTunes（含 Bonjour）
- **复杂程度**：Linux / macOS **不复杂**，一行装完；Windows **麻烦**

**结论**：**桌面 PC 长期用这个最爽**；换到手机 aidlux 时不可靠（依赖手机 OS 的 mDNS 支持）。

---

### 方案 C：ESP32 NVS + Serial 命令

**做法**：把 `PC_HOST` 存到 ESP32 的 NVS（非易失存储），启动时先看 NVS，没有才用 `secrets.h` / `secrets.local.h` 的默认值。通过 USB Serial 命令或 HTTP 更新 NVS。

**固件侧**：
```cpp
#include <Preferences.h>

// 启动时读
Preferences prefs;
prefs.begin("net", false);
String host = prefs.getString("pc_host", String(PC_HOST));
uint16_t port = prefs.getUInt("pc_port", PC_PORT);
prefs.end();

// 串口命令（在 main.cpp 里解析）
// SET_HOST 192.168.1.21
// SET_PORT 9000
// SHOW_NET          // 打印当前值
// REBOOT
```

| 项 | 说明 |
|---|---|
| 代码改动 | 约 100 行（Preferences + 串口命令解析） |
| 换 IP | USB 串口敲 `SET_HOST 新IP` + `REBOOT`，不重烧 |
| 换 SSID | 同上，加 `SET_SSID` / `SET_PASS` 命令 |
| 优点 | 无需 USB 重新烧录；保留出厂默认值 |
| 缺点 | 必须物理接 USB 敲命令；aidlux 手机上没 USB 敲命令的界面 |

**结论**：**开发期很好用**（USB 串口手就在手边），但不适合"用户拿到的产品"。

---

### 方案 D：ESP32 内嵌 Web UI + mDNS（**行业最佳实践**）

**做法**：
1. ESP32 内嵌一个 Web Server（`WebServer` 或 `ESPAsyncWebServer`）
2. 通过 mDNS 广播：`http://esp32-voice.local`
3. 浏览器打开这个地址 → Web 表单填写：
   - Wi-Fi SSID / 密码
   - PC Host IP / 端口
4. ESP32 保存到 NVS，重启

**固件侧需要**：
```
lib_deps:
    armarulz/esp32asyncwebserver@^1.5     // 或 arduino 自带 WebServer
    me-no-dev/ESPAsyncWebServer@^1.2.3    // 或 arduino 自带 WebServer
```
- `WebServer` 监听端口（比如 80）
- 表单 POST 到 `/config`
- 保存到 NVS，重启

**mDNS 广播 HTTP 服务**：
```cpp
MDNS.begin("esp32-voice");
MDNS.addService("http", "tcp", 80);   // 现在广播的是 Web UI 端口
```

| 项 | 说明 |
|---|---|
| 代码改动 | 约 300–500 行（Web 框架 + HTML 模板 + NVS 读写 + 重启） |
| 换 IP | 浏览器打开 `http://esp32-voice.local` → 表单改 → 保存 |
| 换 SSID | 同上，同一个页面 |
| 优点 | **用户友好**，标准做法，跨设备通用 |
| 缺点 | 需要引入 Web 库（体积增加 ~150 KB flash）；ESP32 同时跑 Web Server + TCP Client 8888 端口资源占用增加 |
| 与 aidlux 兼容 | aidlux 上浏览器打开 Web UI 也能配 |

**结论**：**长期目标形态**。**当前不推荐立刻做**——引入第三方 Web 库、增加固件复杂度、需要维护 HTML。

---

### 方案 E：SoftAP 配网（首次联网）

**做法**：ESP32 首次启动若没配过 Wi-Fi，自动开一个热点 `ESP32-Voice-Config`，密码 `12345678`；PC / 手机连上这个热点，打开 `http://192.168.4.1`，配网页面填 SSID / 密码 / PC Host；配完重启，切换到 STA 模式。

**固件侧**：
- `WiFi.mode(WiFi_AP)` 作为兜底
- 检测 STA 是否成功，30 s 内没连上则切 AP
- 配完写 NVS + 重启

| 项 | 说明 |
|---|---|
| 代码改动 | 约 500 行（Web UI + SoftAP + 状态机） |
| 首次配网 | 用户连 ESP32 热点 → 网页填 SSID → 保存 |
| 优点 | **完全不需要预知 PC IP**，适合分发给非技术用户 |
| 缺点 | 首次配网必须靠近 ESP32；PC 端要能连热点；配完后切换回 STA，用户可能困惑 |
| 与 aidlux 兼容 | 手机浏览器打开热点里的 `192.168.4.1` 也能配 |

**结论**：**分发给非技术用户 / 家庭场景时最佳**。开发期用不上。

---

## 3. 反向发现：PC Server 怎么找到 ESP32？

**当前**：PC Server 不知道 ESP32 在哪。ESP32 是主动连接方，Server 端 `bind(0.0.0.0, 8888)` 等就行。

**为什么有时候 PC 也想主动找 ESP32？**
- PC Server 想主动 push 音频（例如 TTS 完成、想主动发一段音乐）
- PC Server 想读取 ESP32 状态（RSSI、电池、VAD 状态）
- PC Server 想 OTA 升级固件
- aidlux 手机作为 Server 时，也需要知道 ESP32 在哪

**当前已有的基础设施**：ESP32 侧已启用 mDNS，广播 `esp32-voice.local` 和 `tcp:8888`。

**PC 侧消费 mDNS 方案**：

```python
# 方案 1：subprocess 调用 avahi-browse（Linux）
subprocess.run(["avahi-browse", "-t", "-r", "tcp"])

# 方案 2：Zeroconf 库（跨平台，需 pip install）
import zeroconf
br = zeroconf.Zeroconf()
services = br.browse("_tcp._tcp.local.")   # 太宽
# 或者监听特定类型

# 方案 3：直接解析 .local
socket.getaddrinfo("esp32-voice.local", 8888)
```

**注意**：`zeroconf` 库**不是标准库**，会破坏 aidlux 迁移承诺。
**aidlux 兼容的替代方案**：
```python
# 只用 socket 标准库：直接解析 .local 主机名
# 前提：PC 侧装了 avahi-daemon（提供 mDNS 解析）
addr = socket.getaddrinfo("esp32-voice.local", 8888)[0][4][0]
```

---

## 4. 行业最佳实践参考

以下产品的实际做法（供借鉴，不是要照抄）：

| 产品 | 首次配网 | 后续配置 | 反向发现 | 备注 |
|------|----------|----------|----------|------|
| **ESPHome** | 蓝牙 BLE 配网（`esphome config-editor`） | YAML 重烧 | Home Assistant 端维护设备列表 | 需要蓝牙，ESP32-S3 支持 BLE |
| **Home Assistant**（自建 ESPHome） | ESPHome BLE | Home Assistant UI | SSDP / Zeroconf | 完整智能家居栈 |
| **Tasmota** | SoftAP 配网 | Web UI + MQTT | mDNS (`tasmota_xxxx.local`) | 最贴近本项目的开源参考 |
| **Amazon Alexa**（Echo 设备） | 手机 App 引导（SoftAP / QR） | 云端注册 | 云端分配 ID，设备主动连云 | 云端依赖 |
| **Google Home**（Nest） | 手机 App（WiFi Direct） | 云端 | 云端 | 云端依赖 |
| **OpenHAB**（自研 ESP32 设备） | SoftAP + Web | MQTT / REST | UPnP / SSDP | 开源智能家居 |
| **Bangle.js**（可穿戴） | 手机 App BLE | 手机端 WebUI | BLE | 无 Wi-Fi |
| **LoRa 网关（TTN）** | 出厂预设 | MQTT broker 云端 | 云端 | 云端依赖 |

**共同特点**：

1. **首次配网 ≠ 后续配置**
   - 首次：SoftAP 或 BLE，用户手动
   - 后续：设备已连上家庭 Wi-Fi，改配置走云端或 mDNS Web UI

2. **mDNS 是标配**
   - ESPHome / Tasmota / OpenHAB / Home Assistant 都默认开 mDNS
   - 让"局域网内发现"变成零成本

3. **NVS / 非易失存储**是标配
   - 出厂只存一份默认配置
   - 用户改动都落 NVS
   - 出厂烧录与用户配置分离

4. **Web UI 是标配**
   - Tasmota / ESPHome Web UI 就是 Web 表单
   - 但**只在需要时才启动**（Tasmota 平时不开 80 端口）
   - 本项目的 Phase 1 不需要这个

5. **反向发现**（Server → 设备）
   - 多数用 **SSDP**（UPnP）或 **mDNS**
   - ESPHome / Home Assistant 走 Zeroconf
   - 本项目的 Server 主动找 ESP32 场景 Phase 1 没有

---

## 5. 推荐落地顺序

按 Phase 分层，**每阶段保持 aidlux 兼容**（stdlib only）：

### Phase 1（当前，已完成）
- 编译期常量 + mDNS 广播 ESP32 自己
- 换 IP 要重烧

### Phase 1.5（建议下一小步，改动小）
- **PC 侧**：装 `avahi-daemon`，`config.local.json` 里 `pc_host` 改成 `"my-pc.local"`
- **固件侧**：零改动
- 收益：换 IP / 换 PC 不用重烧
- 风险：Windows 环境需要额外装软件；手机 aidlux 环境不一定支持 mDNS

### Phase 2（建议）
- **固件侧**：加入 NVS + Serial 命令（方案 C）
  - `SET_HOST <ip>` / `SET_PORT <port>` / `SET_SSID <ssid>` / `SET_PASS <pass>` / `SHOW_NET` / `REBOOT`
  - 启动读 NVS，回退到 `secrets.local.h`（由 `config.local.json` 生成）
- **PC 侧**：加一个 `net_admin.py` 脚本，通过 USB Serial 敲命令
- 收益：不用重烧固件，用户可以在 PC 上改配置
- 风险：需要 USB 物理连接

### Phase 3（可选，产品化）
- **固件侧**：Web UI + SoftAP（方案 D + E）
  - `ESPAsyncWebServer` + 简 HTML 表单
  - 出厂默认开 SoftAP 直到 NVS 有 SSID
  - 用户浏览器打开 `http://esp32-voice.local` 配网
- **代价**：引入第三方 Web 库，固件体积 +150 KB，代码复杂度上升
- **aidlux 影响**：Web UI 用 HTML + JSON，与 Python 端解耦，不影响 aidlux

### Phase 3.5（可选，反向发现）
- **PC 侧**：`wifi_server.py` 启动时解析 `esp32-voice.local` 显示设备状态
- **限制**：依赖 PC 端 mDNS 解析（`avahi-daemon` 或零 conf）
- **aidlux 兼容**：只用 `socket.getaddrinfo`，保持 stdlib

---

## 6. 决策矩阵（快速参考）

| 使用场景 | 推荐方案 | 说明 |
|----------|----------|------|
| 桌面 PC + 固定网络（家庭路由） | A + B | DHCP 保留 + mDNS 主机名 |
| 桌面 PC + 经常换网络 | B | mDNS 主机名 |
| 桌面 PC + 频繁改配置 | A + C | DHCP 保留 + NVS/Serial |
| Windows + WSL2（NAT 模式） | A 变体（直连 WSL2 IP） | mDNS 组播不通，见 §6.1 |
| Windows + WSL2（桥接模式） | A + B | 桥接后 mDNS 可能通，见 §6.1 |
| macOS | A + B | 自带 Bonjour，最省事 |
| Windows 原生 | A | 装 iTunes / dnssd-win 可选 |
| 移动端 aidlux | B（若能 mDNS） / 直连手机 IP | 需实验手机 mDNS |
| 分发给非技术用户 | D + E | Web UI + SoftAP |
| Server 需要主动找 ESP32 | 方案 B 扩展 + PC 端零 conf | 依赖 PC 侧 mDNS |

### 6.1 Windows + WSL 场景说明

WSL2 有默认 NAT 和可选桥接两种模式，mDNS 通不通取决于模式：

#### NAT 模式（WSL2 默认）

```text
[路由器] ── 二层 ── [Windows 宿主] ── NAT ── [WSL2 虚拟机]
                                                    │
                                [ESP32] 只能到 Windows，到不了 WSL2 的组播
```

| 项 | 效果 |
|---|---|
| TCP 直连 WSL2 IP（`172.20.x.x`） | ✅ **仅 Windows 侧可**（Windows → WSL 走内核路由，通） |
| **ESP32 → WSL2 IP（`172.20.x.x`）** | ❌ **不通**（WSL 私有网段不在路由器路由表里） |
| **ESP32 → Windows 宿主 IP** | ⚠️ 有监听则通；默认 Wi-Fi Server 在 WSL 内，Windows 侧无监听 |
| WSL2 里 `ping esp32-voice.local` | ❌ 不通（组播到不了 WSL2 虚拟网卡） |
| WSL2 里 avahi 广播 | ❌ ESP32 收不到 |
| WSL2 IP 稳定性 | ❌ 每次启动可能变 |

**结论**：NAT 模式下 ESP32 只能打 Windows 宿主 IP。要让这个 IP 上的 8888 转发到 WSL 内的 `wifi_server.py`，必须：

1. **Windows 侧建 portproxy**（把 Windows 的 8888 转到 WSL 的 8888）
2. **Windows 防火墙放行入站 TCP 8888**

具体命令见下方「NAT 模式端口转发步骤」小节。

#### NAT 模式端口转发步骤（管理员 PowerShell）

```powershell
# ① 查 WSL2 当前 IP
wsl -e ip -4 addr show eth0 | grep inet
# 例如 inet 172.25.219.111/20

# ② 查 Windows 宿主 LAN IP
ipconfig
# 例如 IPv4 地址 . . . . . . . . : 192.168.0.3

# ③ 添加 portproxy 转发（管理员）
netsh interface portproxy add v4tov4 `
    listenaddress=0.0.0.0           `
    listenport=8888                 `
    connectaddress=172.25.219.111   `
    connectport=8888

# ④ 验证
netsh interface portproxy show v4tov4

# ⑤ 放行 Windows 防火墙
New-NetFirewallRule `
    -DisplayName "ESP32 Voice AI TCP 8888" `
    -Direction Inbound -Protocol TCP `
    -LocalPort 8888 -Action Allow

# ⑥ 从 Windows 测通
Test-NetConnection 192.168.0.3 -Port 8888
# TcpTestSucceeded: True  ← 通

# ⑦ config.local.json 里 pc_host 写 Windows 宿主 IP（不是 WSL IP）
# { "pc_host": "192.168.0.3", "pc_port": 8888 }
```

**注意**：WSL2 IP 每次 `wsl --shutdown` 后可能变化，需重复步骤 ①③。长期方案见下方「桥接模式」。

完整排查与清理命令见 [`test-2026-09-09-phase1-wifi-voice-loop.md`](./test-2026-09-09-phase1-wifi-voice-loop.md) **附录 F**。

#### 桥接模式（可选）

```text
[路由器] ── 二层 ── [Windows 网卡] ── 桥 ── [WSL2 桥接网卡]
                                                    │
                                [ESP32] 同二层，mDNS 组播可通
```

配置步骤：
1. Windows：Hyper-V 管理器 → Virtual Switch Manager → 新建 External（绑定物理网卡）
2. `%UserProfile%\.wslconfig`：
   ```ini
   [wsl2]
   networkingMode=bridged
   ```
3. 重启 WSL：`wsl --shutdown`；`wsl`
4. WSL2 里 `ip addr` 应看到 `192.168.1.x` 真实局域网 IP
5. 路由器端给这个 MAC 做 DHCP 保留 → 拿固定 IP

| 项 | 效果 |
|---|---|
| TCP 直连 WSL2 IP（`192.168.1.x`） | ✅ 通 |
| WSL2 里 `ping esp32-voice.local` | ⚠️ 取决于网卡驱动的组播支持，多数可行 |
| WSL2 里 avahi 广播 → ESP32 收 | ⚠️ 同上 |
| WSL2 IP 稳定性 | ✅ 通过 DHCP 保留固定 |

**结论**：桥接后 mDNS **通常能通**，但仍需在具体网卡上验证。

#### WSL 场景推荐三步走

1. **第一步**：NAT 模式下用 **portproxy** 跑通（**不能**直接让 ESP32 打 WSL IP）
   - WSL2 里 `ip -4 addr show eth0 | grep inet` 拿 WSL IP
   - Windows 管理员 PowerShell 里 `netsh interface portproxy add v4tov4 ...` 把 Windows:8888 → WSL:8888
   - `New-NetFirewallRule ... -LocalPort 8888 -Action Allow` 放行 Windows 防火墙
   - `config.local.json` 里 `pc_host` 写 **Windows 宿主 IP**（不是 WSL IP）
   - `wifi_server.py --port 8888` 起服务
   - 验证 ESP32 日志 `[wifi] tcp connected`
   - 完整命令见上文「NAT 模式端口转发步骤」和 [`test-2026-09-09-phase1-wifi-voice-loop.md`](./test-2026-09-09-phase1-wifi-voice-loop.md) 附录 F

2. **第二步**：换 IP 稳定化
   - 开启桥接模式 → 路由器 DHCP 保留 → WSL2 拿固定 IP
   - 一次配置，长期有效

3. **第三步（可选）**：上 mDNS
   - WSL2 里：`sudo apt install avahi-daemon avahi-utils`
   - `hostnamectl set-hostname my-pc`
   - ESP32 侧：`config.local.json` 里 `pc_host = "my-pc.local"`
   - 验证：`avahi-browse -a | grep my-pc.local`；ESP32 日志显示 connect 成功
   - 如果 mDNS 不通，退回到第二步的固定 IP

#### 验证 mDNS 是否通（WSL 内）

```bash
# 装
sudo apt install avahi-daemon avahi-utils nss-mdns
sudo systemctl enable --now avahi-daemon

# 设主机名
hostnamectl set-hostname my-pc

# 查本机
avahi-browse -a | grep my-pc.local

# 从 Windows 侧试
# 在 PowerShell：ping my-pc.local
# 如果 Windows 侧不通，是 WSL 虚拟网卡组播隔离
```

---

## 7. 与现有代码的对应关系

| 现有代码 | 覆盖的方案 | 是否需要改 |
|----------|-----------|-----------|
| [`firmware/esp32/src/secrets.h`](../firmware/esp32/src/secrets.h) | 方案 A / B 的默认值来源 | 不改（作为回退） |
| [`firmware/esp32/src/network/wifi_client.cpp`](../firmware/esp32/src/network/wifi_client.cpp) | 方案 A / B 已支持（`fromString` + `gethostbyname`） | 不改 |
| 同上 | mDNS 广播 ESP32 侧 | 不改 |
| [`pc/wifi_server.py`](../pc/wifi_server.py) | Server 端 `bind(0.0.0.0)` | 不改 |
| 未实现 | 方案 C：NVS + Serial 命令 | 新增 `net_config.h/.cpp` + `main.cpp` 命令解析 |
| 未实现 | 方案 D：Web UI | 新增 `net_web.{h,cpp}` + `lib_deps: ESPAsyncWebServer` |
| 未实现 | 方案 E：SoftAP 配网 | 新增 `net_softap.{h,cpp}` + 状态机 |
| 未实现 | 反向发现 | PC 端 `zeroconf` 或 `socket.getaddrinfo("esp32-voice.local")` |

---

## 8. 迁移到 aidlux 时的注意事项

| 项 | 说明 |
|---|---|
| 手机 IP 是动态的 | **不能用方案 A**（DHCP 保留） |
| 手机 mDNS 支持 | iOS 支持 mDNS（Bonjour 是系统级）；Android 需要 App 层实现，aidlux 环境待验证 |
| 用户如何发现 aidlux IP | 手机连上 Wi-Fi 后，`ifconfig` 看 IP；或 aidlux 内嵌 mDNS 广播自己 |
| 推荐方案 | 让 aidlux 也广播自己为 `aidlux-voice.local`，ESP32 侧 `config.local.json` 里 `pc_host = "aidlux-voice.local"` |
| 或反向 | ESP32 已经广播 `esp32-voice.local`，aidlux 侧启动时 `socket.getaddrinfo("esp32-voice.local")` 找到 ESP32，然后 Server 主动去 listen，等 ESP32 连过来 |

**核心洞察**：无论迁到哪个设备，只要**双方都用 mDNS 广播 + 主机名解析**，IP 变化就不用改配置。

---

## 9. 版本

| 版本 | 日期 | 变更 |
|------|------|------|
| v1 | 2026-09-09 | 初版：现状分析 + 5 种方案对比 + 行业最佳实践 + 反向发现 + aidlux 迁移注意事项 |
| v1.1 | 2026-09-09 | §6.1 NAT 模式补完整 portproxy + 防火墙命令；修正"ESP32 不能直接打 WSL IP"的描述 |

---

## 10. 相关文档

- 系统架构与数据流：[`architecture.md`](./architecture.md)
- 通信协议（TCP 帧格式）：[`protocol.md`](./protocol.md)
- 固件编译与网络配置（config.local.json）：[`firmware.md`](./firmware.md)
- 硬件接线（含 Wi-Fi）：[`wiring.md`](./wiring.md)
- 阶段路线：[`roadmap.md`](./roadmap.md)
