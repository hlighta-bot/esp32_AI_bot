# MAX9814 麦克风独立测试固件

**用途**：验证 MAX9814 麦克风接线与信号是否正常。对着麦克风说话，串口 monitor 里能直接看到 raw / rms / 可视化条波动。

**特点**：
- 无 Wi-Fi、无 lib_deps、无外部依赖 → 编译最快、最容易成功
- 只需要一根 USB 线，不需要配网
- 用来在 TC-01（接线）和 TC-05（ADC 采集）之间快速验证麦克风

---

## 1. 硬件接线（MAX9814 5-pin breakout）

| MAX9814 引脚 | 连接到 ESP32-S3 | 说明 |
|---|---|---|
| GND | GND | 必须共地 |
| VDD | 3V3 | 建议 VDD↔GND 接 100 nF 电容 |
| Out | **GPIO1** (ADC1_CH0) | 音频输出 |
| GAIN | **悬空** | +50 dB（推荐） |
| AR | **悬空** | DC-coupled，静默输出 ≈ 1.65 V |

**接线检查**：用万用表测 MAX9814 Out 对 GND，静默时应为 1.65 V（±0.1 V）；说话时应能看到电压微小波动。

---

## 2. 编译

```bash
cd ~/projects/esp32-voice-ai/firmware/esp32-mic-test
pio run -e esp32-s3-mic-test
```

首次编译会自动下载 espressif32 平台（几 MB），需要 1–3 分钟。

## 3. 烧录

```bash
pio run -e esp32-s3-mic-test --target upload
```

## 4. 打开串口监视

```bash
pio device monitor -b 921600
```

或用 VS Code PlatformIO 侧边栏的 Serial Monitor 按钮。

---

## 5. 期望输出

**启动头（一次性）**：

```text
==========================================
  MAX9814 Mic Test
==========================================
ADC pin        : GPIO1 (ADC1_CH0)
ADC range      : 12-bit (0..4095)
Expected silent: raw ~= 2048 (dc ~= 0)
Sample rate    : 10000 Hz
Report period  : 100 ms
Speaking into mic should make 'rms' rise and bar grow.
------------------------------------------
```

**运行期（每 100 ms 一行）**：

```text
# 静默：
[mic] raw= 2046  dc=   -2  rms=  12.4  min=2042 max=2051 | ........................................
[mic] raw= 2049  dc=    1  rms=   8.7  min=2043 max=2052 | ........................................
[mic] raw= 2045  dc=   -3  rms=  15.2  min=2040 max=2050 | ........................................

# 说话（对着麦克风 10-30 cm）：
[mic] raw= 1820  dc= -228  rms= 340.5  min=1720 max=2180 | ############################............
[mic] raw= 2480  dc= +432  rms= 420.3  min=1890 max=2620 | ###################################.....
[mic] raw= 1650  dc= -398  rms= 502.1  min=1580 max=2710 | #######################################.

# 松开后 1-2 秒回落到静默水平
```

## 6. 判定标准

| 现象 | 判定 |
|---|---|
| 静默 raw 稳定在 2040–2056、rms < 30 | ✅ 麦克风正常，静默基线正确 |
| 说话时 raw 偏离 2048 超过 ±200，rms > 200 | ✅ 麦克风能拾音 |
| 松开后 1–2 秒 rms 回落 | ✅ 无残留 |
| 静默 raw = 0 或 4095（拉满或拉空） | ❌ 未共地 / VDD 没接 3V3 / Out 线断 |
| 静默 raw ≈ 1650（不是 2048） | ❌ AR 接到了 VDD（切到 AC-coupled），把 AR 悬空 |
| 静默 raw ≈ 2048 但说话 rms 变化 < 30 | ❌ 麦克风损坏 / 距离太远 / GAIN 错接到 GND |
| rms 变化 < 30 但明显能听到声音 | ⚠️ 100 nF 电容缺失，Wi-Fi 电流干扰吞掉了信号 |
| rms 一直很大，bar 满 | ⚠️ GAIN 接到 VDD（+60 dB 过强）/ MAX98357A 5V 电源串扰 |

---

## 7. 常见问题

### Q: `Command 'pio' not found`

PlatformIO 没安装。任选其一：

```bash
# 方式 A（推荐）：pip 安装平台独立版
pip install platformio
# 之后命令是 `platformio` 或 `pio`（视 shell hash 情况）
```

```bash
# 方式 B：apt 安装（可能需要 sudo）
sudo apt install platformio
```

安装完执行 `pio --version` 或 `platformio --version` 确认版本。

### Q: 启动就死机 / 打印乱码

- 波特率确认是 921600
- USB 线不能是纯充电线（要数据线）
- 换另一根 USB 或换 USB 口

### Q: 首次编译非常慢

第一次会下载 espressif32 平台和工具链（~500 MB），耐心等。之后秒级。

### Q: 想调灵敏度

在 [`src/main.cpp`](./src/main.cpp) 顶部改：

- `SAMPLES_PER_MS`：采样密度（默认 10，越大越流畅）
- `RMS_DB_SCALE`：bar 归一化系数（默认 60，越小越敏感）
- 硬件侧：GAIN 引脚接 GND 是 +40 dB（更弱），接 VDD 是 +60 dB（更强）

---

## 8. 回到主固件

验证完麦克风没问题后，切回主固件：

```bash
cd ~/projects/esp32-voice-ai/firmware/esp32
pio run -e esp32-s3-n16r8-wifi --target upload
```

（记得编辑 `~/projects/esp32-voice-ai/config.local.json` 里的 Wi-Fi 参数）
