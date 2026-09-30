# ESP32 Voice AI - 硬件资料汇总

> 本文档统一记录 ESP32-S3 N16R8 周边硬件的型号、参数、引脚分配与接线规范。
> 引脚定义必须与固件一致；新增外设时先更新本文档的 GPIO 占用表，再写代码。
>
> 相关文档：
> - 接线细节与故障排查：[`wiring.md`](./wiring.md)
> - 固件结构与编译：[`firmware.md`](./firmware.md)
> - 通信协议：[`protocol.md`](./protocol.md)

---

## 1. 硬件清单

| 组件 | 型号 | 用途 | 状态 |
|------|------|------|------|
| MCU | ESP32-S3 N16R8 | 主控 + I2S 主设备 + Wi-Fi STA | 已验证 |
| 功放 | MAX98357A | I2S 数字音频功放 | 已验证 |
| 扬声器 | 3W / 8Ω 或以上 | 音频输出 | 已验证 |
| 麦克风 | MAX9814 | 模拟 MEMS 麦克风（ADC1） | 已验证 |
| LCD | 0.96inch IPS Module (ST7735S) | 状态显示 / 表情交互 | 已接入（2026-09-27 LCD MVP） |
| 舵机 | MG90S（金属齿，9g） | Pan/Tilt 或动作反馈 | 待接入 |

---

## 2. GPIO 占用表（ESP32-S3 N16R8）

> **新增外设前必须先查此表，避免引脚冲突。**
> 来源：[`firmware/esp32/src/main.cpp`](../firmware/esp32/src/main.cpp)、[`firmware/esp32/src/servo/servo_control.h`](../firmware/esp32/src/servo/servo_control.h)

| GPIO | 功能 | 模块 | 方向 | 备注 |
|------|------|------|------|------|
| GPIO0 | BOOT / Config Mode 入口 | 板载按键 | 输入 | strapping pin，长按 3s 进 Config Mode |
| GPIO1 | MAX9814 Out | 麦克风 | 输入(ADC1_CH0) | `MIC_GPIO`，去直流偏置 |
| GPIO4 | MG90S Pan (Horizontal) 控制信号 | 舵机 | 输出(LEDC ch1 PWM) | `SERVO_H_SIGNAL_GPIO`，50Hz，左右 |
| GPIO5 | MG90S Tilt (Vertical) 控制信号 | 舵机 | 输出(LEDC ch0 PWM) | `SERVO_V_SIGNAL_GPIO`，50Hz，上下 |
| GPIO10 | LCD CS | ST7735S | 输出 | `LCD_CS_GPIO`，4-line SPI CS |
| GPIO11 | LCD DC | ST7735S | 输出 | `LCD_DC_GPIO`，0=Command, 1=Data |
| GPIO12 | LCD RES | ST7735S | 输出 | `LCD_RST_GPIO`，低电平复位 |
| GPIO13 | LCD SDA (MOSI) | ST7735S | 输出 | `LCD_MOSI_GPIO`，SPI MOSI |
| GPIO14 | LCD SCL | ST7735S | 输出 | `LCD_SCLK_GPIO`，SPI SCLK，20 MHz |
| GPIO15 | I2S DIN | MAX98357A | 输出 | `I2S_DIN`，音频数据 |
| GPIO16 | I2S BCLK | MAX98357A | 输出 | `I2S_BCLK`，位时钟 |
| GPIO17 | I2S LRCLK | MAX98357A | 输出 | `I2S_LRC`，左右声道时钟 |
| GPIO19 | USB D- | USB-C | — | 烧录/日志/回退，勿占用 |
| GPIO20 | USB D+ | USB-C | — | 烧录/日志/回退，勿占用 |
| GPIO34~37 | Flash / PSRAM | 板载 | — | 不可用 |

### 2.1 禁用 / 避开引脚

- strapping pins：GPIO0、GPIO3、GPIO45、GPIO46（影响启动模式）
- USB D-/D+：GPIO19、GPIO20
- Flash / PSRAM：GPIO34~GPIO37
- ADC2：Wi-Fi 启用时被占用，只能用 ADC1（GPIO1~GPIO9）

### 2.2 LCD 引脚分配（已接入 · 2026-09-27）

LCD ST7735S 需要 5 根信号线：SCL、SDA、RES、DC、CS。最终分配：

| LCD 引脚 | GPIO | 备注 |
|----------|------|------|
| SCL | GPIO14 | SPI SCLK，20 MHz |
| SDA | GPIO13 | SPI MOSI |
| RES | GPIO12 | Reset（低电平） |
| DC  | GPIO11 | Data/Command |
| CS  | GPIO10 | Chip Select |
| BLK | 3.3V   | 常亮，不用 PWM 调光 |

> GPIO 8~14 全部空闲（Phase 1 未占用 GPIO 8 及以上）。选定 GPIO10~GPIO14 这 5 个连续 IO，
> 便于杜邦线成组接线；不与 I2S（GPIO25/26/27）/ 舵机（GPIO4/5）/ USB（GPIO19/20）/ ADC1（GPIO1）冲突。
> 相对原 GPIO8~12 分配，整体上移 2 位，为未来扩展（如 OLED、SD 卡 SPI 共享等）留出 GPIO8/9 窗口。

---

## 3. LCD - 0.96inch IPS Module (ST7735S)

### 3.1 基本信息

| 参数 | 值 |
|------|----|
| 尺寸 | 0.96 inch |
| 驱动 IC | ST7735S |
| 分辨率 | 160 × 80 Pixel |
| 显示类型 | IPS，全视角 |
| 接口 | 4-line SPI |
| 触摸 | 无 |
| VCC | 3.3V |
| 背光 BLK | 高电平点亮 |
| 背光电流 | 约 23.4 mA |
| 资料 | [LCDwiki - 0.96inch IPS Module](https://www.lcdwiki.com/zh/0.96inch_IPS_Module) |

### 3.2 引脚定义

| LCD 引脚 | 功能 | 电平/说明 |
|----------|------|-----------|
| GND | 地 | — |
| VCC | 电源 | **3.3V，禁止接 5V** |
| SCL | SPI Clock | SPI SCLK |
| SDA | SPI MOSI | 写数据 |
| RES | Reset | 低电平复位 |
| DC | Data/Command | 0=Command, 1=Data |
| CS | Chip Select | 低电平使能 |
| BLK | Backlight | 高电平点亮 |

### 3.3 接线注意事项

1. **VCC 必须接 3.3V**，接 5V 可能损坏 LCD。
2. BLK 可直接接 3.3V 常亮，或接 GPIO 用 PWM 调光（第一版建议常亮）。
3. RES 可接 GPIO 做硬件复位，也可接 RC 复位电路；第一版建议接 GPIO 以便初始化。
4. SPI 速率建议先用 20~40 MHz，稳定后再提高。
5. LCD 模块独立实现于 `firmware/esp32/src/display/`，不把显示代码混入录音/VAD/Wi-Fi。

#### 3.3.1 3.3V 电源分配（3V3 pin 不够怎么办）

**核心事实**：ESP32-S3 板上所有 `3V3` 引脚在电气上是**同一根电源轨**，均由板载 LDO（通常 AMS1117-3.3 或 AP2112K）供电，额定输出 800 mA ~ 1 A。因此"接哪个 3V3 pin"本质是同一件事，多个设备**并联**到同一 3V3 pin / 同一根 3V3 走线上是正常做法。

**电流预算（本项目）**：

| 设备 | 典型电流 |
|------|---------|
| MAX98357A（3W 功放，idle） | 20–80 mA |
| MAX9814（MEMS 麦克风） | ~5 mA |
| ST7735S LCD（含背光常亮） | 25–40 mA |
| ESP32-S3 本体 | 80–150 mA |
| **合计** | **~130–260 mA** |

LDO 额定 800 mA，余量 > 3×。

**三种可选方案**：

- **方案 A · 并联到已有 3V3 电源轨（推荐）**
  LCD VCC + BLK 直接并联到 MAX9814 VCC 焊点 / MAX98357A VIN 焊点旁的同一 3V3 走线上，GND 并联。这是最省事的做法，电气上就是所有 3.3V 设备的常规并行供电。
- **方案 B · 从 5V pin 加一颗 LDO**
  ESP32-S3 板的 `5V` pin → AP2112K-3.3 / AMS1117-3.3 → LCD VCC / BLK。电流独立，不干扰主 3V3 轨。需要多一颗 LDO 器件。
- **方案 C · 借 `VDD3P3_OUT`（GPIO19）**
  ESP32-S3 有专门的 `VDD3P3_OUT`（多数板作为 GPIO19），电气上是 3.3V 电源轨。但**默认避开使用**（避免 GPIO 冲突）；只有在其他方案都不便时才用。

**不推荐做法**：直接把 LCD VCC 接到 USB 5V 或 5V pin（ST7735S 是 3.3V 器件，接 5V 会**烧屏**）。

**本项目采用**：方案 A（LCD VCC / BLK 并联到 MAX9814 的 VCC 焊点）。

### 3.4 固件模块（已实现 · 2026-09-27 LCD MVP）

```
firmware/esp32/src/display/
├── display.h
└── display.cpp
```

依赖：`adafruit/Adafruit GFX Library@^1.12` + `adafruit/Adafruit ST7735 and ST7789 Library@^1.11.0`（见 [`platformio.ini`](../firmware/esp32/platformio.ini)）。

公开 API：
- `display_init()` — 初始化 SPI 与 ST7735S（`initR(INITR_GREENTAB)`，160×80 tab，20 MHz）
- `display_clear()` — 清屏
- `display_show_text(const char*)` — 顶部 ASCII 文本
- `display_show_face(const char*)` — 5 种表情（normal/listening/thinking/speaking/error）
- `display_set_state(const char*)` — 8 种状态（STARTING/WIFI/READY/LISTENING/THINKING/SPEAKING/SERVO/ERROR）；相同状态 no-op

关键设计：
- 未 ready 时所有 API no-op（不阻塞、不 panic、不 while）
- 状态改变才重绘（`strcmp` 去重）
- 不暴露 Adafruit_GFX / ST7735 内部类型
- **Display failure must not block Robot Core**

---

## 4. MG90S 舵机

### 4.1 资料来源

- 用户提供的本地规格书：`MG90S铁.doc`
- 提取脚本：[`scripts/extract_mg90s_doc.py`](../scripts/extract_mg90s_doc.py)
- 规格书编号：`190322-P-0090-MM-4PS`
- 产品名称：伺服器 Analog Servo（9克金属齿模拟舵机）
- 型号：P-0090-MM

### 4.2 电气特性（来自规格书）

| 项目 | 5.0V | 6.0V |
|------|------|------|
| 空载转速 Operating speed (at no load) | 0.11 ± 0.01 sec/60° | 0.11 ± 0.01 sec/60° |
| 空载电流 Running current (at no load) | 350 ± 10 mA | 400 ± 10 mA |
| 停止扭力 Stall torque (at locked) | 2.2 ± 0.01 kg·cm | 2.5 ± 0.1 kg·cm |
| 堵转电流 Stall current (at locked) | 1000 ± 30 mA | 1200 ± 30 mA |
| 待机电流 Idle current (at stopped) | 10 ± 5 mA | 10 ± 5 mA |

> 注：规格书原文 5.0V 列空载转速为 `0.13 ± 0.01 sec/60`，6.0V 列为 `0.11 ± 0.01 sec/60`。
> 上表 5.0V 转速按原文保留为 `0.13 ± 0.01 sec/60°`，请以实际测试为准。

修正表：

| 项目 | 5.0V | 6.0V |
|------|------|------|
| 空载转速 | 0.13 ± 0.01 sec/60° | 0.11 ± 0.01 sec/60° |

### 4.3 控制特性（来自规格书）

| 项目 | 规格 |
|------|------|
| 控制系统 | 改变脉冲宽度（Change the pulse width） |
| 放大器种类 | 数字控制器 Digital controller |
| 操作角度 Operating travel | 180° ± 8°（在 500~2500 µs） |
| 中立位置 Neutral position | 1500 µs |
| 脉波宽度范围 Pulse width range | 500 ~ 2500 µs |
| 脉波讯号虚位 Dead band width | 6 µs |
| 旋转方向 | 逆时针 Counter Clockwise（在 500~2500 µs） |
| 可动作角度范围 Maximum travel | 约 180°（在 500~2500 µs） |

### 4.4 机械特性（来自规格书）

| 项目 | 规格 |
|------|------|
| 机构极限角度 Limit angle | 230° |
| 重量 Weight | 12g ± 2g（不含舵臂） |
| 导线长度 | 230 ± 5 mm |
| 舵臂规格 Horn gear spline | 20T |
| 齿轮虚位 excessive play | 1 |

### 4.5 使用环境（来自规格书）

| 项目 | 规格 |
|------|------|
| 保存温度 | -20 ~ 60 °C |
| 操作温度 | -10 ~ 50 °C |
| 操作电压 | 4.8 ~ 6.0 V |

### 4.6 PWM 参数推导

根据规格书控制特性：

| 参数 | 值 | 说明 |
|------|----|------|
| PWM 频率 | 50 Hz（周期 20 ms） | 标准舵机频率 |
| 脉宽范围 | 500 ~ 2500 µs | 规格书明确 |
| 中立脉宽 | 1500 µs | 规格书明确 |
| 角度范围 | 约 180°（500~2500 µs） | 规格书明确 |
| 推荐工作电压 | 5.0V 或 6.0V | 不要用 3.3V 驱动 |

### 4.7 接线与供电规范

1. **舵机电源禁止直接从 ESP32 GPIO 或 3.3V LDO 取电**。
   - 堵转电流可达 1000~1200 mA，会拉垮 ESP32 电源导致复位。
2. **舵机电源使用独立 5V（或 6V）电源**（双舵机同时堵转时更需独立供电）。
3. **舵机地线必须与 ESP32 共地**，否则控制信号不稳定。
4. **控制信号 GPIO（双舵机）**：
   - `SERVO_H_SIGNAL_GPIO = 4`（Pan / 左右，LEDC channel 1）
   - `SERVO_V_SIGNAL_GPIO = 5`（Tilt / 上下，LEDC channel 0）
   - 定义位置：[`servo_control.h`](../firmware/esp32/src/servo/servo_control.h)
5. 控制信号线建议串接 220Ω~1kΩ 电阻，抑制瞬态电流。

### 4.8 固件现状

- 现有模块：[`firmware/esp32/src/servo/servo_control.cpp`](../firmware/esp32/src/servo/servo_control.cpp)
- 当前参数（双舵机）：
  - `SERVO_V_SIGNAL_GPIO = 5`（Tilt / Vertical / 上下）
  - `SERVO_H_SIGNAL_GPIO = 4`（Pan / Horizontal / 左右）
  - `SERVO_V_PWM_CHANNEL = 0` / `SERVO_H_PWM_CHANNEL = 1`
  - `SERVO_PWM_FREQ_HZ = 50`
  - `SERVO_PWM_RES_BITS = 13`
  - `SERVO_CENTER_ANGLE = 90`
  - `SERVO_MIN_ANGLE = 60` / `SERVO_MAX_ANGLE = 120`
  - `SERVO_STEP_DEG = 10`
  - 向后兼容别名：`SERVO_LEFT_ANGLE = SERVO_MIN_ANGLE`，`SERVO_RIGHT_ANGLE = SERVO_MAX_ANGLE`
- 当前实现使用保守角度范围（60~120°），未使用全 180°。
- 初始化序列：`servo_init()` → `servo_center_all()`（Pan、Tilt 均回中）→ READY；不再执行自测序列。
- 状态管理：**PC 端无状态，ESP32 保留 `s_verticalAngle` / `s_horizontalAngle`**。
- 语音命令映射：见 [`docs/protocol.md`](protocol.md) §4.3 与 [`pc/command_router.py`](../pc/command_router.py)。
- 后续 MG90S 状态联动阶段再根据规格书扩展角度映射。

---

## 5. 电源规范

| 模块 | 电压 | 供电来源 | 备注 |
|------|------|----------|------|
| ESP32-S3 | 3.3V / 5V(VIN) | USB 或独立 5V | — |
| MAX98357A | 5V (VIN) | ESP32 VIN 或独立 5V | 峰值 300~500mA |
| MAX9814 | 3.3V | ESP32 3.3V | 与 ESP32 同源 |
| LCD ST7735S | 3.3V | ESP32 3.3V | 禁止 5V |
| MG90S | 5V 或 6V | **独立电源** | 堵转可达 1.2A |

> 所有模块 GND 必须共地。

---

## 6. 版本

| 版本 | 日期 | 变更 |
|------|------|------|
| v1 | 2026-09-26 | 新建：整合 LCD ST7735S、MG90S 规格书参数、GPIO 占用表、电源规范 |
| v2 | 2026-09-27 | 双舵机（Pan=GPIO4/Tilt=GPIO5）；从 LCD 候选中移除 GPIO4；补充 SERVO_H/V_* 常量、Step=10°、初始化回中、状态归属 ESP32 |
