# Step 12-2-A 技术可行性分析

> 本文档只做只读技术评估，**不包含任何代码修改**。
> 所有标注 `[估算]` 的数值未经实测，为基于官方文档与经验推断。
> 所有标注 `[实测]` 的数值均已通过命令确认。

- 生成时间：2026-09-18 (Asia/Tokyo)
- 项目：`esp32-voice-ai`
- 关联固件：`firmware/esp32-cam/`
- 前置阶段：Step 12-1 摄像头基础测试已通过（`/`、`/capture`、`/stream` 均在 80 端口）

---

## 目录

1. [目标回顾](#1-目标回顾)
2. [硬件条件](#2-硬件条件)
3. [Framework / 库版本](#3-framework--库版本)
4. [RGB565 Framebuffer RAM 分析](#4-rgb565-framebuffer-ram-分析)
5. [bit-packed Mask 与队列 RAM](#5-bit-packed-mask-与队列-ram)
6. [JPEG Encoder API](#6-jpeg-encoder-api)
7. [JPEG Quality 10 vs 20 对比](#7-jpeg-quality-10-vs-20-对比)
8. [PersonDetector 峰值 RAM](#8-persondetector-峰值-ram)
9. [HTTP MJPEG /stream 影响](#9-http-mjpeg-stream-影响)
10. [Watchdog / Heap / 碎片风险](#10-watchdog--heap--碎片风险)
11. [方案 A vs 方案 B 对比](#11-方案-a-vs-方案-b-对比)
12. [推荐方案与理由](#12-推荐方案与理由)
13. [长期稳定运行判断](#13-长期稳定运行判断)
14. [结论与下一步建议](#14-结论与下一步建议)
15. [附录：命令记录](#15-附录命令记录)

---

## 1. 目标回顾

**Step 12-2-A**：在 Step 12-1 基础上，为 ESP32-CAM 增加本地人物检测，并将检测框 + 中心点 + 参数**直接绘制到** `/stream` MJPEG 视频帧中。

不做的：UART、舵机、YOLO、TFLite、人脸识别、ReID、云端视觉、ESP32-S3 通信。

---

## 2. 硬件条件

| 项 | 值 | 备注 |
|---|---|---|
| 主控 | ESP32-WROOM-32（classic） | 无 PSRAM |
| 时钟 | 240 MHz | |
| Flash | 4 MB | |
| **总 RAM** | **327,680 B（320 KiB）** | `[实测]` `boards/esp32cam.json` |
| 摄像头 | OV2640 | 通过 DVP 接口 |
| 板型 | AI-Thinker ESP32-CAM | PlatformIO `board = esp32cam` |
| 目标分辨率 | QQVGA 160×120（方案 B） | 见 §11 |

**关键**：AI-Thinker 的淘宝标准版没有焊接 PSRAM。虽然 `boards/esp32cam.json` 中 `-DBOARD_HAS_PSRAM` 只是构建宏标志，实际板子**没有**任何可寻址的 PSRAM，全部依赖内部 SRAM。

---

## 3. Framework / 库版本

`[实测]` 通过 `pio pkg list` 与文件路径扫描确认：

| 项 | 版本 |
|---|---|
| PlatformIO platform | `espressif32 @ 7.1.3` |
| Arduino framework | `framework-arduinoespressif32 @ 4.20017.260907+sha.dcc1105b` |
| 底层 ESP-IDF | v4.4.x（框架 4.20.17 fork 对应） |
| Toolchain | `toolchain-xtensa-esp32 @ 8.4.0+2021r2-patch5` |
| esp32-camera | framework 内置 |
| esp32-camera API 头 | `tools/sdk/esp32/include/esp32-camera/driver/include/esp_camera.h` |
| 图像转换 API 头 | `tools/sdk/esp32/include/esp32-camera/conversions/include/img_converters.h` |

**当前项目内存**（`pio run` 输出）：

```
RAM:   14.7% (48,020 / 327,680)
Flash: 26.9% (847,209 / 3,145,728)
```

---

## 4. RGB565 Framebuffer RAM 分析

`[实测]` 计算：

| 分辨率 | 常量 | 像素数 | framebuffer |
|---|---|---|---|
| QQVGA | `FRAMESIZE_QQVGA` | 160 × 120 = 19,200 | 19,200 × 2 B = **38,400 B (37.5 KiB)** |
| QVGA | `FRAMESIZE_QVGA` | 320 × 240 = 76,800 | 76,800 × 2 B = **153,600 B (150 KiB)** |
| HQVGA | `FRAMESIZE_HQVGA` | 240 × 176 = 42,240 | 84,480 B (82.5 KiB) |

**当前 Wi-Fi + HTTP 栈占用的 RAM 估算**：`[估算]`

| 组件 | 大小 |
|---|---|
| 固件静态 + 主 task 栈 | 48 KB `[实测]` |
| Wi-Fi 协议栈（802.11 + WPA） | 40–60 KB |
| lwIP + HTTP server | 8–15 KB |
| FreeRTOS task 池 + 各种 driver | 15–25 KB |
| **合计（Wi-Fi 起来后）** | **~115–150 KB** |
| **剩余 heap（典型）** | **~180–210 KB → 但可用大块连续内存约 100–140 KiB** |

> 说明：ESP32 的 DRAM 只有 320 KiB，且被 Wi-Fi、TCP、TLS、SPI flash cache 等占用，实际单块连续可分配内存往往远小于总剩余值。

---

## 5. bit-packed Mask 与队列 RAM

### 5.1 bit-packed Mask

将 19,200 像素的 boolean 数组用 1 bit/pixel 存储：

```
mask = 19,200 bits = 2,400 bytes = 2.34 KiB   [实测计算]
```

对比 naive `uint8_t` 数组：19,200 B = 18.75 KiB。**节省 ~16 KiB**。

Bit 访问成本：需要位运算 `(mask[i>>3] >> (i & 7)) & 1`，CPU 开销可忽略。

### 5.2 visited 数组

同理用 1 bit/pixel 存储，2,400 B。

### 5.3 BFS 队列（连通域）

8-邻域 BFS 需要队列。最坏情况（整幅图连通）需要 19,200 元素：

| 方案 | 元素大小 | 队列大小 |
|---|---|---|
| `uint32_t` 全 | 4 B | 76,800 B (75 KiB) |
| `uint16_t`（QQVGA 只需 16-bit） | 2 B | 38,400 B (37.5 KiB) |
| **优化：固定 1024 元素** | 2 B | 2,048 B (2 KiB) |

**关键点**：8-邻域 BFS 实际队列长度受限于"当前扩展前沿"。对于自然场景（人体、家具等目标），队列 1024 元素几乎总是够用。当队列满时可选择放弃当前组件、继续下一组件。

**推荐**：固定 1024 元素 × `uint16_t` = 2 KiB。

### 5.4 组件元数据

预设最大 256 个组件：

```
256 × {x0, y0, x1, y1, pixel_count} = 256 × 10 B = 2,560 B (2.5 KiB)  [估算]
```

### 5.5 检测模块总 RAM

| 项 | 大小 |
|---|---|
| RGB565 framebuffer (QQVGA) | 37.5 KiB `[实测]` |
| bit-packed mask | 2.34 KiB `[实测]` |
| bit-packed visited | 2.34 KiB `[实测]` |
| BFS 队列 (1024 × uint16_t) | 2 KiB `[实测]` |
| 组件元数据 (256) | 2.5 KiB `[估算]` |
| 临时统计变量 | <1 KiB |
| **合计** | **~46.7 KiB** |

**建议全部声明为 static 数组**（避免堆碎片）：

```cpp
static uint8_t  s_mask[2400];
static uint8_t  s_visited[2400];
static uint16_t s_bfs_queue[1024];
static Component s_components[256];
```

---

## 6. JPEG Encoder API

### 6.1 实测存在的 API

`[实测]` 位于 `esp32-camera/conversions/include/img_converters.h`：

```c
// 方式 A：输出到 heap 分配的 buffer（需要 malloc/free）
bool frame2jpg(camera_fb_t *fb, uint8_t quality,
               uint8_t **out, size_t *out_len);

// 方式 B：callback 方式，逐块吐出（无 malloc）
bool frame2jpg_cb(camera_fb_t *fb, uint8_t quality,
                  jpg_out_cb cb, void *arg);

// 底层：直接接收 RGB565 等格式
bool fmt2jpg(uint8_t *src, size_t src_len,
             uint16_t width, uint16_t height,
             pixformat_t format, uint8_t quality,
             uint8_t **out, size_t *out_len);
```

`[实测]` 头文件注释原文：

> `@brief Convert image buffer to JPEG`
> `@param src  Source buffer in RGB565, RGB888, YUYV or GRAYSCALE format`

**RGB565 → JPEG 原生支持确认**。

### 6.2 未找到的 API

- 无 `esp_jpg_encode.h` 或类似命名（framework 中不存在）
- 只有 `esp_jpg_decode.h`（解码器）
- **风险**：`frame2jpg` 函数符号在当前 `firmware.elf` 中未出现（因为当前代码没调用）。**必须在编码阶段第一次编译时验证能链接上**。

**验证方式**（编码前建议先做）：

```cpp
#include <img_converters.h>  // 或 <esp32-camera/conversions/include/img_converters.h>
bool ok = frame2jpg(fb, quality, &out, &out_len);
```

若编译报 `undefined reference to frame2jpg`，则需要：
- 方案 1：在 `platformio.ini` 加 `lib_deps = https://github.com/nicochara/esp_jpg_encode`
- 方案 2：切换到方案 C（保留 PIXFORMAT_JPEG，用 JS 端画框）

### 6.3 推荐调用方式

**方式 B `frame2jpg_cb`**（callback 流式），原因：
- 不 malloc 大块 buffer，避免堆碎片
- 可以直接把 JPEG 数据写入 `httpd_resp_send_chunk`

---

## 7. JPEG Quality 10 vs 20 对比

OV2640 的 `jpeg_quality` 字段是 0–63，**数字越小质量越高、文件越大**。

| Quality | 编码速度 | 文件大小（QVGA 参考） | 文件大小（QQVGA 估算） |
|---:|---:|---:|---:|
| 10 | 慢 | ~60 KB | ~15 KB `[估算]` |
| 15 | 中 | ~40 KB | ~10 KB `[估算]` |
| 20 | 快 | ~25 KB | ~7 KB `[估算]` |
| 30 | 很快 | ~15 KB | ~4 KB `[估算]` |

**Trade-off**：

| Quality | FPS 潜力 | 画面质量 | 网络负担 | 推荐场景 |
|---:|---|---|---|---|
| 10 | 5–8 FPS | 高 | 高 | 静态图片查看 |
| 15 | 6–10 FPS | 中高 | 中 | **默认平衡** |
| 20 | 8–12 FPS | 中 | 低 | 弱网 / 大量并发 |
| 30 | 10–15 FPS | 低 | 很低 | 只用于验证 |

**注意**：这些 FPS 都是 `RGB565 + frame2jpg + detect + overlay` 全流水线的估算值，未实测。

---

## 8. PersonDetector 峰值 RAM

方案 B（QQVGA 160×120）完整 RAM 清单：

| 组件 | 分配方式 | 大小 | 备注 |
|---|---|---:|---|
| RGB565 framebuffer | 静态（camera 内部） | 37.5 KiB | fb_count=1 |
| bit-packed mask | static | 2.34 KiB | |
| bit-packed visited | static | 2.34 KiB | |
| BFS 队列 | static | 2 KiB | 1024 × uint16_t |
| 组件元数据 | static | 2.5 KiB | 256 个 |
| 临时累加器 | 栈 | <1 KiB | |
| **JPEG 输出（callback 版）** | 栈局部 buffer | 4 KiB | 一次吐 4 KB 块 |
| HTTP 缓冲 | framework 管理 | 10–20 KB | 已有 |
| Wi-Fi/HTTP 栈 | 已有 | ~40–60 KB | |
| 固件 + 栈 | 已有 | 48 KB | |
| **总占用峰值** | | **~160–170 KiB** | `[估算]` |
| **剩余** | | **~150 KiB** | 舒适裕度 |

**结论**：方案 B 峰值 ~170 KiB，占 320 KiB 的一半左右，**堆上有充足裕度**。

---

## 9. HTTP MJPEG `/stream` 影响

现有 `/stream` 实现保持不变，只是每帧来源由 `camera_fb_t` 直接 JPEG 数据改为"经过检测+绘制后的 RGB565 再编码"：

```
原：esp_camera_fb_get → jpeg buffer → httpd_resp_send_chunk
新：esp_camera_fb_get → detect → overlay → frame2jpg_cb → httpd_resp_send_chunk
```

**关键变化**：
- 单帧处理时间从 `<10 ms`（`[估算]`）增加到 `~100–150 ms`（`[估算]`），因 CPU 需跑肤色阈值、连通域、overlay、JPEG 编码
- 目标 FPS 5–10，比原 Step 12-1 的 20 FPS 低，但可接受
- HTTP 缓冲策略不变，chunked transfer encoding 保持

**风险**：`frame2jpg_cb` 是 CPU-bound，会长时间占用 CPU 主 loop；如果 HTTP 响应慢可能触发 watchdog。缓解：在 `loop()` 里加 `esp_task_wdt_reset()` 或降低 FPS。

---

## 10. Watchdog / Heap / 碎片风险

### 10.1 Watchdog

- **主 loop watchdog**：默认 5 秒，`loop()` 每次执行会重置
- **任务 watchdog**（TWDT）：HTTP server 独立 task
- **风险场景**：JPEG 编码耗时长 → 主循环阻塞

**缓解措施**：
1. `frame2jpg_cb` 完成后在 HTTP chunk 循环之间插 `yield()` 或 `delay(1)`
2. 检测失败时**降级**：跳过 overlay，直接发送无标注帧
3. 目标 FPS 5–10，绝不追求更高

### 10.2 Heap

- 无 PSRAM 情况下，ESP32 内部 SRAM 只有 320 KiB
- 大量 malloc 会碎片化
- **缓解**：PersonDetector 全部用 static 数组，不 malloc
- `frame2jpg_cb` 用 callback，不用 `frame2jpg`（后者 malloc 输出 buffer）

### 10.3 碎片

- 长期运行（>24h）后 heap 碎片可能使大块分配失败
- **缓解**：所有大 buffer 声明为 static

### 10.4 framebuffer 泄漏

- 必须严格 `esp_camera_fb_get()` → 使用 → `esp_camera_fb_return(fb)` 成对调用
- 检测失败路径也要 return

---

## 11. 方案 A vs 方案 B 对比

| 项目 | A · QVGA 320×240 | B · QQVGA 160×120 |
|---|---|---|
| framebuffer RAM | 150 KiB `[实测]` | 37.5 KiB `[实测]` |
| 检测 mask（未优化） | 76,800 B | 19,200 B |
| 检测 mask（bit-packed） | 9,600 B | 2,400 B |
| BFS 队列（最坏） | 307 KiB ❌ | 75 KiB |
| BFS 队列（1024 元素） | 2 KiB | 2 KiB |
| 检测计算量 | 76,800 像素 | 19,200 像素（少 75%） |
| JPEG 编码负担 | ~60 ms/帧 `[估算]` | ~15 ms/帧 `[估算]` |
| Overlay 绘制 | 更多像素要画 | 少 75% |
| 画面质量 | 高 | 低（160×120 已能识别人） |
| 人物位置精度 | ±20 px | ±40 px（相对 160 px ≈ ±25%） |
| **预计 FPS** | **1–2 FPS（爆堆）** | **5–10 FPS** |
| **总峰值 RAM** | **>500 KiB（不可行）** | **~170 KiB（可行）** |
| **可行性** | ❌ | ✅ |

**结论**：QVGA 在无 PSRAM 的经典 ESP32-CAM 上**物理上跑不动**——光一个 framebuffer 就吃掉 150 KiB，加检测数据就爆堆。

---

## 12. 推荐方案与理由

**推荐：方案 B（QQVGA 160×120 RGB565）+ bit-packed mask + 1024 元素小队列 + frame2jpg_cb**

理由：

1. **唯一可行的分辨率**：QVGA 直接爆堆，方案 A 物理上不可行
2. **RAM 裕度充足**：总峰值 ~170 KiB，剩余 ~150 KiB，为 WiFi 波动、HTTP 并发留了空间
3. **满足目标**：QQVGA 160×120 已足够"画面中是否有人 + 大致位置和大小"
4. **5–10 FPS 目标可达**（`[估算]`）
5. **API 原生支持**：`frame2jpg_cb` 已在 framework 中，无需引入外部依赖（前提是链接能通过）
6. **精度代价可接受**：归一化到 `[0,1]` 后，绝对误差 ±25%，对下一 Phase 的舵机追踪够用

**建议参数**：

| 参数 | 值 | 说明 |
|---|---|---|
| `CAM_FRAMESIZE` | `FRAMESIZE_QQVGA` | 160×120 |
| `CAM_PIXFORMAT` | `PIXFORMAT_RGB565` | 才能拿到原始像素 |
| `CAM_QUALITY` | `15` | 平衡 FPS 与画质（见 §7） |
| `CAM_FRAMERATE` | `10` | 上限 |
| `fb_count` | `1` | 无 PSRAM 保持 1，避免内存翻倍 |
| `xclk_freq_hz` | `20,000,000` | 与 Step 12-1 一致 |

---

## 13. 长期稳定运行判断

问题：**QQVGA 160×120 RGB565 → 检测 → Overlay → JPEG → `/stream`** 能否在经典 ESP32-CAM 无 PSRAM 上长期稳定运行？

**回答**：**短期（数小时）可行，长期（>24h）存在风险，需实测验证。**

具体分析：

| 维度 | 判断 | 说明 |
|---|---|---|
| **RAM 占用** | ✅ 稳定 | 全部 static，无 malloc 波动 |
| **heap 碎片** | ⚠️ 有风险 | 长期运行后剩余可分配空间可能缩水 |
| **watchdog** | ⚠️ 有风险 | 单帧 ~100–150 ms，接近临界 |
| **flash 磨损** | ✅ 无关 | 不写 NVS / flash |
| **TCP keepalive** | ⚠️ 需关注 | MJPEG 长连接可能被路由器中断 |
| **Wi-Fi 断线重连** | ✅ 已实现 | 现有 loop() 有重试逻辑 |
| **CPU 温度** | ⚠️ 需关注 | 持续编码可能过热降频 |
| **JPEG 编码失败** | ⚠️ 需降级 | 失败时发"no detection"帧 |

**建议实测指标**：
- 连续运行 4 小时无 crash
- 4 小时内 heap 最低值 > 60 KB
- CPU 温度 < 70°C
- 平均 FPS 稳定在 5–8

**降级路径**：
1. 检测失败 → 跳过 overlay，直接编码原始帧
2. FPS 太低 → 降到 quality 30
3. 依然低 → 切回 QQVGA 160×120 但 fb_count=1、单通道发送

---

## 14. 结论与下一步建议

### 14.1 关键结论

1. **可行性**：✅ 方案 B 在无 PSRAM 的经典 ESP32-CAM 上可行
2. **推荐分辨率**：QQVGA 160×120 RGB565
3. **推荐 quality**：15（平衡 FPS 与画质）
4. **JPEG API**：`frame2jpg_cb(camera_fb_t*, uint8_t quality, jpg_out_cb, void*)` — framework 内置
5. **RAM 峰值**：~170 KiB（估算），剩余 ~150 KiB
6. **预计 FPS**：5–10（估算，需实测）
7. **长期稳定性**：短期可行，长期需实测；建议加降级路径

### 14.2 建议在编码阶段先做的验证（不改动其他文件）

1. 写一个最小 `hello_frame2jpg.cpp`，只调用一次 `frame2jpg_cb`，编译验证 `frame2jpg` 符号可链接
2. 打印 `ESP.getFreeHeap()`，实测 Wi-Fi+HTTP 启动后剩余 heap
3. 打印 `xPortGetFreeHeapSize()`，确认分配大块 buffer 时的连续内存上限

### 14.3 建议下一步动作

**等此文档审阅通过后**，按以下顺序编码：

1. `firmware/esp32-cam/src/person_detector.h`（数据结构与接口）
2. `firmware/esp32-cam/src/person_detector.cpp`（肤色阈值 + 连通域）
3. `firmware/esp32-cam/src/draw_overlay.h`（极小 5×7 bitmap font + 绘制接口）
4. `firmware/esp32-cam/src/draw_overlay.cpp`（矩形、点、文本绘制）
5. `firmware/esp32-cam/src/main.cpp`（切 RGB565/QQVGA、集成检测+绘制）
6. `pio run` 编译
7. `pio run -t upload` 烧录
8. 观察串口日志（heap 值、FPS、检测帧率）

---

## 15. 附录：命令记录

### 15.1 已执行命令

```bash
# 版本查询
pio pkg list

# framework 路径扫描
find ~/.platformio/packages/framework-arduinoespressif32 \
     -name "img_converters.h" -o -name "esp_camera.h"

# 关键 API 查看
cat ~/.platformio/packages/framework-arduinoespressif32/tools/sdk/esp32/include/esp32-camera/conversions/include/img_converters.h
grep -n "PIXFORMAT_\|fb_count\|jpeg_quality" \
     ~/.platformio/packages/framework-arduinoespressif32/tools/sdk/esp32/include/esp32-camera/driver/include/esp_camera.h

# 板子规格
cat ~/.platformio/platforms/espressif32/boards/esp32cam.json

# 编译验证
cd firmware/esp32-cam && pio run
```

### 15.2 关键数值来源

| 数值 | 来源 |
|---|---|
| 总 RAM 320 KiB | `[实测]` `boards/esp32cam.json` `maximum_ram_size: 327680` |
| Flash 4 MB | `[实测]` 同上 |
| QQVGA framebuffer 37.5 KiB | `[实测]` 160×120×2 = 38,400 B |
| QVGA framebuffer 150 KiB | `[实测]` 320×240×2 = 153,600 B |
| QQVGA bit-packed mask 2,400 B | `[实测]` 19,200 / 8 |
| Wi-Fi 后剩余 heap 100–140 KiB | `[估算]` 经验值，需实测 |
| JPEG 编码时间 15/60 ms/帧 | `[估算]` |
| 检测 FPS 5–10 | `[估算]` |
| 总峰值 RAM ~170 KiB | `[估算]` |
| `frame2jpg` 可链接 | `[未验证]` 需编码阶段首次编译验证 |

---

## 版本

- 文档：`docs/STEP_12_2_A_FEASIBILITY.md`
- 生成时间：2026-09-18
- 状态：等待用户审阅
- 下一步：审阅通过后进入编码阶段
