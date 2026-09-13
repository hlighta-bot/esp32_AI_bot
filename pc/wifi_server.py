"""
wifi_server.py - ESP32 上行录音接收 + 下行 TTS 发送
====================================================

Phase 1 主链路的服务端：

  ESP32 (MAX9814 → VAD → RECM 流 → RPTF)
        │
        │ TCP 长连接
        ▼
  本服务
        │
        ▼
  voice_pipeline
        │
        ▼
  PLAY + chunks + ACK

重要：

本版本使用 TCP 长连接。

一次 TCP 连接可以处理多轮：

    RECM
      ↓
    RPTF
      ↓
    Whisper / LLM / TTS
      ↓
    PLAY + chunks + ACK
      ↓
    等待下一轮 RECM
      ↓
    RPTF
      ↓
    ...

socket.timeout 不代表 TCP 断开。
只有：

    recv() == b""
    ConnectionResetError
    其他真正的 socket 错误

才会结束 ClientSession。

aidlux 兼容性：
  - 仅使用 Python 标准库（socket / threading / struct）
  - 单进程多线程（非 asyncio）
  - 无第三方网络依赖，可原样搬入 aidlux slim Python
  - voice_pipeline 内使用的 asr/llm/tts 依赖保持外部注入

协议（与 firmware/esp32/src/protocol/frame.h 一致）：

上行：

    RECM | u16 flags | u32 chunk_size | pcm (<=4096)
    RPTF | u32 total_size

下行：

    PLAY | u32 size
    chunk + ACK ×N

运行：

    python wifi_server.py

默认：

    0.0.0.0:8888

也可以：

    python wifi_server.py --port 9000
    python wifi_server.py --host 127.0.0.1
"""

import argparse
import io
import os
import socket
import struct
import sys
import threading
import time
from pathlib import Path


# ============================================================
# 项目内模块
# ============================================================

sys.path.insert(
    0,
    str(Path(__file__).resolve().parent)
)

import config

from send_wav import ESP32Player

from voice_pipeline import (
    pipeline,
    pcm_to_wav,
    wav_duration_seconds,
)


# ============================================================
# 协议常量
# ============================================================

PROTOCOL_REC = config.PROTOCOL_REC
PROTOCOL_RPTF = config.PROTOCOL_RPTF
PROTOCOL_PLAY = config.PROTOCOL_PLAY
PROTOCOL_ACK = config.PROTOCOL_ACK

CHUNK_SIZE = config.CHUNK_SIZE


# ============================================================
# 单段录音最大长度
# ============================================================

MAX_SESSION_BYTES = 4 * 1024 * 1024


# ============================================================
# ClientSession
# ============================================================

class ClientSession(threading.Thread):
    """
    处理一个 ESP32 TCP 长连接。

    生命周期：

        accept
          ↓
        connected
          ↓
        等待 RECM
          ↓
        RECM × N
          ↓
        RPTF
          ↓
        Whisper / LLM / TTS
          ↓
        PLAY + chunks + ACK
          ↓
        继续等待 RECM
          ↓
        ...

    只在真正 TCP 断开时退出。
    """

    def __init__(
        self,
        sock,
        addr,
        llm_engine=None,
    ):
        super().__init__(daemon=True)

        self.sock = sock
        self.addr = addr
        self.llm_engine = llm_engine

        self.stop_event = threading.Event()

        self.log_prefix = (
            f"[sess {addr[0]}:{addr[1]}]"
        )

        # 当前录音 PCM
        self._session_pcm = None
        self._session_bytes = 0
        self._session_t0 = None

        # 当前待发送 PCM
        self._pending_pcm = b""


    # ========================================================
    # 底层：读取指定数量
    # ========================================================

    def _recv_exact(self, n):
        """
        读取恰好 n 字节。

        返回：

            bytes
                成功读取

            None
                TCP 真正断开或发生不可恢复错误

        特别注意：

            socket.timeout

        不代表连接断开。

        它只表示：

            在当前 timeout 时间内暂时没有收到数据。

        因此 timeout 后继续等待。
        """

        if n <= 0:
            return b""

        chunks = []

        remaining = n

        while remaining > 0:

            if self.stop_event.is_set():
                return None

            try:

                buf = self.sock.recv(
                    remaining
                )

            except socket.timeout:

                # =================================================
                # 关键修改：
                #
                # timeout != TCP disconnect
                #
                # ESP32 可能只是暂时没有说话。
                # 保持 TCP 长连接，继续等待。
                # =================================================

                continue

            except ConnectionResetError:

                print(
                    f"{self.log_prefix} "
                    f"connection reset by peer"
                )

                return None

            except OSError as e:

                print(
                    f"{self.log_prefix} "
                    f"socket recv error: {e}"
                )

                return None

            except Exception as e:

                print(
                    f"{self.log_prefix} "
                    f"unexpected recv error: {e}"
                )

                return None


            # =====================================================
            # recv() 返回 b""：
            #
            # 这是 TCP FIN。
            #
            # 此时才是真正的“对端关闭连接”。
            # =====================================================

            if not buf:

                print(
                    f"{self.log_prefix} "
                    f"peer closed TCP connection"
                )

                return None


            chunks.append(buf)

            remaining -= len(buf)


        return b"".join(chunks)


    # ========================================================
    # 主线程
    # ========================================================

    def run(self):

        print(
            f"{self.log_prefix} connected"
        )

        try:

            while not self.stop_event.is_set():

                # ================================================
                # 等待下一帧
                #
                # 如果 ESP32 暂时没有说话：
                #
                #     socket.timeout
                #
                # 不退出。
                # ================================================

                frame = self._read_one_frame()


                if frame is None:

                    # 真正 TCP 断开
                    break


                kind, payload = frame


                if kind == "RECM":

                    self._handle_rec(
                        payload
                    )


                elif kind == "RPTF":

                    self._handle_rptf(
                        payload
                    )


                elif kind == "SKIP":

                    # 未知 magic。
                    #
                    # _read_one_frame() 已经处理了
                    # 一个 4-byte magic。
                    #
                    # 这里继续等待下一帧。
                    continue


                else:

                    print(
                        f"{self.log_prefix} "
                        f"unknown frame: {kind!r}"
                    )


        finally:

            try:

                self.sock.close()

            except Exception:
                pass


            print(
                f"{self.log_prefix} exited"
            )


    # ========================================================
    # 读取一帧
    # ========================================================

    def _read_one_frame(self):
        """
        读取：

            RECM
            RPTF

        返回：

            ("RECM", payload)
            ("RPTF", total)
            ("SKIP", magic)

        TCP 真正断开：

            None
        """

        # ========================================================
        # 读取 4-byte magic
        # ========================================================

        magic = self._recv_exact(4)


        if magic is None:
            return None


        # ========================================================
        # RECM
        # ========================================================

        if magic == PROTOCOL_REC:

            # flags(2) + size(4)
            header = self._recv_exact(6)


            if header is None:
                return None


            flags = struct.unpack(
                "<H",
                header[0:2]
            )[0]


            chunk_size = struct.unpack(
                "<I",
                header[2:6]
            )[0]


            if (
                chunk_size == 0
                or chunk_size > MAX_SESSION_BYTES
            ):

                print(
                    f"{self.log_prefix} "
                    f"invalid RECM size={chunk_size}"
                )

                return None


            pcm = self._recv_exact(
                chunk_size
            )


            if pcm is None:
                return None


            return (
                "RECM",
                (
                    flags,
                    pcm
                )
            )


        # ========================================================
        # RPTF
        # ========================================================

        elif magic == PROTOCOL_RPTF:

            header = self._recv_exact(4)


            if header is None:
                return None


            total = struct.unpack(
                "<I",
                header
            )[0]


            return (
                "RPTF",
                total
            )


        # ========================================================
        # 未知 magic
        # ========================================================

        else:

            print(
                f"{self.log_prefix} "
                f"bad magic {magic!r}, resyncing"
            )


            return (
                "SKIP",
                magic
            )


    # ========================================================
    # RECM
    # ========================================================

    def _handle_rec(self, payload):

        flags, pcm = payload


        # ========================================================
        # 新建一段录音
        # ========================================================

        if (
            not hasattr(
                self,
                "_session_pcm"
            )
            or self._session_pcm is None
        ):

            self._session_pcm = []

            self._session_bytes = 0

            self._session_t0 = time.time()


            print(
                f"{self.log_prefix} "
                f"new recording session"
            )


        # ========================================================
        # 累加 PCM
        # ========================================================

        self._session_pcm.append(
            pcm
        )

        self._session_bytes += len(
            pcm
        )


        # ========================================================
        # VAD trigger
        # ========================================================

        if flags & 0x0002:

            print(
                f"{self.log_prefix} "
                f"VAD trigger, "
                f"first byte={flags & 0x0001}"
            )


        # ========================================================
        # 防止无限增长
        # ========================================================

        if (
            self._session_bytes
            > MAX_SESSION_BYTES
        ):

            print(
                f"{self.log_prefix} "
                f"session too large "
                f"({self._session_bytes}), drop"
            )

            self._session_pcm = None

            self._session_bytes = 0


    # ========================================================
    # RPTF
    # ========================================================

    def _handle_rptf(self, total_size):

        t0 = time.time()


        # ========================================================
        # 必须先收到 RECM
        # ========================================================

        if (
            not hasattr(
                self,
                "_session_pcm"
            )
            or self._session_pcm is None
        ):

            print(
                f"{self.log_prefix} "
                f"RPTF without RECM, skip"
            )

            return


        # ========================================================
        # 合并 PCM
        # ========================================================

        pcm = b"".join(
            self._session_pcm
        )


        duration = (
            len(pcm)
            / 2
            / config.RECORD_SAMPLE_RATE
        )


        print(
            f"{self.log_prefix} "
            f"session end, "
            f"pcm={len(pcm)} B, "
            f"~{duration:.1f}s "
            f"(declared {total_size} B)"
        )


        # ========================================================
        # PCM → WAV
        # ========================================================

        wav_bytes = pcm_to_wav(
            pcm
        )


        # ========================================================
        # ASR + LLM + TTS
        # ========================================================

        reply_wav = pipeline(
            wav_bytes,
            llm_engine=self.llm_engine
        )


        # ========================================================
        # 保存调试 WAV
        # ========================================================

        try:

            with open(
                f"debug_{int(time.time())}.wav",
                "wb"
            ) as f:

                f.write(
                    wav_bytes
                )

        except Exception as e:

            print(
                f"{self.log_prefix} "
                f"debug wav save failed: {e}"
            )


        # ========================================================
        # 发送回复
        # ========================================================

        if reply_wav:

            self._send_reply(
                reply_wav
            )

        else:

            print(
                f"{self.log_prefix} "
                f"pipeline returned nothing"
            )


        # ========================================================
        # 清理当前录音
        #
        # 注意：
        #
        # 不关闭 TCP。
        #
        # 下一次 RECM 会重新建立 _session_pcm。
        # ========================================================

        self._session_pcm = None

        self._session_bytes = 0

        self._session_t0 = None


        print(
            f"{self.log_prefix} "
            f"round took "
            f"{time.time() - t0:.1f}s"
        )


        print(
            f"{self.log_prefix} "
            f"ready for next recording"
        )


    # ========================================================
    # 下发 TTS
    # ========================================================

    def _send_reply(self, wav_bytes):

        if not self._send_play_header(
            wav_bytes
        ):

            return


        if not self._send_play_chunks(
            wav_bytes
        ):

            return


        print(
            f"{self.log_prefix} "
            f"PLAY completed, "
            f"TCP connection kept alive"
        )


    # ========================================================
    # PLAY header
    # ========================================================

    def _send_play_header(self, wav_bytes):

        try:

            import io as _io
            import soundfile as sf
            import numpy as np

            from scipy.signal import (
                resample_poly
            )

            from math import gcd


            # ====================================================
            # WAV
            # ====================================================

            audio, sr = sf.read(
                io.BytesIO(wav_bytes),
                always_2d=True
            )


            # ====================================================
            # Stereo → Mono
            # ====================================================

            if audio.shape[1] > 1:

                audio = np.mean(
                    audio,
                    axis=1
                )

            else:

                audio = audio[:, 0]


            # ====================================================
            # 重采样
            # ====================================================

            if (
                sr
                != config.RECORD_SAMPLE_RATE
            ):

                g = gcd(
                    int(sr),
                    config.RECORD_SAMPLE_RATE
                )


                audio = resample_poly(
                    audio,
                    config.RECORD_SAMPLE_RATE // g,
                    int(sr) // g
                )


            # ====================================================
            # float → PCM16
            # ====================================================

            audio = np.clip(
                audio,
                -1.0,
                1.0
            )


            pcm = (
                audio * 32767
            ).astype(
                np.int16
            ).tobytes()


        except Exception as e:

            print(
                f"{self.log_prefix} "
                f"prepare_reply_audio failed: {e}"
            )

            return False


        # ========================================================
        # PLAY header
        # ========================================================

        header = (
            PROTOCOL_PLAY
            + struct.pack(
                "<I",
                len(pcm)
            )
        )


        try:

            self.sock.sendall(
                header
            )


            self._pending_pcm = pcm


            print(
                f"{self.log_prefix} "
                f"PLAY header sent, "
                f"pcm={len(pcm)} B"
            )


            return True


        except (
            BrokenPipeError,
            ConnectionResetError
        ) as e:

            print(
                f"{self.log_prefix} "
                f"send PLAY failed: {e}"
            )

            self.stop_event.set()

            return False


        except OSError as e:

            print(
                f"{self.log_prefix} "
                f"send PLAY socket error: {e}"
            )

            self.stop_event.set()

            return False


    # ========================================================
    # PLAY chunks + ACK
    # ========================================================

    def _send_play_chunks(
        self,
        wav_bytes
    ):

        pcm = getattr(
            self,
            "_pending_pcm",
            b""
        )


        if not pcm:

            return False


        sent = 0


        while sent < len(pcm):

            end = min(
                sent + CHUNK_SIZE,
                len(pcm)
            )


            chunk = pcm[
                sent:end
            ]


            # ====================================================
            # 发送 PCM
            # ====================================================

            try:

                self.sock.sendall(
                    chunk
                )


            except (
                BrokenPipeError,
                ConnectionResetError
            ) as e:

                print(
                    f"{self.log_prefix} "
                    f"send chunk failed: {e}"
                )

                self.stop_event.set()

                return False


            except OSError as e:

                print(
                    f"{self.log_prefix} "
                    f"send chunk socket error: {e}"
                )

                self.stop_event.set()

                return False


            # ====================================================
            # 等待 ESP32 ACK
            # ====================================================

            ack = self._recv_exact(
                len(PROTOCOL_ACK)
            )


            if ack is None:

                print(
                    f"{self.log_prefix} "
                    f"ACK receive failed"
                )

                self.stop_event.set()

                return False


            if ack != PROTOCOL_ACK:

                print(
                    f"{self.log_prefix} "
                    f"ACK mismatch: {ack!r}"
                )

                self.stop_event.set()

                return False


            sent = end


        # ========================================================
        # 清理
        # ========================================================

        self._pending_pcm = b""


        return True


# ============================================================
# AudioServer
# ============================================================

class AudioServer:
    """
    TCP listener。

    一个 ESP32 对应一个 ClientSession。

    ClientSession 使用长连接。
    """

    def __init__(
        self,
        host="0.0.0.0",
        port=8888,
        llm_engine=None
    ):

        self.host = host

        self.port = int(port)

        self.llm_engine = llm_engine

        self.stop_event = threading.Event()

        self._sessions = set()

        self._lock = threading.Lock()


    # ========================================================
    # start
    # ========================================================

    def start(self):

        self._server = socket.socket(
            socket.AF_INET,
            socket.SOCK_STREAM
        )


        self._server.setsockopt(
            socket.SOL_SOCKET,
            socket.SO_REUSEADDR,
            1
        )


        self._server.bind(
            (
                self.host,
                self.port
            )
        )


        self._server.listen(
            config.SERVER_MAX_CLIENTS
        )


        # ====================================================
        # accept timeout
        #
        # 这里只用于让 server.stop()
        # 可以定期检查。
        #
        # 与客户端 TCP timeout 无关。
        # ====================================================

        self._server.settimeout(
            0.5
        )


        print(
            f"[server] listening on "
            f"{self.host}:{self.port} "
            f"(engine="
            f"{self.llm_engine or config.DEFAULT_LLM_ENGINE}"
            f")"
        )


        return self


    # ========================================================
    # serve_forever
    # ========================================================

    def serve_forever(self):

        try:

            while not self.stop_event.is_set():

                try:

                    sock, addr = (
                        self._server.accept()
                    )


                except socket.timeout:

                    continue


                except OSError:

                    break


                # =================================================
                # TCP_NODELAY
                # =================================================

                sock.setsockopt(
                    socket.IPPROTO_TCP,
                    socket.TCP_NODELAY,
                    1
                )


                # =================================================
                # 重要：
                #
                # 保留 timeout。
                #
                # timeout 不再被解释成断开。
                #
                # _recv_exact() 会在 timeout 后继续等待。
                # =================================================

                sock.settimeout(
                    config.TCP_IO_TIMEOUT
                )


                print(
                    f"[server] accepted "
                    f"{addr[0]}:{addr[1]}"
                )


                sess = ClientSession(
                    sock,
                    addr,
                    self.llm_engine
                )


                sess.start()


                with self._lock:

                    self._sessions.add(
                        sess
                    )


        except KeyboardInterrupt:

            print(
                "[server] stopping..."
            )


        finally:

            self.stop()


    # ========================================================
    # stop
    # ========================================================

    def stop(self):

        self.stop_event.set()


        with self._lock:

            for s in list(
                self._sessions
            ):

                s.stop_event.set()


                try:

                    s.sock.shutdown(
                        socket.SHUT_RDWR
                    )

                except Exception:
                    pass


                try:

                    s.sock.close()

                except Exception:
                    pass


            self._sessions.clear()


        try:

            self._server.close()

        except Exception:
            pass


# ============================================================
# CLI
# ============================================================

def main():

    parser = argparse.ArgumentParser(
        description=(
            "ESP32 Voice AI - "
            "Wi-Fi Server"
        )
    )


    parser.add_argument(
        "--host",
        default=config.WIFI_HOST_BIND
    )


    parser.add_argument(
        "--port",
        type=int,
        default=config.WIFI_TCP_PORT
    )


    parser.add_argument(
        "--engine",
        default=None,
        help=(
            "LLM engine: "
            "sensenova / ollama / gemini"
        )
    )


    args = parser.parse_args()


    server = AudioServer(
        host=args.host,
        port=args.port,
        llm_engine=args.engine
    )


    server.start()


    print(
        "[server] waiting for ESP32 clients..."
    )


    print(
        f"[server] current llm engine: "
        f"{server.llm_engine}"
    )


    server.serve_forever()


# ============================================================
# main
# ============================================================

if __name__ == "__main__":
    main()

