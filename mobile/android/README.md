# mobile/android/

> 目录当前**为空**，仅作为后续 Android 客户端的预留占位。

---

## 状态

| 项 | 值 |
|----|----|
| 阶段 | 预留 / Not Started |
| 计划开始 | 待 Phase 2（Wi-Fi 传输）落地后 |
| 责任人 | TBD |

---

## 计划中的功能

Android 客户端目标是**远程操控 / 监听 ESP32**，与 PC 端并存（非替代）：

```text
┌─────────────┐   Wi-Fi (UDP)   ┌──────────────┐
│ Android App │ ──────────────► │   ESP32-S3   │
│  (mic/TTS)  │ ◄────────────── │              │
└─────────────┘   Wi-Fi (UDP)   └──────────────┘
                                     │
                                     │ I2S
                                     ▼
                                MAX98357A
                                     │
                                     ▼
                                  Speaker
```

初步范围：

* 唤醒 / 录音 → 通过 UDP 送到 ESP32 或 PC 服务
* 展示 ASR / LLM 文本流
* 简单 TTS 配置（音色、语速）
* 固件 OTA 更新触发按钮

---

## 前置依赖

Android 客户端强依赖下列阶段完成：

1. **Phase 2 — Wi-Fi 传输层落地**
   当前 `pc/transport_wifi.py` 尚为占位，需先完成 UDP 协议 + ACK 逻辑（见
   [`../../docs/architecture.md`](../../docs/architecture.md) §10.1 短期任务）。
2. **协议稳定**
   复用 [`../../docs/protocol.md`](../../docs/protocol.md) 中的 `PLAY / ACK / PLAYBACK_FINISHED`，
   必要时扩展多客户端会话字段。

---

## 建议目录结构（未来）

```text
mobile/android/
├── README.md                 # 本文件
├── app/
│   ├── build.gradle
│   └── src/main/
│       ├── AndroidManifest.xml
│       ├── java/
│       └── res/
├── build.gradle
├── settings.gradle
└── gradle.properties
```

建议使用 **Jetpack Compose + Material 3**，音频通道走 `AudioRecord` / `AudioTrack`，
网络层与 PC 端复用同一 UDP 协议常量。

---

## 相关文档

* 系统架构：[`../../docs/architecture.md`](../../docs/architecture.md)
* 通信协议：[`../../docs/protocol.md`](../../docs/protocol.md)
* 硬件接线：[`../../docs/wiring.md`](../../docs/wiring.md)
* 阶段路线：[`../../docs/roadmap.md`](../../docs/roadmap.md)

---

## 备注

* 若 Phase 2 长期未启动，可考虑删除本目录，避免误导贡献者。
* 请勿在本目录提交任何包含密钥或证书的文件。
