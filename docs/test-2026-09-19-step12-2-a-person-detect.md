# 测试用例 · 2026-09-19 · Step 12-2-A ESP32-CAM 本地人物检测

> **文档编号**：TC-20260919-STEP12-2A-DETECT
> **版本**：v1.0
> **适用代码**：Step 12-2-A（`firmware/esp32-cam`，`env:esp32-cam`）
> **测试目标**：验证 ESP32-CAM + OV2640 在 QQVGA 160×120 RGB565 图像上完成本地低资源人物检测（YCbCr 肤色 + 8-邻域 BFS 连通域），并把检测框 / 中心点 / 参数通过 Overlay 绘制到 `/capture` 与 `/stream` 的 JPEG 中。
>
> **测试时长**：全流程走一遍约 30–45 分钟；每个 TC 独立完成，可按编号跳测。
>
> **参考**：[`docs/STEP_12_2_A_FEASIBILITY.md`](./STEP_12_2_A_FEASIBILITY.md)、[`docs/STEP_12_VISION_SERVO_PLAN.md`](./STEP_12_VISION_SERVO_PLAN.md)、[`docs/test-2026-09-16-step12-1-cam-base.md`](./test-2026-09-16-step12-1-cam-base.md)、[`firmware/esp32-cam/src/main.cpp`](../firmware/esp32-cam/src/main.cpp)

---

## 0. 目录

| 编号 | 测试项 | 时长 | 依赖 |
|------|--------|------|------|
| TC-01 | 前置条件检查（硬件/软件/固件） | 3 min | 无 |
| TC-02 | 固件编译（重点：`frame2jpg_cb` 链接） | 3 min | TC-01 |
| TC-03 | 固件烧录 + 启动日志确认 | 3 min | TC-02 |
| TC-04 | Wi-Fi STA 连接 + HTTP 服务起 | 3 min | TC-03 |
| TC-05 | `/capture` 单帧下载（含 overlay） | 5 min | TC-04 |
| TC-06 | **`/stream` MJPEG 视频流（含 overlay）** | 5 min | TC-05 |
| TC-07 | 检测算法正确性（正样本 / 负样本） | 8 min | TC-06 |
| TC-08 | 性能与内存监控（FPS / FreeHeap / LargestBlock） | 5 min | TC-06 |
| TC-09 | 长时间稳定性（30 分钟压测） | 30 min | TC-06 |
| TC-10 | 错误路径与断线恢复 | 5 min | TC-04 |
| TC-11 | LED 无污染摄像头视野 | 2 min | TC-06 |

**附录**：A 参数速查 · B 串口日志速查 · C 常见故障 · D 参考文档 · E 执行记录模板 · **F 边界与"不做"清单**

---

## 通用前置条件

```text
[ ] AI-Thinker ESP32-CAM 板子（带 OV2640）
[ ] USB 数据线（能传数据，不是仅充电线）
[ ] Wi-Fi 路由器（2.4 GHz 频段可用，SSID 已改到 main.cpp 第 38 行）
[ ] 电脑已装 PlatformIO（VS Code 插件）
[ ] 电脑已装 USB-UART 驱动（CP210x / CH340）
[ ] 浏览器（Chrome / Edge / Firefox 均可）
[ ] 串口监视器（PlatformIO 内置 或 `screen -L /dev/ttyUSB0 115200`）
[ ] 项目位于 /home/hqb/projects/esp32-voice-ai
[ ] Step 12-1 已通过（`/capture` 与 `/stream` 基础版曾实测通过）
```

### 路径约定（**重要**）

本文档所有 shell 命令默认使用一个环境变量 `$PROJECT_ROOT` 指向项目根目录。
**请在打开一个终端后先执行一次**：

```bash
export PROJECT_ROOT=/home/hqb/projects/esp32-voice-ai
echo "PROJECT_ROOT=$PROJECT_ROOT"   # 校验
ls "$PROJECT_ROOT/firmware/esp32-cam/src/"   # 应看到 5 个源文件
```

后续所有命令都用 `$PROJECT_ROOT` 前缀，**不依赖 cwd**。
如果你更喜欢用相对路径，也可以直接：

```bash
cd /home/hqb/projects/esp32-voice-ai
```

之后所有 `esp32-voice-ai/...` 的相对路径都从当前目录算起。

**不要**在 `firmware/esp32-cam/` 子目录下运行不带 `cd` 前缀的相对路径命令，会找不到文件。

---

## TC-01 · 前置条件检查

**目的**：一次性核对本次测试的所有前置条件，避免中途卡壳。

### 步骤

1. **确认 Step 12-2-A 代码已就位**：
   ```bash
   ls -la "$PROJECT_ROOT/firmware/esp32-cam/src/"
   ```
   应看到以下 5 个文件：
   - `main.cpp`
   - `person_detector.h`
   - `person_detector.cpp`
   - `draw_overlay.h`
   - `draw_overlay.cpp`

2. **确认摄像头参数已切换到 RGB565 / QQVGA**：
   ```bash
   grep -nE "CAM_PIXFORMAT|CAM_FRAMESIZE|CAM_QUALITY|CAM_FRAMERATE" \
     "$PROJECT_ROOT/firmware/esp32-cam/src/main.cpp"
   ```
   期望：
   ```text
   CAM_PIXFORMAT    PIXFORMAT_RGB565
   CAM_FRAMESIZE    FRAMESIZE_QQVGA
   CAM_QUALITY      15
   CAM_FRAMERATE    10
   ```

3. **确认 `frame2jpg_cb` 已声明可用**：
   ```bash
   grep -n "img_converters.h" "$PROJECT_ROOT/firmware/esp32-cam/src/main.cpp"
   grep -n "frame2jpg_cb"      "$PROJECT_ROOT/firmware/esp32-cam/src/main.cpp"
   ```
   应有 include + 至少 1 处调用。

4. **确认 Wi-Fi 凭据已改成你的路由器**：
   ```bash
   grep -nE "WIFI_SSID|WIFI_PASS" "$PROJECT_ROOT/firmware/esp32-cam/src/main.cpp"
   ```

### 期望结果

```text
[ ] 5 个源文件都在位
[ ] 摄像头参数为 RGB565 / QQVGA / quality 15 / frameratelimit 10
[ ] frame2jpg_cb 已 include + 已使用
[ ] Wi-Fi 凭据已改为本地路由器
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 只看到 3 个源文件 | 分支没切对 / 之前没保存；`cd "$PROJECT_ROOT" && git status firmware/esp32-cam/` 确认 |
| 摄像头参数仍是 QVGA / JPEG | Step 12-1 的旧代码；重新编辑 main.cpp |
| `img_converters.h` 找不到 | PlatformIO packages 未装；执行 `pio pkg install` |

---

## TC-02 · 固件编译（重点：`frame2jpg_cb` 链接）

**目的**：验证新增代码能干净编译，重点确认 framework 提供的 `frame2jpg_cb` 能被链接进来。

### 步骤

1. 进入项目目录并编译：
   ```bash
   cd "$PROJECT_ROOT/firmware/esp32-cam"
   pio run
   ```

2. 观察输出：
   - 期望看到 `Compiling .pio/build/esp32-cam/src/person_detector.cpp.o`
   - 期望看到 `Compiling .pio/build/esp32-cam/src/draw_overlay.cpp.o`
   - 期望看到 `Linking .pio/build/esp32-cam/firmware.elf`
   - 期望最终一行 `========================= [SUCCESS] =========================`

3. **重点验证：`frame2jpg_cb` 已链接到 ELF**：
   ```bash
   cd "$PROJECT_ROOT/firmware/esp32-cam"
   /home/hqb/.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-nm \
       -C .pio/build/esp32-cam/firmware.elf \
       | grep -iE "frame2jpg|detectPerson|drawPerson|jpegOut|jpegCollect"
   ```

### 期望结果

```text
[ ] pio run 输出 SUCCESS
[ ] RAM ≤ 120 KiB（37.5% 以下）
[ ] Flash ≤ 1 MB（26.9% 以下）
[ ] nm 输出包含：
      T frame2jpg_cb            ← 符号存在（T 表示定义，U 才是未解析）
      T detectPerson(...)
      T drawPersonOverlay(...)
      t jpegOutCb(...)          ← /capture 用
      t jpegCollectCb(...)      ← /stream 用
```

参考（Step 12-2-A 初编实测）：
- RAM  **80292 bytes / 327680 bytes  = 24.5%**
- Flash **862877 bytes / 3145728 bytes = 27.4%**
- `frame2jpg_cb` 地址 `0x401049c4`（T 类型）

### 排查思路

| 现象 | 可能原因 | 处理 |
|------|----------|------|
| `undefined reference to frame2jpg_cb` | Framework 缺少该符号 | STOP，报告；**不引入第三方库、不切换 plan C**，回滚到 Step 12-1 待查 |
| `undefined reference to detectPerson` | 未把 `person_detector.cpp` 加入编译 | 确认文件路径在 `src/` 下 |
| `undefined reference to drawPersonOverlay` | 未把 `draw_overlay.cpp` 加入编译 | 同上 |
| RAM > 150 KiB | 参数错（比如还是 QVGA RGB565） | 回 TC-01 检查 |
| `%f` 相关警告 | Arduino snprintf 不支持浮点 | 已用 `fmtFloat()` 手动格式化替代 |

---

## TC-03 · 固件烧录 + 启动日志确认

**目的**：把编译产物刷入 ESP32-CAM，通过串口确认启动流程正常。

### 步骤

1. 烧录：
   ```bash
   cd "$PROJECT_ROOT/firmware/esp32-cam"
   pio run -t upload
   ```
   或 PlatformIO 工具栏 Upload 按钮。

2. 打开串口监视（115200 baud）：
   ```bash
   cd "$PROJECT_ROOT/firmware/esp32-cam"
   pio device monitor
   ```
   或
   ```bash
   screen /dev/ttyUSB0 115200
   ```

3. 拔插 USB 或按 RESET 重启板子，观察启动日志。

### 期望结果

```text
[ ] [CAM] configuring camera...
[ ] [CAM] esp_camera_init() OK
[ ] [CAM] sensor PID: 0x26           ← OV2640 识别成功
[ ] [CAM] frame size: QVGA 320x240    ← 老字符串未改，注意（见"排查思路"）
[ ] [WIFI] connecting to SSID: hqbwifi_36
[ ] ESP32-CAM ONLINE
[ ] IP : x.x.x.x
[ ] [HTTP] main server started on port 80
[ ] [HTTP] register / : 0x0
[ ] [HTTP] register /capture : 0x0
[ ] [HTTP] register /stream : 0x0
```

**说明**：`[CAM] frame size: QVGA 320x240` 是硬编码的旧日志字符串，不影响功能。若你看到实际是 QQVGA，说明代码逻辑正确；此日志字符串是死字符串，Step 12-2-A 未修改。**不影响测试，可作为已知遗留项**。

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 无 `[CAM] esp_camera_init() OK` | 摄像头排线未接好 / 引脚接错 / 板子损坏 |
| 反复重启 | USB 供电不足；换数据线或换 USB 口 |
| `[WIFI] FAILED to connect` | SSID/密码错 / 路由器 2.4 GHz 关闭 / 5 GHz only |
| `[HTTP] main server start failed: 0x103` | 端口 80 被占用（本板重启即可） |

---

## TC-04 · Wi-Fi STA 连接 + HTTP 服务起

**目的**：确认 Wi-Fi 与 HTTP 全部就绪。

### 步骤

1. 从 TC-03 的串口日志中读取 IP，例如 `192.168.0.6`。
2. 用同一 Wi-Fi 网络的电脑访问：
   ```
   http://192.168.0.6/
   ```
3. 应看到 HTML 首页，显示 IP、`/stream`、`/capture` 链接。

### 期望结果

```text
[ ] 首页加载成功
[ ] 首页显示实际 IP
[ ] 页面显示 "ESP32-CAM · OV2640 · Step 12-1" 标题
[ ] 三个端点链接（/, /capture, /stream）都存在
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 无法访问 | 电脑与 ESP32 不在同一网段 |
| 浏览器超时 | 检查路由器 IP 是否变了 |
| 404 | 端口错（必须 80，不带端口就是 80） |

---

## TC-05 · `/capture` 单帧下载（含 overlay）

**目的**：验证单帧路径：`fb_get → detectPerson → drawPersonOverlay → frame2jpg_cb → HTTP chunked send`。

### 步骤

1. 让摄像头对准一个**能看到人脸的场景**（自己、家人、玩偶头部均可）。
2. 浏览器打开：
   ```
   http://192.168.0.6/capture
   ```
3. 观察加载出的 JPEG 图像。
4. 打开串口监视器，同时观察一行日志：
   ```text
   [CAPTURE] detected=<0|1> conf=<0.00-1.00> detect=<us>us jpeg=<bytes>B free=<heap>B max=<blk>B
   ```

### 期望结果

**图像内容**：
```text
[ ] 顶部有 32 px 高的黑色信息条（占图像 1/4）
[ ] 信息条内 3 行文字：
      行 1：P:<Y|N> X:<0.xx> Y:<0.xx>
      行 2：W:<0.xx> H:<0.xx> C:<0.xx>
      行 3：SKIN OK 或 NO SKIN
[ ] 检测到人体时：绿色 2 px 粗的检测框 + 红色 5 px 十字中心点
[ ] 未检测到时：不画框，只显示 "NO SKIN"
```

**串口日志**：
```text
[ ] 有 [CAPTURE] 行输出
[ ] jpeg=<bytes> 在 1000-12000 之间（12 KiB 是 buffer 上限）
[ ] free=<heap>B 至少 60 KiB（避免内存压力）
[ ] detect=<us>us 一般 < 20000 us（20 ms）
[ ] conf 在检测到人体时 ≥ 0.30
```

### 排查思路

| 现象 | 可能原因 | 处理 |
|------|----------|------|
| `/capture` 打开显示 500 | `frame2jpg_cb` 返回 false | 检查串口日志的 `[CAPTURE]` 输出 |
| 图像有 overlay 但没框 | 没检测到人 / 阈值过高 | 走近镜头 / 打更亮的灯 / 换肤色更明显的场景 |
| 图像显示 "NO SKIN" | 场景无肤色 / 光线太暗 / 手遮挡镜头 | 换场景重测 |
| `jpeg=<bytes>` = 0 | 编码失败 | 串口看错误 |
| FreeHeap < 30 KiB | 内存紧张 | 减少其他任务 / 回滚到 Step 12-1 排查 |

---

## TC-06 · `/stream` MJPEG 视频流（含 overlay）

**目的**：验证连续路径，每帧都经过 detect + overlay + JPEG 编码 + 分片发送。

### 步骤

1. 打开首页 `http://192.168.0.6/`，点击 `/stream` 或直接访问：
   ```
   http://192.168.0.6/stream
   ```
2. 观察图像是否连续刷新。
3. 打开串口监视器，观察每 10 帧的输出：
   ```text
   [STREAM] f=<n> fps=<x.x> det=<us>us enc=<us>us jpeg=<bytes>B free=<heap>B max=<blk>B p=<0|1>
   ```

### 期望结果

```text
[ ] 视频流可播放
[ ] 每帧顶部有黑色信息条，能看到 P/X/Y/W/H/C 更新
[ ] 检测到人体时看到绿色框 + 红色十字，会跟随人体移动
[ ] 串口每 10 帧一行 [STREAM] 日志
[ ] fps 期望 3-10（Step 12-2-A 目标 5-10 FPS；实测可能更低）
[ ] det=<us>us 一般 < 20000
[ ] enc=<us>us 一般 < 50000
[ ] free=<heap>B 长期稳定，不持续下降
```

### 排查思路

| 现象 | 可能原因 | 处理 |
|------|----------|------|
| 只有第一帧，后续卡住 | frame2jpg_cb 编码超时 / 内存不足 | 检查 `enc=<us>us` 是否 > 500 ms |
| fps < 1 | 检测或编码耗时过长 | 降 quality 到 25，或后续考虑降分辨率 |
| 串口报 `jpeg encode failed: 0x102` | `ESP_ERR_NOT_SUPPORTED`，即 s_jpegBuf 溢出 | 提高 CAM_QUALITY 到 25（JPEG 更小） |
| 串口报 `header send failed` | 客户端提前断开 | 正常；重开浏览器即可 |
| `free=<heap>B` 持续下降 | 内存泄漏 | STOP，报告，回滚排查 |

---

## TC-07 · 检测算法正确性（正样本 / 负样本）

**目的**：验证 PersonDetector 在真实场景下的合理表现。

### 正样本场景（应检测到）

| 场景 | 距离 | 说明 |
|------|------|------|
| 人脸正面 | 0.5-1.5 m | 应看到人脸周围绿色框，中心在面部 |
| 手掌 | 0.3-0.8 m | 手应被识别为人体组件 |
| 头部 + 上半身 | 1-2 m | 框应覆盖头部到肩部 |

### 负样本场景（不应稳定检测到）

| 场景 | 期望 |
|------|------|
| 空白墙壁 | 不画框，显示 "NO SKIN" |
| 深色衣物无人 | 不画框 |
| 只有木纹桌面 | 不画框（木纹颜色接近肤色，可能少量误检） |
| 灯光直接射入镜头 | 可能误检（已知局限，不视为失败） |

### 步骤

1. 逐个场景对准镜头，观察 `/stream` 的框位置与 `conf` 值。
2. 记录每个场景的：
   - `p=<0|1>`（是否检测到）
   - `conf`（confidence）
   - 框位置是否合理

### 期望结果

```text
[ ] 3 个正样本场景全部检出（p=1）
[ ] 2/3 以上负样本场景未检出（p=0）
[ ] 人脸场景 conf ≥ 0.40
[ ] 检测框位置与真人/真人体的位置相符
[ ] 未检测到时显示 NO SKIN，不出现误报的固定小方框
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 手/脸都不检出 | 光线太暗 / YCbCr 阈值不适合该肤色 / 遮挡太多 |
| 空场景也检出 | 场景有大量米色/肉色物体（墙壁/木质家具） |
| 框位置偏离 | 归一化到像素的换算有 bug（检查 drawPersonOverlay 的 boxX0/Y0 计算） |

**已知局限（不视为测试失败）**：
- 肤色阈值基于 LuvSkin，对暗肤色/深肤色/异光场景误检率较高
- 检测的是"肤色连通区域"，不是解剖学上的"人"
- 单一连通域，不区分头/手/多个人
- 帧率低（10 FPS 目标），快速移动的对象可能脱框

---

## TC-08 · 性能与内存监控

**目的**：确认检测 + overlay + JPEG 编码的耗时与内存占用符合预期。

### 步骤

1. 打开 `/stream`，保持 60 秒。
2. 记录至少 6 条 `[STREAM]` 日志（60 秒 × 5 帧/10 帧 = 6 组）。
3. 用以下表格记录：

| # | f | fps | det(μs) | enc(μs) | jpeg(B) | free(heap) | max(blk) | p |
|---|---|-----|---------|---------|---------|------------|----------|---|
| 1 | 10 | | | | | | | |
| 2 | 20 | | | | | | | |
| ... | ... | ... | ... | ... | ... | ... | ... | ... |

### 期望结果

**性能（目标区间）**：
```text
[ ] fps ≥ 3（Step 12-2-A 目标 5-10；实测可能 3-8）
[ ] det=<us> 一般 < 20000（20 ms）
[ ] enc=<us> 一般 < 50000（50 ms）
[ ] jpeg=<bytes> 稳定在 2000-10000 之间
[ ] free=<heap> 稳态 ≥ 60 KiB
[ ] max=<blk> ≥ 32 KiB（保证大分配可用）
```

**稳定性**：
```text
[ ] free=<heap> 波动 < 10 KiB（不持续下降）
[ ] 6 组日志数值差异 < 20%
```

### 排查思路

| 现象 | 可能原因 | 处理 |
|------|----------|------|
| fps < 2 | 检测或编码太慢 | 降 CAM_QUALITY 到 25，或降 CAM_FRAMERATE 到 5 |
| free=heap 持续下降 | 内存泄漏 | STOP，报告 |
| max=blk 突然变小 | 内存碎片化 | 减少并发操作，考虑重启 |
| det=us 突然飙高 | 组件数量多 / BFS 队列溢出频繁 | 减少场景复杂度 |

---

## TC-09 · 长时间稳定性（30 分钟压测）

**目的**：确认 30 分钟内无人物重启 / 无人物内存泄漏 / 无人物死锁。

### 步骤

1. 打开 `/stream`，浏览器保持页面打开。
2. 定时（每 5 分钟）记录一次串口 `[STREAM]` 输出。
3. 30 分钟后关闭浏览器。

### 期望结果

```text
[ ] 30 分钟期间无重启（无重复的 [WIFI] connecting 日志）
[ ] free=<heap> 首尾差异 < 20 KiB
[ ] max=<blk> 首尾差异 < 8 KiB
[ ] 30 分钟内 fps 平均 ≥ 3
[ ] 无 `jpeg encode failed` 累积
[ ] 30 分钟内 `det=<us>us` 平均值不显著上升（< 30% 增长）
```

### 排查思路

| 现象 | 可能原因 | 处理 |
|------|----------|------|
| 中间重启 | Watchdog 触发 / 电源不足 | 换 5V/2A 电源；查看崩溃前的日志 |
| 后半程 fps 明显下降 | 热降频 / 内存碎片化 | 加散热 / 降 CAM_FRAMERATE |
| 后半程 free 明显下降 | 内存泄漏 | STOP，报告，回滚排查 |

---

## TC-10 · 错误路径与断线恢复

**目的**：确认所有错误分支行为符合预期。

### 场景 A · 客户端断开

**步骤**：
1. 打开 `/stream`
2. 5 秒后关闭浏览器标签
3. 观察串口日志

**期望**：
```text
[ ] 串口出现 [STREAM] JPEG send failed 或 frame end failed
[ ] 串口出现 [STREAM] client disconnected, frames=<n>
[ ] 板子不重启，HTTP 服务继续正常
[ ] 重新打开 /stream 可正常工作
```

### 场景 B · 打开多个客户端

**步骤**：
1. 打开 `/capture` × 3（三个浏览器标签）
2. 打开 `/stream` × 1
3. 观察串口日志

**期望**：
```text
[ ] 所有请求都能返回响应
[ ] 无 panic
[ ] free=<heap> 波动 < 15 KiB
```

### 场景 C · 摄像头硬件故障模拟

**步骤**：
1. 让摄像头对墙壁（无信号源）持续 10 秒
2. 观察串口日志

**期望**：
```text
[ ] 无 panic
[ ] 无重启
[ ] /stream 继续刷新（可能显示 NO SKIN）
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 断开后需要重启才能恢复 | httpd server 状态异常；本固件不应有此问题 |
| 多客户端下内存不足 | 检查 fb_count（本固件 = 1）与并发请求 |

---

## TC-11 · LED 无污染摄像头视野

**目的**：确认 Step 12-1 修复的 LED 问题在 Step 12-2-A 仍然成立。

### 步骤

1. 上电启动，观察板载 LED。
2. 打开 `/stream`，观察画面中是否有明显的 LED 光点。

### 期望结果

```text
[ ] 上电 + Wi-Fi 连接期间 LED 快速闪烁（连接中）
[ ] 连接完成后 LED 保持常灭
[ ] /stream 画面中看不到明显的 LED 光点
[ ] 无循环心跳（每 2 秒闪一次的旧 bug 已修复）
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| LED 常亮 | initWifi() 里漏了 LED_OFF；检查第 986 行附近 |
| LED 每 2 秒闪一次 | loop() 里的心跳代码未清干净 |

---

## 附录 A · 参数速查

| 参数 | 位置 | 值 | 说明 |
|------|------|----|------|
| CAM_PIXFORMAT | main.cpp | `PIXFORMAT_RGB565` | 让 detector 直接读像素 |
| CAM_FRAMESIZE | main.cpp | `FRAMESIZE_QQVGA` | 160×120 = 38400 B |
| CAM_QUALITY | main.cpp | `15` | JPEG 质量，越小越清晰但越大 |
| CAM_FRAMERATE | main.cpp | `10` | 目标帧率上限 |
| QQVGA_FRAME_BYTES | main.cpp | `38400` | framebuffer 大小 |
| MASK_BYTES | person_detector.h | `2400` | bit-packed mask 大小 |
| BFS_QUEUE_SIZE | person_detector.h | `1024` | BFS 队列固定大小 |
| MAX_COMPONENTS | person_detector.h | `256` | 组件元数据上限 |
| SKIN_CB_MIN/MAX | person_detector.h | `77/127` | YCbCr 肤色 Cb 阈值 |
| SKIN_CR_MIN/MAX | person_detector.h | `133/173` | YCbCr 肤色 Cr 阈值 |
| MIN_COMPONENT_AREA | person_detector.h | `30` | 最小面积（像素） |
| MAX_COMPONENT_AREA_FRAC | person_detector.h | `0.40` | 最大面积比例 |
| CONF_WEIGHT_AREA | person_detector.h | `0.40` | Confidence 面积权重 |
| CONF_WEIGHT_DENSITY | person_detector.h | `0.30` | Confidence 密度权重 |
| CONF_WEIGHT_ASPECT | person_detector.h | `0.30` | Confidence 长宽比权重 |
| DETECTION_THRESHOLD | person_detector.h | `0.30` | 检测阈值 |
| JPEG_BUF_SIZE | main.cpp | `12288` | JPEG 收集 buffer 大小 |
| HTTP_PORT | main.cpp | `80` | HTTP 端口 |
| HTTP_CTRL_PORT | main.cpp | `32768` | HTTP server 控制端口 |
| XCLK | main.cpp | `20 MHz` | 摄像头时钟 |

---

## 附录 B · 串口日志速查

| 前缀 | 含义 | 期望频率 |
|------|------|----------|
| `[CAM]` | 摄像头初始化 | 启动 1 次 |
| `[WIFI]` | Wi-Fi 连接 | 启动 1 次（或重连时） |
| `[HTTP]` | HTTP 服务 | 启动 1 次 |
| `[CAPTURE]` | `/capture` 请求 | 每次请求 1 行 |
| `[STREAM] client connected` | `/stream` 连接 | 每次连接 1 行 |
| `[STREAM] f=.. fps=..` | `/stream` 帧日志 | 每 10 帧 1 行 |
| `[STREAM] client disconnected` | `/stream` 断开 | 每次断开 1 行 |
| `[STREAM] frame end failed` | 发送错误 | 异常时才出现 |
| `[STREAM] jpeg encode failed` | 编码失败 | 异常时才出现 |
| `[CAPTURE] JPEG encode failed` | 单帧编码失败 | 异常时才出现 |

### 关键字段说明

- **fps**：10 帧滑窗估计（不精确）
- **det=us**：detectPerson 耗时（μs）
- **enc=us**：frame2jpg_cb 编码耗时（μs）
- **jpeg=B**：本帧 JPEG 字节数（`/stream`）或 `ctx.totalLen`（`/capture`）
- **free=heap**：`ESP.getFreeHeap()`，单位字节
- **max=blk**：`heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)`
- **p=<0|1>**：本帧是否检测到人体

---

## 附录 C · 常见故障速查

| 现象 | 可能原因 | 处理 |
|------|----------|------|
| 浏览器 500 错误 | `frame2jpg_cb` 返回 false | 串口看错误码，检查内存 |
| `/stream` 只有 1 帧然后卡住 | JPEG 编码耗时过长 / 客户端断线 | 降 quality / 重开浏览器 |
| 图像全黑 | 摄像头排线未接好 | 断电重插 OV2640 |
| 图像全白 | 曝光过度 | 减少环境光 / 调整 `set_brightness` |
| 帧率 < 1 | 检测或编码太慢 | 降 quality / 降分辨率 / 停 stream |
| `No SKIN` 一直显示 | 无肤色 / 光线差 | 换场景 / 打灯 |
| 检测框抖动 | 检测阈值波动 | 提高 DETECTION_THRESHOLD 到 0.35 |
| 内存持续下降 | 内存泄漏 | STOP，报告，回滚排查 |
| 板子反复重启 | 电源不足 / Watchdog | 换 5V/2A 电源；查看崩溃前日志 |

---

## 附录 D · 参考文档

- [`docs/STEP_12_2_A_FEASIBILITY.md`](./STEP_12_2_A_FEASIBILITY.md) —— 可行性分析（含 A/B 计划对比、内存计算、`frame2jpg_cb` API 查证）
- [`docs/STEP_12_VISION_SERVO_PLAN.md`](./STEP_12_VISION_SERVO_PLAN.md) —— Step 12 总体规划
- [`docs/test-2026-09-16-step12-1-cam-base.md`](./test-2026-09-16-step12-1-cam-base.md) —— Step 12-1 基础测试用例
- [`firmware/esp32-cam/src/main.cpp`](../firmware/esp32-cam/src/main.cpp) —— 摄像头/HTTP 主代码
- [`firmware/esp32-cam/src/person_detector.h`](../firmware/esp32-cam/src/person_detector.h) —— 检测器接口与参数
- [`firmware/esp32-cam/src/person_detector.cpp`](../firmware/esp32-cam/src/person_detector.cpp) —— 检测器实现
- [`firmware/esp32-cam/src/draw_overlay.h`](../firmware/esp32-cam/src/draw_overlay.h) —— Overlay 接口
- [`firmware/esp32-cam/src/draw_overlay.cpp`](../firmware/esp32-cam/src/draw_overlay.cpp) —— Overlay 实现
- [`firmware/esp32-cam/platformio.ini`](../firmware/esp32-cam/platformio.ini) —— PlatformIO 配置

---

## 附录 E · 执行记录模板

**执行日期**：______  **执行人**：______  **固件版本**：______  **ESP32-CAM IP**：______

| 编号 | 通过 | 备注 |
|------|------|------|
| TC-01 | ☐ | |
| TC-02 | ☐ | RAM: ______%  Flash: ______%  frame2jpg_cb 链接: ☐ |
| TC-03 | ☐ | |
| TC-04 | ☐ | |
| TC-05 | ☐ | 首次检测到人体: ☐  conf=______ |
| TC-06 | ☐ | fps 稳定值: ______  det(μs): ______  enc(μs): ______ |
| TC-07 | ☐ | 正样本检出: ___/3  负样本无误: ___/4 |
| TC-08 | ☐ | 平均 fps: ______  平均 free: ______ KiB |
| TC-09 | ☐ | 30 分钟无重启: ☐  无内存泄漏: ☐ |
| TC-10 | ☐ | 断线恢复: ☐  多客户端: ☐ |
| TC-11 | ☐ | LED 常灭: ☐  画面无 LED 光点: ☐ |

**问题记录**：

```text
[时间] [现象] [串口日志摘录] [处理]
```

---

## 附录 F · 边界与"不做"清单

Step 12-2-A 严格限定于：
- ✅ 本地人物区域检测（YCbCr 肤色 + 连通域）
- ✅ Overlay 绘制（框 + 中心点 + 参数文字）
- ✅ HTTP `/capture` 与 `/stream` 的可视化
- ✅ FreeHeap / LargestFreeBlock / FPS / JPEG size 日志
- ✅ 所有 `fb_get` 都有配对 `fb_return`

Step 12-2-A **明确不做**：
- ❌ YOLO / TFLite / 神经网络 / 任何深度学习推理
- ❌ 人脸识别 / ReID / 姿态估计
- ❌ UART 通信
- ❌ 舵机控制
- ❌ 主工程（`firmware/esp32/**`）通信
- ❌ ESP32-S3 相关任何文件
- ❌ 语音/TCP 相关任何文件
- ❌ 引入第三方 JPEG 库
- ❌ 切换 QVGA（Plan A 已在可行性分析中放弃）
- ❌ 长时任务队列 / `uint32_t queue[19200]` 类大临时 buffer

如以上任何一项在本次测试中被意外触发，请 STOP 并报告，不要自行扩展范围。

---

## 版本

| 版本 | 日期 | 变更 |
|------|------|------|
| v1.0 | 2026-09-19 | 初版，Step 12-2-A 完整测试用例 |
