# Step 12 · 视觉 + Pan/Tilt 硬件调查与实施计划

> 说明：本文档只做**硬件调查 + 软件方案分析 + 分阶段实施计划**，**不包含任何代码修改**。
> 现有工程（语音 / TCP / Wi-Fi / Web / VAD / BLE / Gateway）不动。

- 生成时间：2026-09-16 12:36 JST
- 目标：ESP-CAM + OV2640 检测人物 → UART → ESP32-S3 → Pan/Tilt 舵机 → 机器人看向人
- 第一阶段能力：仅输出 `person_detected / center_x / center_y [/ width / height / confidence]`，不做人脸识别 / ReID / 云端 / 复杂跟踪

---

## 第一阶段：硬件调查（ESP32-CAM + OV2640）

### 1.1 具体芯片型号

淘宝上的「ESP32-CAM 开发板 + OV2640」绝大多数是以下规格之一：

| 型号 | 主芯片 | Flash | PSRAM | 备注 |
|---|---|---|---|---|
| ESP32-CAM v1.0 (Espressif 官方) | **ESP32-WROOM-32** (经典 ESP32, Xtensa dual-core) | 4 MB | **无** | 常见版本 A |
| 山寨 ESP32-CAM | **ESP32-WROOM-32D / 32E** (经典 ESP32) | 2~4 MB | 无 | 常见版本 B |

**结论**：
- 主芯片 = **经典 ESP32 (ESP32-WROOM-32)**，**不是 ESP32-S3**
- 单双核 240 MHz，Xtensa LX6
- 只有内部 SRAM ≈ **320 KB**，**没有 PSRAM**
- 4 MB Flash 上限（部分版本 2 MB）
- **这一条决定了后续人物检测方案的天花板**，见第二阶段

### 1.2 OV2640 接口方式

- OV2640 通过 **CSI (Camera Serial Interface)** 直连 ESP32-CAM 主芯片
- 是 **8 位并行数据总线** + 4 条控制线 (VSYNC / HREF / XCLK / PCLK)
- 走 ESP32 内部的 **Camera peripheral**，不是 UART / I2C / SPI
- 通过 I2C 只做初始化 (寄存器配置，SCCB 接口)
- 输出格式：RGB565 / JPEG / YUV422 / Grayscale8 等，JPEG 是常见压缩输出

**关键点**：OV2640 是直连主控的摄像头，**不能被 ESP32-S3 直接访问**；ESP32-S3 只能通过 UART 拿到「ESP32-CAM 处理后的结果」或「JPEG 帧」。

### 1.3 摄像头占用的 GPIO（ESP32-CAM 官方接线）

来源：Espressif 官方 ESP32-CAM schematic（v1.0 / v1.1 一致）

| GPIO | 用途 | 备注 |
|---|---|---|
| GPIO 5 | CAM_CTRL_CLK | XCLK |
| GPIO 4 | CAM_SIO_C | SCCB/I2C SCL |
| GPIO 6 | CAM_SIO_D | SCCB/I2C SDA |
| GPIO 7 | CAM_DATA_D7 | D7 |
| GPIO 8 | CAM_DATA_D6 | D6 |
| GPIO 9 | CAM_DATA_D5 | D5 |
| GPIO 10 | CAM_DATA_D4 | D4 |
| GPIO 11 | CAM_DATA_D3 | D3 |
| GPIO 12 | CAM_DATA_D2 | D2 |
| GPIO 13 | CAM_DATA_D1 | D1 |
| GPIO 14 | CAM_DATA_D0 | D0 |
| GPIO 15 | CAM_VSYNC | VSYNC |
| GPIO 16 | CAM_HREF | HREF |
| GPIO 17 | CAM_PCLK | PCLK |
| GPIO 3 | CAM_POWER_DOWN | 关摄像头 |
| GPIO 0 | CAM_RESET | 摄像头复位 |
| GPIO 2 | LED | 板载 LED |
| GPIO 34/35 | (未使用) | 输入专用，可用于触摸 |

**摄像头 + 电源 + LED 占了：GPIO 0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17**

### 1.4 ESP32-CAM 剩余可用 GPIO

经典 ESP32 总共 34 个 GPIO (GPIO 0~33, 有 1 个不存在 = GPIO 3 是复位，实际可用 34 个)。ESP32-CAM 板占用后剩余：

| GPIO | 用途 | 说明 |
|---|---|---|
| GPIO 18 | **UART0_TXD** | 调试串口 TX |
| GPIO 21 | **UART0_RXD** | 调试串口 RX |
| GPIO 19 | **UART1_TXD** | UART1 TX（**推荐做 ESP-CAM→S3 数据 TX**） |
| GPIO 20 | **UART1_RXD** | UART1 RX |
| GPIO 22 | (空闲) | 可用 |
| GPIO 23 | (空闲) | 可用 |
| GPIO 25 | SPI_MISO | 备用 SPI |
| GPIO 26 | SPI_MOSI | 备用 SPI |
| GPIO 27 | SPI_CLK | 备用 SPI |
| GPIO 32 | ADC1_CH6 | ADC 输入 |
| GPIO 33 | ADC1_CH7 | ADC 输入 |
| GPIO 34 | (触摸) | **input-only** |
| GPIO 35 | (触摸) | **input-only** |
| GPIO 36 | (触摸) | **input-only** |
| GPIO 39 | (触摸) | **input-only** |

**结论**：ESP32-CAM 剩余 4~8 个双向 GPIO + 4 个 input-only GPIO，足够接 UART1 (GPIO 19/20) 到 ESP32-S3。

### 1.5 ESP32-CAM ↔ ESP32-S3 UART 是否可行

**可行**。理由：

1. UART 是物理上最通用的外设，两边都原生支持
2. ESP32-CAM 板已有 UART1 (GPIO 19/20) 空闲
3. ESP32-S3 有多路 UART 可用（UART1/UART2），GPIO 10/11 / 17/18 都空闲（见第三阶段）
4. 波特率 115200 / 230400 都跑得动，字符帧短（<20 字节），几乎不可能丢
5. 调试友好：串口监视器可直接观察

### 1.6 接线方案

```
ESP32-CAM GPIO 19 (UART1_TXD)  →  ESP32-S3 UART1_RXD (GPIO 10)
ESP32-CAM GPIO 20 (UART1_RXD)  →  ESP32-S3 UART1_TXD (GPIO 11)
ESP32-CAM GND                  →  ESP32-S3 GND   ← 必须共地
ESP32-CAM 3V3 (可选)           →  ESP32-S3 3V3  ← 可选，用于电平确认
```

> 说明：
> - UART 是差分容忍的（3.3V TTL），共地即可
> - ESP32-CAM 板通常只有一路 UART 引出到板上的 USB-UART 桥；如果没引出，就要焊接 2 根杜邦线从板上的 GPIO 19/20 焊盘引出

### 1.7 电平兼容性

| 器件 | TX 高电平 | 逻辑 |
|---|---|---|
| ESP32-CAM (经典 ESP32) | 3.3 V | 3.3 V TTL |
| ESP32-S3 N16R8 | 3.3 V | 3.3 V TTL |

**结论**：两边都是 3.3 V TTL UART，**直连安全**，不需要电平转换芯片，不需要 MAX232/RS485 收发器。

### 1.8 推荐 UART GPIO

| 方向 | ESP32-CAM | ESP32-S3 | 说明 |
|---|---|---|---|
| ESP-CAM 数据 → ESP-S3 | **GPIO 19** (UART1_TXD) | **GPIO 10** (UART1_RXD) | 主数据方向 |
| ESP-S3 命令 → ESP-CAM | **GPIO 11** (UART1_TXD) | **GPIO 12** (UART1_RXD) | 可选，用于远程复位 / 调参 |
| 波特率 | 115200 | 115200 | 保守值，长距离也可用 |

**为什么不用 GPIO 17/18 (UART2 默认)**：ESP32-S3 当前已经用了 GPIO 16/17 做 I2S (BCLK / LRC)，GPIO 17 冲突。用 UART1 (10/11) 最干净。

---

## 第二阶段：人物检测方案调查

**核心约束**：ESP32-CAM 芯片 = 经典 ESP32-WROOM-32，**320 KB SRAM，无 PSRAM**，4 MB Flash，240 MHz。

这一条限制非常关键：

- OV2640 一帧 QVGA (320×240) RGB565 = 320 × 240 × 2 = **150 KB**
- 一帧 VGA (640×480) RGB565 = **600 KB** → 完全放不下
- 一帧 JPEG 压缩后 (VGA, 中质量) ≈ 15~30 KB，可以放
- **任何深度学习目标检测（YOLOv5n, MobileNet-SSD）都需要 PSRAM + TFLite Micro**，经典 ESP32 直接跑不了

所以「ESP32-CAM 本地跑深度学习」这条路基本走不通（除非改用 ESP32-S3-CAM / AI-Thinker 板）。

### 2.1 方案对比

| 方案 | 描述 | 可行性 | FPS 预期 | 延迟 | 功耗 | 实现复杂度 | 推荐 |
|---|---|---|---|---|---|---|---|
| **A** | ESP32-CAM 本地深度学习 (YOLOv5n-TFLite) | ❌ 无 PSRAM，跑不了 | - | - | - | - | **不推荐** |
| **B** | ESP32-CAM 本地简单视觉算法（运动检测 / 肤色 / 轮廓） | ✅ 完全可行 | 10~15 fps | < 100 ms | 中 | 中 | ⭐ **首选** |
| **C** | ESP32-CAM 把 JPEG 帧发给 ESP32-S3，ESP32-S3 本地跑 | ❌ ESP32-S3 也没 PSRAM 装大模型；且 S3 已经在跑语音，CPU 忙 | - | - | - | - | **不推荐** |
| **D** | ESP32-CAM 把 JPEG 帧发给 PC / aidlux，PC 做检测，再发回坐标 | ✅ 硬件可行，需要额外网络 | 5~10 fps | 200~500 ms | 高 | 高 | 备选 |
| **E** | 换板子：ESP32-S3-CAM 或 AI-Thinker ESP32-S3 (带 8MB PSRAM) | ✅ 硬件级最优 | 15~30 fps | < 100 ms | 中 | 中 | 如果预算允许，**最优** |

### 2.2 推荐方案：B（ESP32-CAM 本地简单视觉算法）

**具体算法选择**（按推荐度排序）：

1. **背景差分 + 最大连通域**（背景 = 开机时自动采集，或用「上一次帧」的滑动平均作为背景）
   - 优点：几乎零额外内存，30 fps 轻松，实现 100 行代码以内
   - 缺点：对静态人物不敏感（人站着不动就「消失」）；对光照变化敏感
   - 适用场景：机器人看到「有人出现 / 在动」就转头

2. **肤色检测 + 最大连通域**（在 YCrCb 或 HSV 空间提取肤色像素）
   - 优点：对静态人物也有效；不依赖背景
   - 缺点：暗光失效；非肤色人物不识别；实现复杂度中等
   - 混合方案：把 (1) 与 (2) 结果做 OR 融合

3. **边缘密度 + 头部轮廓启发式**
   - 用 Sobel 边缘检测，在画面中央 2/3 区域找「上小下大的椭圆候选」
   - 优点：对姿态有一定鲁棒性
   - 缺点：调参难，误报多

**最终推荐**：**方案 2 = 肤色检测 + 最大连通域**，理由：

- 静态人物也 OK
- 不需要背景校准
- 用经典 ESP32 也跑得动（QVGA 分辨率下 ~15 fps 肤色提取，然后连通域标记 <5 ms）
- 代码量约 200~300 行 C++
- 误报通过阈值 + 最小面积 + 位置过滤能压下来

**分辨率建议**：
- 输入：`FRAME_SIZE_QVGA` (320×240) 或 `FRAME_SIZE_UQQVGA` (160×120)
- 输出：归一化坐标 (0.0 ~ 1.0)
- 使用 JPEG 输出给调试串口 / 调试 Wi-Fi（可选）
- 检测只用 QVGA RGB565（内部处理）

### 2.3 明确不做的东西

- ❌ 不加载神经网络（ESP32-CAM 硬件不支持）
- ❌ 不做人脸识别 / 身份识别 / ReID
- ❌ 不依赖云端
- ❌ 不做多人复杂跟踪（只输出「最显著一个人」）
- ❌ 不做行为识别（走路 / 挥手 / 静止）
- ❌ 不做姿态估计 / 关键点检测

### 2.4 输出接口

ESP32-CAM 每次检测到一个人，就把结果通过 UART 发给 ESP32-S3：

```
PERSON,1,0.63,0.48,0.30,0.55\n
```

**字段含义**（与第四阶段协议一致）：

| 字段 | 类型 | 范围 | 说明 |
|---|---|---|---|
| 命令 | 字符串 | `PERSON` | 固定前缀，防止误识别 |
| detected | 0 / 1 | - | 1 = 检测到，0 = 没检测到 |
| center_x | 浮点 | 0.0~1.0 | 目标水平中心，0.5 = 画面中央 |
| center_y | 浮点 | 0.0~1.0 | 目标垂直中心，0.5 = 画面中央 |
| width | 浮点 | 0.0~1.0 | 目标框归一化宽度 |
| height | 浮点 | 0.0~1.0 | 目标框归一化高度 |

**帧率建议**：10 Hz（100 ms 一帧）。原因：
- 舵机响应慢（约 200~500 ms 才能转到目标角度），10 Hz 足够
- UART 带宽绰绰有余
- CPU / 功耗都能留出余量
- 10 Hz 也方便串口监视器肉眼观察

---

## 第三阶段：ESP32-S3 舵机控制调查

### 3.1 当前 ESP32-S3 GPIO 使用（已确认）

来源：[`firmware/esp32/src/main.cpp`](../firmware/esp32/src/main.cpp) + [`docs/hardware.md`](hardware.md)

| GPIO | 用途 | 定义位置 | 备注 |
|---|---|---|---|
| GPIO 0 | BOOT 按键（进入 Config Mode） | `CONFIG_MODE_BUTTON_GPIO` | 内部上拉 |
| GPIO 1 | MAX9814 麦克风 ADC1_CH0 | `MIC_GPIO` | Wi-Fi 变体专用 |
| GPIO 15 | I2S DIN → MAX98357A | `I2S_DIN` | 音频输出 |
| GPIO 16 | I2S BCLK | `I2S_BCLK` | 音频时钟 |
| GPIO 17 | I2S LRC / LRCLK | `I2S_LRC` | 音频时钟 |
| GPIO 19/20 | USB-CDC (禁用) | `ARDUINO_USB_CDC_ON_BOOT=0` | 已禁用，可用 |
| GPIO 43/44 | UART0（Serial 921600） | Arduino 默认 | 调试串口 |

### 3.2 GPIO 冲突分析

- GPIO 17 已经被 I2S 占用 → **UART2 默认 (17/18) 冲突**，不能用 UART2 默认引脚
- GPIO 0/1/15/16/17 已被占用
- GPIO 19/20 已释放（USB-CDC 关闭）
- GPIO 33/36/37 用于 Flash/PSRAM（QIO OPI），**不可用**
- 其他 GPIO (4~14, 18, 21, 26, 27, 38, 42) 全部空闲

### 3.3 Pan/Tilt GPIO 推荐

**推荐分配**：

| 功能 | GPIO | 备注 |
|---|---|---|
| Pan Servo PWM | **GPIO 4** | 空闲，远离 Flash/PSRAM，无冲突 |
| Tilt Servo PWM | **GPIO 5** | 空闲，紧邻 GPIO 4，方便走线 |
| UART1_RX (ESP-CAM 数据) | **GPIO 10** | 空闲，UART1 默认 |
| UART1_TX (命令给 ESP-CAM) | **GPIO 11** | 空闲，UART1 默认 |

**理由**：
- GPIO 4/5 位置在 ESP32-S3 板子两侧对称，走线干净
- 远离 I2S / ADC / USB / UART0，避免数字/模拟干扰
- GPIO 10/11 是 UART1 默认分配，无需 `UART.begin(baud, SERIAL_TX_ONLY, rx, tx)` 手动映射

### 3.4 PWM 方案

Arduino-ESP32 framework 已内置 **`ESP32Servo.h`**，是 ESP32 平台上最主流的舵机库。

**不需要新增库**：
- `ESP32Servo.h` 已经在 Arduino-ESP32 3.x framework 中
- 基于 ESP32 的 LEDC PWM 硬件（`ledcSetup` / `ledcAttachPin`）
- 每个舵机占一个 LEDC channel
- 频率默认 50 Hz，占空比 0.5%~2.5% 对应 0°~180°

**可选替代**：
- `Adafruit_Servo_Library`：功能更多（支持 8 个舵机、可变频率），本项目用不到
- 手写 LEDC：不推荐，`ESP32Servo.h` 就是官方推荐

### 3.5 是否需要新增 lib_deps

**当前** [`firmware/esp32/platformio.ini`](../firmware/esp32/platformio.ini) line 37~39 只有两行注释掉的库。实际运行时不需要额外依赖，因为 `ESP32Servo.h` 是 framework 内置。

**建议**：在真正实施 Pan/Tilt 时，**无需修改 `lib_deps`**。只需要在源码 `#include <ESP32Servo.h>` 即可。

### 3.6 舵机供电

**强警告**：舵机不能用 ESP32 GPIO 直接供电，也不能用 ESP32 板上的 5V 直接供电。

**推荐拓扑**：

```
[ 独立 5V/3A USB 电源或 7.4V 锂电 + 5V LDO ]
   │
   ├─→ 5V ─→ Pan Servo V+
   ├─→ 5V ─→ Tilt Servo V+
   └─→ GND ─┐
             │
             ├─→ ESP32-S3 GND   ← 必须共地
             │
   ESP32-S3 GPIO 4 ──→ Pan Servo SIGNAL
   ESP32-S3 GPIO 5 ──→ Tilt Servo SIGNAL
```

**关键点**：
1. **舵机电源独立**：5V 3A 以上电源，或用 3~4 节锂电池供电
2. **共地**：舵机电源 GND 必须连到 ESP32-S3 GND，否则 PWM 无法识别
3. **SIGNAL 用 ESP32-S3 GPIO 直接驱动**：舵机信号线只吃逻辑电平，不吸电流
4. **加 100~470 µF 电解电容**在舵机电源 V+ / GND 之间，防止瞬时电流过大拉低电压导致 ESP32-S3 重启
5. **PWM 频率**：默认 50 Hz 即可，舵机标准周期

### 3.7 ESP32-S3 上舵机抖动 & 电源塌陷风险

- 每次舵机启动/停止会有 100~300 mA 尖峰电流
- 如果舵机电源 < 2A，容易把 5V 拉低到 4.2 V 以下，触发 ESP32-S3 复位
- **缓解方案**：独立供电 + 共地 + 大电容 + 舵机平滑运动（`Servo.writeMicroseconds` 分步走，不是一步到位）

---

## 第四阶段：通信协议设计

### 4.1 最终协议（推荐）

```
PERSON,<detected>,<center_x>,<center_y>,<width>,<height>,<confidence>\r\n
```

**字段说明**：

| 序号 | 字段 | 类型 | 范围 / 值 | 是否必填 |
|---|---|---|---|---|
| 1 | 前缀 | 字符串 | 固定 `PERSON` | 必填 |
| 2 | detected | 整数 | `0` 或 `1` | 必填 |
| 3 | center_x | 浮点 | `0.000` ~ `1.000`，三位小数 | 检测到时必填 |
| 4 | center_y | 浮点 | `0.000` ~ `1.000` | 检测到时必填 |
| 5 | width | 浮点 | `0.000` ~ `1.000` | 检测到时必填 |
| 6 | height | 浮点 | `0.000` ~ `1.000` | 检测到时必填 |
| 7 | confidence | 浮点 | `0.000` ~ `1.000` | 检测到时必填 |
| - | 结束符 | 字符 | `\r\n` | 必填 |

**示例**：

- 有人，画面中央偏右，占画面 30%×55%：
  ```
  PERSON,1,0.630,0.480,0.300,0.550,0.82\r\n
  ```
- 无人：
  ```
  PERSON,0\r\n
  ```
- ESP32-S3 命令（ESP-S3 → ESP-CAM，可选，用于调参）：
  ```
  CFG,BAUD,115200\r\n
  CFG,SLEEP,1\r\n
  ```

### 4.2 为什么选这个协议

| 属性 | 说明 |
|---|---|
| **简单** | 一行文本，逗号分隔，无嵌套 |
| **易调试** | 串口监视器直接看，不需要工具解码 |
| **不需要 JSON** | 省掉解析开销（ESP32-CAM 端 CPU 宝贵） |
| **有换行结束符** | `\r\n`，标准 TCP / UART 行分隔 |
| **有固定前缀** | `PERSON,` 前缀让 ESP32-S3 能过滤误码 |
| **能处理异常** | ESP32-S3 端做状态机：逐字符读、按 `\n` 切行、`sscanf` 解析；解析失败就丢这一行 |
| **不会 crash ESP32-S3** | 用 `String` + `split(',')` 解析，任何长度、任何字符都能处理，不会数组越界 |

### 4.3 ESP32-S3 接收侧的健壮性设计

**必须遵守的规则**：

1. **逐字节读 UART，不用 `Serial.read()` 阻塞等长度**
2. **累积到 buffer，遇到 `\n` 才认为一行完整**
3. **buffer 上限**：例如 64 字节；超过就丢弃
4. **只接受以 `PERSON,` 或 `CFG,` 开头的行**
5. **字段数校验**：
   - `PERSON,0` → 只允许 2 个字段
   - `PERSON,1,...` → 必须 7 个字段
6. **数值范围校验**：
   - detected ∈ {0, 1}
   - 所有浮点 ∈ [0.0, 1.0]
7. **解析失败的处理**：丢弃当前 buffer，重新接收，不打日志（或只计数，避免刷屏）
8. **超时保护**：连续 2 秒收不到任何数据，认为 ESP-CAM 离线，触发「人物消失」状态
9. **不做 JSON / XML 解析**，只用字符串 split + `strtof`

### 4.4 帧率与抖动

- ESP-CAM 端 10 Hz 发送
- ESP32-S3 端做**滑动平均滤波**：最近 3 帧 center_x / center_y 求平均，输出给舵机
- 避免单次噪声导致舵机抖动
- 死区处理见 Step 6

---

## 第五阶段：分阶段实施计划

### 通用限制（每个 Step 都遵守）

- 每次只改一个固件（ESP-CAM 或 ESP32-S3），不并行修改
- 每次改动都可单独编译验证
- 保留旧固件，出问题能回退
- 每个 Step 结束前跑一遍「回归测试」：确认原有语音系统不受影响

### Step 1：ESP32-CAM 单独跑摄像头

**目标**：让 OV2640 输出图像，JPEG 通过 Wi-Fi HTTP 或 USB CDC 可下载，肉眼看到画面

**改动**：
- 新建独立项目 `firmware/esp32-cam/`（不与 ESP32-S3 工程混用）
- 使用官方 `esp32-camera` driver + `esp_http_server` 或 `AsyncWebServer`
- 分辨率 QVGA (320×240) JPEG quality 10
- 输出 URL：`http://<ip>/stream` (M-JPEG) 和 `/capture` (单帧)

**验收**：
- [ ] Wi-Fi 连上路由
- [ ] 浏览器访问 `http://<ip>/capture` 能下载 JPEG
- [ ] 浏览器访问 `http://<ip>/stream` 能看到实时画面
- [ ] 帧率 ≥ 10 fps
- [ ] 无重启 / 无 crash

### Step 2：ESP32-CAM 实现人物检测

**目标**：在摄像头帧上做肤色 + 最大连通域检测，输出归一化坐标到串口日志

**改动**：
- 新增 `src/detect/person_detector.cpp/h`
- 分辨率改为 `FRAME_SIZE_UQQVGA` (160×120) 或 QVGA
- 输出到 `Serial.println()`，例如：
  ```
  PERSON,1,0.630,0.480,0.300,0.550,0.82
  PERSON,0
  ```
- 检测帧率 10 Hz（其余帧跳过）

**验收**：
- [ ] 有人时串口 100 ms 内打印 `PERSON,1,...`
- [ ] 无人时打印 `PERSON,0`
- [ ] 手举白色物体（非肤色）不报人
- [ ] 手举肤色物体能报人
- [ ] CPU 占用 < 60%，帧率不掉

### Step 3：ESP32-CAM 输出人物坐标到 UART1

**目标**：把检测结果改从 USB CDC (Serial) 改到 UART1 (GPIO 19/20)，走 ESP32-S3 那条线

**改动**：
- `HardwareSerial serial1(1); serial1.begin(115200);`
- 检测逻辑不变，输出改到 `serial1.printf(...)`
- USB CDC 保留仅用于调试日志

**验收**：
- [ ] ESP32-S3 或 PC USB-UART 接到 ESP-CAM 的 GPIO 19/20 能看到 `PERSON,...` 帧
- [ ] 波特率、换行符正确
- [ ] 10 Hz 稳定输出

### Step 4：UART 发送坐标给 ESP32-S3

**目标**：物理接线 ESP-CAM ↔ ESP32-S3，ESP32-S3 通过 UART1 (GPIO 10/11) 收到坐标

**改动**：
- 硬件：焊接/杜邦线 4 根线（TX / RX / GND / VCC-可选）
- ESP32-S3 端写一个**只读的接收探针** `src/vision/vision_receiver_test.cpp`（或临时 main.cpp），串口打印收到的每一行

**验收**：
- [ ] ESP32-S3 串口监视器能打印 ESP-CAM 发来的 `PERSON,...` 帧
- [ ] 无丢失 / 无乱码
- [ ] 运行 5 分钟无 crash

### Step 5：ESP32-S3 接收 + 解析 + 状态机

**目标**：把 Step 4 的探针升级为稳定的接收 + 解析模块

**改动**：
- 新增 `src/vision/vision_receiver.cpp/h`
- 解析器：buffer 累积、`PERSON,` 前缀校验、字段校验、范围校验
- 输出 `PersonDetection` 结构体给上层
- 提供「最近一次检测」「是否在线」的 API

**验收**：
- [ ] 收到非法数据不 crash
- [ ] 收到超长行不 crash
- [ ] 收到半行不 crash
- [ ] 5 分钟压测无内存泄漏

### Step 6：单独测试 Pan 舵机

**目标**：Pan 舵机能响应串口命令转动到指定角度

**改动**：
- 新增 `src/servo/pan_tilt_test.cpp`（独立测试固件，不合并到主工程）
- 使用 `ESP32Servo.h`
- 串口命令：`PAN,0` / `PAN,90` / `PAN,180` → 转动
- 每次只允许一个命令在执行

**验收**：
- [ ] 舵机响应 200~500 ms 内到位
- [ ] 无异常噪音
- [ ] 供电稳定，ESP32-S3 不重启

### Step 7：单独测试 Tilt 舵机

**目标**：同 Step 6，独立测试 Tilt

**验收**：同 Step 6

### Step 8：人物位置 → Pan/Tilt

**目标**：ESP32-S3 收到坐标 → 计算目标舵机角度 → 平滑转动

**核心映射**（可参数化）：

| 视觉 | 舵机目标 |
|---|---|
| `center_x` | 相对水平中心 → Pan 角度 |
| `center_y` | 相对垂直中心 → Tilt 角度 |

**具体算法**：

```
// 参数（可配）
PAN_CENTER        = 90      // 舵机居中角度 (0~180)
PAN_RANGE         = 120     // Pan 总转动范围 (°)
TILT_CENTER       = 90
TILT_RANGE        = 60

// 从归一化坐标 (0.5 = 中心) 反算角度
pan_target  = PAN_CENTER + (center_x - 0.5) * PAN_RANGE
tilt_target = TILT_CENTER + (center_y - 0.5) * TILT_RANGE

// 上下限
pan_target  = clamp(pan_target,  PAN_MIN,  PAN_MAX)
tilt_target = clamp(tilt_target, TILT_MIN, TILT_MAX)
```

**注意方向**：如果实测画面左 → 舵机右转方向不对，翻转 `(center_x - 0.5)` 的符号即可，不要动其他公式。

**验收**：
- [ ] 人在画面最左 → Pan 转到左限位
- [ ] 人在画面最右 → Pan 转到右限位
- [ ] 人在画面正中 → Pan 回到中心
- [ ] Tilt 同理
- [ ] 手动改变人的位置，舵机实时跟随

### Step 9：加入中心死区和防抖

**目标**：解决舵机抖动、噪声跳变、快速往复

**核心参数**：

| 参数 | 建议初值 | 说明 |
|---|---|---|
| `DEAD_ZONE_X` | 0.02 (2%) | `center_x` 在 [0.48, 0.52] 内不触发 Pan |
| `DEAD_ZONE_Y` | 0.02 | `center_y` 在 [0.48, 0.52] 内不触发 Tilt |
| `MIN_ANGLE_STEP` | 3° | 目标角度与当前角度差 < 3° 就不动 |
| `SMOOTH_ALPHA` | 0.3 | 指数移动平均：`target_new = alpha * target_in + (1-alpha) * target_prev` |
| `MAX_MOVEMENT_MS` | 400 | 单次最大运动时间，超了分步走 |
| `UPDATE_INTERVAL_MS` | 150 | 舵机目标角度最小更新周期（~6.7 Hz，比帧率 10 Hz 略低，避免高频抖动） |
| `LOSS_TIMEOUT_MS` | 2000 | 收到 `PERSON,0` 或 2s 没数据 → 进入「失锁」状态 |

**防抖策略**：

1. **中心死区**：目标角度在死区内 → 保持当前角度，不动
2. **最小步长**：`|target_new - current| < MIN_ANGLE_STEP` → 不动
3. **指数平滑**：所有目标角度先做 EMA 平滑
4. **速率限制**：单次运动不超过 400 ms，超过就分多帧发送
5. **节流**：舵机目标角度每 150 ms 更新一次，不跟着 10 Hz 逐帧变

**验收**：
- [ ] 人在画面正中央，舵机不动（死区生效）
- [ ] 人快速左右移动，舵机平滑跟随，不抖动
- [ ] 人静止时，舵机不来回抖动
- [ ] 断电重启后舵机归中（Step 10）

### Step 10：最终闭环 + 边缘情况

**目标**：完成「看到人 → 转头 → 持续看着人」的闭环，处理所有边缘情况

**必须处理的边缘情况**：

| 情况 | 期望行为 |
|---|---|
| **启动时** | 舵机归中：Pan = 90°, Tilt = 90° |
| **人物消失（`PERSON,0`）** | 保持当前角度，等待 5s；5s 后回到中心 |
| **ESP-CAM 离线（2s 无数据）** | 同「人物消失」 |
| **人物从画面外进入** | 舵机平滑跟随 |
| **人物超出可转范围** | 转到限位，等待人回到范围内 |
| **人物位置剧烈跳变**（如从最左瞬间到最右） | 限速跟随，不允许瞬移 |
| **多个「人物」同时出现** | ESP-CAM 端只输出最大那个；ESP32-S3 不处理多目标 |
| **UART 中断 / 乱码** | 静默丢弃，等下一次 |
| **舵机过流 / 卡死** | 检测不到，靠外部断电保护 |

**最终验收清单**：

- [ ] 开机 5s 内舵机归中
- [ ] 人在画面左 → Pan 转到左
- [ ] 人在画面右 → Pan 转到右
- [ ] 人在画面上 → Tilt 向上
- [ ] 人在画面下 → Tilt 向下
- [ ] 人在画面中央 → 舵机停在中心（死区）
- [ ] 人消失 5s 后回到中心
- [ ] 10 分钟连续运行无 crash、无内存泄漏
- [ ] 现有语音系统（Wi-Fi / TCP / VAD / I2S）完全不受影响

---

## 附：风险与已知问题

1. **经典 ESP32-CAM 硬件限制**：320 KB SRAM + 无 PSRAM，无法跑深度学习。这是硬伤，只能靠算法选择规避。如果未来预算允许，建议换 ESP32-S3-CAM（8MB PSRAM）+ MobileNet-SSD，体验会好很多。

2. **舵机供电是最大风险点**：舵机瞬间电流大，如果独立电源不稳，ESP32-S3 会随机重启。必须在硬件层面解决（独立 5V/3A 电源 + 100µF 电容）。

3. **UART 共地必须做**：不共地 UART 收不到数据，且可能损坏 ESP32-S3。

4. **GPIO 引脚选错会冲突**：GPIO 17 已被 I2S 占用，不要动。GPIO 33/36/37 是 Flash/PSRAM，不能碰。

5. **肤色检测有场景限制**：暗光、非肤色、戴帽子 / 墨镜可能漏检。第一阶段先接受这个限制。

6. **不修改现有语音系统**：所有新增代码应放在 `src/vision/` 和 `src/servo/` 目录下，独立于 `src/audio/`、`src/network/`、`src/web/`、`src/vad/`。

---

## 附：文件规划（未来实施时用，现在**不创建**）

```
esp32-voice-ai/
├── firmware/
│   ├── esp32-cam/                          ← 新工程，独立
│   │   ├── platformio.ini
│   │   ├── boards/
│   │   └── src/
│   │       ├── main.cpp
│   │       ├── camera_init.cpp/h
│   │       └── detect/
│   │           └── person_detector.cpp/h
│   └── esp32/                              ← 现有工程
│       ├── platformio.ini                  ← 不需要改
│       └── src/
│           ├── main.cpp                    ← 后续在此挂接 vision_receiver
│           ├── vision/                     ← 新目录
│           │   └── vision_receiver.cpp/h
│           ├── servo/                      ← 新目录
│           │   ├── pan_tilt.cpp/h
│           │   └── pan_tilt_test.cpp       ← 独立测试固件
│           └── ...
└── docs/
    └── STEP_12_VISION_SERVO_PLAN.md        ← 本文档
```

---

## 版本

- v1.0 · 2026-09-16 12:36 JST · 硬件调查 + 软件方案分析 + 分阶段实施计划（不含代码）
