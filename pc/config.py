"""
配置中心 - 集中管理所有路径、API 密钥和模型参数
================================================

加载顺序（越靠后越优先）:
  1. 本文件硬编码默认值（保底，便于快速冒烟）
  2. 项目根 config.local.json（网络配置单一真相源，与固件共享）
  3. .env / 环境变量（LLM API Key 等）

网络相关配置（WIFI_TCP_PORT / ESP32_HOSTNAME / ESP32_WIFI_IP）来自
config.local.json；LLM API Key 仍留在 .env。
"""

import json
import os
from pathlib import Path
from typing import Any, Dict


# ============================================================
# 项目根目录
# ============================================================

PROJECT_DIR = Path(__file__).resolve().parent  # pc/ 目录
ROOT_DIR = PROJECT_DIR.parent                  # 项目根
CONFIG_JSON = ROOT_DIR / "config.local.json"


# ============================================================
# 加载 .env
# ============================================================

try:
    from dotenv import load_dotenv

    load_dotenv(PROJECT_DIR / ".env")

except ImportError:
    pass


# ============================================================
# 加载 config.local.json（网络配置单一真相源）
# ============================================================
#
# 与 scripts/gen_secrets.py 共享同一份 JSON。
# 缺失时静默回退，避免影响非 Wi-Fi 路径的使用者。
#

def _load_local_config() -> Dict[str, Any]:
    if not CONFIG_JSON.exists():
        return {}
    try:
        with CONFIG_JSON.open("r", encoding="utf-8") as f:
            data = json.load(f)
        return data if isinstance(data, dict) else {}
    except (OSError, json.JSONDecodeError) as e:
        # 不中断启动；打印提示便于用户排查
        import sys
        print(
            f"[config] WARN: 无法读取 {CONFIG_JSON}: {e}，将使用硬编码默认值",
            file=sys.stderr,
        )
        return {}


LOCAL_CONFIG: Dict[str, Any] = _load_local_config()


# ============================================================
# 路径配置
# ============================================================

WHISPER_CLI = (
    "/home/hqb/ai-apps/whisper.cpp/"
    "build/bin/whisper-cli"
)

WHISPER_MODEL = (
    "/home/hqb/ai-apps/whisper.cpp/"
    "models/ggml-base.bin"
)

WHISPER_LIB = (
    "/home/hqb/ai-apps/whisper.cpp/"
    "build/src"
)

GGML_LIB = (
    "/home/hqb/ai-apps/whisper.cpp/"
    "build/ggml/src"
)


# ============================================================
# LLM 配置
# ============================================================


# ------------------------------------------------------------
# SenseNova (商汤)
# ------------------------------------------------------------

SENSENOVA_API_KEY = os.getenv(
    "SENSENOVA_API_KEY",
    "",
)

SENSENOVA_URL = os.getenv(
    "SENSENOVA_URL",
    "https://token.sensenova.cn/v1/chat/completions",
)

SENSENOVA_MODEL = os.getenv(
    "SENSENOVA_MODEL",
    "sensenova-6.8-flash-lite",
)


# ------------------------------------------------------------
# Ollama
# ------------------------------------------------------------

OLLAMA_URL = os.getenv(
    "OLLAMA_URL",
    "http://localhost:11434",
)

OLLAMA_MODEL = os.getenv(
    "OLLAMA_MODEL",
    "qwen2.5:7b",
)


# ------------------------------------------------------------
# Gemini
# ------------------------------------------------------------

GEMINI_API_KEY = os.getenv(
    "GEMINI_API_KEY",
    "",
)

GEMINI_URL = os.getenv(
    "GEMINI_URL",
    "https://generativelanguage.googleapis.com/v1beta",
)

GEMINI_MODEL = os.getenv(
    "GEMINI_MODEL",
    "Gemini 3.1 Flash Lite",
)


# ------------------------------------------------------------
# 系统提示词 (System Prompt)
# ------------------------------------------------------------

# 用于所有 LLM 引擎的统一系统提示词，让回复更像日常对话。
# 可通过环境变量 SYSTEM_PROMPT 覆盖。
SYSTEM_PROMPT = os.getenv(
    "SYSTEM_PROMPT",
    (
        "你是一个通过音箱和用户语音聊天的语音助手。\n"
        "请遵守以下规则：\n"
        "1. 回复要简短口语化，一般 1-3 句话，最多不超过 5 句话。\n"
        "2. 不使用 Markdown、列表、编号、代码块、表格、链接、表情符号，"
        "因为回复会被 TTS 朗读。\n"
        "3. 不要自称 AI 助手，不要说'作为…'、'我建议你…'这类书面语；"
        "语气自然亲切，像朋友聊天。\n"
        "4. 用户问简单问题时直接给答案，不要展开长篇解释。\n"
        "5. 涉及具体操作或计算时，用一两句话概括关键结果即可。\n"
        "6. 如果不确定，就直接说'不太清楚'或'我不太确定'，不要编造。"
    ),
)


# ------------------------------------------------------------
# 默认 LLM 引擎
# ------------------------------------------------------------

# 可选：
#
# "sensenova"
# "ollama"
# "gemini"

DEFAULT_LLM_ENGINE = os.getenv(
    "DEFAULT_LLM_ENGINE",
    "gemini",
)


# ============================================================
# TTS 配置
# ============================================================

TTS_VOICE = "zh-CN-XiaoxiaoNeural"

TTS_RATE = "-10%"

TTS_PITCH = "-1Hz"


# ============================================================
# 音频配置
# ============================================================

RECORD_SAMPLE_RATE = 16000

RECORD_CHANNELS = 1

RECORD_DEVICE = None
# None 表示默认输入设备


# ============================================================
# ESP32 配置
# ============================================================

ESP32_BAUD = 921600

CHUNK_SIZE = 4096
# 单声道采样块大小 (字节)

ESP32_VID = 0x1A86

ESP32_PID = 0x55D3

PLAYBACK_TIMEOUT_BUFFER = 5.0
# 播放完成等待缓冲时间 (秒)

PLAYBACK_POLL_INTERVAL = 0.05
# 播放完成轮询间隔 (秒)


# ============================================================
# 串口通信
# ============================================================

SERIAL_TIMEOUT = 2

SERIAL_WRITE_TIMEOUT = 5


# ============================================================
# Wi-Fi / TCP 通信
# ============================================================
#
# 网络相关常量优先从项目根 config.local.json 读取；缺失则回退到硬编码值。
# 这样固件和 PC 端共享同一份配置。详见 docs/firmware.md §38。
#

def _local_get(key: str, default):
    return LOCAL_CONFIG.get(key, default)

# 目标期主链路：ESP32 与 server 走 TCP（PC 或 aidlux）
WIFI_TCP_PORT = _local_get("pc_port", 8888)

# ESP32 主机名（mDNS 广播用）
ESP32_HOSTNAME = _local_get("esp32_hostname", "esp32-voice")

# mDNS 服务名：PC 侧可通过 esp32-voice.local:8888 解析
ESP32_MDNS_NAME = ESP32_HOSTNAME

# Server 端 PC_HOST（供 wifi_server.py / voice_chat.py 参考）
PC_HOST = _local_get("pc_host", "192.168.1.20")

# Wi-Fi SSID / 密码（Python 端主要用于日志与 voice_chat.py 场景；
# 固件端由 gen_secrets.py 从同一份 JSON 生成 C 头文件）
WIFI_SSID = _local_get("wifi_ssid", "your_wifi_ssid")
WIFI_PASS = _local_get("wifi_pass", "your_wifi_password")

# ESP32 的 IP（PC 端 voice_chat.py --transport wifi 用来主动发起下行）
# 环境变量 ESP32_IP 优先，其次 config.local.json 里的 pc_host
ESP32_WIFI_IP = os.getenv("ESP32_IP", PC_HOST)

# wifi_server.py 绑定地址；0.0.0.0 允许局域网任意来源连接
WIFI_HOST_BIND = os.getenv("WIFI_HOST_BIND", "0.0.0.0")

# TCP 连接 / 读写超时（秒）
TCP_CONNECT_TIMEOUT = 5
TCP_IO_TIMEOUT = 5

# Server 端最大连接数（Phase 1 单客户端，预留扩展）
SERVER_MAX_CLIENTS = 4


# ============================================================
# 串口 / Wi-Fi 共享协议常量
# ============================================================

# 下行：PC → ESP32 播放
PROTOCOL_PLAY = b"PLAY"
# 每 chunk 确认（上下行共用）
PROTOCOL_ACK = b"ACK"
# 播放完成标记（ESP32 端可选发送；PC 端有兼容检测）
PROTOCOL_FINISHED = "PLAYBACK_FINISHED"

# 上行：ESP32 → PC 录音分块
# RECM | u16 flags | u32 chunk_size | pcm (<=4096)
PROTOCOL_REC = b"RECM"
# 本段录音结束（VAD 静音触发）
# RPTF | u32 total_size
PROTOCOL_RPTF = b"RPTF"

# RECM flags 位定义
REC_FLAG_FIRST = 0x0001      # bit0: 该段录音首包
REC_FLAG_VAD_TRIGGER = 0x0002  # bit1: VAD 触发点


# ============================================================
# ASR 配置
# ============================================================

ASR_TIMEOUT = 120
# Whisper 识别超时 (秒)

# 送入 ASR 前的轻量预处理参数。
# 经验值来自 support/batch_wave_stats.csv + 人工标注：
# - 250~300 RMS 可过滤大部分“背景全是杂音”的无效录音
# - 6 秒上限可避免超长噪声段放大 Whisper 幻觉

ASR_PRETRIM_MIN_RMS = 260

ASR_PRETRIM_MAX_DURATION_SEC = 6.0


# ============================================================
# LLM 超时配置
# ============================================================

LLM_TIMEOUT_SENSENOVA = 60

LLM_TIMEOUT_OLLAMA = 120

LLM_TIMEOUT_GEMINI = 60


# ============================================================
# 静音检测配置
# ============================================================

SILENCE_MAX_DURATION = 30.0

SILENCE_DURATION = 2.0

SILENCE_THRESHOLD = -45.0

SILENCE_POLL_INTERVAL = 0.1


# ============================================================
# 会话控制配置 (P1 + P2)
# ============================================================
#
# 本节配置属于"会话控制层"，不依赖任何 LLM。
# 切换 LLM Provider 时无需修改此节。
#
# 处理流程：
#
#   ASR (Whisper)
#       ↓
#   CommandRouter.classify(text)
#       ↓
#   ┌─────────────────────────────────────────────┐
#   │ WAKE_WORD  → 激活，回复确认语（不经过 LLM）  │
#   │ INTERRUPT  → 停止 TTS（不经过 LLM）          │
#   │ USER_TEXT  → 交给 LLM Router                 │
#   └─────────────────────────────────────────────┘
# ============================================================

# 唤醒词
#
# 用户说出唤醒词后设备进入对话模式。
#
# 匹配方式（严格）：
#   - "你好" 单独出现：触发
#   - "你好" + 标点（。！？!?，、~～）：触发
#   - "你好" + 少量语气词（啊/呀/呢/吧/哦）+ 可选标点：触发
#   - "你好" 后面出现任何实质性内容（"你好，帮我查天气"、
#     "你好帮我查天气"等）：不触发，走 USER_TEXT
#
# 详细规则见 command_router.py::_is_wake_word()。
WAKE_WORD = "你好"

# 唤醒回复语
WAKE_WORD_REPLY = "我在，请说"

# 打断词列表
#
# 用户在 AI 播放 TTS 时说出包含这些词的语句，
# 立即停止 TTS 播放。
#
# 匹配方式：包含匹配
# 注意：打断词检测在唤醒词之后，避免误判。
#
# ESP32 端同时有能量检测（INTERRUPT_RMS_THRESHOLD），
# 即使 ASR 未识别出文字，大声说话也能触发打断。
INTERRUPT_WORDS = [
    "停",
    "停止",
    "别说了",
    "等一下",
    "闭嘴",
]