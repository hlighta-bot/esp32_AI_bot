"""
command_router.py - 会话控制层
================================

职责：
  将 ASR 文本分类为四种事件类型，不依赖任何 LLM。

  ASR Provider (Whisper)
      ↓
  CommandRouter.classify(text)
      ↓
  ┌─────────────────────────────────────────────┐
  │ CommandType.WAKE_WORD  → 激活 / 回复确认    │
  │ CommandType.INTERRUPT  → 停止 TTS           │
  │ CommandType.SERVO_COMMAND → SVCO 独立协议帧 │
  │ CommandType.USER_TEXT  → 交给 LLM Router    │
  └─────────────────────────────────────────────┘

设计原则：
  - 唤醒词 不经过 LLM
  - "停" 不经过 LLM
  - 舵机命令（向左 / 向上 / 回中 等）不经过 LLM，直接发 SVCO
  - 只有 USER_TEXT 才进入 LLM Router → Ollama / SenseNova / Gemini / ...
  - 切换 LLM Provider 不影响唤醒/打断/舵机逻辑

分类规则：
  - 文本严格匹配唤醒词（可带标点/语气词尾）→ WAKE_WORD
  - 文本包含打断词 → INTERRUPT
  - 文本严格匹配舵机命令短语（可带标点/语气词尾）→ SERVO_COMMAND
  - 其他 → USER_TEXT

严格匹配说明：
  舵机命令采用与唤醒词相同的"严格短语"策略。
  只有当整个 ASR 文本严格等于舵机短语本体，或其后仅跟
  标点/语气词时才判定为 SERVO_COMMAND。
  句子中出现"向左"、"右边"等片段仍判定为 USER_TEXT。

注意：
  本模块属于"会话控制层"，不属于"LLM 层"。
  切换云端/本地 LLM 时无需修改此文件。
"""

from dataclasses import dataclass
from enum import Enum
from typing import Optional, Tuple
import re

import config


# ============================================================
# 匹配辅助正则片段
# ============================================================

# "你好" 或舵机短语 后面允许跟随的非实质内容：
#   - 空白
#   - 常见中英文标点
#   - 少量语气词（啊/呀/呢/吧/哦）
# 一旦出现其他中文字符或字母，判定为 USER_TEXT。
_PUNCT = r"[\s,。！？!?，、~～.·;；:：\-–—]*"
_TAIL = r"[啊呀呢吧哦]"
# 允许多个 (标点 / 语气词) 交替出现，顺序任意
_SUFFIX = rf"({_PUNCT}|{_TAIL})*"


# ============================================================
# 事件类型
# ============================================================

class CommandType(Enum):
    """ASR 文本分类结果"""

    WAKE_WORD = "wake_word"        # 唤醒词触发
    INTERRUPT = "interrupt"        # 打断词触发
    SERVO_COMMAND = "servo_command"  # 舵机命令，直发 SVCO
    USER_TEXT = "user_text"        # 普通用户输入，交给 LLM


@dataclass
class CommandResult:
    """classify_with_details() 返回结构。"""

    cmd_type: CommandType
    # 仅当 cmd_type == SERVO_COMMAND 时非空；
    # 为 config.SERVO_CMD_* 中的字节码
    servo_command: Optional[int] = None


# ============================================================
# 舵机命令短语表
#
# 顺序敏感：先匹配长短语（如 "向左" / "抬头" / "回到中间"），
# 再匹配短字（"左" / "右" / "上" / "下"）。
# 由于使用 fullmatch + 尾部允许标点/语气词，长短语不会被短字覆盖。
# ============================================================

# (正则 pattern 关键字, config.SERVO_CMD_ 常量名)
_SERVO_PATTERNS: list = [
    # 上下 (Vertical)
    ("向上",     "SERVO_CMD_VERTICAL_UP"),
    ("抬头",     "SERVO_CMD_VERTICAL_UP"),
    ("上",       "SERVO_CMD_VERTICAL_UP"),
    ("向下",     "SERVO_CMD_VERTICAL_DOWN"),
    ("低头",     "SERVO_CMD_VERTICAL_DOWN"),
    ("下",       "SERVO_CMD_VERTICAL_DOWN"),
    # 左右 (Horizontal)
    ("向左",     "SERVO_CMD_HORIZONTAL_LEFT"),
    ("左转",     "SERVO_CMD_HORIZONTAL_LEFT"),
    ("左",       "SERVO_CMD_HORIZONTAL_LEFT"),
    ("向右",     "SERVO_CMD_HORIZONTAL_RIGHT"),
    ("右转",     "SERVO_CMD_HORIZONTAL_RIGHT"),
    ("右",       "SERVO_CMD_HORIZONTAL_RIGHT"),
    # 回中
    ("回到中间", "SERVO_CMD_CENTER_ALL"),
    ("回正",     "SERVO_CMD_CENTER_ALL"),
    ("回中",     "SERVO_CMD_CENTER_ALL"),
]


# ============================================================
# CommandRouter
# ============================================================

class CommandRouter:
    """
    会话控制层 - ASR 文本分类器

    将 Whisper 识别出的文本分类为 WAKE_WORD / INTERRUPT /
    SERVO_COMMAND / USER_TEXT。
    不调用 LLM，不依赖具体 LLM 实现。
    """

    def __init__(self):
        self._wake_word: str = config.WAKE_WORD
        self._interrupt_words: list = list(config.INTERRUPT_WORDS)

        # 预编译唤醒词严格匹配正则：
        #   ^<wake><punct/tail>*$
        escaped_wake = re.escape(self._wake_word)
        self._wake_pattern = re.compile(
            rf"^{escaped_wake}{_SUFFIX}$"
        )

        # 预编译舵机短语正则
        self._servo_patterns: list = []
        for phrase, cmd_const_name in _SERVO_PATTERNS:
            code = getattr(config, cmd_const_name)
            pattern = re.compile(
                rf"^{re.escape(phrase)}{_SUFFIX}$"
            )
            self._servo_patterns.append((pattern, phrase, code))

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
        """
        if not text:
            return False
        return self._wake_pattern.fullmatch(text) is not None

    # --------------------------------------------------------
    # 舵机命令严格匹配
    # --------------------------------------------------------

    def _match_servo_command(
        self, text: str
    ) -> Optional[Tuple[int, str]]:
        """
        返回 (SERVO_CMD_*, 匹配的短语)，无匹配返回 None。

        严格短语匹配：文本必须完整等于舵机短语本体
        或其后仅跟标点/语气词。
        """
        if not text:
            return None
        for pattern, phrase, code in self._servo_patterns:
            if pattern.fullmatch(text) is not None:
                return (code, phrase)
        return None

    # --------------------------------------------------------
    # 分类（保留旧签名：仅返回 CommandType）
    # --------------------------------------------------------

    def classify(self, text: Optional[str]) -> CommandType:
        """
        将 ASR 文本分类为事件类型。

        Args:
            text: Whisper 识别结果，可能为 None 或空字符串

        Returns:
            CommandType.WAKE_WORD / INTERRUPT / SERVO_COMMAND / USER_TEXT
        """
        result = self.classify_with_details(text)
        return result.cmd_type

    # --------------------------------------------------------
    # 分类（增强版：返回 CommandType + SERVO 命令码）
    # --------------------------------------------------------

    def classify_with_details(
        self, text: Optional[str]
    ) -> CommandResult:
        """
        与 classify() 相同，但额外返回 SERVO_COMMAND 对应的
        命令码。wifi_server 用这个方法获取 SVCO payload 所需的
        command byte。
        """
        if not text:
            return CommandResult(CommandType.USER_TEXT)

        text = text.strip()
        if not text:
            return CommandResult(CommandType.USER_TEXT)

        # 1. 唤醒词（最高优先，严格前缀）
        if self._is_wake_word(text):
            return CommandResult(CommandType.WAKE_WORD)

        # 2. 打断词（包含匹配）
        for w in self._interrupt_words:
            if w in text:
                return CommandResult(CommandType.INTERRUPT)

        # 3. 舵机命令（严格短语）
        matched = self._match_servo_command(text)
        if matched is not None:
            code, phrase = matched
            return CommandResult(CommandType.SERVO_COMMAND, code)

        # 4. 普通用户输入
        return CommandResult(CommandType.USER_TEXT)
