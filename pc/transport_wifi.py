import socket
from transport import TransportInterface

class WifiTransport(TransportInterface):
    def __init__(self, ip, port=8888):
        self.addr = (ip, port)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM) # UDP

    def connect(self) -> bool:
        return True # UDP 无需连接

    def disconnect(self):
        self.sock.close()

    def send(self, data: bytes) -> bool:
        return self.sock.sendto(data, self.addr) > 0

    def receive(self, size: int) -> bytes:
        data, _ = self.sock.recvfrom(size)
        return data

    def in_waiting(self) -> int:
        """UDP 非阻塞探测：有可收数据返回长度，否则返回 0"""
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
        """UDP 没有真正的"行"，此处返回一次收到的一包数据（最多 4096 字节）"""
        try:
            self.sock.setblocking(False)
            try:
                data, _ = self.sock.recvfrom(4096)
                self.sock.setblocking(True)
                return data
            except BlockingIOError:
                self.sock.setblocking(True)
                return b''
        except Exception:
            return b''
