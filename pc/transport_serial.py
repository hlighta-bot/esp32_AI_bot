import serial
from transport import TransportInterface
from config import ESP32_BAUD, SERIAL_TIMEOUT

class SerialTransport(TransportInterface):
    def __init__(self, port):
        self.port = port
        self.ser = None

    def connect(self) -> bool:
        try:
            self.ser = serial.Serial(self.port, ESP32_BAUD, timeout=SERIAL_TIMEOUT)
            return True
        except Exception:
            return False

    def disconnect(self):
        if self.ser:
            self.ser.close()

    def send(self, data: bytes) -> bool:
        return self.ser.write(data) == len(data)

    def receive(self, size: int) -> bytes:
        return self.ser.read(size)

    def in_waiting(self) -> int:
        """返回串口缓冲区中已可读的字节数（封装 pyserial 的 in_waiting 属性）"""
        if self.ser is None:
            return 0
        return self.ser.in_waiting

    def readline(self) -> bytes:
        """读取一行（以 \n 结尾），用于接收 ESP32 日志/协议字符串"""
        if self.ser is None:
            return b''
        return self.ser.readline()
