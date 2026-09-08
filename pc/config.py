"""
配置中心 - 集中管理所有路径、API 密钥和模型参数
================================================

从 .env 文件加载敏感信息，其余在此文件统一管理。
"""

import os
from pathlib import Path


# ============================================================
# 项目根目录
# ============================================================

PROJECT_DIR = Path(__file__).resolve().parent


# ============================================================
# 加载 .env
# ============================================================

try:
    from dotenv import load_dotenv

    load_dotenv(PROJECT_DIR / ".env")

except ImportError:
    pass


# ============================================================
# 路径配置
# ============================================================

WHISPER_CLI = (
    "/home/hqb/ai-apps/whisper.cpp/"
    "build/bin/whisper-cli"
)

WHISPER_MODEL = (
    "/home/hqb/ai-apps/whisper.cpp/"
    "models/ggml-tiny.bin"
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
    "sensenova",
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
# 串口协议
# ============================================================

PROTOCOL_PLAY = b"PLAY"

PROTOCOL_ACK = b"ACK"

PROTOCOL_FINISHED = "PLAYBACK_FINISHED"


# ============================================================
# ASR 配置
# ============================================================

ASR_TIMEOUT = 120
# Whisper 识别超时 (秒)


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