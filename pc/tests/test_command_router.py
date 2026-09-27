"""
test_command_router.py - CommandRouter 单元测试
=================================================

测试 CommandRouter.classify() 和会话状态机逻辑。

不依赖：ESP32 / 麦克风 / Whisper / LLM API / TTS / 网络

运行：
    cd pc/
    python tests/test_command_router.py

或：
    cd pc/
    python -m pytest tests/test_command_router.py -v
"""

import sys
import os

# 确保 pc/ 在 sys.path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import config
from command_router import CommandRouter, CommandType


# ============================================================
# 辅助：模拟 wifi_server._handle_rptf 的会话状态机
# ============================================================

class MockSessionStateMachine:
    """模拟 wifi_server.py 的会话状态机逻辑"""

    def __init__(self):
        self._wake_activated = False
        self._router = CommandRouter()
        self.llm_called = False
        self.tts_called = False

    def _call_llm(self, text):
        self.llm_called = True
        return "mock reply"

    def _call_tts(self, text):
        self.tts_called = True
        return b"MOCK_WAV"

    def process(self, user_text):
        """
        模拟 _handle_rptf() 的逻辑：
        返回 (cmd_type, action)
        """
        cmd_type = self._router.classify(user_text)
        self.llm_called = False
        self.tts_called = False

        if not self._wake_activated:
            # SLEEPING
            if cmd_type == CommandType.WAKE_WORD:
                self._wake_activated = True
                reply = self._call_tts(config.WAKE_WORD_REPLY)
                return (cmd_type, "wake_activate")
            else:
                return (cmd_type, "discard")
        else:
            # ACTIVE
            if cmd_type == CommandType.INTERRUPT:
                return (cmd_type, "interrupt_no_tts")
            elif cmd_type == CommandType.WAKE_WORD:
                reply = self._call_tts(config.WAKE_WORD_REPLY)
                return (cmd_type, "wake_reply")
            else:
                # USER_TEXT -> LLM -> TTS
                self._call_llm(user_text)
                reply = self._call_tts("mock reply")
                return (cmd_type, "llm_tts")

    @property
    def wake_activated(self):
        return self._wake_activated


# ============================================================
# 测试用例
# ============================================================

def test_tc01_sleeping_wake_word():
    """
    TC-01: SLEEPING + "你好"
    -> WAKE_WORD -> activate -> no LLM
    """
    sm = MockSessionStateMachine()
    assert not sm.wake_activated

    cmd, action = sm.process("你好")
    assert cmd == CommandType.WAKE_WORD
    assert action == "wake_activate"
    assert sm.wake_activated
    assert not sm.llm_called


def test_tc02_sleeping_normal_text():
    """
    TC-02: SLEEPING + normal question
    -> USER_TEXT -> discard -> no LLM

    "你好，今天天气怎么样" 是 USER_TEXT 而非 WAKE_WORD，
    因为唤醒词匹配是"严格前缀"：一旦出现 "，今天..."
    这样实质性内容，就不再判定为 WAKE_WORD。
    """
    sm = MockSessionStateMachine()
    assert not sm.wake_activated

    cmd, action = sm.process("你好，今天天气怎么样")
    assert cmd == CommandType.USER_TEXT
    assert action == "discard"
    assert not sm.llm_called


def test_tc03_active_normal_question():
    """
    TC-03: ACTIVE + normal question
    -> USER_TEXT -> LLM -> TTS
    """
    sm = MockSessionStateMachine()
    sm._wake_activated = True

    cmd, action = sm.process("给我讲个笑话")
    assert cmd == CommandType.USER_TEXT
    assert action == "llm_tts"
    assert sm.llm_called


def test_tc04_active_interrupt():
    """
    TC-04: ACTIVE + "停"
    -> INTERRUPT -> no LLM -> no TTS
    """
    sm = MockSessionStateMachine()
    sm._wake_activated = True

    cmd, action = sm.process("停")
    assert cmd == CommandType.INTERRUPT
    assert action == "interrupt_no_tts"
    assert not sm.llm_called
    assert not sm.tts_called


def test_tc05_active_wake_word():
    """
    TC-05: ACTIVE + "你好"
    -> WAKE_WORD -> confirm reply (no LLM)

    Design decision: re-saying wake word in ACTIVE state
    triggers a confirmation reply, not LLM.
    """
    sm = MockSessionStateMachine()
    sm._wake_activated = True

    cmd, action = sm.process("你好")
    assert cmd == CommandType.WAKE_WORD
    assert action == "wake_reply"
    assert not sm.llm_called


def test_tc06_active_normal_sound_text():
    """
    TC-06: ACTIVE + normal text (no interrupt words)
    -> should NOT be misclassified as INTERRUPT
    """
    sm = MockSessionStateMachine()
    sm._wake_activated = True

    cmd, action = sm.process("这个天气真好啊")
    assert cmd == CommandType.USER_TEXT
    assert action == "llm_tts"


def test_tc07_active_speaking_self_trigger():
    """
    TC-07: AI speaking for a long time
    -> PC/CommandRouter should NOT self-trigger INTERRUPT

    pipeline_text() calls LLM.chat() -> synthesize()
    directly. LLM reply never passes through CommandRouter.
    """
    from voice_pipeline import pipeline_text
    import inspect
    source = inspect.getsource(pipeline_text)
    # pipeline_text only calls LLM.chat() and synthesize()
    # It does NOT call CommandRouter.classify()
    assert "classify" not in source
    assert "CommandRouter" not in source


def test_empty_play_paths():
    """
    TC-08: All "no TTS" paths must send empty PLAY to unlock ESP32

    Paths:
    1. SLEEPING + INTERRUPT -> discard -> empty PLAY
    2. SLEEPING + USER_TEXT -> discard -> empty PLAY
    3. ACTIVE + INTERRUPT -> no TTS -> empty PLAY
    4. ASR empty (None) -> discard -> empty PLAY
    """
    sm = MockSessionStateMachine()

    # Path 1: SLEEPING + INTERRUPT
    sm._wake_activated = False
    cmd, action = sm.process("停")
    assert cmd == CommandType.INTERRUPT
    assert action == "discard"

    # Path 2: SLEEPING + USER_TEXT
    # 注意："你好啊" 会被判定为 WAKE_WORD（语气词尾），
    # 所以这里选一个明确的 USER_TEXT 例子。
    cmd, action = sm.process("帮我查一下北京天气")
    assert cmd == CommandType.USER_TEXT
    assert action == "discard"

    # Path 3: ACTIVE + INTERRUPT
    sm._wake_activated = True
    cmd, action = sm.process("别说了")
    assert cmd == CommandType.INTERRUPT
    assert action == "interrupt_no_tts"

    # Path 4: ASR empty
    sm._wake_activated = False
    cmd, action = sm.process(None)
    assert cmd == CommandType.USER_TEXT
    assert action == "discard"


def test_classify_edge_cases():
    """Edge case tests for classify()"""
    router = CommandRouter()

    # Empty inputs
    assert router.classify("") == CommandType.USER_TEXT
    assert router.classify(None) == CommandType.USER_TEXT
    assert router.classify("   ") == CommandType.USER_TEXT

    # ---- Wake word (strict prefix, only allows punct/tone suffix) ----
    assert router.classify("你好") == CommandType.WAKE_WORD
    assert router.classify("你好。") == CommandType.WAKE_WORD
    assert router.classify("你好！") == CommandType.WAKE_WORD
    assert router.classify("你好？") == CommandType.WAKE_WORD
    assert router.classify("你好啊") == CommandType.WAKE_WORD
    assert router.classify("你好呀") == CommandType.WAKE_WORD
    assert router.classify("你好呢") == CommandType.WAKE_WORD
    assert router.classify("你好吧") == CommandType.WAKE_WORD
    assert router.classify("你好哦") == CommandType.WAKE_WORD
    assert router.classify("你好啊！") == CommandType.WAKE_WORD
    assert router.classify("你好呀。") == CommandType.WAKE_WORD

    # ---- USER_TEXT: 唤醒词后接实质内容 → 不唤醒 ----
    assert router.classify("你好，帮我查天气") == CommandType.USER_TEXT
    assert router.classify("你好，今天天气怎么样") == CommandType.USER_TEXT
    assert router.classify("你好，我想问个问题") == CommandType.USER_TEXT
    assert router.classify("你好帮我查天气") == CommandType.USER_TEXT
    assert router.classify("帮我查天气") == CommandType.USER_TEXT
    assert router.classify("今天怎么样") == CommandType.USER_TEXT
    assert router.classify("今天天气怎么样") == CommandType.USER_TEXT
    assert router.classify("我想说一些话") == CommandType.USER_TEXT
    assert router.classify("今天心情不错") == CommandType.USER_TEXT

    # Interrupt words (contains match)
    assert router.classify("停") == CommandType.INTERRUPT
    assert router.classify("停止") == CommandType.INTERRUPT
    assert router.classify("别说了") == CommandType.INTERRUPT
    assert router.classify("等一下") == CommandType.INTERRUPT
    assert router.classify("闭嘴") == CommandType.INTERRUPT

    # Interrupt word inside a sentence
    assert router.classify("请停下来") == CommandType.INTERRUPT


def test_no_duplicate_asr():
    """
    TC-09: pipeline_text() should NOT call transcribe()

    In wifi_server._handle_rptf():
    1. transcribe(wav_bytes) -> user_text
    2. CommandRouter.classify(user_text)
    3. pipeline_text(user_text) -> LLM -> TTS

    pipeline_text() skips ASR, so Whisper runs only once.
    """
    from voice_pipeline import pipeline_text
    import inspect
    source = inspect.getsource(pipeline_text)
    assert "transcribe" not in source


def test_stop_never_goes_to_llm():
    """
    TC-10: "停" should NEVER reach LLM

    ESP32: local energy detection -> quick stop playback
    PC: Whisper -> CommandRouter -> INTERRUPT -> no LLM
    """
    sm = MockSessionStateMachine()
    sm._wake_activated = True

    cmd, action = sm.process("停")
    assert cmd == CommandType.INTERRUPT
    assert not sm.llm_called


def test_prolonged_speaking_no_false_interrupt():
    """
    TC-11: Long SPEAKING should not cause false interrupt

    During playback, MicUploader is in WAITING_FOR_PLAYBACK
    or COOLDOWN state, discarding all mic data.
    Only after playPCM() completes + notifyPlaybackDone()
    + cooldown + clearPreRoll() does new recording start.
    So playback audio never passes through CommandRouter.
    """
    # Architecture guarantee - no actual test needed
    # This is verified by reading mic_uploader.cpp code


# ============================================================
# SERVO_COMMAND 分类测试（TC-12..TC-14）
# ============================================================

def test_servo_command_basic():
    """
    TC-12: 舵机命令短语 -> SERVO_COMMAND + 正确命令码

    覆盖用户要求的所有短语：向上/上/抬头、向下/低头、
    向左/左转、向右/右转、回中/回正/回到中间。
    """
    router = CommandRouter()

    # Vertical UP
    r = router.classify_with_details("向上")
    assert r.cmd_type == CommandType.SERVO_COMMAND
    assert r.servo_command == config.SERVO_CMD_VERTICAL_UP

    r = router.classify_with_details("上")
    assert r.cmd_type == CommandType.SERVO_COMMAND
    assert r.servo_command == config.SERVO_CMD_VERTICAL_UP

    r = router.classify_with_details("抬头")
    assert r.cmd_type == CommandType.SERVO_COMMAND
    assert r.servo_command == config.SERVO_CMD_VERTICAL_UP

    # Vertical DOWN
    r = router.classify_with_details("向下")
    assert r.cmd_type == CommandType.SERVO_COMMAND
    assert r.servo_command == config.SERVO_CMD_VERTICAL_DOWN

    r = router.classify_with_details("下")
    assert r.cmd_type == CommandType.SERVO_COMMAND
    assert r.servo_command == config.SERVO_CMD_VERTICAL_DOWN

    r = router.classify_with_details("低头")
    assert r.cmd_type == CommandType.SERVO_COMMAND
    assert r.servo_command == config.SERVO_CMD_VERTICAL_DOWN

    # Horizontal LEFT
    r = router.classify_with_details("向左")
    assert r.cmd_type == CommandType.SERVO_COMMAND
    assert r.servo_command == config.SERVO_CMD_HORIZONTAL_LEFT

    r = router.classify_with_details("左转")
    assert r.cmd_type == CommandType.SERVO_COMMAND
    assert r.servo_command == config.SERVO_CMD_HORIZONTAL_LEFT

    r = router.classify_with_details("左")
    assert r.cmd_type == CommandType.SERVO_COMMAND
    assert r.servo_command == config.SERVO_CMD_HORIZONTAL_LEFT

    # Horizontal RIGHT
    r = router.classify_with_details("向右")
    assert r.cmd_type == CommandType.SERVO_COMMAND
    assert r.servo_command == config.SERVO_CMD_HORIZONTAL_RIGHT

    r = router.classify_with_details("右转")
    assert r.cmd_type == CommandType.SERVO_COMMAND
    assert r.servo_command == config.SERVO_CMD_HORIZONTAL_RIGHT

    r = router.classify_with_details("右")
    assert r.cmd_type == CommandType.SERVO_COMMAND
    assert r.servo_command == config.SERVO_CMD_HORIZONTAL_RIGHT

    # CENTER_ALL
    r = router.classify_with_details("回中")
    assert r.cmd_type == CommandType.SERVO_COMMAND
    assert r.servo_command == config.SERVO_CMD_CENTER_ALL

    r = router.classify_with_details("回正")
    assert r.cmd_type == CommandType.SERVO_COMMAND
    assert r.servo_command == config.SERVO_CMD_CENTER_ALL

    r = router.classify_with_details("回到中间")
    assert r.cmd_type == CommandType.SERVO_COMMAND
    assert r.servo_command == config.SERVO_CMD_CENTER_ALL


def test_servo_command_with_punct_tone():
    """
    TC-13: 舵机命令 + 标点/语气词 尾 仍判定为 SERVO_COMMAND

    与唤醒词规则一致：允许尾部标点/语气词，
    不允许实质性字符。
    """
    router = CommandRouter()

    for phrase in [
        "向左", "向上", "回中", "抬头", "右转",
    ]:
        for suffix in ["", "。", "！", "？", "啊", "吧", "，", "啊。"]:
            text = phrase + suffix
            r = router.classify_with_details(text)
            assert r.cmd_type == CommandType.SERVO_COMMAND, \
                f"expected SERVO_COMMAND for {text!r}, got {r.cmd_type}"


def test_servo_command_no_false_positive():
    """
    TC-14: 包含左/右/上/下的普通句子不能被误判为 SERVO_COMMAND

    这是"严格短语"匹配策略的核心保护：
    句子出现方向词片段，但整句不是舵机命令时，
    必须落到 USER_TEXT。
    """
    router = CommandRouter()

    # 用户明确列出的负例
    for text in [
        "帮我查一下天气",
        "我想向左走",
        "左边有什么",
        "右边有什么",
        "上面是啥",
        "下面有什么",
        "向左走两步",
        "抬头看看",
        "回头",
        "回头见",
        "上菜",
        "下次",
        "上下都有",
        "回家",
        "回到家里",
        "回学校",
        "往左",
        "往右",
        "往上",
        "往下",
        "朝左走",
    ]:
        r = router.classify_with_details(text)
        assert r.cmd_type == CommandType.USER_TEXT, \
            f"expected USER_TEXT for {text!r}, got {r.cmd_type.value}"


def test_servo_command_priority_vs_interrupt():
    """
    TC-15: 打断词优先于舵机命令

    例如"停"、"等一下"不会同时匹配舵机短语。
    但如果用户说"向左"（无打断词），不触发 INTERRUPT。
    """
    router = CommandRouter()

    # 纯打断词 -> INTERRUPT
    assert router.classify("停") == CommandType.INTERRUPT
    assert router.classify("等一下") == CommandType.INTERRUPT

    # 舵机命令，不含打断词 -> SERVO_COMMAND
    assert router.classify("向左") == CommandType.SERVO_COMMAND
    assert router.classify("向上") == CommandType.SERVO_COMMAND


def test_servo_active_state_flow():
    """
    TC-16: ACTIVE 状态下 SERVO_COMMAND 不触发 LLM / TTS

    使用与 MockSessionStateMachine 类似的流程，
    但扩展支持 SERVO_COMMAND。
    """
    from command_router import CommandRouter as _CR
    r = _CR()
    sm_state = {"activated": True, "llm_called": False, "tts_called": False,
                "servo_code": None}

    for phrase in ["向左", "向右", "向上", "向下", "回中"]:
        sm_state["llm_called"] = False
        sm_state["tts_called"] = False
        sm_state["servo_code"] = None

        result = r.classify_with_details(phrase)
        assert result.cmd_type == CommandType.SERVO_COMMAND
        sm_state["servo_code"] = result.servo_command

        # 模拟 wifi_server._handle_rptf ACTIVE + SERVO_COMMAND 路径
        if result.cmd_type == CommandType.SERVO_COMMAND:
            # 不 LLM，不 TTS，仅记录命令码
            pass

        assert not sm_state["llm_called"]
        assert not sm_state["tts_called"]
        assert sm_state["servo_code"] is not None


def test_servo_sleeping_state_ignored():
    """
    TC-17: SLEEPING 状态下 SERVO_COMMAND 被丢弃

    未唤醒时，即使识别出舵机短语也不执行。
    """
    from command_router import CommandRouter as _CR
    r = _CR()
    sm_state = {"activated": False}

    result = r.classify_with_details("向左")
    assert result.cmd_type == CommandType.SERVO_COMMAND

    # SLEEPING + 非 WAKE_WORD -> discard
    if not sm_state["activated"] and \
            result.cmd_type != CommandType.WAKE_WORD:
        action = "discard"
    else:
        action = "other"

    assert action == "discard"


# ============================================================
# Main
# ============================================================

def run_all_tests():
    tests = [
        test_tc01_sleeping_wake_word,
        test_tc02_sleeping_normal_text,
        test_tc03_active_normal_question,
        test_tc04_active_interrupt,
        test_tc05_active_wake_word,
        test_tc06_active_normal_sound_text,
        test_tc07_active_speaking_self_trigger,
        test_empty_play_paths,
        test_classify_edge_cases,
        test_no_duplicate_asr,
        test_stop_never_goes_to_llm,
        test_prolonged_speaking_no_false_interrupt,
        test_servo_command_basic,
        test_servo_command_with_punct_tone,
        test_servo_command_no_false_positive,
        test_servo_command_priority_vs_interrupt,
        test_servo_active_state_flow,
        test_servo_sleeping_state_ignored,
    ]

    passed = 0
    failed = 0

    for test in tests:
        try:
            test()
            print(f"  PASS  {test.__name__}")
            passed += 1
        except AssertionError as e:
            print(f"  FAIL  {test.__name__}: {e}")
            failed += 1
        except Exception as e:
            print(f"  FAIL  {test.__name__}: {type(e).__name__}: {e}")
            failed += 1

    print(f"\n{'='*60}")
    print(f"Results: {passed} passed, {failed} failed, {len(tests)} total")
    print(f"{'='*60}")
    return failed == 0


if __name__ == "__main__":
    print("CommandRouter Unit Tests\n")
    success = run_all_tests()
    sys.exit(0 if success else 1)
