# 测试用例 · 2026-09-16 · Step 12-1 ESP32-CAM 摄像头基础

> **文档编号**：TC-20260916-STEP12-1-CAM
> **版本**：v1.0
> **适用代码**：Step 12-1（`firmware/esp32-cam`，`env:esp32-cam`）
> **测试目标**：验证 ESP32-CAM + OV2640 能上电 → 初始化摄像头 → 连接 Wi-Fi → 通过 HTTP 输出单帧 JPEG 与 MJPEG Stream
>
> **测试时长**：全流程走一遍约 15–25 分钟；每个 TC 独立完成，可按编号跳测。
>
> **参考**：[`docs/STEP_12_VISION_SERVO_PLAN.md`](./STEP_12_VISION_SERVO_PLAN.md)、[`firmware/esp32-cam/src/main.cpp`](../firmware/esp32-cam/src/main.cpp)

---

## 0. 目录

| 编号 | 测试项 | 时长 | 依赖 |
|------|--------|------|------|
| TC-01 | 硬件接线与板子确认 | 5 min | 无 |
| TC-02 | Wi-Fi 凭据修改 | 2 min | TC-01 |
| TC-03 | 固件编译 | 3 min | TC-02 |
| TC-04 | 固件烧录 | 3 min | TC-03 |
| TC-05 | 串口监视 + 启动日志 | 3 min | TC-04 |
| TC-06 | OV2640 摄像头初始化 | 2 min | TC-05 |
| TC-07 | Wi-Fi STA 连接路由器 | 3 min | TC-06 |
| TC-08 | HTTP 单帧 /capture 下载 | 2 min | TC-07 |
| TC-09 | **HTTP MJPEG Stream /stream** | 5 min | TC-08 |
| TC-10 | 长时间稳定性（10 分钟压测） | 10 min | TC-09 |
| TC-11 | 无 Wi-Fi / 摄像头硬件错误处理 | 5 min | TC-04 |

**附录**：A 参数速查 · B 诊断命令 · C 常见故障 · D 参考文档 · E 执行记录模板

---

## 通用前置条件

```text
[ ] AI-Thinker ESP32-CAM 板子（带 OV2640）
[ ] USB 数据线（能传数据，不是仅充电线）
[ ] Wi-Fi 路由器（2.4 GHz 频段可用）
[ ] 电脑已装 PlatformIO（VS Code 插件）
[ ] 电脑已装 USB-UART 驱动（CP210x / CH340）
[ ] 浏览器（Chrome / Edge / Firefox 均可）
[ ] 项目位于 /home/hqb/projects/esp32-voice-ai
```

---

## TC-01 · 硬件接线与板子确认

**目的**：确认板子型号、摄像头排线、USB 数据通道可用。

### 步骤

1. **目视检查**：
   - 板上有丝印 `ESP32-CAM` 或 `AI-THINKER`
   - 摄像头柔性排线接好，OV2640 芯片朝上
   - 板上有 USB 接口（USB-C 或 Micro-USB）
   - 板上有 1 个 BOOT 按键、1 个 LED 指示灯（GPIO 2）
2. **确认摄像头引脚方向**：
   - OV2640 排线**朝外**（远离 ESP32-CAM 主控）
   - 芯片正面朝上（能看到 `OV2640` 字样）
   - 上下反了 → 无法初始化（会在 TC-06 报错）
3. **USB 连接**：用 USB 线连接电脑，电脑应识别为新串口设备：
   ```bash
   ls /dev/ttyACM* /dev/ttyUSB*
   # 应看到 1 个新增设备，例如 /dev/ttyUSB0
   ```
4. **万用表（可选）**：测量板上的 3V3 对 GND 应为 3.2–3.4 V

### 期望结果

```text
[ ] 板子上丝印正确，摄像头排线接好
[ ] USB 连接后系统识别出 /dev/ttyUSB0 或 /dev/ttyACM0
[ ] ESP32-CAM 3V3 对 GND ≈ 3.3 V
[ ] 板载 LED（GPIO 2）上电后不亮或慢闪
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 电脑未识别串口 | USB 数据线仅充电 / USB-UART 桥驱动未装（Windows 需装 CP210x / CH340 驱动） |
| 摄像头排线看不到 OV2640 | 排线反了，上下翻过来重插 |
| 3V3 = 0 V | USB 口供电不足或主板损坏 |
| 板子反复重启 | 摄像头排线短路，断电重插 |

---

## TC-02 · Wi-Fi 凭据修改

**目的**：把固件中的占位符换成实际路由器 SSID / 密码。

### 步骤

1. 打开文件：
   ```
   /home/hqb/projects/esp32-voice-ai/firmware/esp32-cam/src/main.cpp
   ```
2. 找到第 30–31 行：
   ```cpp
   #define WIFI_SSID       "REPLACE_ME_SSID"
   #define WIFI_PASS       "REPLACE_ME_PASSWORD"
   ```
3. 改为实际值，例如：
   ```cpp
   #define WIFI_SSID       "MyHomeWiFi"
   #define WIFI_PASS       "MyPassword123"
   ```
4. 保存文件

### 期望结果

```text
[ ] WIFI_SSID 和 WIFI_PASS 已替换为真实值
[ ] 密码不超过 31 字符（Wi-Fi 标准上限）
[ ] SSID 与密码不区分大小写：需完全一致（Wi-Fi 标准）
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 忘记修改 | 上板串口会打印 `NOTE: SSID/PASS is still placeholder` |
| 密码有特殊字符 | 双引号内不需要转义反斜杠，只需转义 `"` 和 `\` |
| 密码太长 | Wi-Fi 密码 8–63 字符，超过 63 会被 Wi-Fi 路由器拒绝 |

---

## TC-03 · 固件编译

**目的**：验证 PlatformIO 能编译出 ESP32-CAM 固件。

### 步骤

1. 打开终端：
   ```bash
   cd /home/hqb/projects/esp32-voice-ai/firmware/esp32-cam
   export PATH="$HOME/.platformio/penv/bin:$PATH"
   ```
2. 执行编译：
   ```bash
   pio run -e esp32-cam
   ```

### 期望结果

```text
[ ] 编译：SUCCESS (耗时约 3–15 秒)
[ ] RAM:  约 14.7%  (used ~48028 bytes from 327680 bytes)
[ ] Flash: 约 26.9% (used ~845649 bytes from 3145728 bytes)
[ ] firmware.elf 与 firmware.bin 已生成于 .pio/build/esp32-cam/
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| `Unknown board: 'esp32cam'` | PlatformIO platform-espressif32 未安装或版本过旧 |
| `httpd_send() takes 3 arguments` | 你手动改了 httpd_send 调用参数（framework 版本决定 3 参 vs 4 参） |
| `'camera_config_t' has no member 'fb_globals'` | 你在 camera_config_t 里加了 fb_globals（当前 framework 不支持） |
| `cannot open source file 'esp_camera.h'` | VS Code linter 报错，**PlatformIO 编译时不受影响**，可忽略 |

---

## TC-04 · 固件烧录

**目的**：把固件上传到 ESP32-CAM。

### 步骤

1. 确认 USB 串口：
   ```bash
   pio device list
   ```
2. 烧录（替换 `<PORT>` 为实际串口）：
   ```bash
   cd /home/hqb/projects/esp32-voice-ai/firmware/esp32-cam
   pio run -e esp32-cam -t upload --upload-port /dev/ttyUSB0
   ```
3. Windows 用户：
   ```bash
   pio run -e esp32-cam -t upload --upload-port COM5
   ```
4. 若上传卡住，尝试：
   - 按住 BOOT 键 → 松开 → 再运行 upload
   - 或降低波特率：`--upload-speed 115200`

### 期望结果

```text
[ ] 烧录：Serial port: /dev/ttyUSB0
[ ] 烧录：Connecting...
[ ] 烧录：Writing at 0x00001000 ... (进度条)
[ ] 烧录：Successfully uploaded 845649 bytes
[ ] ESP32-CAM 自动重启
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| `Error: Could not open /dev/ttyUSB0` | 串口被串口监视器占用，先关掉 |
| `A fatal error occurred: Failed to connect to Espressif device` | 需要进下载模式（按住 BOOT 释放） |
| `No serial ports found` | USB 未连接或驱动未装 |
| `ESP_ERROR: chip id mismatch` | 板子是 ESP32-S3，不是 ESP32-CAM 经典芯片；需换 board |
| 上传成功但没重启 | 手动按一次板子上的复位键 |

---

## TC-05 · 串口监视 + 启动日志

**目的**：观察启动流程，确认固件正常跑起来。

### 步骤

1. 用 USB 线连 ESP32-CAM，运行：
   ```bash
   cd /home/hqb/projects/esp32-voice-ai/firmware/esp32-cam
   pio device monitor --port /dev/ttyUSB0
   ```
2. 观察输出（波特率默认 115200）

### 期望结果

**正常启动**：
```text
==============================================
 ESP32-CAM · Step 12-1 · OV2640 base test
==============================================
[CAM] initializing OV2640 ...
[WIFI] scanning SSID=<你的SSID>
[CAM] OV2640 init OK
==============================================
 ESP32-CAM ONLINE
 IP         : 192.168.x.x
 Stream URL : http://192.168.x.x/stream
 Capture    : http://192.168.x.x/capture
 Home       : http://192.168.x.x/
==============================================
[HTTP] main server on port 80 (/, /capture, /stream)
```

**记录 ESP32-CAM 的 IP 地址，后续 TC-08 / TC-09 需要用到**。

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 串口无输出 | 波特率错（应为 115200）；USB 数据线仅充电；BOOT 键没释放 |
| 看到乱码 | 波特率错 |
| 只打印 1-2 行就卡住 | 卡在 Wi-Fi 连接中，看 TC-07 |
| 打印 `ESP_ERR` 但继续 | 摄像头初始化失败，看 TC-06 |

---

## TC-06 · OV2640 摄像头初始化

**目的**：验证 OV2640 能被成功初始化。

### 步骤

1. 观察 TC-05 串口输出中的：
   ```text
   [CAM] initializing OV2640 ...
   [CAM] OV2640 init OK
   ```
2. 若没有 `[CAM] OV2640 init OK`，检查日志中的 `ESP_ERR` 值：
   ```text
   [CAM] ESP_ERR 0x102   ← ESP_ERR_NOT_FOUND，说明摄像头没识别到
   [CAM] ESP_ERR 0x103   ← ESP_ERR_INVALID_ARG，说明引脚配置错
   ```

### 期望结果

```text
[ ] 串口打印 [CAM] OV2640 init OK
[ ] 板载 LED（GPIO 2）在 Wi-Fi 未连接时会闪，连接成功常亮
[ ] 摄像头排线松紧度正确（过松会偶发失败）
```

### 排查思路

| ESP_ERR 码 | 含义 | 排查 |
|-----------|------|------|
| `0x101` (ESP_ERR_INVALID_STATE) | 摄像头之前已初始化 | 检查代码是否重复调用 `esp_camera_init` |
| `0x102` (ESP_ERR_NOT_FOUND) | 找不到 SCCB / I2C 设备 | 排线松动、排线反了、SIOD/SIOC 引脚错 |
| `0x103` (ESP_ERR_INVALID_ARG) | 配置参数错 | 检查 `pin_xclk`、`pin_sccb_sda`、`pin_sccb_scl` 定义 |
| `0x105` (ESP_ERR_NOT_SUPPORTED) | 摄像头型号不支持 | 更换 `sensor_t` 或降级到 OV2640 |
| `0x108` (ESP_ERR_NO_MEM) | 内存不足 | 减少 `fb_count`（当前=1），降低分辨率 |
| `0x109` (ESP_ERR_TIMEOUT) | SCCB 通信超时 | XCLK 频率太高（当前 20 MHz，可降到 10 MHz）；排线问题 |

**若你的板子不是 AI-Thinker 布局**：需要修改 [`src/main.cpp`](../firmware/esp32-cam/src/main.cpp) 中的 GPIO 定义：
- **Espressif 官方 ESP32-CAM v1.0**：`Y2..Y9 = GPIO 13/14/12/10/11/9/8/7`
- **XCLK=0, SIOD=26, SIOC=27** 两种板子相同

---

## TC-07 · Wi-Fi STA 连接路由器

**目的**：验证 ESP32-CAM 能连上路由器拿到 IP。

### 步骤

1. 观察 TC-05 串口输出
2. 若 20 秒内没连上，串口会打印：
   ```text
   [WIFI] FAILED to connect, will keep retrying in background
   ```
3. 板子会每 10 秒重试一次

### 期望结果

```text
[WIFI] scanning SSID=<你的SSID>
...
==============================================
 ESP32-CAM ONLINE
 IP         : 192.168.x.x
==============================================
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 一直 `scanning` 后超时 | SSID / 密码错；路由器 5GHz-only（ESP32-CAM 只支持 2.4 GHz） |
| 拿到 IP 但很快掉线 | AP isolation 打开；路由器信号弱（RSSI < -70 dBm） |
| `0.0.0.0` | DHCP 未响应；检查路由器 DHCP 是否开启 |
| 密码特殊字符导致失败 | 检查双引号内是否转义正确 |
| 路由器有「AP 隔离 / AP 隔离客户端」 | 关闭该功能，ESP32 才能与其他设备互通 |

---

## TC-08 · HTTP 单帧 /capture 下载

**目的**：验证 HTTP 服务能返回单张 JPEG 图片。

### 前置条件

- TC-07 通过，ESP32-CAM 已拿到 IP

### 步骤

1. 打开浏览器，访问：
   ```
   http://<ESP32-CAM_IP>/capture
   ```
   例如：`http://192.168.1.123/capture`
2. 观察浏览器显示的 JPEG 图片
3. 刷新页面，应能看到不同时刻的画面

### 期望结果

```text
[ ] 浏览器加载出画面（不是空白 / 报错）
[ ] 画面为 320×240 分辨率（QVGA）
[ ] 画面颜色正常（不是全黑 / 全白 / 花屏）
[ ] 刷新页面能看到画面变化
[ ] 图片可另存为 .jpg
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 浏览器 502 / 504 | HTTP 服务未启动，看串口 |
| 图片全黑 | 摄像头被遮住 / 光线不足 |
| 图片花屏（马赛克） | 排线松动、XCLK 频率过高、SIOD/SIOC 引脚错 |
| 图片彩色条纹 | 排线接地不良 |
| 图片全白 | OV2640 曝光爆表（自动亮度失效） |
| 加载 30 秒超时 | Wi-Fi 不稳定 / 路由器 AP 隔离 |
| `Connection refused` | 80 端口被占用；ESP32-CAM 未成功启动 HTTP |

---

## TC-09 · HTTP MJPEG Stream /stream（**核心测试**）

**目的**：验证浏览器能持续看到实时画面。

### 前置条件

- TC-08 通过

### 步骤

1. 打开浏览器，访问：
   ```
   http://<ESP32-CAM_IP>/stream
   ```
   或访问主页：
   ```
   http://<ESP32-CAM_IP>/
   ```
   主页会自动嵌入 `<img src="/stream">`
2. 观察画面是否持续更新
3. 用不同浏览器（Chrome / Firefox / Safari）都测试一遍
4. 在画面前晃动物体，观察是否实时反映

### 期望结果

```text
[ ] 浏览器显示实时画面（连续刷新，不是静止图片）
[ ] 画面帧率约 5–15 fps（QVGA + JPEG 编码瓶颈）
[ ] 移动物体能在画面中实时反映（延迟 < 1 秒）
[ ] 长时间保持连接（30 秒以上不断）
[ ] 关闭标签页后重新打开，能重新建立连接
[ ] 主页 http://<IP>/ 中的嵌入 <img> 也能正常加载
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 画面加载出第一帧就卡住 | MJPEG 多部分边界格式问题；检查 `BOUNDARY` 常量 |
| 画面偶尔断，几秒后恢复 | Wi-Fi 不稳定；浏览器自动重连 |
| 浏览器显示「等待流式数据」 | 服务器未返回正确 `Content-Type: multipart/x-mixed-replace` |
| 画面帧率极低（< 3 fps） | 减少 JPEG 质量（改 `CAM_QUALITY` 到 12–14）；改小分辨率 |
| Chrome 显示画面但 Firefox 不显示 | Firefox 对 MJPEG 支持差异，改用 Chrome |
| 关闭标签页后 ESP32-CAM 卡死 | Stream handler 未处理 disconnect；需检查 `httpd_send` 返回值 |
| 画面倒置 | OV2640 参数问题，可加 `s->set_vflip(s, 1)` 与 `s->set_hmirror(s, 1)` |

---

## TC-10 · 长时间稳定性（10 分钟压测）

**目的**：验证连续运行 10 分钟无 crash、无内存泄漏、无重启。

### 步骤

1. 保持浏览器打开 `http://<IP>/stream`
2. 保持串口监视打开
3. 观察 10 分钟，记录：
   - 是否有重启（串口会重新打印启动日志）
   - 是否画面卡顿或断流
   - 是否有内存错误日志（`heap` / `abort` / `panic`）
4. 10 分钟结束后：
   - 记录 ESP32-CAM 是否仍在运行
   - 记录浏览器画面是否仍流畅

### 期望结果

```text
[ ] 10 分钟内未重启
[ ] 10 分钟内画面未完全中断
[ ] 串口日志无 panic / abort / heap 相关错误
[ ] 板载 LED 保持心跳（每 2 秒闪一次）
[ ] 帧率稳定在 5–15 fps
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 3-5 分钟后重启 | 内存泄漏、堆碎片化；考虑减少 `fb_count` 或改分辨率 |
| 画面逐渐变慢 | HTTP 客户端堆积；`streamHandler` 未释放资源 |
| `assert failed: ...` | 内存溢出或任务栈溢出 |
| `Brownout detector` | 供电不足，换 5V 电源 |

---

## TC-11 · 无 Wi-Fi / 摄像头硬件错误处理

**目的**：验证异常情况下的优雅降级。

### 步骤

**测试 A：无 Wi-Fi**
1. 修改 `WIFI_SSID` 为一个不存在的 SSID
2. 重编译、烧录
3. 观察串口输出

**测试 B：拔摄像头**
1. 断电，拔掉摄像头排线
2. 上电，观察串口输出
3. 重新接回，重启

### 期望结果

**测试 A（无 Wi-Fi）**：
```text
[WIFI] scanning SSID=<不存在的SSID>
...
[WIFI] FAILED to connect, will keep retrying in background
[CAM] OV2640 init OK   ← 摄像头仍可初始化
[HTTP] skipped: Wi-Fi not connected yet
[WIFI] retrying ...     ← 每 10 秒重试
```

**测试 B（拔摄像头）**：
```text
[CAM] ESP_ERR 0x102   ← ESP_ERR_NOT_FOUND
[CAM] FAILED to init OV2640. Check wiring / camera module.
[WIFI] ...
[HTTP] skipped: Wi-Fi not connected yet   ← 因为摄像头失败，直接 return
```

### 排查思路

| 现象 | 期望 |
|------|------|
| 无 Wi-Fi 时程序崩溃 | ❌ 应优雅降级，仅跳过 HTTP 服务 |
| 拔摄像头后 ESP32 反复重启 | ❌ 应打印错误日志后保持运行（不重启） |
| 恢复摄像头后不重启就能用 | ❌ 需要重启才能重新初始化（当前实现如此，可接受） |

---

## 附录 A · 参数速查

| 参数 | 位置 | 值 | 说明 |
|------|------|-----|------|
| `WIFI_SSID` | `src/main.cpp:30` | 用户配置 | 路由器 SSID |
| `WIFI_PASS` | `src/main.cpp:31` | 用户配置 | Wi-Fi 密码 |
| `CAM_FRAMESIZE` | `src/main.cpp:76` | `FRAMESIZE_QVGA` (320×240) | 输出分辨率 |
| `CAM_QUALITY` | `src/main.cpp:77` | 10 | JPEG 质量（10=高，14=低） |
| `CAM_FRAMERATE` | `src/main.cpp:78` | 20 | 目标帧率上限 |
| `fb_count` | `src/main.cpp:278` | 1 | 帧缓冲数（无 PSRAM 只能 1） |
| `XCLK` | `src/main.cpp:59` | 20 MHz | 摄像头时钟 |
| `HTTP_PORT` | `src/main.cpp:82` | 80 | HTTP 服务端口 |
| `BOUNDARY` | `src/main.cpp:84` | `frame` | MJPEG 多部分边界 |
| `SERIAL_BAUD` | `src/main.cpp:300` | 115200 | 串口波特率 |

**AI-Thinker 引脚（源码位置 `src/main.cpp:43-70`）**：

| GPIO | 信号 | 备注 |
|------|------|------|
| 0 | XCLK | 摄像头时钟 |
| 2 | LED | 板载 LED |
| 5 | Y2 | 摄像头数据 D0 |
| 18 | Y3 | 摄像头数据 D1 |
| 19 | Y4 | 摄像头数据 D2 |
| 21 | Y5 | 摄像头数据 D3 |
| 22 | PCLK | 摄像头像素时钟 |
| 23 | HREF | 摄像头行有效 |
| 25 | VSYNC | 摄像头场同步 |
| 26 | SIOD | SCCB/I2C SDA |
| 27 | SIOC | SCCB/I2C SCL |
| 32 | PWDN | 摄像头电源关闭 |
| 34 | Y8 | 摄像头数据 D6 |
| 35 | Y9 | 摄像头数据 D7 |
| 36 | Y6 | 摄像头数据 D4 |
| 39 | Y7 | 摄像头数据 D5 |
| -1 | RESET | 板子无复位引脚 |

---

## 附录 B · 诊断命令

```bash
# 查看串口设备
ls /dev/ttyUSB* /dev/ttyACM*
pio device list

# 编译
cd ~/projects/esp32-voice-ai/firmware/esp32-cam
pio run -e esp32-cam

# 烧录
pio run -e esp32-cam -t upload --upload-port /dev/ttyUSB0

# 监视
pio device monitor --port /dev/ttyUSB0

# 编译+烧录+监视一体化
pio run -e esp32-cam -t upload -t monitor --upload-port /dev/ttyUSB0

# 上传后自动重启
pio run -e esp32-cam -t upload --upload-port /dev/ttyUSB0 && pio device monitor --port /dev/ttyUSB0

# 用 curl 测试 HTTP
curl -I http://<IP>/                 # 查看首页响应头
curl -I http://<IP>/capture          # 查看单帧响应头
curl -o /tmp/frame.jpg http://<IP>/capture   # 下载单帧
curl -m 5 http://<IP>/stream -o /tmp/stream.mjpg  # 抓 5 秒流

# 检查 Wi-Fi 是否在 2.4 GHz
nmap --open -p 80 <IP>               # 需要安装 nmap

# 查看 ESP32-CAM 内存
# 通过串口输出（无内置命令，需手动加日志）

# Windows 侧（WSL2）
# 若 ESP32-CAM 需要访问 WSL2 内的服务（本测试不需要，此步仅记录）
wsl -e ip -4 addr show eth0 | grep inet
```

---

## 附录 C · 常见故障速查

| 现象 | 优先排查 |
|------|----------|
| 上电无输出 | 波特率 115200、USB 数据线（不是仅充电线）、USB-UART 驱动 |
| `No serial ports found` | USB 未连 / 驱动未装 / 串口被占用 |
| 编译失败 `httpd_send` 参数 | 手动改了 httpd_send 调用，恢复 3 参版本 |
| `camera_config_t` `fb_globals` 报错 | 移除 `fb_globals` 行（当前 framework 不支持） |
| 编译成功但烧录失败 | 下载模式：按住 BOOT → 松开 |
| 烧录成功但串口无输出 | 手动复位（按 RESET 键或断电重上电） |
| 串口乱码 | 波特率错（应 115200） |
| 摄像头初始化失败 `ESP_ERR 0x102` | 排线松动、排线反了、SIOD/SIOC 引脚错 |
| 摄像头初始化失败 `ESP_ERR 0x103` | 引脚配置错（对照 AI-Thinker 表） |
| 摄像头初始化失败 `ESP_ERR 0x108` | 内存不足，减少 fb_count |
| 摄像头初始化失败 `ESP_ERR 0x109` | SCCB 超时，XCLK 降到 10 MHz 或检查排线 |
| Wi-Fi 连不上 | SSID/密码、2.4GHz、AP isolation、RSSI |
| HTTP 502 / 504 | HTTP 服务未启动（摄像头 init 失败） |
| `/capture` 全黑 | 摄像头被遮住、光线不足 |
| `/capture` 花屏 | 排线松动、XCLK 过高 |
| `/stream` 卡第一帧 | MJPEG 边界格式错（检查 BOUNDARY 常量） |
| `/stream` 帧率极低 | 增大 CAM_QUALITY 数值（14 = 更快） |
| 长时间运行崩溃 | 内存泄漏，减少 fb_count 或降低分辨率 |
| 画面倒置 | 添加 `s->set_vflip(s, 1)` 与 `s->set_hmirror(s, 1)` |
| 板载 LED 不亮 | GPIO 2 定义错或 LED 硬件损坏 |
| 10 分钟压测失败 | 减少 fb_count、降低质量、降低帧率 |
| ESP32-CAM 反复重启 | 供电不足（USB 口电流不够），换 5V/2A 电源 |

---

## 附录 D · 参考文档

- Step 12 总计划：[`STEP_12_VISION_SERVO_PLAN.md`](./STEP_12_VISION_SERVO_PLAN.md)
- 摄像头源代码：[`firmware/esp32-cam/src/main.cpp`](../firmware/esp32-cam/src/main.cpp)
- PlatformIO 配置：[`firmware/esp32-cam/platformio.ini`](../firmware/esp32-cam/platformio.ini)
- Step 12-1 编译报告：见 commit message / PR 描述
- AI-Thinker ESP32-CAM 官方文档：https://wiki.ai-thinker.com/esp32-cam
- Espressif esp32-camera 组件：https://github.com/espressif/esp32-camera

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

**ESP32-CAM 实际 IP**：__________________
**Wi-Fi SSID**：__________________
**板型确认**：☐ AI-Thinker ☐ Espressif 官方 v1.0 ☐ 其他（注明）

**结论**：☐ 全部通过 ☐ 部分失败（列出 TC 编号）

**签名**：______________  **日期**：______________

---

## 版本

| 版本 | 日期 | 变更 |
|------|------|------|
| v1.0 | 2026-09-16 | 初版：Step 12-1 ESP32-CAM 摄像头基础测试用例（TC-01~11） |
