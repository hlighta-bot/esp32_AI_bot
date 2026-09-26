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
| LCD | 0.96inch IPS Module | 状态显示 / 表情交互 | 待接入 |
| 舵机 | MG90S（金属齿，9g） | Pan/Tilt 或动作反馈 | 待接入 |

---

## 2. GPIO 占用表（ESP32-S3 N16R8）

> **新增外设前必须先查此表，避免引脚冲突。**
> 来源：[`firmware/esp32/src/main.cpp`](../firmware/esp32/src/main.cpp)、[`firmware/esp32/src/servo/servo_control.h`](../firmware/esp32/src/servo/servo_control.h)

| GPIO | 功能 | 模块 | 方向 | 备注 |
|------|------|------|------|------|
| GPIO0 | BOOT / Config Mode 入口 | 板载按键 | 输入 | strapping pin，长按 3s 进 Config Mode |
| GPIO1 | MAX9814 Out | 麦克风 | 输入(ADC1_CH0) | `MIC_GPIO`，去直流偏置 |
| GPIO5 | MG90S 控制信号 | 舵机 | 输出(LEDC PWM) | `SERVO_SIGNAL_GPIO`，50Hz |
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

### 2.2 LCD 候选引脚（待分配）

LCD ST7735S 需要 5 根信号线：SCL、SDA、RES、DC、CS。

候选范围（避开上表已占用与禁用引脚）：
- GPIO2、GPIO4、GPIO6、GPIO7、GPIO8、GPIO9、GPIO10~GPIO14、GPIO18、GPIO21、GPIO38~GPIO42

> **注意**：GPIO2 在某些 ESP32-S3 模组上连接板载 LED，需确认是否冲突。
> 最终分配在 LCD 驱动实现时确定，并回填到上表。

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

### 3.4 固件模块规划（待实现）

```
firmware/esp32/src/display/
├── display.h
└── display.cpp
```

第一阶段接口：
- `display_init()`
- `display_clear()`
- `display_show_text()`
- `display_show_face()`
- `display_set_state()`

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
2. **舵机电源使用独立 5V（或 6V）电源**。
3. **舵机地线必须与 ESP32 共地**，否则控制信号不稳定。
4. **控制信号 GPIO**：当前固件使用 `GPIO5`（[`servo_control.h`](../firmware/esp32/src/servo/servo_control.h)）。
5. 控制信号线建议串接 220Ω~1kΩ 电阻，抑制瞬态电流。

### 4.8 固件现状

- 现有模块：[`firmware/esp32/src/servo/servo_control.cpp`](../firmware/esp32/src/servo/servo_control.cpp)
- 当前参数：
  - `SERVO_SIGNAL_GPIO = 5`
  - `SERVO_PWM_FREQ_HZ = 50`
  - `SERVO_PWM_RES_BITS = 13`
  - `SERVO_CENTER_ANGLE = 90`
  - `SERVO_LEFT_ANGLE = 60`
  - `SERVO_RIGHT_ANGLE = 120`
- 当前实现使用保守角度范围（60~120°），未使用全 180°。
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
