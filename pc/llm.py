"""
LLM 大模型对话模块
====================

支持多种 LLM 引擎:

    - SenseNova (商汤)
    - Ollama (本地)
    - Gemini (Google)

依赖:

    requests
    python-dotenv

安装:

    pip install requests python-dotenv


接口:

    LLMRouter.chat(text)
        发送文字，返回回复

    LLMRouter.set_engine(engine)
        切换引擎

    LLMRouter.available_engines
        查看可用引擎


使用示例:

    from llm import LLMRouter


    # 默认引擎
    llm = LLMRouter()

    reply = llm.chat("你好")


    # 使用 Gemini
    llm = LLMRouter(engine="gemini")

    reply = llm.chat("你好")


    # 使用 Ollama
    llm.set_engine("ollama")

    reply = llm.chat("你好")
"""


import requests


from config import (
    # --------------------------------------------------------
    # SenseNova
    # --------------------------------------------------------
    SENSENOVA_API_KEY,
    SENSENOVA_URL,
    SENSENOVA_MODEL,

    # --------------------------------------------------------
    # Ollama
    # --------------------------------------------------------
    OLLAMA_URL,
    OLLAMA_MODEL,

    # --------------------------------------------------------
    # Gemini
    # --------------------------------------------------------
    GEMINI_API_KEY,
    GEMINI_URL,
    GEMINI_MODEL,

    # --------------------------------------------------------
    # 通用
    # --------------------------------------------------------
    DEFAULT_LLM_ENGINE,
    SYSTEM_PROMPT,

    # --------------------------------------------------------
    # Timeout
    # --------------------------------------------------------
    LLM_TIMEOUT_SENSENOVA,
    LLM_TIMEOUT_OLLAMA,
    LLM_TIMEOUT_GEMINI,
)


class LLMRouter:
    """
    LLM 路由器

    统一管理多个 LLM 引擎。
    """

    # ========================================================
    # 支持的引擎
    # ========================================================

    AVAILABLE_ENGINES = (
        "sensenova",
        "ollama",
        "gemini",
    )


    # ========================================================
    # 初始化
    # ========================================================

    def __init__(self, engine=None):

        self.engine = engine or DEFAULT_LLM_ENGINE

        if self.engine not in self.AVAILABLE_ENGINES:

            print(
                f"[LLM] 未知引擎: {self.engine}"
            )

            print(
                f"[LLM] 使用默认引擎: "
                f"{DEFAULT_LLM_ENGINE}"
            )

            self.engine = DEFAULT_LLM_ENGINE


    # ========================================================
    # available_engines
    # ========================================================

    @property
    def available_engines(self):

        return self.AVAILABLE_ENGINES


    # ========================================================
    # chat
    # ========================================================

    def chat(self, text):
        """
        发送文字给 LLM。

        Args:
            text:
                用户输入的文本

        Returns:
            str:
                LLM 回复文本

            失败:
                返回空字符串
        """

        if not text:

            return ""


        # ----------------------------------------------------
        # SenseNova
        # ----------------------------------------------------

        if self.engine == "sensenova":

            return self._sensenova_chat(text)


        # ----------------------------------------------------
        # Ollama
        # ----------------------------------------------------

        elif self.engine == "ollama":

            return self._ollama_chat(text)


        # ----------------------------------------------------
        # Gemini
        # ----------------------------------------------------

        elif self.engine == "gemini":

            return self._gemini_chat(text)


        return ""


    # ========================================================
    # set_engine
    # ========================================================

    def set_engine(self, engine):
        """
        切换 LLM 引擎。

        Args:
            engine:
                "sensenova"
                "ollama"
                "gemini"

        Returns:
            True:
                切换成功

            False:
                引擎不存在
        """

        if engine not in self.AVAILABLE_ENGINES:

            print(
                f"[LLM] 不支持的引擎: {engine}"
            )

            return False


        self.engine = engine

        print(
            f"[LLM] 当前引擎: {self.engine}"
        )

        return True


    # ========================================================
    # SenseNova
    # ========================================================

    def _sensenova_chat(self, text):
        """
        SenseNova 对话。
        """

        if not SENSENOVA_API_KEY:

            print(
                "[SenseNova] API Key 未配置"
            )

            return ""


        try:

            response = requests.post(

                SENSENOVA_URL,

                headers={
                    "Authorization":
                        f"Bearer {SENSENOVA_API_KEY}",

                    "Content-Type":
                        "application/json",
                },

                json={

                    "model":
                        SENSENOVA_MODEL,

                    "messages": [

                        {
                            "role": "system",

                            "content": SYSTEM_PROMPT,
                        },

                        {
                            "role": "user",

                            "content": text,
                        }

                    ],
                },

                timeout=
                    LLM_TIMEOUT_SENSENOVA,
            )


            # HTTP 错误直接抛出
            response.raise_for_status()


            res_json = response.json()


            # ------------------------------------------------
            # OpenAI-compatible response
            # ------------------------------------------------

            choices = res_json.get(
                "choices",
                []
            )


            if not choices:

                print(
                    "[SenseNova] "
                    "响应中没有 choices"
                )

                return ""


            message = choices[0].get(
                "message",
                {}
            )


            answer = message.get(
                "content",
                ""
            )


            return answer


        except requests.exceptions.Timeout:

            print(
                "[SenseNova] 请求超时"
            )

            return ""


        except requests.exceptions.RequestException as e:

            print(
                f"[SenseNova] HTTP Error: {e}"
            )


            try:

                print(
                    response.text
                )

            except Exception:

                pass


            return ""


        except Exception as e:

            print(
                f"[SenseNova] Error: {e}"
            )

            return ""


    # ========================================================
    # Ollama
    # ========================================================

    def _ollama_chat(self, text):
        """
        Ollama 对话。
        """

        try:

            response = requests.post(

                f"{OLLAMA_URL}/api/chat",

                json={

                    "model":
                        OLLAMA_MODEL,

                    "messages": [

                        {
                            "role": "system",

                            "content": SYSTEM_PROMPT,
                        },

                        {
                            "role": "user",

                            "content": text,
                        }

                    ],

                    "stream": False,
                },

                timeout=
                    LLM_TIMEOUT_OLLAMA,
            )


            response.raise_for_status()


            res_json = response.json()


            message = res_json.get(
                "message",
                {}
            )


            answer = message.get(
                "content",
                ""
            )


            return answer


        except requests.exceptions.Timeout:

            print(
                "[Ollama] 请求超时"
            )

            return ""


        except requests.exceptions.RequestException as e:

            print(
                f"[Ollama] HTTP Error: {e}"
            )

            return ""


        except Exception as e:

            print(
                f"[Ollama] Error: {e}"
            )

            return ""


    # ========================================================
    # Gemini
    # ========================================================

    def _gemini_chat(self, text):
        """
        Gemini 对话。

        使用 Gemini REST API:

        POST
        /v1beta/models/{model}:generateContent
        """

        if not GEMINI_API_KEY:

            print(
                "[Gemini] API Key 未配置"
            )

            return ""


        try:

            # ------------------------------------------------
            # 构造 URL
            # ------------------------------------------------

            url = (
                f"{GEMINI_URL}"
                f"/models/"
                f"{GEMINI_MODEL}"
                f":generateContent"
            )


            # ------------------------------------------------
            # 请求
            # ------------------------------------------------

            response = requests.post(

                url,

                headers={

                    "x-goog-api-key":
                        GEMINI_API_KEY,

                    "Content-Type":
                        "application/json",
                },

                json={

                    "systemInstruction": {

                        "parts": [

                            {
                                "text": SYSTEM_PROMPT
                            }

                        ],
                    },

                    "contents": [

                        {

                            "role": "user",

                            "parts": [

                                {
                                    "text": text
                                }

                            ],
                        }

                    ]
                },

                timeout=
                    LLM_TIMEOUT_GEMINI,
            )


            # ------------------------------------------------
            # HTTP 状态检查
            # ------------------------------------------------

            response.raise_for_status()


            # ------------------------------------------------
            # JSON
            # ------------------------------------------------

            res_json = response.json()


            # ------------------------------------------------
            # candidates
            # ------------------------------------------------

            candidates = res_json.get(
                "candidates",
                []
            )


            if not candidates:

                print(
                    "[Gemini] "
                    "响应中没有 candidates"
                )

                return ""


            # ------------------------------------------------
            # content
            # ------------------------------------------------

            content = candidates[0].get(
                "content",
                {}
            )


            # ------------------------------------------------
            # parts
            # ------------------------------------------------

            parts = content.get(
                "parts",
                []
            )


            if not parts:

                print(
                    "[Gemini] "
                    "响应中没有 parts"
                )

                return ""


            # ------------------------------------------------
            # 提取文字
            # ------------------------------------------------

            answer_parts = []


            for part in parts:

                if "text" in part:

                    answer_parts.append(
                        part["text"]
                    )


            answer = "".join(
                answer_parts
            )


            return answer


        except requests.exceptions.Timeout:

            print(
                "[Gemini] 请求超时"
            )

            return ""


        except requests.exceptions.RequestException as e:

            print(
                f"[Gemini] HTTP Error: {e}"
            )


            # ------------------------------------------------
            # 打印 Google 返回的详细错误
            # ------------------------------------------------

            try:

                print(
                    "[Gemini] Server response:"
                )

                print(
                    response.text
                )

            except Exception:

                pass


            return ""


        except Exception as e:

            print(
                f"[Gemini] Error: {e}"
            )

            return ""


# ============================================================
# __main__
# ============================================================

if __name__ == "__main__":

    import sys


    # --------------------------------------------------------
    # 获取命令行输入
    # --------------------------------------------------------

    text = (

        " ".join(sys.argv[1:])

        if len(sys.argv) > 1

        else
            "你好，请用一句话介绍你自己"
    )


    # --------------------------------------------------------
    # 测试 Gemini
    # --------------------------------------------------------

    llm = LLMRouter(
        engine="gemini"
    )


    print("=" * 60)

    print(
        f"Engine : {llm.engine}"
    )

    print(
        f"Model  : {GEMINI_MODEL}"
    )

    print(
        f"Input  : {text}"
    )

    print("=" * 60)

    print()


    # --------------------------------------------------------
    # 调用
    # --------------------------------------------------------

    reply = llm.chat(text)


    # --------------------------------------------------------
    # 输出
    # --------------------------------------------------------

    print("Reply:")

    print(reply)