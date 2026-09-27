// ============================================================
// frame.h - PC ↔ ESP32 协议常量与 pack/unpack 工具
// ============================================================
//
// 与 pc/config.py 中的 PROTOCOL_* 保持一致。
// 字节序：全部 little-endian。
//
// 下行（PC → ESP32）：
//   PLAY | u32 size        | chunk + ACK ×N
//
// 上行（ESP32 → PC）：
//   RECM | u16 flags       | u32 chunk_size | pcm (<=4096)
//
// ACK 复用下行 ACK：
//   ACK  (3 bytes)
// ============================================================

#ifndef FRAME_H
#define FRAME_H

#include <Arduino.h>

// ------------------------------------------------------------
// Magic 命令
// ------------------------------------------------------------
//
// 这里使用固定长度二进制数据，不包含 '\0'。
// 因此使用 uint8_t，而不是 char[] 字符串。
// ------------------------------------------------------------

static const uint8_t PROTO_PLAY[4] = {'P', 'L', 'A', 'Y'};
static const uint8_t PROTO_REC[4]  = {'R', 'E', 'C', 'M'};
static const uint8_t PROTO_RPTF[4] = {'R', 'P', 'T', 'F'};
static const uint8_t PROTO_ACK[3]  = {'A', 'C', 'K'};

// SVCO: 独立舵机控制协议 (PC → ESP32)
//   SVCO | u8 command | i16 parameter (little-endian)
//   command 枚举见 SERVO_CMD_* 定义（与 pc/config.py 保持一致）
static const uint8_t PROTO_SERVO[4] = {'S', 'V', 'C', 'O'};

// ------------------------------------------------------------
// SVCO command 编码（PC 与 ESP32 必须保持一致）
//
// 载荷: u8 command + i16 parameter (little-endian)
// 第一版 parameter 保留，当前所有命令均忽略（值为 0）。
// ------------------------------------------------------------

#define SERVO_CMD_VERTICAL_UP          0x01
#define SERVO_CMD_VERTICAL_DOWN        0x02
#define SERVO_CMD_HORIZONTAL_LEFT      0x11
#define SERVO_CMD_HORIZONTAL_RIGHT     0x12
#define SERVO_CMD_VERTICAL_CENTER      0x21
#define SERVO_CMD_HORIZONTAL_CENTER    0x22
#define SERVO_CMD_CENTER_ALL           0x2F

#define SVCO_PAYLOAD_LEN  3  // u8 cmd + i16 param

// ------------------------------------------------------------
// RECM flags 位
// ------------------------------------------------------------

#define REC_FLAG_FIRST        0x0001   // bit0: 该段录音首包
#define REC_FLAG_VAD_TRIGGER  0x0002   // bit1: VAD 触发点

// ------------------------------------------------------------
// 包大小（与 pc/config.py::CHUNK_SIZE 一致）
// ------------------------------------------------------------

#define FRAME_CHUNK_SIZE      4096

// ------------------------------------------------------------
// 打包工具：把 int 序列化为 little-endian 字节
// ------------------------------------------------------------

inline void packU16(uint8_t *out, uint16_t v)
{
    out[0] = (uint8_t)(v & 0xFF);
    out[1] = (uint8_t)((v >> 8) & 0xFF);
}

inline void packU32(uint8_t *out, uint32_t v)
{
    out[0] = (uint8_t)(v & 0xFF);
    out[1] = (uint8_t)((v >> 8) & 0xFF);
    out[2] = (uint8_t)((v >> 16) & 0xFF);
    out[3] = (uint8_t)((v >> 24) & 0xFF);
}

inline uint16_t unpackU16(const uint8_t *in)
{
    return (uint16_t)(in[0] | (in[1] << 8));
}

inline uint32_t unpackU32(const uint8_t *in)
{
    return (uint32_t)in[0]
         | ((uint32_t)in[1] << 8)
         | ((uint32_t)in[2] << 16)
         | ((uint32_t)in[3] << 24);
}

#endif  // FRAME_H