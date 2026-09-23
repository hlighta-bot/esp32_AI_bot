# 测试用例 · 2026-09-21 · Step 12-2-B ESP32-CAM 人物检测优化验证

> **文档编号**：TC-20260921-S12-2-B-PERSON-OPTIMIZE
> **版本**：v1.0
> **适用代码**：Step 12-2-B（`firmware/esp32-cam/src/person_detector.{h,cpp}` 优化版）
> **测试目标**：验证优化版人物区域检测在 QQVGA 160×120 RGB565 上的效果——`RGB565 → YCbCr(加宽 LuvSkin) → bit-mask → 3×3 morphology opening → 8-邻域 BFS(4096 队列) → 硬过滤 → 4 维打分 → N-confirm/M-loss 时间稳定性 → bbox EMA 平滑 → 归一化输出`
> **前置测试**：Step 12-2-A 基线测试（[`test-2026-09-19-step12-2-a-person-detect.md`](./test-2026-09-19-step12-2-a-person-detect.md)），确认摄像头基础功能正常
>
> **测试时长**：全流程约 45–60 分钟；每个 TC 独立，可按编号跳测
>
> **说明**：本测试为纯启发式算法（非 ML），confidence 是 heuristic score 而非概率；测试重点是"相对上一版是否更好"，不是"达到某个 ML 指标"

---

## 0. 目录

| 编号 | 测试项 | 时长 | 依赖 |
|------|--------|------|------|
| TC-01 | 硬件接线与电源检查 | 5 min | 无 |
| TC-02 | 固件编译（重点：静态内存占用 ≤ 30%） | 3 min | TC-01 |
| TC-03 | 烧录 + 启动日志确认 | 3 min | TC-02 |
| TC-04 | Wi-Fi STA 连接 + HTTP 服务起 | 3 min | TC-03 |
| TC-05 | `/capture` 单帧回归（含优化 overlay） | 3 min | TC-04 |
| TC-06 | `/stream` MJPEG 视频流（含优化 overlay） | 3 min | TC-04 |
| TC-07 | **正样本场景检出**（真人 / 多距离 / 多光照） | 10 min | TC-06 |
| TC-08 | **负样本场景无误检**（暖色背景 / 木纹 / 无人的静物） | 8 min | TC-06 |
| TC-09 | **时间稳定性**（N-confirm=3 / M-loss=2 生效） | 5 min | TC-06 |
| TC-10 | **bbox 平滑**（EMA α=9/32 无抖动） | 5 min | TC-06 |
| TC-11 | 性能与内存监控（RAM / FreeHeap / LargestFreeBlock / det μs） | 5 min | TC-06 |
| TC-12 | 长时间稳定性（30 分钟压测） | 30 min | TC-06 |
| TC-13 | LED 无污染摄像头视野 | 2 min | TC-06 |
| TC-14 | 边缘场景与故障路径 | 5 min | TC-06 |

**附录**：A 参数速查 · B 串口日志速查 · C 常见故障 · D 参考文档 · E 执行记录模板 · **F 优化前后对比基线**（**必读：了解要对比什么**）

---

## 通用前置条件

```text
[ ] AI-Thinker ESP32-CAM 板子（ESP32-WROOM-32 + OV2640）
[ ] 5V/2A 电源适配器或 USB-A 数据口直连（不建议 USB hub）
[ ] 数据模式 USB 线（不是仅充电线）
[ ] 电脑已装 PlatformIO（VS Code 插件或 CLI）
[ ] 摄像头能对着真人（建议 0.5–2 m 距离）
[ ] Wi-Fi 路由器可用（SSID/密码已填在 main.cpp 顶部）
[ ] 浏览器可打开 http://<ESP32-CAM-IP>/stream
[ ] 已阅读本文件附录 F，理解优化前后差异
```

---

## TC-01 · 硬件接线与电源检查

**目的**：确保摄像头排线、电源、USB 通信正常，避免后续测试反复排查。

### 步骤

1. 目视检查 OV2640 摄像头排线是否牢固插入（AI-Thinker ESP32-CAM 板右侧插座）
2. 万用表电压档量 ESP32-CAM 板 3V3 引脚对 GND：应为 3.2–3.4 V
3. 万用表蜂鸣档量 USB 口 GND 与板上 GND：应 < 0.1 Ω
4. 用另一根 USB 数据线对比 —— 若换线后串口才能识别，说明当前线是"仅充电线"

### 期望结果

```text
- ESP32-CAM 3V3 = 3.2–3.4 V
- USB GND 与板 GND 连通
- 电脑端能识别 USB 设备（`ls /dev/ttyUSB*` 或设备管理器看到 ESP32 Serial）
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 3V3 = 0 V | USB 线仅充电 / 电源不足 |
| 3V3 大幅波动 | 电源不足，换 5V/2A 电源 |
| 串口不识别 | 换数据模式 USB 线 / 重装 USB-Serial 驱动 |
| 摄像头排线松动 | 断电后重插 OV2640 |

---

## TC-02 · 固件编译（重点：静态内存占用）

**目的**：验证优化版 person_detector.cpp 编译通过，且静态内存占用在可接受范围。

### 前置条件

- PlatformIO 已安装 espressif32 平台
- `firmware/esp32-cam/src/main.cpp` 顶部 Wi-Fi SSID/密码 已填（不影响编译）

### 步骤

```bash
cd ~/projects/esp32-voice-ai/firmware/esp32-cam
pio run -v
```

### 期望结果

```text
- 编译成功：[SUCCESS]
- 0 warning, 0 error
- RAM:   约 27%（88,860 / 327,680 B）— 参考值，可能 ±2%
- Flash: 约 27.5%（864,877 / 3,145,728 B）
- firmware.bin 大小 ≈ 870 KB
- 无 heap 分配相关错误（本模块全部静态）
```

### 排查思路

| 现象 | 可能原因 | 处理 |
|------|----------|------|
| 找不到平台 espressif32@6.10.0 | PlatformIO 未缓存该版本 | `pio platform install espressif32` 或检查 platformio.ini 是否显式锁版本 |
| `cannot find -lcxx` 或类似链接错误 | xtensa 工具链未装齐 | `pio run -v` 前清理 `.pio/` 后重建 |
| 编译失败在 person_detector.cpp | 优化代码编译错误 | 见附录 C |
| RAM > 40% | 静态 buffer 超限 | 减少 BFS_QUEUE_SIZE / 检查其他模块 |
| RAM < 20% | 检测模块未被链接 | 检查 main.cpp 是否调用 detectPerson |

---

## TC-03 · 烧录 + 启动日志确认

**目的**：验证固件正确烧录，摄像头和 Wi-Fi 初始化成功。

### 步骤

```bash
cd ~/projects/esp32-voice-ai/firmware/esp32-cam
pio run -t upload
```

然后开串口监视器（115200, no parity, 8N1）：

```bash
io device monitor -p /dev/ttyUSB0 -b 115200 --rts 0 --dtr 0
```

### 期望结果

```text
==============================================
 ESP32-CAM · Step 12-1 · OV2640 base test
==============================================
[PSRAM] found   : NO
[PSRAM] size    : 0 bytes
[PSRAM] free    : 0 bytes
[MEM]   heap    : ~200000 bytes  ← 初始值，> 150 KB
[LED]   initialized
[WIFI] connecting to hqbwifi_36 ...
[WIFI] connected, IP: 192.168.x.x
[CAM] initializing OV2640 ...
[CAM] OV2640 init OK
[HTTP] starting on port 80
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 反复重启 | 电源不足 / 摄像头故障 |
| `[CAM] FAILED to init OV2640` | 摄像头排线松动 / 硬件故障 |
| Wi-Fi 连不上 | SSID/密码错 / 距离过远 |
| `[MEM] heap < 100 KB` | 其他模块占用了大量内存 |

---

## TC-04 · Wi-Fi STA 连接 + HTTP 服务起

**目的**：确认 HTTP server 正常启动，浏览器可访问。

### 步骤

1. 从串口日志拿到 IP：`[WIFI] connected, IP: 192.168.x.x`
2. 浏览器打开 `http://192.168.x.x/`
3. 应看到 Step 12 HTML 首页，含 `/capture` 和 `/stream` 链接

### 期望结果

```text
- 首页能加载
- 显示 ESP32-CAM Step 12 标识
- 提供 /capture 和 /stream 两个链接
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 502 / 连接被拒 | HTTP 未启动，看串口 `[HTTP]` 日志 |
| 首页空白 | JavaScript 错误，浏览器 F12 查看 |
| 完全打不开 | 防火墙 / 不在同一局域网 / IP 写错 |

---

## TC-05 · `/capture` 单帧回归

**目的**：单帧下载后，overlay 应正确渲染优化后的检测结果。

### 步骤

1. 浏览器打开 `http://192.168.x.x/capture`
2. 观察下载的 JPEG（160×120 尺寸）
3. 让真人对着摄像头（0.5–2 m）
4. 反复点击刷新观察

### 期望结果

```text
- JPEG 图像正常渲染（不过暗、不过曝）
- 有人的时候：绿框出现在人物区域（bbox 略宽松可接受）
- 无人时：无绿框
- 帧率不敏感（单帧下载）
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 图像全黑 | 摄像头排线 |
| 图像过曝 | 环境光太强，`set_brightness` 调低 |
| 无人也有框 | DETECTION_THRESHOLD 过低，见 TC-07 |
| 有人但无框 | 光线太暗 / 距离太远 / 阈值过高，见 TC-07 |

---

## TC-06 · `/stream` MJPEG 视频流

**目的**：连续帧流可稳定运行，overlay 实时刷新。

### 步骤

1. 浏览器打开 `http://192.168.x.x/stream`
2. 观察 MJPEG 视频流
3. 串口日志每 10 帧应出现一行 `[STREAM]` + 一行 `[PERSON]`

### 期望结果

```text
- 视频流稳定显示（约 5–10 fps）
- 串口每 10 帧出现：
  [STREAM] f=NN fps=.. det=NNNus enc=NNNus jpeg=NNNNB free=NNNNN max=NNNNN p=0/1
  [PERSON] skin=NNN cand=N score=NN conf=0.NN x=0.NN y=0.NN w=0.NN h=0.NN tC=N tL=N sm=0/1
- 有真人时绿框跟随人物
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 视频只有 1 帧然后卡住 | 编码耗时过长 / 客户端断线，重启浏览器 |
| fps < 3 | JPEG quality 过高，改 CAM_QUALITY 到 20 |
| 画面全黑 | 摄像头问题，回 TC-01 |
| `[PERSON]` 行缺失 | 串口 buffer 溢出，降低 fps 或减少日志字段 |

---

## TC-07 · 正样本场景检出（**核心优化验证**）

**目的**：验证加宽 LuvSkin 阈值 + 形态学 opening + 4 维打分后，真人检出率相比 v1.0 是否有明显提升。

### 正样本场景（应检测到）

- **场景 A**：浅肤色真人，正面，0.5 m 距离，室内正常光照
- **场景 B**：浅肤色真人，侧面 30°，1 m 距离
- **场景 C**：深肤色真人，正面，1 m 距离（**这是加宽阈值的关键场景**）
- **场景 D**：半身（露脸和肩膀），0.8 m 距离
- **场景 E**：手部特写（应检出，虽然不完美）
- **场景 F**：多人（画面中有 2 个人，任一个被检出即可）

### 步骤

1. 打开 `/stream`
2. 依次测试 A–F，每个场景持续观察 15 秒
3. 记录每个场景下串口 `[PERSON]` 日志：
   - `cand=N`（候选数量，通常 1–3）
   - `score=NN`（0-100 整数分）
   - `tC=3`（时间稳定性达到 3 帧才检测）
   - `sm=1`（EMA 平滑生效）
4. 记录每个场景"首次出现绿框"的时间

### 期望结果

| 场景 | 检出 | conf 范围 | 备注 |
|------|------|-----------|------|
| A 浅肤色正面 | ✅ 检出 | 0.60–0.95 | 最佳场景 |
| B 浅肤色侧面 | ✅ 检出 | 0.50–0.85 | 略降 |
| C 深肤色正面 | ✅ 检出 | 0.45–0.75 | **优化后应显著改善** |
| D 半身 | ✅ 检出 | 0.50–0.85 | |
| E 手部特写 | 可能检出 | 0.30–0.60 | 边缘场景 |
| F 多人 | 至少 1 个检出 | 0.45–0.90 | 通常检出最大的一个 |

**通过条件**：场景 A / B / C / D 至少 4/4 检出。

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 场景 A 都不检出 | 阈值过高 / 距离太远 / 光线问题，见附录 C |
| 场景 C 漏检 | Cb/Cr 阈值仍需加宽，或光照影响（可尝试加灯） |
| 检出但 conf < 0.40 | 时间稳定性未达 3 帧，多观察几秒 |
| 只检出手部不检脸 | 面部阴影 / 头发遮挡，属于启发式算法局限 |

---

## TC-08 · 负样本场景无误检（**核心优化验证**）

**目的**：验证加宽阈值不会让"暖色背景"大面积吞掉真脸，也不会导致空场景乱报。

### 负样本场景（不应稳定检测到）

- **场景 G**：空场景（无人，只有墙 / 桌面）
- **场景 H**：木质桌面 / 木地板特写
- **场景 I**：暖色调墙面 / 砖墙
- **场景 J**：黄色衣物 / 橙色物体（非人体）
- **场景 K**：手部以外的肤色物体（如肤色玩具、肤色抱枕）
- **场景 L**：光线极弱（<5 lux）—— 应表现为 conf 低、detected=false

### 步骤

1. 打开 `/stream`
2. 依次测试 G–L，每个场景持续观察 20 秒
3. 观察串口 `[PERSON]` 日志：
   - `cand` 应大多为 0 或很少
   - 若 `cand > 0`，`score` 应 < 40
   - 稳定 `detected=false`（overlay 无绿框）

### 期望结果

| 场景 | 稳定检出 | 说明 |
|------|----------|------|
| G 空场景 | ❌ 不检出 | 最基础 |
| H 木质桌面 | ❌ 稳定不检出 | **加宽阈值的关键回归** |
| I 砖墙 | ❌ 稳定不检出 | |
| J 橙色衣物 | ❌ 稳定不检出 | |
| K 肤色物体 | 可能短暂检出，但 tC 到不了 3 | **时间稳定性应兜底** |
| L 极弱光 | ❌ 稳定不检出 | |

**通过条件**：G / H / I / J / L 至少 5/5 稳定不检出。

### 排查思路

| 现象 | 可能原因 | 处理 |
|------|----------|------|
| H 木质桌面稳定检出 | 阈值加宽过头 | Cb 上限从 135 降到 130 |
| K 肤色物体稳定检出 | 硬过滤不足 | 提高 MIN_COMPONENT_AREA 到 80 |
| G 空场景检出 | 摄像头噪点 → 形态学不足 | 提高 erode MIN_8_NEIGHBOR 到 5 |
| 场景 J 检出 | 橙色物体色相太接近肤色 | 属于启发式局限，可接受 |

---

## TC-09 · 时间稳定性验证

**目的**：确认 N-confirm=3 / M-loss=2 状态机生效。

### 步骤

1. 打开 `/stream`
2. 突然把真人手快速挡到摄像头再移开（模拟单帧干扰）
3. 观察串口日志：`tC` 和 `tL` 变化
4. 观察 overlay：手挡瞬间不应导致"框闪烁"

### 期望结果

```text
- 单帧干扰时，tC 从 3 跌到 0，tL 从 0 升到 1
- overlay 上绿框不应"闪一下消失"，因为状态机保持 lastDetected=true
- 干扰持续 3+ 帧后，才真正 detected=false
```

### 关键日志片段（示例）

```text
[PERSON] ... tC=3 tL=0 sm=1    ← 稳定检测到
[PERSON] ... tC=0 tL=1 sm=0    ← 手挡一下（单帧丢失）
[PERSON] ... tC=1 tL=0 sm=0    ← 手离开，重新累积 confirm
[PERSON] ... tC=2 tL=0 sm=0    ← 累积中
[PERSON] ... tC=3 tL=0 sm=1    ← 重新达阈值，恢复输出
```

**通过条件**：单帧干扰不导致 overlay 上绿框明显闪烁。

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 手挡瞬间绿框消失 | TEMPORAL_LOSS_FRAMES 太小，可增到 3 |
| 手离开后绿框很久才恢复 | TEMPORAL_CONFIRM_FRAMES 太大，可减到 2 |

---

## TC-10 · bbox 平滑验证

**目的**：确认 EMA（α=9/32）已生效，bbox 无剧烈抖动。

### 步骤

1. 打开 `/stream`
2. 让人慢速横移（0.5 m 距离，速度 5 cm/s）
3. 观察 overlay 上绿框中心点移动
4. 串口 `[PERSON]` 日志每 10 帧一条，观察 `x` 和 `w` 的相邻帧差值

### 期望结果

```text
- sm=1 应始终出现（除首次进入）
- 相邻 `[PERSON]` 行的 x 变化应 < 0.05（0.05 = 图像 5%）
- w 和 h 应在稳定区间波动 < 0.03
- 视觉感受：绿框"跟随但不抖动"
```

### 关键观察

| 现象 | 期望值 |
|------|--------|
| 静止时 `x` 变化 | < 0.02 |
| 慢速横移时 `x` 变化 | 0.02–0.05（平滑跟随） |
| `sm` 字段 | 稳定为 1（进入稳定后） |
| 首次检测到 | `sm=0`（因为还没 seed） |

**通过条件**：静止时 bbox 抖动幅度 < 0.02。

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 静止时 bbox 仍大幅抖动 | EMA α 太大（9 应降到 6），或检测到的是不稳定组件 |
| 横移时 bbox 跟不上 | EMA α 太小（9 应升到 12） |
| `sm=0` 持续 | 每次检测到不同的候选，s_prevValid 每次被重置 |

---

## TC-11 · 性能与内存监控

**目的**：验证优化后的算法在 QQVGA 帧上运行时不拖垮板子。

### 步骤

1. 打开 `/stream`
2. 串口日志观察 60 秒
3. 记录以下数据：

| 指标 | 参考值 | 单位 |
|------|--------|------|
| fps | 5–10 | fps |
| det=μs | 3000–6000 | 微秒 |
| enc=μs | 15000–25000 | 微秒 |
| jpeg=B | 2500–5000 | 字节 |
| free=heap | 60–120 | KB |
| max=blk | 40–80 | KB |
| skin= | 500–5000 | 像素（有肤色时） |
| cand= | 0–5 | 候选数 |

### 期望结果

```text
- fps ≥ 3（能持续看到画面刷新）
- det=μs ≤ 6000（形态学 + 大 BFS 队列开销应在 6 ms 内）
- free=heap 应稳定（不持续下降）
- max=blk ≥ 30 KB（保证后续 HTTP 发送 chunk 可用）
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| fps < 3 | 检测或编码太慢 |
| det=μs > 10000 | 形态学或 BFS 有性能问题 |
| free=heap 持续下降 | 内存泄漏，STOP 并报告 |
| max=blk < 20 KB | 内存碎片，重启一次看是否恢复 |

---

## TC-12 · 长时间稳定性（30 分钟压测）

**目的**：验证 30 分钟连续运行不重启、不崩溃、不泄漏。

### 步骤

1. 打开 `/stream`
2. 保持浏览器连接 30 分钟
3. 期间随机让人进出画面
4. 观察串口：
   - 无 "Watchdog reset" 字样
   - 无 "Brownout detector" 字样
   - free=heap 数值在前后 30 分钟内差异 < 5 KB
   - fps 波动在 ±20% 内

### 期望结果

```text
- 30 分钟无重启
- free=heap 起点 vs 终点差异 < 5 KB
- fps 平均值 ≥ 3
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 30 分钟内重启 | 电源不足 / 内存不足 / 检测死循环 |
| free=heap 快速下降 | 内存泄漏，STOP 并报告 |
| fps 逐渐下降 | 内存碎片化，可能重启恢复 |
| 检测逻辑卡死 | Watchdog reset，看重启前最后日志 |

---

## TC-13 · LED 无污染摄像头视野

**目的**：确保 GPIO4 Flash LED 不会遮挡摄像头视野，也不影响肤色判断。

### 步骤

1. 打开 `/stream`
2. 观察画面中是否出现白色高亮方块 / 光点
3. 观察画面边缘是否有 LED 反光

### 期望结果

```text
- 画面中没有 LED 光点
- 画面没有异常过曝区域
- 检测逻辑未把 LED 位置误判为肤色
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 画面右上角有白光点 | LED 位置就在摄像头旁，正常，但应避免 |
| LED 光点被判为肤色 | 罕见，若发生提高 DETECTION_THRESHOLD |

---

## TC-14 · 边缘场景与故障路径

### 场景 M：客户端断开

**步骤**：`/stream` 打开后关闭浏览器标签页

**期望**：
```text
串口：[STREAM] client disconnected, frames=NNN
下次打开时能重新连接
```

### 场景 N：多客户端同时打开

**步骤**：2 个浏览器标签同时打开 `/stream`

**期望**：
```text
ESP32-CAM 内部处理第二个客户端可能因帧缓冲冲突而失败
串口应有错误日志，但不崩溃
```

### 场景 O：摄像头被完全遮挡

**步骤**：用手覆盖摄像头

**期望**：
```text
[PERSON] skin=0 cand=0 score=0 conf=0.00 tC=0 tL=2
overlay 无绿框
```

### 场景 P：Wi-Fi 断开重连

**步骤**：暂停路由器 5 秒再恢复

**期望**：
```text
串口出现 [WIFI] disconnected 和 [WIFI] connected
HTTP 服务重启
浏览器自动重连 MJPEG
```

---

## 附录 A · 参数速查（优化版）

| 参数 | 文件 | 值 | 含义 |
|------|------|-----|------|
| DETECTION_WIDTH / HEIGHT | person_detector.h | 160 × 120 | QQVGA 分辨率 |
| DETECTION_PIXELS | person_detector.h | 19200 | 图像总像素 |
| MASK_BYTES | person_detector.h | 2400 | bit-packed mask 大小 |
| BFS_QUEUE_SIZE | person_detector.h | **4096** | BFS 队列（原 1024 → 4096） |
| MAX_COMPONENTS | person_detector.h | 256 | 组件元数据上限 |
| **SKIN_CB_MIN / MAX** | person_detector.h | **77–135** | Cb 阈值（原 77–127 加宽） |
| **SKIN_CR_MIN / MAX** | person_detector.h | **128–180** | Cr 阈值（原 133–173 加宽） |
| MIN_COMPONENT_AREA | person_detector.h | **60** | 最小连通像素（原 30） |
| MAX_COMPONENT_AREA_FRAC | person_detector.h | **0.25** | 最大占比（原 0.40） |
| MIN_COMPONENT_FILL | person_detector.h | 0.20 | 填充率下限（新增） |
| MAX_COMPONENT_FILL | person_detector.h | 0.95 | 填充率上限（新增） |
| MIN_COMPONENT_ASPECT | person_detector.h | 0.20 | 宽高比下限（新增） |
| MAX_COMPONENT_ASPECT | person_detector.h | 1.20 | 宽高比上限（新增） |
| MIN_BOX_W / H | person_detector.h | 6 / 8 | bbox 最小尺寸（新增） |
| MAX_BOX_W_FRAC / H_FRAC | person_detector.h | 0.60 / 0.70 | bbox 最大占比（新增） |
| CONF_WEIGHT_AREA | person_detector.h | 0.30 | 面积权重 |
| CONF_WEIGHT_FILL | person_detector.h | 0.25 | 填充率权重（新增） |
| CONF_WEIGHT_ASPECT | person_detector.h | 0.25 | 宽高比权重 |
| CONF_WEIGHT_POS | person_detector.h | 0.20 | 位置先验权重（新增） |
| **DETECTION_THRESHOLD** | person_detector.h | **0.40** | 检测阈值（原 0.30） |
| **TEMPORAL_CONFIRM_FRAMES** | person_detector.h | **3** | N-confirm 阈值（新增） |
| **TEMPORAL_LOSS_FRAMES** | person_detector.h | **2** | M-loss 阈值（新增） |
| EMA_ALPHA_NUM / DEN | person_detector.cpp | 9 / 32 | EMA α ≈ 0.281（新增） |
| CAM_QUALITY | main.cpp | 15 | JPEG quality |
| CAM_FRAMERATE | main.cpp | 10 | 目标 fps |

---

## 附录 B · 串口日志速查

| 前缀 | 含义 | 期望频率 |
|------|------|----------|
| `[CAM]` | 摄像头初始化 | 启动 1 次 |
| `[WIFI]` | Wi-Fi 连接 | 启动 1 次（或重连时） |
| `[HTTP]` | HTTP 服务 | 启动 1 次 |
| `[PSRAM]` | PSRAM 诊断 | 启动 1 次 |
| `[MEM]` | 内存初始诊断 | 启动 1 次 |
| `[CAPTURE]` | `/capture` 请求 | 每次请求 1 行（静默，无 `[PERSON]`） |
| `[STREAM] client connected` | `/stream` 连接 | 每次连接 1 行 |
| `[STREAM] f=.. fps=..` | `/stream` 帧日志 | 每 10 帧 1 行 |
| **`[PERSON] skin=.. cand=..`** | **检测诊断** | **每 10 帧 1 行** |
| `[STREAM] client disconnected` | `/stream` 断开 | 每次断开 1 行 |
| `[STREAM] frame end failed` | 发送错误 | 异常时才出现 |
| `[STREAM] jpeg encode failed` | 编码失败 | 异常时才出现 |
| `Watchdog reset` | 看门狗超时 | **绝不应出现** |
| `Brownout detector` | 电源不足 | **绝不应出现** |

### `[PERSON]` 字段说明

```text
[PERSON] skin=NNN cand=N score=NN conf=0.NN x=0.NN y=0.NN w=0.NN h=0.NN tC=N tL=N sm=0/1
```

| 字段 | 含义 | 单位/范围 |
|------|------|-----------|
| `skin` | 形态学后的皮肤 mask 总像素数 | 0–19200 |
| `cand` | 通过硬过滤的候选数量 | 0–MAX_COMPONENTS |
| `score` | 当前帧最高候选分数 | 0–100（×100） |
| `conf` | 归一化置信度 | 0.00–1.00 |
| `x, y` | bbox 中心归一化 | 0.00–1.00 |
| `w, h` | bbox 尺寸归一化 | 0.00–1.00 |
| `tC` | 连续检测到候选的帧数 | 0–TEMPORAL_CONFIRM_FRAMES |
| `tL` | 连续丢失候选的帧数 | 0–TEMPORAL_LOSS_FRAMES |
| `sm` | EMA 平滑是否生效 | 0=首次 seed，1=已平滑 |

---

## 附录 C · 常见故障速查

| 现象 | 可能原因 | 处理 |
|------|----------|------|
| 浏览器 500 错误 | `frame2jpg_cb` 返回 false | 串口看错误码，检查内存 |
| `/stream` 只有 1 帧然后卡住 | JPEG 编码耗时过长 | 降 quality / 重开浏览器 |
| 图像全黑 | 摄像头排线未接好 | 断电重插 OV2640 |
| 图像全白 | 曝光过度 | 减少环境光 / `set_brightness` |
| 帧率 < 3 | 检测或编码太慢 | 降 quality / 停 stream |
| **`cand=0` 恒为 0（有真人也不检出）** | 阈值太高 / 光线太差 | 降低 DETECTION_THRESHOLD 到 0.35 |
| **场景 H（木质桌面）稳定误检** | 阈值加宽过头 | Cb_MAX 从 135 降到 130 |
| **bbox 剧烈抖动** | EMA α 太大或检测到不同组件 | 降低 EMA 分子 9 → 6 |
| **`tC` 从不达到 3** | 单帧检测到但候选不稳定 | 检查是否有多个皮肤区域在切换 |
| **`free=heap` 持续下降** | 内存泄漏 | STOP，回滚排查 |
| 板子反复重启 | 电源不足 / Watchdog | 换 5V/2A 电源 |
| 场景 C（深肤色）不检出 | Cb/Cr 覆盖不足 | Cr_MIN 从 128 降到 122 |

---

## 附录 D · 参考文档

- [`test-2026-09-19-step12-2-a-person-detect.md`](./test-2026-09-19-step12-2-a-person-detect.md) —— Step 12-2-A 基线测试用例（本测试的对照版本）
- [`STEP_12_2_A_FEASIBILITY.md`](./STEP_12_2_A_FEASIBILITY.md) —— 可行性分析（含 A/B 方案对比、内存计算）
- [`STEP_12_VISION_SERVO_PLAN.md`](./STEP_12_VISION_SERVO_PLAN.md) —— Step 12 总体规划
- [`test-2026-09-16-step12-1-cam-base.md`](./test-2026-09-16-step12-1-cam-base.md) —— Step 12-1 基础测试用例
- [`roadmap.md`](./roadmap.md) —— Step 12 主线规划与 Architecture Baseline
- [`firmware/esp32-cam/src/person_detector.h`](../firmware/esp32-cam/src/person_detector.h) —— 优化后接口与参数
- [`firmware/esp32-cam/src/person_detector.cpp`](../firmware/esp32-cam/src/person_detector.cpp) —— 优化后实现
- [`firmware/esp32-cam/src/main.cpp`](../firmware/esp32-cam/src/main.cpp) —— 摄像头/HTTP 主代码（含 `[PERSON]` 日志）
- [`firmware/esp32-cam/src/draw_overlay.h`](../firmware/esp32-cam/src/draw_overlay.h) —— Overlay 接口（未改动）
- [`firmware/esp32-cam/platformio.ini`](../firmware/esp32-cam/platformio.ini) —— PlatformIO 配置（未改动）

---

## 附录 E · 执行记录模板

**执行日期**：______  **执行人**：______  **固件版本**：______  **ESP32-CAM IP**：______
**固件 SHA256**：______  **测试环境光照**：☐ 正常 ☐ 弱光 ☐ 强光

| 编号 | 通过 | 备注 |
|------|------|------|
| TC-01 | ☐ | 电源 / USB / 排线 |
| TC-02 | ☐ | RAM: ______%  Flash: ______% |
| TC-03 | ☐ | Wi-Fi IP: ______ |
| TC-04 | ☐ | 首页: ______ |
| TC-05 | ☐ | 单帧 overlay 正常 |
| TC-06 | ☐ | fps 稳定值: ______ |
| TC-07 | ☐ | 正样本 A/B/C/D 检出: ___/4 |
| TC-08 | ☐ | 负样本 G/H/I/J/L 无误: ___/5 |
| TC-09 | ☐ | 时间稳定性生效: ☐ tC/tL 观察 |
| TC-10 | ☐ | bbox 抖动幅度: ______ (期望 <0.02) |
| TC-11 | ☐ | fps: ______  det(μs): ______  free(KB): ______ |
| TC-12 | ☐ | 30 min 无重启: ☐ 无泄漏: ☐ |
| TC-13 | ☐ | 无 LED 光点: ☐ |
| TC-14 | ☐ | 断线/多客户端/遮挡/Wi-Fi 恢复 |

**问题记录**：

```text
[时间] [TC 编号] [现象] [串口日志摘录] [处理]
```

**优化前后对比**（可选，但强烈建议）：

| 场景 | v1.0 (09-19) 结果 | v1.1 (09-21) 结果 | 变化 |
|------|-------------------|-------------------|------|
| 场景 A 浅肤色正面 | ______ | ______ | ☐ 好 ☐ 平 ☐ 差 |
| 场景 C 深肤色正面 | ______ | ______ | ☐ 好 ☐ 平 ☐ 差 |
| 场景 H 木质桌面误检 | ______ | ______ | ☐ 好 ☐ 平 ☐ 差 |
| bbox 抖动幅度 | ______ | ______ | ☐ 好 ☐ 平 ☐ 差 |
| fps | ______ | ______ | ☐ 好 ☐ 平 ☐ 差 |
| RAM 占用 | ______% | ______% | |
| 首次检出时间 | ______ ms | ______ ms | |

---

## 附录 F · 优化前后对比基线（**必读**）

**本测试的核心是"相对验证"**：验证 v1.1 相对 v1.0 在几个具体维度上有提升，而不是"达到某个 ML 指标"。

### F.1 五大约束（v1.0 的缺陷）

| # | 缺陷 | 影响 | v1.1 的修复 |
|---|------|------|-------------|
| 1 | LuvSkin 阈值仅覆盖浅肤色（Cb 77-127, Cr 133-173） | 深肤色漏检 | **加宽到 Cb 77-135, Cr 128-180** |
| 2 | 无形态学去噪 | YCbCr ±1 抖动产生孤立噪点 | **3×3 opening (erode≥4 + dilate)** |
| 3 | BFS 队列仅 1024 | QQVGA 大人脸连通域 3000+ 被丢弃 | **队列扩到 4096** |
| 4 | 无硬过滤（仅面积 + 占比） | 平坦色块 / 细长条被误判 | **加填充率、宽高比、bbox 尺寸硬过滤** |
| 5 | 无时间稳定性 + 无 EMA | bbox 每帧乱跳 | **N-frames-confirm=3 / M-frames-loss=2 + EMA α=9/32** |

### F.2 五大约束 → 五个必测 TC

| 缺陷 | 对应 TC | 通过标准 |
|------|---------|----------|
| 阈值偏窄 | TC-07 场景 C | 深肤色能检出 |
| 无去噪 | TC-08 场景 G / L | 空场景不报 |
| BFS 队列小 | TC-07 场景 A / D | 大人脸能检出 |
| 硬过滤不足 | TC-08 场景 H / J | 木纹/橙色不报 |
| 无平滑 | TC-10 | 静止时抖动 < 0.02 |

### F.3 已知局限（v1.1 仍会失败的场景）

以下场景**预期会失败**，不需要在本测试中算失败：

- **强逆光**：肤色像素被削平，任何 YCbCr 阈值都会漏检
- **深色衣物完全遮挡**：可见皮肤 < MIN_COMPONENT_AREA
- **暖色木质大面积**：即使加宽阈值后仍可能被误判为皮肤（属于启发式局限，需 Face Detection 才能真解决）
- **手部特写**：可能检出手，但打分不高（属于本模块边界，S12-4 Face Detection 会解决）
- **多人同时出现**：只会选中"打分最高"的 1 个，属于启发式简化

### F.4 本测试明确不做

- ❌ 用 ML 指标（mAP、IoU）评判
- ❌ 训练数据集
- ❌ 切换 YOLO / TFLite
- ❌ 修改 draw_overlay / HTTP / 摄像头初始化代码
- ❌ 修改 ESP32-S3 主工程
- ❌ 修改 platformio.ini（不改依赖）
- ❌ 添加 UART / 舵机 / 语音接口

如果本次测试发现某 TC 需要修改超出 `firmware/esp32-cam/src/person_detector.{h,cpp}` + `main.cpp 内 streamHandler 局部`，请 **STOP 并报告**，不要自行扩大范围。

---

## 附录 G · 参数调节建议（仅在测试失败时参考）

**不要预先调节。** 只有当某个 TC 失败且已确认不是硬件/环境问题时，才参考下表的调节方向：

| 场景 | 症状 | 调节方向 | 优先顺序 |
|------|------|----------|----------|
| 场景 C 深肤色漏检 | `skin` 值很小 | `SKIN_CR_MIN 128 → 122` | 1 |
| 场景 H 木纹误检 | `cand ≥ 1` 且 `score > 40` | `SKIN_CB_MAX 135 → 130` | 1 |
| 场景 G 空场景误检 | `skin` 有噪点 | 提高 erode `MIN_8_NEIGHBOR 4 → 5` | 2 |
| bbox 抖动大 | `x` 相邻帧差 > 0.05 | `EMA_ALPHA_NUM 9 → 6` | 2 |
| 场景 D 半身漏检 | `cand = 0` | `MIN_COMPONENT_AREA 60 → 40` | 3 |
| fps < 3 | 帧率不足 | `CAM_QUALITY 15 → 20` | 3 |

调节原则：
1. **每次只调 1 个参数**，观察 10 帧日志再判断
2. **每次调整后重新走 TC-07 + TC-08**（最容易暴露副作用）
3. **调过的参数值写进附录 E 执行记录**，便于回滚

---

## 版本

| 版本 | 日期 | 变更 |
|------|------|------|
| v1.0 | 2026-09-21 | 初版：Step 12-2-B 优化版测试用例（TC-01~14），含 5 大约束 → 5 个必测 TC 映射、参数速查、优化前后对比模板 |
