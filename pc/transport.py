from abc import ABC, abstractmethod

class TransportInterface(ABC):
    @abstractmethod
    def connect(self) -> bool:
        pass

    @abstractmethod
    def disconnect(self):
        pass

    @abstractmethod
    def send(self, data: bytes) -> bool:
        """发送数据块"""
        pass

    @abstractmethod
    def receive(self, size: int) -> bytes:
        """接收指定大小的数据块"""
        pass

    @abstractmethod
    def in_waiting(self) -> int:
        """返回缓冲区中已可读的字节数（用于轮询 FINISHED 等日志行）"""
        pass

    @abstractmethod
    def readline(self) -> bytes:
        """读取一行文本（以 \n 结尾），用于接收 ESP32 状态字符串"""
        pass
