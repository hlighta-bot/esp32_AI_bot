// ============================================================
// PersonDetector · Step 12-2-A (优化版)
//
// 目的：
//   在 QQVGA 160x120 RGB565 图像上做低资源人物区域候选检测。
//
// 算法（轻量级传统视觉，不含任何 ML）：
//   1. RGB565 -> 8bit RGB (线性扩展)
//   2. RGB -> YCbCr (ITU-R BT.601, 整数近似)
//   3. YCbCr 肤色阈值 (LuvSkin 加宽版, 覆盖深肤色)
//   4. bit-packed skin mask (2400 B)
//   5. 形态学 opening (3x3 erode + dilate) 去除孤立噪声
//   6. 8-邻域 BFS 连通域 (queue 4096, 覆盖大人脸)
//   7. 硬过滤：面积 / 填充率 / 宽高比 / 外接框尺寸 / 最大占比
//   8. 候选打分：area + fill + aspect + position(人脸偏上)
//      —— 这是 HEURISTIC SCORE, 不是 ML confidence
//   9. 时间稳定性：N-frames-confirm / M-frames-loss 状态机
//   10. bbox EMA 平滑（定点整数 α=0.35）
//
// 输出：
//   PersonDetection (归一化 0.0~1.0) + 可选 PersonDebugInfo
//
// 内存 (全部静态, 无 heap):
//   mask           : 2400 B  (bit-packed skin)
//   mask2 (morph)  : 2400 B  (opening 中间缓冲)
//   visited        : 2400 B  (bit-packed visited)
//   bfs_queue      : 8192 B  (4096 x uint16_t)
//   components     : ~1.3 KB (MAX_COMPONENTS 元数据)
//   总占用 ~ 16.7 KiB, ESP32-CAM 320 KiB SRAM 充裕
//
// 不做：
//   YOLO / TFLite / 人脸识别 / ReID / 神经网络 / face detection
//   （face detection 是 S12-4 的目标，本模块仅输出"人物区域候选"）
//
// 已知局限：
//   - YCbCr 阈值是启发式，对深肤色覆盖较好，但对强逆光 / 阴影 /
//     暖色调背景（木地板、砖墙）仍可能误检。
//   - 单 RGB565 帧无法判定"脸"或"人"，只能给"肤色区域候选"。
//   - confidence 是启发式分数，绝对值不可与 ML 置信度比较。
// ============================================================

#ifndef PERSON_DETECTOR_H
#define PERSON_DETECTOR_H

#include <cstdint>
#include <cstddef>


// ============================================================================
// 尺寸常量
// ============================================================================

#define DETECTION_WIDTH   160
#define DETECTION_HEIGHT  120
#define DETECTION_PIXELS  (DETECTION_WIDTH * DETECTION_HEIGHT)  // 19200

// bit-packed mask 大小 = 19200 / 8
#define MASK_BYTES        (DETECTION_PIXELS / 8)                 // 2400

// BFS 队列：1024 -> 4096（防止 QQVGA 图上的人脸连通域被丢）
#define BFS_QUEUE_SIZE    4096

// 组件元数据上限
#define MAX_COMPONENTS    256


// ============================================================================
// 肤色阈值 (YCbCr, ITU-R BT.601)
//
// 经典 LuvSkin: Cb 77-127, Cr 133-173  —— 只对浅肤色可靠。
// 本实现使用加宽版:  Cb 77-135, Cr 128-180  —— 覆盖深肤色 / 室内暖光。
//
//   Cb 下限 77 保持（更蓝 = 非皮肤）
//   Cb 上限 135 加宽（原 127，覆盖偏灰/偏冷皮肤）
//   Cr 下限 128 加宽（原 133，覆盖深肤色）
//   Cr 上限 180 加宽（原 173，覆盖亮肤色 + 光斑边缘）
//
// 权衡：加宽降低漏检，但会引入少量背景误检（木地板、肤色相近的家具）。
//       由后续形态学 + 面积/填充率/宽高比过滤 + 时间稳定性兜底。
// ============================================================================

#define SKIN_CB_MIN       77
#define SKIN_CB_MAX       135
#define SKIN_CR_MIN      128
#define SKIN_CR_MAX      180


// ============================================================================
// 形态学 opening (3x3) 去噪
//
//   - 先 3x3 erode：中心 skin 且 8 邻居中至少 4 个也是 skin 才保留
//     （去除孤立 1-2 像素的噪点）
//   - 再 3x3 dilate：任一 8 邻居是 skin 就标记（恢复边界像素）
//
//   目的：解决 YCbCr 阈值抖动导致的"散点"假阳性
//   风险：极端小物体（手指尖）可能被吃掉，本任务可接受
// ============================================================================


// ============================================================================
// 硬过滤（任何一条不满足即丢弃候选）
// ============================================================================

#define MIN_COMPONENT_AREA       60     // 最小连通像素数（原 30，噪声太大）
#define MAX_COMPONENT_AREA_FRAC  0.25f  // 最大占比（原 0.40，0.40 会把背景吞）
#define MIN_COMPONENT_FILL       0.20f  // 填充率下限（skin_pixels / bbox_area）
#define MAX_COMPONENT_FILL       0.95f  // 填充率上限（太满 = 平坦背景块）
#define MIN_COMPONENT_ASPECT     0.20f  // bbox 宽高比下限（W/H）
#define MAX_COMPONENT_ASPECT     1.20f  // bbox 宽高比上限
#define MIN_BOX_W                6      // bbox 最小宽（像素）
#define MIN_BOX_H                8      // bbox 最小高（像素）
#define MAX_BOX_W_FRAC           0.60f  // bbox 宽不超过图像宽 60%
#define MAX_BOX_H_FRAC           0.70f  // bbox 高不超过图像高 70%


// ============================================================================
// 候选打分（HEURISTIC，不是 ML confidence）
//
//   area     : bbox 面积在 4%-25% 图像占比为满分
//   fill     : 填充率 0.40-0.75 为满分
//   aspect   : 宽高比 0.30-0.90 为满分（人体偏竖长）
//   position : 中心 y 在 0.20-0.55（上半图）加权最高
//
//   综合 conf = 0.30*area + 0.25*fill + 0.25*aspect + 0.20*position
//   clamp 到 [0.0, 1.0]
// ============================================================================

#define CONF_WEIGHT_AREA     0.30f
#define CONF_WEIGHT_FILL     0.25f
#define CONF_WEIGHT_ASPECT   0.25f
#define CONF_WEIGHT_POS      0.20f

// 检测阈值：分数低于此认为不是候选
// 0.30 -> 0.40（原版过低，随机色块容易过阈）
#define DETECTION_THRESHOLD  0.40f


// ============================================================================
// 时间稳定性
//
//   TEMPORAL_CONFIRM_FRAMES = 3
//     连续 3 帧检测到候选才把 detected=true
//     （抑制单帧闪烁的假阳性）
//
//   TEMPORAL_LOSS_FRAMES = 2
//     连续 2 帧丢失才把 detected=false
//     （抑制单帧遮挡 / 眨眼导致的抖动）
// ============================================================================

#define TEMPORAL_CONFIRM_FRAMES  3
#define TEMPORAL_LOSS_FRAMES     2


// ============================================================================
// bbox 平滑 (EMA)
//
//   使用定点 α = 9/32 ≈ 0.281（目标 0.30-0.35）
//   new = (9 * curr) + (23 * prev) ) >> 5
//   纯整数运算，ESP32-CAM 无浮点开销。
//   仅在检测到且 previous.detected 为 true 时应用。
// ============================================================================


// ============================================================================
// PersonDetection —— 主输出（保持向后兼容）
//
// 调用方构造顺序（main.cpp）:
//   PersonDetection det = {false, 0, 0, 0, 0};
// 因此字段顺序 MUST 保持：detected, center_x, center_y, width, height, confidence
// ============================================================================

struct PersonDetection
{
    bool  detected;      // 是否检测到（经过时间稳定过滤后）
    float center_x;      // 0.0 ~ 1.0, 已 EMA 平滑
    float center_y;      // 0.0 ~ 1.0, 已 EMA 平滑
    float width;         // 0.0 ~ 1.0, 已 EMA 平滑
    float height;        // 0.0 ~ 1.0, 已 EMA 平滑
    float confidence;    // 0.0 ~ 1.0, 启发式综合评分（不是 ML confidence）
};


// ============================================================================
// PersonDebugInfo —— 可选诊断输出
//
// 供 HTTP / 串口日志使用，caller 决定何时打印（推荐 2-5 Hz）。
// 所有字段均为整型或定点缩放浮点，避免浮点格式化开销。
//
// 字段含义：
//   component_count      : 通过硬过滤的候选数量
//   best_score           : 当前帧最高候选分数（0-100 整数，= conf * 100）
//   temporal_confirm     : 连续检测到候选的帧数（0-TEMPORAL_CONFIRM_FRAMES）
//   temporal_loss        : 连续丢失候选的帧数（0-TEMPORAL_LOSS_FRAMES）
//   smoothing_active     : 是否正在应用 bbox EMA
//   detected             : 输出给上层的状态（经过时间过滤后）
//   skin_pixels_total    : 形态学之后的皮肤 mask 总像素数
// ============================================================================

struct PersonDebugInfo
{
    int     component_count;
    int     best_score;             // 0-100
    int     temporal_confirm;
    int     temporal_loss;
    bool    smoothing_active;
    bool    detected;
    int     skin_pixels_total;
};


// ============================================================================
// 主接口
//
// rgb565 : QQVGA 160x120 RGB565 图像数据 (38400 bytes)
// out    : 输出检测结果
// dbg    : 可选，NULL 表示不要诊断信息
//
// 返回 true 表示检测成功执行（不代表检测到人体）
// 返回 false 表示参数错误
//
// 无 heap 分配。可安全在 loop() / stream 每帧调用。
//
// 注意：本函数内部维护时间稳定性 / 平滑状态（静态变量）。
//       若需要重置，调用 resetPersonDetectorState()。
// ============================================================================

bool detectPerson(
    const uint8_t *rgb565,
    PersonDetection *out,
    PersonDebugInfo *dbg = nullptr
);


// ============================================================================
// 状态重置
//
// 清空时间稳定性计数与 bbox 平滑历史。
// 用于：Wi-Fi 重连、长时间断流恢复、单元测试隔离。
// ============================================================================

void resetPersonDetectorState(void);


#endif  // PERSON_DETECTOR_H
