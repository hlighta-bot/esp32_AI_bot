// ============================================================
// PersonDetector · Step 12-2-A (优化版实现)
//
// 完整流程：
//   1. buildSkinMask     : RGB565 → YCbCr → 加宽 LuvSkin 阈值 → mask
//   2. morphologyOpen3x3 : 3x3 erode + dilate 去除孤立噪点
//   3. bfsComponents     : 8-邻域 BFS 连通域（队列 4096）
//   4. filterCandidates  : 面积 / 填充率 / 宽高比 / bbox 尺寸 硬过滤
//   5. scoreCandidate    : area + fill + aspect + position 加权打分
//   6. temporal          : N-confirm / M-loss 状态机
//   7. smoothBbox        : EMA 平滑（定点 α=9/32）
//
// 内存（全部静态，无 heap）：
//   s_mask       : 2400 B
//   s_mask2      : 2400 B  (opening 中间缓冲)
//   s_visited    : 2400 B
//   s_bfs_queue  : 8192 B  (4096 × uint16_t)
//   s_components : ~2.6 KB (256 × ComponentInfo)
//   合计约 18.4 KiB
//
// 关键取舍：
//   - BFS 队列从 1024 扩到 4096：QQVGA 上人脸连通域可达 3000+ 像素，
//     旧版会因队列溢出丢弃整块，是漏检主因。
//   - 加宽 Cb/Cr 阈值：原版只覆盖浅肤色，深肤色漏检率高。
//   - 硬过滤比原"最大候选优先"更严格，配合打分避免暖色背景吞过真脸。
// ============================================================

#include "person_detector.h"

#include <cstring>


// ============================================================================
// 静态缓冲区
// ============================================================================

static uint8_t  s_mask[MASK_BYTES];              // 原始 skin mask
static uint8_t  s_mask2[MASK_BYTES];             // morphology opening 输出
static uint8_t  s_visited[MASK_BYTES];           // BFS visited
static uint16_t s_bfs_queue[BFS_QUEUE_SIZE];     // BFS 队列

// 组件元数据
struct ComponentInfo
{
    int x0;
    int y0;
    int x1;
    int y1;
    int pixel_count;
};

static ComponentInfo s_components[MAX_COMPONENTS];


// ============================================================================
// 时间稳定性 + 平滑状态
// ============================================================================

// 上一次 EMA 平滑后的 bbox（Q8 定点：整像素 × 256，保留亚像素精度）
static int16_t s_prevCxp = 0;   // center_x * 256
static int16_t s_prevCyp = 0;   // center_y * 256
static int16_t s_prevWp  = 0;   // width    * 256
static int16_t s_prevHp  = 0;   // height   * 256
static bool    s_prevValid = false;

// 时间稳定性计数器
static int s_confirmFrames = 0;  // 连续检测到候选的帧数
static int s_lossFrames    = 0;  // 连续丢失候选的帧数
static bool s_lastDetected = false;


// ============================================================================
// Bit 操作
// ============================================================================

static inline void setBit(uint8_t *arr, int idx)
{
    arr[idx >> 3] |= (uint8_t)(1u << (idx & 7));
}

static inline void clearBit(uint8_t *arr, int idx)
{
    arr[idx >> 3] &= (uint8_t)~(1u << (idx & 7));
}

static inline bool getBit(const uint8_t *arr, int idx)
{
    return (arr[idx >> 3] >> (idx & 7)) & 1u;
}


// ============================================================================
// RGB565 解包
//
//   [15:11] R (5bit)
//   [10:5]  G (6bit)
//   [4:0]   B (5bit)
//
// 线性扩展到 8bit（比右移零填充更平滑，减少颜色阶梯对阈值的影响）
// ============================================================================

static inline void unpackRGB565(
    uint16_t rgb565,
    uint8_t &r,
    uint8_t &g,
    uint8_t &b)
{
    int rawR = (rgb565 >> 11) & 0x1F;
    int rawG = (rgb565 >> 5)  & 0x3F;
    int rawB =  rgb565        & 0x1F;

    r = (uint8_t)((rawR << 3) | (rawR >> 2));
    g = (uint8_t)((rawG << 2) | (rawG >> 4));
    b = (uint8_t)((rawB << 3) | (rawB >> 2));
}


// ============================================================================
// YCbCr 计算（ITU-R BT.601 整数近似）
//
// Cb = 128 + (-43*R - 85*G + 128*B) >> 8
// Cr = 128 + (128*R - 107*G - 21*B) >> 8
//
// 注意：>> 8 在 C++ 中对负值采用 truncation toward zero，边界像素可能偏 1。
//       这个 ±1 抖动由下游形态学 + 时间稳定性吸收，此处不额外做 floor。
// ============================================================================

static inline void rgbToCbCr(
    uint8_t r,
    uint8_t g,
    uint8_t b,
    int &cb,
    int &cr)
{
    cb = 128 + ((-43 * r - 85 * g + 128 * b) >> 8);
    cr = 128 + ((128 * r - 107 * g - 21 * b) >> 8);
}


// ============================================================================
// 肤色判断（LuvSkin 加宽版）
//
// 相比经典 LuvSkin (Cb 77-127, Cr 133-173)：
//   Cb 上限 127 → 135：覆盖偏冷/偏灰皮肤
//   Cr 下限 133 → 128：覆盖深肤色
//   Cr 上限 173 → 180：覆盖亮肤色 + 光斑边缘
//
// 权衡：加宽后背景误检会略增，靠 morphology + 面积/填充率/宽高比过滤兜底。
// ============================================================================

static inline bool isSkin(int cb, int cr)
{
    return (cb >= SKIN_CB_MIN && cb <= SKIN_CB_MAX) &&
           (cr >= SKIN_CR_MIN && cr <= SKIN_CR_MAX);
}


// ============================================================================
// Step 1: 构建 skin mask
// ============================================================================

static int buildSkinMask(const uint8_t *rgb565)
{
    memset(s_mask, 0, MASK_BYTES);

    int skinCount = 0;

    for (int i = 0; i < DETECTION_PIXELS; i++)
    {
        uint16_t px =
            (uint16_t)rgb565[i * 2] |
            ((uint16_t)rgb565[i * 2 + 1] << 8);

        uint8_t r, g, b;
        unpackRGB565(px, r, g, b);

        int cb, cr;
        rgbToCbCr(r, g, b, cb, cr);

        if (isSkin(cb, cr))
        {
            setBit(s_mask, i);
            skinCount++;
        }
    }

    return skinCount;
}


// ============================================================================
// Step 2: 3x3 形态学 opening = erode + dilate
//
// erode  : 中心 skin 且 8 邻居中 >= MIN_8_NEIGHBOR 个也是 skin 才保留
//          （去孤立噪点，抗 YCbCr ±1 抖动）
// dilate : 任一 8 邻居是 skin 就标记
//          （恢复边界像素，避免过度收缩）
//
// 内存：输入 s_mask，输出 s_mask2。erode 阶段读 s_mask、写 s_mask2；
//       dilate 阶段读 s_mask2 中的 erode 结果、写回 s_mask2。
//       为避免"边读边写"污染，dilate 阶段先拷贝 erode 结果到局部视图。
//
// 说明：为了减少内存和复杂度，这里 dilate 直接迭代两遍，第一遍写 s_mask2'
//       （暂时借用另一个静态缓冲 s_mask 作为中间缓冲不合适，因为它已被
//       erode 结果覆盖——见下面的实现顺序）。
//
// 简化：先用 s_mask2 做 erode 结果，再把它复制回 s_mask 之前的 erode 版本
//       不需要——直接两阶段：
//         阶段 A: erode(s_mask) -> s_mask2
//         阶段 B: dilate(s_mask2) -> s_mask2（用另一块 in-place 缓冲 s_mask）
// ============================================================================

static int morphologyOpen3x3()
{
    const int MIN_8_NEIGHBOR = 4;  // erode 阈值

    // ---- 阶段 A: erode ----
    // 遍历每个像素，统计 s_mask 中 8 邻居 skin 数量
    // 中心非 skin 直接清；中心 skin 且邻居 >= 4 才保留
    memset(s_mask2, 0, MASK_BYTES);

    for (int y = 1; y < DETECTION_HEIGHT - 1; y++)
    {
        for (int x = 1; x < DETECTION_WIDTH - 1; x++)
        {
            int idx = y * DETECTION_WIDTH + x;

            if (!getBit(s_mask, idx))
            {
                continue;
            }

            int neighborCount = 0;
            for (int dy = -1; dy <= 1; dy++)
            {
                for (int dx = -1; dx <= 1; dx++)
                {
                    if (dx == 0 && dy == 0) continue;
                    if (getBit(s_mask, (y + dy) * DETECTION_WIDTH + (x + dx)))
                    {
                        neighborCount++;
                    }
                }
            }

            if (neighborCount >= MIN_8_NEIGHBOR)
            {
                setBit(s_mask2, idx);
            }
        }
    }

    // ---- 阶段 B: dilate ----
    // 先把 erode 结果复制到 s_mask（作为 dilate 输入）
    // 然后在 s_mask2 上做"任一邻居为 skin"的扩展
    memcpy(s_mask, s_mask2, MASK_BYTES);
    memset(s_mask2, 0, MASK_BYTES);

    for (int y = 1; y < DETECTION_HEIGHT - 1; y++)
    {
        for (int x = 1; x < DETECTION_WIDTH - 1; x++)
        {
            int idx = y * DETECTION_WIDTH + x;

            if (getBit(s_mask, idx))
            {
                setBit(s_mask2, idx);
                continue;
            }

            // 任一 8 邻居是 skin
            bool any = false;
            for (int dy = -1; dy <= 1 && !any; dy++)
            {
                for (int dx = -1; dx <= 1; dx++)
                {
                    if (dx == 0 && dy == 0) continue;
                    if (getBit(s_mask, (y + dy) * DETECTION_WIDTH + (x + dx)))
                    {
                        any = true;
                        break;
                    }
                }
            }

            if (any)
            {
                setBit(s_mask2, idx);
            }
        }
    }

    // 计算 skin 总数（供 debug）
    int skinCount = 0;
    for (int y = 0; y < DETECTION_HEIGHT; y++)
    {
        for (int x = 0; x < DETECTION_WIDTH; x++)
        {
            if (getBit(s_mask2, y * DETECTION_WIDTH + x))
            {
                skinCount++;
            }
        }
    }

    // 把结果写回 s_mask，供 BFS 使用
    memcpy(s_mask, s_mask2, MASK_BYTES);

    return skinCount;
}


// ============================================================================
// Step 3: BFS 8-邻域连通域
//
// 关键改动 vs 原版：
//   1. BFS_QUEUE_SIZE 1024 → 4096（防止 QQVGA 图大人脸连通域溢出丢弃）
//   2. 移除"非皮肤邻居也标记 visited"：原版会把背景大块吞掉；
//      新版只标记 skin 且已访问的像素。
//      副作用：BFS 每次邻居检查会重复扫描非 skin 邻居。QQVGA 规模下可忽略。
//
// 输出：s_components[]，返回通过面积/尺寸过滤的候选数量
// ============================================================================

static int bfsComponents()
{
    memset(s_visited, 0, MASK_BYTES);

    int componentCount = 0;

    for (int y = 0; y < DETECTION_HEIGHT; y++)
    {
        for (int x = 0; x < DETECTION_WIDTH; x++)
        {
            int idx = y * DETECTION_WIDTH + x;

            if (getBit(s_visited, idx))
            {
                continue;
            }

            if (!getBit(s_mask, idx))
            {
                // 非 skin 像素不做 BFS 起点，也不做标记（下次外层循环会略过）
                continue;
            }

            // 新组件起点
            setBit(s_visited, idx);

            int qHead = 0;
            int qTail = 0;

            s_bfs_queue[qTail++] = (uint16_t)idx;

            int compX0 = x, compY0 = y;
            int compX1 = x, compY1 = y;
            int compPixels = 0;
            bool overflow = false;

            while (qHead < qTail)
            {
                uint16_t cur = s_bfs_queue[qHead++];

                int cx = cur % DETECTION_WIDTH;
                int cy = cur / DETECTION_WIDTH;

                compPixels++;

                if (cx < compX0) compX0 = cx;
                if (cx > compX1) compX1 = cx;
                if (cy < compY0) compY0 = cy;
                if (cy > compY1) compY1 = cy;

                for (int dy = -1; dy <= 1; dy++)
                {
                    for (int dx = -1; dx <= 1; dx++)
                    {
                        if (dx == 0 && dy == 0) continue;

                        int nx = cx + dx;
                        int ny = cy + dy;

                        if (
                            nx < 0 || nx >= DETECTION_WIDTH ||
                            ny < 0 || ny >= DETECTION_HEIGHT
                        )
                        {
                            continue;
                        }

                        int nIdx = ny * DETECTION_WIDTH + nx;

                        if (getBit(s_visited, nIdx))
                        {
                            continue;
                        }

                        if (!getBit(s_mask, nIdx))
                        {
                            // 非 skin：不标记 visited（保留给下一个 component 扫描）
                            continue;
                        }

                        setBit(s_visited, nIdx);

                        if (qTail >= BFS_QUEUE_SIZE)
                        {
                            overflow = true;
                            break;
                        }

                        s_bfs_queue[qTail++] = (uint16_t)nIdx;
                    }

                    if (overflow) break;
                }

                if (overflow) break;
            }

            if (overflow)
            {
                // 队列溢出（4096 仍溢出即超大区域，正常人体不会触发）
                // 仍尝试保存但标记 pixel_count = BFS_QUEUE_SIZE，后续评分会压低
                compPixels = BFS_QUEUE_SIZE;
            }

            // ---- 硬过滤 ----

            if (compPixels < MIN_COMPONENT_AREA)
            {
                continue;
            }

            int boxW = compX1 - compX0 + 1;
            int boxH = compY1 - compY0 + 1;

            if (boxW < MIN_BOX_W || boxH < MIN_BOX_H)
            {
                continue;
            }

            // 面积占图像比例
            if ((float)compPixels / (float)DETECTION_PIXELS > MAX_COMPONENT_AREA_FRAC)
            {
                continue;
            }

            // bbox 占图像比例
            if (
                (float)boxW / (float)DETECTION_WIDTH > MAX_BOX_W_FRAC ||
                (float)boxH / (float)DETECTION_HEIGHT > MAX_BOX_H_FRAC
            )
            {
                continue;
            }

            // 填充率
            int boxArea = boxW * boxH;
            if (boxArea > 0)
            {
                float fill = (float)compPixels / (float)boxArea;
                if (fill < MIN_COMPONENT_FILL || fill > MAX_COMPONENT_FILL)
                {
                    continue;
                }
            }

            // 宽高比
            float aspect = (float)boxW / (float)boxH;
            if (aspect < MIN_COMPONENT_ASPECT || aspect > MAX_COMPONENT_ASPECT)
            {
                continue;
            }

            // 保存到元数据
            if (componentCount < MAX_COMPONENTS)
            {
                s_components[componentCount].x0 = compX0;
                s_components[componentCount].y0 = compY0;
                s_components[componentCount].x1 = compX1;
                s_components[componentCount].y1 = compY1;
                s_components[componentCount].pixel_count = compPixels;
                componentCount++;
            }
        }
    }

    return componentCount;
}


// ============================================================================
// Step 4: 候选打分（HEURISTIC，不是 ML confidence）
//
// 4 个维度：
//   area    权重 0.30 — bbox 面积在 4%-25% 图像占比为满分
//   fill    权重 0.25 — 填充率 0.40-0.75 为满分
//   aspect  权重 0.25 — 宽高比 0.30-0.90 为满分（人体偏竖长）
//   position 权重 0.20 — 中心 y 在 0.20-0.55（上半图）最高分
//
// 满分 = 1.0，最低 = 0.0
// ============================================================================

static float scoreCandidate(const ComponentInfo *c)
{
    int boxW = c->x1 - c->x0 + 1;
    int boxH = c->y1 - c->y0 + 1;
    int boxArea = boxW * boxH;
    int cx = (c->x0 + c->x1) / 2;
    int cy = (c->y0 + c->y1) / 2;

    // ---- area 分 ----
    float areaFrac =
        (float)boxArea / (float)DETECTION_PIXELS;
    float areaScore;
    if (areaFrac < 0.04f)
    {
        areaScore = areaFrac / 0.04f * 0.4f;
    }
    else if (areaFrac <= 0.25f)
    {
        areaScore = 1.0f;
    }
    else
    {
        areaScore = 1.0f - (areaFrac - 0.25f) * 2.0f;
        if (areaScore < 0.0f) areaScore = 0.0f;
    }

    // ---- fill 分 ----
    float fill = boxArea > 0
        ? (float)c->pixel_count / (float)boxArea
        : 0.0f;
    float fillScore;
    if (fill >= 0.40f && fill <= 0.75f)
    {
        fillScore = 1.0f;
    }
    else if (fill < 0.40f)
    {
        fillScore = fill / 0.40f * 0.6f;
    }
    else
    {
        fillScore = 1.0f - (fill - 0.75f) * 2.0f;
        if (fillScore < 0.0f) fillScore = 0.0f;
    }

    // ---- aspect 分 ----
    float aspect = (float)boxW / (float)boxH;
    float aspectScore;
    if (aspect >= 0.30f && aspect <= 0.90f)
    {
        aspectScore = 1.0f;
    }
    else if (aspect < 0.30f)
    {
        aspectScore = aspect / 0.30f * 0.5f;
    }
    else
    {
        aspectScore = 1.0f - (aspect - 0.90f) * 0.8f;
        if (aspectScore < 0.0f) aspectScore = 0.0f;
    }

    // ---- position 分（人脸通常偏上）----
    float cyNorm = (float)cy / (float)DETECTION_HEIGHT;
    float posScore;
    if (cyNorm >= 0.20f && cyNorm <= 0.55f)
    {
        // 中心 y 在 0.375（理想人脸位置）附近满分
        float dist = (cyNorm < 0.375f)
            ? (0.375f - cyNorm)
            : (cyNorm - 0.375f);
        posScore = 1.0f - dist * 3.0f;
        if (posScore < 0.0f) posScore = 0.0f;
    }
    else if (cyNorm < 0.20f)
    {
        // 太靠上（头顶/镜头反光）
        posScore = cyNorm / 0.20f * 0.3f;
    }
    else
    {
        // 太靠下（地面/桌子）
        posScore = (1.0f - cyNorm) * 0.5f;
        if (posScore < 0.0f) posScore = 0.0f;
    }

    float conf =
        CONF_WEIGHT_AREA   * areaScore +
        CONF_WEIGHT_FILL   * fillScore +
        CONF_WEIGHT_ASPECT * aspectScore +
        CONF_WEIGHT_POS    * posScore;

    if (conf < 0.0f) conf = 0.0f;
    if (conf > 1.0f) conf = 1.0f;

    return conf;
}


// ============================================================================
// bbox 平滑 (EMA, 定点 α=9/32)
//
// new = (9 * curr + 23 * prev) >> 5
// 输入：Q8 定点（× 256）
// 输出：Q8 定点
// ============================================================================

static inline int16_t emaMerge(int16_t prev, int16_t curr)
{
    int32_t s = 9 * (int32_t)curr + 23 * (int32_t)prev;
    return (int16_t)(s >> 5);
}


// ============================================================================
// 时间稳定性状态机
//
//   rawDetected : 单帧算法是否找到候选
//   detectedOut : 经状态机过滤后的输出状态
//
// 状态转换：
//   raw=true 且 confirmFrames 达阈值 → detected=true
//   raw=false 且 lossFrames 达阈值  → detected=false
//   中间状态保持 detected 上一帧的值
//
// 返回：detectedOut
// ============================================================================

static bool applyTemporal(bool rawDetected)
{
    if (rawDetected)
    {
        s_confirmFrames++;
        s_lossFrames = 0;

        if (s_confirmFrames >= TEMPORAL_CONFIRM_FRAMES)
        {
            s_lastDetected = true;
        }
    }
    else
    {
        s_lossFrames++;
        s_confirmFrames = 0;

        if (s_lossFrames >= TEMPORAL_LOSS_FRAMES)
        {
            s_lastDetected = false;
        }
    }

    return s_lastDetected;
}


// ============================================================================
// resetPersonDetectorState —— 清空时间稳定性 / 平滑状态
// ============================================================================

void resetPersonDetectorState(void)
{
    s_prevCxp = 0;
    s_prevCyp = 0;
    s_prevWp  = 0;
    s_prevHp  = 0;
    s_prevValid = false;

    s_confirmFrames = 0;
    s_lossFrames    = 0;
    s_lastDetected  = false;
}


// ============================================================================
// 主入口 detectPerson
// ============================================================================

bool detectPerson(
    const uint8_t *rgb565,
    PersonDetection *out,
    PersonDebugInfo *dbg)
{
    if (rgb565 == nullptr || out == nullptr)
    {
        return false;
    }

    // 输出初始化（保持字段顺序，方便调用方 {false, 0, 0, 0, 0} 部分初始化）
    out->detected   = false;
    out->center_x   = 0.0f;
    out->center_y   = 0.0f;
    out->width      = 0.0f;
    out->height     = 0.0f;
    out->confidence = 0.0f;

    if (dbg != nullptr)
    {
        dbg->component_count    = 0;
        dbg->best_score         = 0;
        dbg->temporal_confirm   = 0;
        dbg->temporal_loss      = 0;
        dbg->smoothing_active   = false;
        dbg->detected           = false;
        dbg->skin_pixels_total  = 0;
    }

    // ---- Step 1: skin mask ----
    buildSkinMask(rgb565);

    // ---- Step 2: morphology opening ----
    int skinPixelsAfterMorph = morphologyOpen3x3();
    if (dbg != nullptr)
    {
        dbg->skin_pixels_total = skinPixelsAfterMorph;
    }

    // ---- Step 3: BFS 连通域 + 硬过滤 ----
    int candidateCount = bfsComponents();
    if (dbg != nullptr)
    {
        dbg->component_count = candidateCount;
    }

    // ---- Step 4: 打分选最优 ----
    int     bestIdx  = -1;
    float   bestConf = 0.0f;

    for (int i = 0; i < candidateCount; i++)
    {
        float conf = scoreCandidate(&s_components[i]);
        if (conf > bestConf)
        {
            bestConf = conf;
            bestIdx  = i;
        }
    }

    if (dbg != nullptr)
    {
        dbg->best_score = (int)(bestConf * 100.0f + 0.5f);
    }

    // ---- Step 5: 原始 detected ----
    bool rawDetected =
        (bestIdx >= 0) && (bestConf >= DETECTION_THRESHOLD);

    if (!rawDetected)
    {
        // 单帧没找到候选
        s_prevValid = false;

        out->detected = applyTemporal(false);
        out->confidence = bestConf;  // 保留原始分供 debug

        if (dbg != nullptr)
        {
            dbg->temporal_confirm = s_confirmFrames;
            dbg->temporal_loss    = s_lossFrames;
            dbg->detected         = out->detected;
        }
        return true;
    }

    // ---- Step 6: 时间稳定性 ----
    bool finalDetected = applyTemporal(true);

    const ComponentInfo *best = &s_components[bestIdx];

    // ---- Step 7: bbox 平滑 ----
    // 只在时间稳定性通过后（finalDetected=true）应用 EMA；
    // 若上一帧未 valid，直接采用当前帧值作为 seed
    int rawCx = (best->x0 + best->x1) / 2;
    int rawCy = (best->y0 + best->y1) / 2;
    int rawW  = best->x1 - best->x0 + 1;
    int rawH  = best->y1 - best->y0 + 1;

    int16_t curCxp = (int16_t)(rawCx * 256);
    int16_t curCyp = (int16_t)(rawCy * 256);
    int16_t curWp  = (int16_t)(rawW  * 256);
    int16_t curHp  = (int16_t)(rawH  * 256);

    bool smoothingActive = false;

    if (s_prevValid)
    {
        s_prevCxp = emaMerge(s_prevCxp, curCxp);
        s_prevCyp = emaMerge(s_prevCyp, curCyp);
        s_prevWp  = emaMerge(s_prevWp,  curWp);
        s_prevHp  = emaMerge(s_prevHp,  curHp);
        smoothingActive = true;
    }
    else
    {
        s_prevCxp = curCxp;
        s_prevCyp = curCyp;
        s_prevWp  = curWp;
        s_prevHp  = curHp;
        s_prevValid = true;
    }

    if (dbg != nullptr)
    {
        dbg->smoothing_active = smoothingActive;
        dbg->temporal_confirm = s_confirmFrames;
        dbg->temporal_loss    = s_lossFrames;
        dbg->detected         = finalDetected;
    }

    if (!finalDetected)
    {
        // 时间稳定性还没达到阈值：不输出，但 confidence 保留
        out->detected = false;
        out->confidence = bestConf;
        return true;
    }

    // ---- Step 8: 归一化输出 ----
    float cxNorm = (float)s_prevCxp / (256.0f * (float)DETECTION_WIDTH);
    float cyNorm = (float)s_prevCyp / (256.0f * (float)DETECTION_HEIGHT);
    float wNorm  = (float)s_prevWp  / (256.0f * (float)DETECTION_WIDTH);
    float hNorm  = (float)s_prevHp  / (256.0f * (float)DETECTION_HEIGHT);

    // clamp
    if (cxNorm < 0.0f) cxNorm = 0.0f;
    if (cxNorm > 1.0f) cxNorm = 1.0f;
    if (cyNorm < 0.0f) cyNorm = 0.0f;
    if (cyNorm > 1.0f) cyNorm = 1.0f;
    if (wNorm  < 0.0f) wNorm  = 0.0f;
    if (wNorm  > 1.0f) wNorm  = 1.0f;
    if (hNorm  < 0.0f) hNorm  = 0.0f;
    if (hNorm  > 1.0f) hNorm  = 1.0f;

    out->detected   = true;
    out->center_x   = cxNorm;
    out->center_y   = cyNorm;
    out->width      = wNorm;
    out->height     = hNorm;
    out->confidence = bestConf;

    return true;
}
