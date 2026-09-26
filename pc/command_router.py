"""
command_router.py - 会话控制层
================================

职责：
  将 ASR 文本分类为三种事件类型，不依赖任何 LLM。

  ASR Provider (Whisper)
      ↓
  CommandRouter.classify(text)
      ↓
  ┌─────────────────────────────────────────┐
  │ CommandType.WAKE_WORD  → 激活 / 回复确认 │
  │ CommandType.INTERRUPT  → 停止 TTS        │
  │ CommandType.USER_TEXT  → 交给 LLM Router │
  └─────────────────────────────────────────┘

设计原则：
  - 唤醒词 不经过 LLM
  - "停" 不经过 LLM
  - 只有 USER_TEXT 才进入 LLM Router → Ollama / SenseNova / Gemini / ...
  - 切换 LLM Provider 不影响唤醒/打断逻辑

分类规则：
  - 文本严格匹配唤醒词（可带标点/语气词尾）→ WAKE_WORD
  - 文本包含打断词 → INTERRUPT
  - 其他 → USER_TEXT

唤醒词匹配采用"严格前缀"而非"包含匹配"，避免
"你好，帮我查天气"这类正常 USER_TEXT 被误判为 WAKE_WORD。

注意：
  本模块属于"会话控制层"，不属于"LLM 层"。
  切换云端/本地 LLM 时无需修改此文件。
"""

from enum import Enum
from typing import Optional
import re

import config


# ============================================================
# 唤醒词匹配辅助
# ============================================================

# "你好" 后面允许跟随的非实质内容：
#   - 空白
#   - 常见中英文标点
#   - 少量语气词（啊/呀/呢/吧/哦）
# 一旦出现其他中文字符或字母，判定为 USER_TEXT。
_WAKE_PUNCT = r"[\s,。！？!?，、~～.·;；:：\-–—]*"
_WAKE_TAIL = r"[啊呀呢吧哦]"


# ============================================================
# 事件类型
# ============================================================

class CommandType(Enum):
    """ASR 文本分类结果"""

    WAKE_WORD = "wake_word"    # 唤醒词触发
    INTERRUPT = "interrupt"    # 打断词触发
    USER_TEXT = "user_text"    # 普通用户输入，交给 LLM


# ============================================================
# CommandRouter
# ============================================================

class CommandRouter:
    """
    会话控制层 - ASR 文本分类器

    将 Whisper 识别出的文本分类为 WAKE_WORD / INTERRUPT / USER_TEXT。
    不调用 LLM，不依赖具体 LLM 实现。
    """

    def __init__(self):
        self._wake_word: str = config.WAKE_WORD
        self._interrupt_words: list = list(
            config.INTERRUPT_WORDS
        )
        # 预编译唤醒词严格匹配正则：
        #   ^<wake><punct/tail>*$
        # 允许唤醒词本体后跟 0..多次 语气词，每次语气词前后都
        # 可以有任意数量的标点/空白。只要文本严格等于该结构
        # 就判定为 WAKE_WORD；一旦出现实质性字符（如"帮我查"），
        # 正则匹配失败，落到 USER_TEXT。
        escaped = re.escape(self._wake_word)
        pattern = rf"^{escaped}{_WAKE_PUNCT}" \
                  rf"({_WAKE_TAIL}{_WAKE_PUNCT})*$"
        self._wake_pattern = re.compile(pattern)

    # --------------------------------------------------------
    # 唤醒词严格匹配
    # --------------------------------------------------------

    def _is_wake_word(self, text: str) -> bool:
        """
        严格判定 text 是否属于"唤醒词本身"。

        允许：
          - 唤醒词本体
          - 唤醒词 + 标点（。！？!?，、~～.- 等）
          - 唤醒词 + 少量语气词（啊/呀/呢/吧/哦）+ 可选标点

        拒绝：
          - 唤醒词 + 任何实质性字符（"帮我查天气"、
            "今天天气怎么样"等）

        例：
          "你好"                        → True
          "你好。" / "你好！"            → True
          "你好啊" / "你好呀" / "你好呢" → True
          "你好，帮我查天气"            → False
          "你好帮我查天气"              → False
          "帮我查天气"                  → False
        """
        if not text:
            return False
        return self._wake_pattern.fullmatch(text) is not None

    # --------------------------------------------------------
    # 分类
    # --------------------------------------------------------

    def classify(
        self,
        text: Optional[str]
    ) -> CommandType:
        """
        将 ASR 文本分类为事件类型。

        Args:
            text: Whisper 识别结果，可能为 None 或空字符串

        Returns:
            CommandType.WAKE_WORD / INTERRUPT / USER_TEXT
        """
        if not text:
            return CommandType.USER_TEXT

        text = text.strip()

        if not text:
            return CommandType.USER_TEXT

        # ------------------------------------------------
        # 唤醒词检测（严格前缀匹配）
        #
        # 只匹配 "你好" 本体或其后跟的
        # 标点 / 语气词（啊、呀、呢、吧、哦）。
        # 一旦出现实质性内容（"帮我查天气"），
        # 就不判定为唤醒词。
        # ------------------------------------------------

        if self._is_wake_word(text):
            return CommandType.WAKE_WORD

        # ------------------------------------------------
        # 打断词检测（包含匹配）
        #
        # "停" / "停止" / "别说了" / "等一下"
        # 都会匹配。
        #
        # 注意：打断词检测在唤醒词之后，
        #       避免唤醒词中包含打断词时误判。
        # ------------------------------------------------

        for w in self._interrupt_words:
            if w in text:
                return CommandType.INTERRUPT

        # ------------------------------------------------
        # 普通用户输入
        # ------------------------------------------------

        return CommandType.USER_TEXT
