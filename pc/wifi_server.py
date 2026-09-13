"""
wifi_server.py - ESP32 上行录音接收 + 下行 TTS 发送
====================================================

Phase 1 主链路的服务端：
  ESP32 (MAX9814 → VAD → RECM 流 → RPTF)  ──TCP──►  本服务
                                                       │
                                                       ▼
                                                   voice_pipeline
                                                       │
                                                       ▼
                                                 PLAY + chunks + ACK

aidlux 兼容性：
  - 仅使用 Python 标准库（socket / threading / struct）
  - 单进程多线程（非 asyncio）
  - 无第三方网络依赖，可原样搬入 aidlux slim Python
  - voice_pipeline 内使用的 asr/llm/tts 依赖保持外部注入

协议（与 firmware/esp32/src/protocol/frame.h 一致）：
  上行：RECM | u16 flags | u32 chunk_size | pcm (<=4096)
        RPTF | u32 total_size
  下行：PLAY | u32 size   | chunk + ACK ×N

运行：
  python wifi_server.py                    # 默认 0.0.0.0:8888
  python wifi_server.py --port 9000        # 改端口
  python wifi_server.py --host 127.0.0.1   # 仅本地
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

# 项目内模块
sys.path.insert(0, str(Path(__file__).resolve().parent))

import config
from send_wav import ESP32Player
from voice_pipeline import pipeline, pcm_to_wav, wav_duration_seconds


# ============================================================
# 协议常量（与 firmware/protocol/frame.h 对齐）
# ============================================================

PROTOCOL_REC  = config.PROTOCOL_REC     # b"RECM"
PROTOCOL_RPTF = config.PROTOCOL_RPTF    # b"RPTF"
PROTOCOL_PLAY = config.PROTOCOL_PLAY
PROTOCOL_ACK  = config.PROTOCOL_ACK
CHUNK_SIZE    = config.CHUNK_SIZE

MAX_SESSION_BYTES = 4 * 1024 * 1024    # 单段录音硬上限 4 MB


# ============================================================
# 单客户端会话
# ============================================================

class ClientSession(threading.Thread):
    """处理单个 ESP32 客户端连接

    生命周期：
      1. accept → start()
      2. 循环读帧：RECM 累加 PCM，RPTF 触发 pipeline
      3. pipeline 产出 WAV 后，通过同一 TCP 连接下发 PLAY+chunks
    """

    def __init__(self, sock, addr, llm_engine=None):
        super().__init__(daemon=True)
        self.sock = sock
        self.addr = addr
        self.llm_engine = llm_engine
        self.stop_event = threading.Event()
        self.log_prefix = f"[sess {addr[0]}:{addr[1]}]"

    # --------------------------------------------------------
    # 底层读满 N 字节（防粘包 / 半包）
    # --------------------------------------------------------

    def _recv_exact(self, n):
        if n <= 0:
            return b""
        chunks = []
        remaining = n
        while remaining > 0:
            try:
                buf = self.sock.recv(remaining)
            except (ConnectionResetError, OSError):
                return None
            except socket.timeout:
                return None
            if not buf:
                return None
            chunks.append(buf)
            remaining -= len(buf)
        return b"".join(chunks)

    # --------------------------------------------------------
    # 主循环
    # --------------------------------------------------------

    def run(self):
        print(f"{self.log_prefix} connected")
        buf = b""

        while not self.stop_event.is_set():
            # ---- 读一帧 ----
            frame = self._read_one_frame()
            if frame is None:
                print(f"{self.log_prefix} closed by peer")
                break

            kind, payload = frame
            if kind == "RECM":
                self._handle_rec(payload)
            elif kind == "RPTF":
                self._handle_rptf(payload)
            else:
                print(f"{self.log_prefix} unknown frame: {kind!r}")

        try:
            self.sock.close()
        except Exception:
            pass
        print(f"{self.log_prefix} exited")

    def _read_one_frame(self):
        """读一个完整帧，返回 (kind, payload)；EOF 返回 None"""
        # 读 4 字节 magic
        magic = self._recv_exact(4)
        if magic is None:
            return None
        if magic == PROTOCOL_REC:
            # flags(2) + size(4)
            header = self._recv_exact(6)
            if header is None:
                return None
            chunk_size = struct.unpack("<I", header[2:6])[0]
            if chunk_size == 0 or chunk_size > MAX_SESSION_BYTES:
                return None
            pcm = self._recv_exact(chunk_size)
            if pcm is None:
                return None
            flags = struct.unpack("<H", header[0:2])[0]
            return ("RECM", (flags, pcm))
        elif magic == PROTOCOL_RPTF:
            header = self._recv_exact(4)
            if header is None:
                return None
            total = struct.unpack("<I", header[0:4])[0]
            return ("RPTF", total)
        else:
            # 未知 magic：丢字节，继续尝试对齐
            print(f"{self.log_prefix} bad magic {magic!r}, resyncing")
            return ("SKIP", magic)

    # --------------------------------------------------------
    # RECM 累加到本地缓冲
    # --------------------------------------------------------

    def _handle_rec(self, payload):
        flags, pcm = payload
        if not hasattr(self, "_session_pcm") or self._session_pcm is None:
            self._session_pcm = []
            self._session_bytes = 0
            self._session_t0 = time.time()
        self._session_pcm.append(pcm)
        self._session_bytes += len(pcm)

        if flags & 0x0002:  # VAD_TRIGGER
            print(f"{self.log_prefix} VAD trigger, first byte={flags & 0x0001}")

        if self._session_bytes > MAX_SESSION_BYTES:
            print(f"{self.log_prefix} session too large ({self._session_bytes}), drop")
            self._session_pcm = None

    def _handle_rptf(self, total_size):
        t0 = time.time()
        # if not getattr(self, "_session_pcm", None):
        if not hasattr(self, "_session_pcm") or self._session_pcm is None:
            print(f"{self.log_prefix} RPTF without RECM, skip")
            return

        pcm = b"".join(self._session_pcm)
        duration = len(pcm) / 2 / config.RECORD_SAMPLE_RATE
        print(f"{self.log_prefix} session end, pcm={len(pcm)} B, "
              f"~{duration:.1f}s (declared {total_size} B)")

        # PCM → WAV
        wav_bytes = pcm_to_wav(pcm)
        # ASR + LLM + TTS
        reply_wav = pipeline(wav_bytes, llm_engine=self.llm_engine)
        with open(f"debug_{int(time.time())}.wav", "wb") as f:
            f.write(wav_bytes)

        if reply_wav:
            self._send_reply(reply_wav)
            # print(f"{self.log_prefix} Pipeline result: {reply_wav}") 
        else:
            print(f"{self.log_prefix} pipeline returned nothing")

        # 清理
        self._session_pcm = None
        self._session_bytes = 0
        print(f"{self.log_prefix} round took {time.time() - t0:.1f}s")

    # --------------------------------------------------------
    # 通过 TCP 下发 TTS 结果（PLAY + chunks + ACK）
    # --------------------------------------------------------

    def _send_reply(self, wav_bytes):
        # 复用 ESP32Player 的协议逻辑：给它一个 transport
        # 但 ESP32Player 需要一个 TransportInterface 抽象；
        # 这里写一个最小的 socket transport 避免依赖 transport_wifi.py 的连接状态
        if not self._send_play_header(wav_bytes):
            return
        if not self._send_play_chunks(wav_bytes):
            return
        # 播放完成：ESP32 端不返回 FINISHED，本端等 duration + buffer
        # 简化：直接返回，让主循环继续监听下一段 RECM
        # 若需要严格等待，可按 PROTOCOL_FINISHED 轮询（当前固件未实现）

    def _send_play_header(self, wav_bytes):
        """预处理 WAV → PCM 并发送 PLAY + size"""
        try:
            import io as _io
            import soundfile as sf
            import numpy as np
            from scipy.signal import resample_poly
            from math import gcd

            audio, sr = sf.read(io.BytesIO(wav_bytes), always_2d=True)
            if audio.shape[1] > 1:
                audio = np.mean(audio, axis=1)
            else:
                audio = audio[:, 0]
            if sr != config.RECORD_SAMPLE_RATE:
                g = gcd(int(sr), config.RECORD_SAMPLE_RATE)
                audio = resample_poly(audio, config.RECORD_SAMPLE_RATE // g,
                                       int(sr) // g)
            audio = np.clip(audio, -1.0, 1.0)
            pcm = (audio * 32767).astype(np.int16).tobytes()
        except Exception as e:
            print(f"{self.log_prefix} prepare_reply_audio failed: {e}")
            return False

        header = PROTOCOL_PLAY + struct.pack("<I", len(pcm))
        try:
            self.sock.sendall(header)
            self._pending_pcm = pcm
            return True
        except Exception as e:
            print(f"{self.log_prefix} send PLAY failed: {e}")
            return False

    def _send_play_chunks(self, wav_bytes):
        """分块发送 PCM + 每块 ACK"""
        pcm = getattr(self, "_pending_pcm", b"")
        if not pcm:
            return False
        sent = 0
        while sent < len(pcm):
            end = min(sent + CHUNK_SIZE, len(pcm))
            chunk = pcm[sent:end]
            try:
                self.sock.sendall(chunk)
            except Exception as e:
                print(f"{self.log_prefix} send chunk failed: {e}")
                return False

            # 等待 ACK
            ack = self._recv_exact(len(PROTOCOL_ACK))
            if ack != PROTOCOL_ACK:
                print(f"{self.log_prefix} ACK mismatch: {ack!r}")
                self.stop_event.set()
                return False
            sent = end
        self._pending_pcm = b""
        return True


# ============================================================
# Server
# ============================================================

class AudioServer:
    """TCP listener：接受 ESP32 客户端，为每个连接起一个 ClientSession"""

    def __init__(self, host="0.0.0.0", port=8888, llm_engine=None):
        self.host = host
        self.port = int(port)
        self.llm_engine = llm_engine
        self.stop_event = threading.Event()
        self._sessions = set()
        self._lock = threading.Lock()

    def start(self):
        self._server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._server.bind((self.host, self.port))
        self._server.listen(config.SERVER_MAX_CLIENTS)
        self._server.settimeout(0.5)
        print(f"[server] listening on {self.host}:{self.port} "
              f"(engine={self.llm_engine or config.DEFAULT_LLM_ENGINE})")
        return self

    def serve_forever(self):
        try:
            while not self.stop_event.is_set():
                try:
                    sock, addr = self._server.accept()
                except socket.timeout:
                    continue
                except OSError:
                    break

                # TCP_NODELAY：减少首包延迟
                sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                sock.settimeout(config.TCP_IO_TIMEOUT)

                sess = ClientSession(sock, addr, self.llm_engine)
                sess.start()
                with self._lock:
                    self._sessions.add(sess)
        except KeyboardInterrupt:
            pass
        finally:
            self.stop()

    def stop(self):
        self.stop_event.set()
        with self._lock:
            for s in list(self._sessions):
                s.stop_event.set()
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
    parser = argparse.ArgumentParser(description="ESP32 Voice AI - Wi-Fi Server")
    parser.add_argument("--host", default=config.WIFI_HOST_BIND)
    parser.add_argument("--port", type=int, default=config.WIFI_TCP_PORT)
    parser.add_argument("--engine", default=None,
                        help="LLM engine: sensenova / ollama / gemini")
    args = parser.parse_args()

    server = AudioServer(host=args.host, port=args.port, llm_engine=args.engine)
    server.start()
    print("[server] waiting for ESP32 clients...")
    print(f"[server] current llm engine: {server.llm_engine}")
    server.serve_forever()


if __name__ == "__main__":
    main()
