"""
Wi-Fi / TCP 传输层（目标期主链路）
==================================

ESP32 与 server（PC 或 aidlux）通过 TCP 双向通信。

设计要点：
  - 只用 Python 标准库 socket（兼容 aidlux slim Python）
  - 单条 TCP 连接同时承载上下行，靠 4 字节 magic 帧区分
  - 关闭 Nagle（TCP_NODELAY），减少首包延迟
  - receive(size) 保证读满 size 字节，防 TCP 粘包/半包
  - in_waiting / readline 走非阻塞 peek，不破坏流

aidlux 兼容性：
  无第三方依赖，可原样搬进 aidlux 环境运行。
"""

import socket

from transport import TransportInterface
from config import TCP_CONNECT_TIMEOUT, TCP_IO_TIMEOUT


class WifiTransport(TransportInterface):
    """TCP 客户端：主动向 server 发起连接（PC 主叫模式）"""

    def __init__(self, ip, port=8888):
        self.ip = ip
        self.port = int(port)
        self.sock = None

    # --------------------------------------------------------
    # 连接 / 断开
    # --------------------------------------------------------

    def connect(self) -> bool:
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.settimeout(TCP_CONNECT_TIMEOUT)
            # 关闭 Nagle 算法，降低 chunk 首包延迟
            self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            self.sock.connect((self.ip, self.port))
            self.sock.settimeout(TCP_IO_TIMEOUT)
            return True
        except Exception:
            return False

    def disconnect(self):
        if self.sock:
            try:
                self.sock.close()
            except Exception:
                pass
            self.sock = None

    # --------------------------------------------------------
    # 发送
    # --------------------------------------------------------

    def send(self, data: bytes) -> bool:
        """阻塞写完整块；不保证一次 send 全部进入内核（TCP 流）"""
        if not self.sock:
            return False
        try:
            view = memoryview(data)
            while view:
                n = self.sock.send(view)
                if n <= 0:
                    return False
                view = view[n:]
            return True
        except Exception:
            return False

    # --------------------------------------------------------
    # 接收（读满 size 字节）
    # --------------------------------------------------------

    def receive(self, size: int) -> bytes:
        """循环 recv 直到读满 size 字节；连接关闭/超时返回已读部分"""
        if not self.sock or size <= 0:
            return b""
        chunks = []
        remaining = size
        while remaining > 0:
            try:
                buf = self.sock.recv(remaining)
            except socket.timeout:
                break
            except (ConnectionResetError, OSError):
                break
            if not buf:
                break
            chunks.append(buf)
            remaining -= len(buf)
        return b"".join(chunks)

    def receive_exact(self, size: int) -> bytes:
        """语义等价 receive，命名便于业务侧表达 '我要恰好这么多字节'"""
        return self.receive(size)

    # --------------------------------------------------------
    # 非阻塞探测 / 读一行（用于 PLAYBACK_FINISHED 之类的日志行）
    # --------------------------------------------------------

    def in_waiting(self) -> int:
        """TCP 无 in_waiting，返回 0 表示'未知'。
        业务侧应改用 receive_exact 或超时轮询。
        为兼容旧代码（send_wav.py 的 _wait_playback_finish）保留 0。
        """
        return 0

    def readline(self) -> bytes:
        """读一行到 \\n；非阻塞，无数据立即返回 b''"""
        if not self.sock:
            return b""
        try:
            old = self.sock.gettimeout()
        except Exception:
            old = None
        self.sock.settimeout(0)
        line = b""
        try:
            while True:
                b = self.sock.recv(1)
                if not b:
                    break
                line += b
                if b == b"\n":
                    break
        except (BlockingIOError, socket.timeout):
            pass
        except Exception:
            pass
        finally:
            if old is not None:
                try:
                    self.sock.settimeout(old)
                except Exception:
                    pass
        return line


class WifiTransportUdp:
    """UDP 变体，仅用于过渡期调试。不参与 TransportInterface。

    保留以便离线抓包 / 与旧 mock server 联调。
    """

    def __init__(self, ip, port=8888):
        self.addr = (ip, int(port))
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def connect(self) -> bool:
        return True  # UDP 无连接

    def disconnect(self):
        try:
            self.sock.close()
        except Exception:
            pass

    def send(self, data: bytes) -> bool:
        return self.sock.sendto(data, self.addr) > 0

    def receive(self, size: int) -> bytes:
        try:
            data, _ = self.sock.recvfrom(max(size, 4096))
            return data[:size]
        except Exception:
            return b""

    def in_waiting(self) -> int:
        try:
            self.sock.setblocking(False)
            try:
                data, _ = self.sock.recvfrom(4096)
                self.sock.setblocking(True)
                return len(data)
            except BlockingIOError:
                self.sock.setblocking(True)
                return 0
        except Exception:
            return 0

    def readline(self) -> bytes:
        try:
            self.sock.setblocking(False)
            try:
                data, _ = self.sock.recvfrom(4096)
                self.sock.setblocking(True)
                return data
            except BlockingIOError:
                self.sock.setblocking(True)
                return b""
        except Exception:
            return b""
