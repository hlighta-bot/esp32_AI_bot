// ============================================================
// ESP32-CAM · Step 12-1 摄像头基础测试
//
// 目的：
//   AI-Thinker ESP32-CAM + OV2640
//   在浏览器中输出 MJPEG Stream
//
// 功能：
//   GET /         -> HTML 首页
//   GET /capture  -> 单帧 JPEG
//   GET /stream   -> MJPEG 连续视频流
//
// HTTP：
//   Port 80  -> / 、/capture、/stream
//
// 硬件：
//   AI-Thinker ESP32-CAM
//   OV2640
//
// 不做：
//   - 人物检测
//   - UART
//   - 舵机
//   - 主工程通信
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <esp_camera.h>
#include <esp_http_server.h>
#include <esp_timer.h>
#include <img_converters.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include "person_detector.h"
#include "draw_overlay.h"


// ============================================================================
// Wi-Fi
// ============================================================================

#define WIFI_SSID       "hqbwifi_36"
#define WIFI_PASS       "asdfghjk"

#define WIFI_IS_PLACEHOLDER \
    (strcmp(WIFI_SSID, "REPLACE_ME_SSID") == 0)


// ============================================================================
// AI-Thinker ESP32-CAM + OV2640 官方 Pin Map
// ============================================================================

#define PWDN_GPIO_NUM    32
#define RESET_GPIO_NUM   -1

#define XCLK_GPIO_NUM    0

#define SIOD_GPIO_NUM    26
#define SIOC_GPIO_NUM    27

#define Y2_GPIO_NUM      5
#define Y3_GPIO_NUM      18
#define Y4_GPIO_NUM      19
#define Y5_GPIO_NUM      21
#define Y6_GPIO_NUM      36
#define Y7_GPIO_NUM      39
#define Y8_GPIO_NUM      34
#define Y9_GPIO_NUM      35

#define VSYNC_GPIO_NUM   25
#define HREF_GPIO_NUM    23
#define PCLK_GPIO_NUM    22


// ============================================================================
// 板载 Flash LED
//
// AI-Thinker ESP32-CAM 常见白色 Flash LED = GPIO4
// ============================================================================

#define LED_GPIO_NUM     4

#define LED_ON           digitalWrite(LED_GPIO_NUM, HIGH)
#define LED_OFF          digitalWrite(LED_GPIO_NUM, LOW)


// ============================================================================
// 摄像头参数
//
// Step 12-2-A：切换到 RGB565 + QQVGA
//   - RGB565 让 PersonDetector 直接读像素（不做 JPEG 解码）
//   - QQVGA 160x120 = 38400 字节 framebuffer，可承受
//   - 帧经过检测 + overlay 绘制后，通过 frame2jpg_cb() 编码为 JPEG
// ============================================================================

#define CAM_PIXFORMAT    PIXFORMAT_RGB565

// QQVGA = 160 x 120
#define CAM_FRAMESIZE    FRAMESIZE_QQVGA

// JPEG quality：
// 数字越小 = 质量越高 = JPEG 数据越大
// Step 12-2-A 使用 15（在 RAM/带宽/FPS 之间取平衡）
#define CAM_QUALITY      15

// 目标最大帧率
#define CAM_FRAMERATE    10

// framebuffer 尺寸校验（QQVGA 160x120 RGB565 = 38400 字节）
#define QQVGA_FRAME_BYTES  (160 * 120 * 2)


// ============================================================================
// HTTP
// ============================================================================

#define HTTP_PORT        80

#define HTTP_CTRL_PORT        32768

#define BOUNDARY         "frame"


// ============================================================================
// Camera Access Synchronization (Task C · 2026-09-21)
//
// 目的：
//   /capture 与 /stream 共享同一摄像头资源池（fb_count=2）与
//   同一静态 JPEG buffer（s_jpegBuf）。若两者同时访问而不加锁：
//     1. /stream 长时间占用 fb → /capture 的 esp_camera_fb_get() 阻塞
//     2. /capture 的 frame2jpg_cb 通过 jpegOutCb 走 chunked 发送 →
//        与 /stream 争抢 TCP socket，导致 /capture 10s+ 无数据
//
// 方案：
//   - s_cameraMutex：保护 [esp_camera_fb_get → detect → overlay →
//                       encodeFrameToBuf → esp_camera_fb_return] 全流程
//   - 网络发送（httpd_resp_send / httpd_resp_send_chunk）全部在锁外执行
//   - s_lastPersonDetection：供 /stream 内部节流帧复用（3 帧检测 1 次）
//     读写也在 s_cameraMutex 内，天然 thread-safe
//
// 生命周期：
//   setup() 中 initCameraSync() 创建 mutex。
//   setup() 阶段无并发，无需 destroy。
// ============================================================================

// Stream 检测节流：每 3 帧做一次 detectPerson，其他帧复用 s_lastPersonDetection
// 目标：把检测耗时 (~50ms) 均摊到 3 帧，等效 fps 提升到 12-15
#define PERSON_DETECT_INTERVAL   3

static httpd_handle_t  g_camera_httpd       = nullptr;
static SemaphoreHandle_t s_cameraMutex      = nullptr;
static PersonDetection   s_lastPersonDetection;
static bool              s_lastPersonValid  = false;


// ============================================================================
// JPEG 编码回调（供 frame2jpg_cb 使用）
//
// frame2jpg_cb() 通过 jpg_out_cb 分片吐出 JPEG 数据。
// 我们把 (arg, index, data, len) 通过 httpd_resp_send_chunk 直接写到 socket，
// 因此不需要为整帧分配一个大 buffer。
// 返回值：0 表示成功继续；非 0 表示请求中止。
// ============================================================================

struct JpegOutCtx
{
    httpd_req_t *req;
    esp_err_t   err;      // 记录发送错误
    size_t      totalLen; // 累计字节数
};


static size_t jpegOutCb(void *arg, size_t index, const void *data, size_t len)
{
    (void)index;

    JpegOutCtx *ctx = (JpegOutCtx *)arg;

    if (ctx->req == nullptr || data == nullptr)
    {
        return 0;
    }

    esp_err_t r = httpd_resp_send_chunk(ctx->req, (const char *)data, len);

    if (r != ESP_OK)
    {
        ctx->err = r;
        return 0;  // 停止编码
    }

    ctx->totalLen += len;
    return len;
}


// 把 RGB565 QQVGA framebuffer 编码为 JPEG 并发送
// 返回 ESP_OK 表示成功；JPEG 字节数通过 *outLen 返回
static esp_err_t encodeFrameJPEG(
    httpd_req_t *req,
    camera_fb_t *fb,
    uint8_t quality,
    size_t *outLen
)
{
    *outLen = 0;

    if (fb == nullptr || req == nullptr)
    {
        return ESP_ERR_INVALID_ARG;
    }

    JpegOutCtx ctx = { .req = req, .err = ESP_OK, .totalLen = 0 };

    bool ok = frame2jpg_cb(fb, quality, jpegOutCb, &ctx);

    if (!ok)
    {
        return ESP_FAIL;
    }

    if (ctx.err != ESP_OK)
    {
        return ctx.err;
    }

    *outLen = ctx.totalLen;
    return ESP_OK;
}


// ============================================================================
// 静态 JPEG buffer + collecting 回调
//
// 用于 MJPEG 分片头：需要先知道 JPEG 字节数才能写 Content-Length
// 因此采用「编码到静态 buffer -> 写 header -> 写 body」的策略
// ============================================================================

static const size_t JPEG_BUF_SIZE = 12 * 1024;  // 12 KiB 足够 QQVGA quality 15

static uint8_t s_jpegBuf[JPEG_BUF_SIZE];


struct JpegCollectCtx
{
    uint8_t *buf;
    size_t   cap;
    size_t   len;
    bool     overflow;
};


static size_t jpegCollectCb(
    void *arg,
    size_t index,
    const void *data,
    size_t len
)
{
    (void)index;

    JpegCollectCtx *ctx = (JpegCollectCtx *)arg;

    if (data == nullptr || ctx == nullptr)
    {
        return 0;
    }

    if (ctx->len + len > ctx->cap)
    {
        ctx->overflow = true;
        return 0;  // 停止编码
    }

    memcpy(ctx->buf + ctx->len, data, len);
    ctx->len += len;
    return len;
}


// 把 RGB565 QQVGA framebuffer 编码为 JPEG 到静态 buffer
// 返回 ESP_OK 表示成功；JPEG 字节数写入 *outLen
// 溢出时返回 ESP_ERR_NOT_SUPPORTED
static esp_err_t encodeFrameToBuf(
    camera_fb_t *fb,
    uint8_t quality,
    size_t *outLen
)
{
    *outLen = 0;

    if (fb == nullptr)
    {
        return ESP_ERR_INVALID_ARG;
    }

    JpegCollectCtx ctx = {
        .buf     = s_jpegBuf,
        .cap     = JPEG_BUF_SIZE,
        .len     = 0,
        .overflow = false
    };

    bool ok = frame2jpg_cb(fb, quality, jpegCollectCb, &ctx);

    if (!ok)
    {
        return ESP_FAIL;
    }

    if (ctx.overflow)
    {
        return ESP_ERR_NOT_SUPPORTED;
    }

    *outLen = ctx.len;
    return ESP_OK;
}


// ============================================================================
// 堆分配的 JPEG buffer 编码（用于 /stream 与并发 /capture 隔离）
//
// 目的：
//   /stream 需要在释放 s_cameraMutex 后发送 JPEG。若发送期间 /capture
//   同时运行，两个 handler 会竞争共享的 s_jpegBuf。
//   因此 /stream 使用 malloc 的独立 buffer，避免竞态。
//
// 返回：
//   非 NULL：JPEG 数据指针（调用方必须 free）
//   NULL  ：分配失败或编码失败，*outLen = 0
// ============================================================================

static uint8_t *encodeFrameMalloc(
    camera_fb_t *fb,
    uint8_t quality,
    size_t *outLen
)
{
    *outLen = 0;

    if (fb == nullptr)
    {
        return nullptr;
    }

    uint8_t *buf = (uint8_t *)malloc(JPEG_BUF_SIZE);

    if (buf == nullptr)
    {
        return nullptr;
    }

    JpegCollectCtx ctx = {
        .buf      = buf,
        .cap      = JPEG_BUF_SIZE,
        .len      = 0,
        .overflow = false
    };

    bool ok = frame2jpg_cb(fb, quality, jpegCollectCb, &ctx);

    if (!ok || ctx.overflow)
    {
        free(buf);
        return nullptr;
    }

    *outLen = ctx.len;
    return buf;
}


// ============================================================================
// 前置声明
// ============================================================================

static esp_err_t indexHandler(httpd_req_t *req);
static esp_err_t captureHandler(httpd_req_t *req);
static esp_err_t streamHandler(httpd_req_t *req);

static bool initCamera();
static bool initCameraSync();
static void initWifi();
static void initHttpServer();
static void initLed();


// ============================================================================
// GET /
//
// 首页
// ============================================================================

static esp_err_t indexHandler(httpd_req_t *req)
{
    IPAddress ip = WiFi.localIP();

    char ipBuf[20];

    snprintf(
        ipBuf,
        sizeof(ipBuf),
        "%u.%u.%u.%u",
        ip[0],
        ip[1],
        ip[2],
        ip[3]
    );


    String html;

    html.reserve(1600);


    // ------------------------------------------------------------
    // HTML
    // ------------------------------------------------------------

    html += "<!DOCTYPE html>";

    html += "<html>";

    html += "<head>";

    html += "<meta charset='utf-8'>";

    html += "<meta name='viewport' ";
    html += "content='width=device-width,initial-scale=1'>";

    html += "<title>ESP32-CAM Step 12-1</title>";


    // ------------------------------------------------------------
    // CSS
    // ------------------------------------------------------------

    html += "<style>";

    html += "body{";
    html += "font-family:sans-serif;";
    html += "margin:16px;";
    html += "background:#1e1e1e;";
    html += "color:#eee;";
    html += "}";

    html += "img{";
    html += "max-width:100%;";
    html += "height:auto;";
    html += "border:1px solid #555;";
    html += "background:#000;";
    html += "}";

    html += "h1{";
    html += "font-size:20px;";
    html += "}";

    html += ".info{";
    html += "color:#aaa;";
    html += "font-size:13px;";
    html += "}";

    html += "a{";
    html += "color:#6cf;";
    html += "}";

    html += "</style>";

    html += "</head>";


    // ------------------------------------------------------------
    // Body
    // ------------------------------------------------------------

    html += "<body>";

    html += "<h1>ESP32-CAM · OV2640 · Step 12-1</h1>";


    // IP
    html += "<p class='info'>";
    html += "IP: ";
    html += ipBuf;
    html += "</p>";


    // ------------------------------------------------------------
    // MJPEG Stream
    // ------------------------------------------------------------

    html += "<h3>MJPEG Stream</h3>";

    html += "<img src='/stream' />";


    // ------------------------------------------------------------
    // Links
    // ------------------------------------------------------------

    html += "<p class='info'>";

    html += "Endpoints: ";


    // Stream
    html += "<a href='/stream'>";
    html += "/stream";
    html += "</a>";


    html += " | ";


    // Capture
    html += "<a href='/capture'>";
    html += "/capture";
    html += "</a>";


    html += "</p>";


    // ------------------------------------------------------------
    // Close
    // ------------------------------------------------------------

    html += "</body>";

    html += "</html>";


    // ------------------------------------------------------------
    // HTTP response
    // ------------------------------------------------------------

    esp_err_t res =
        httpd_resp_set_type(
            req,
            "text/html"
        );

    if (res != ESP_OK)
    {
        Serial.printf(
            "[HTTP] index content type failed: 0x%x\n",
            res
        );

        return res;
    }


    res =
        httpd_resp_send(
            req,
            html.c_str(),
            html.length()
        );


    return res;
}


// ============================================================================
// GET /capture
//
// 单张 JPEG
// ============================================================================

static esp_err_t captureHandler(httpd_req_t *req)
{
    uint32_t tStart = esp_timer_get_time();
    Serial.println("[CAPTURE] start");

    if (s_cameraMutex == nullptr)
    {
        Serial.println("[CAPTURE] mutex not initialized");
        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Camera sync not ready"
        );
        return ESP_FAIL;
    }

    // ------------------------------------------------------------
    // 抢锁（保护 fb_get + detect + overlay + encode + fb_return）
    //
    // 使用 2s 超时，避免 /stream 长时间占用时 /capture 无限阻塞。
    // 超时后返回 503，客户端可稍后重试。
    // ------------------------------------------------------------

    uint32_t waitStart = esp_timer_get_time();
    BaseType_t okWait = xSemaphoreTake(s_cameraMutex, pdMS_TO_TICKS(2000));
    uint32_t waitUs = (uint32_t)(esp_timer_get_time() - waitStart);

    if (okWait != pdTRUE)
    {
        Serial.printf(
            "[CAPTURE] lock wait timeout after %lu us (stream busy?)\n",
            (unsigned long)waitUs
        );
        // 该版本 esp_http_server 未定义 HTTPD_503 / 429 enum，
        // 手动构造 503 Service Unavailable + 文本 body。
        const char *body = "Camera busy";
        if (httpd_resp_set_status(req, "503 Service Unavailable") != ESP_OK)
        {
            return ESP_FAIL;
        }
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_send(req, body, strlen(body));
    }

    if (waitUs > 50000)  // > 50 ms 视为"长等待"，值得打印
    {
        Serial.printf(
            "[CAM] lock wait=%lu us\n",
            (unsigned long)waitUs
        );
    }

    // ------------------------------------------------------------
    // 持锁区间：fb_get → detect → overlay → encodeFrameToBuf → fb_return
    //
    // 全部在 s_cameraMutex 保护下执行，避免与 /stream 抢占 fb 与 s_jpegBuf。
    // ------------------------------------------------------------

    camera_fb_t *fb = nullptr;
    size_t       jpegSize = 0;
    uint8_t     *jpegBuf = nullptr;
    PersonDetection det = {false, 0, 0, 0, 0};

    fb = esp_camera_fb_get();

    if (fb == nullptr)
    {
        Serial.println("[CAPTURE] fb acquire failed");
        xSemaphoreGive(s_cameraMutex);
        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Camera frame error"
        );
        return ESP_FAIL;
    }

    // 检测（同步更新共享 s_lastPersonDetection，供 /stream 复用）
    detectPerson(fb->buf, &det);
    s_lastPersonDetection = det;
    s_lastPersonValid     = true;

    // Overlay
    uint16_t *rgbBuf = (uint16_t *)fb->buf;
    drawPersonOverlay(rgbBuf, 160, 120, det);

    // 编码到 malloc buffer（与 /stream 隔离，支持并发 /capture）
    jpegBuf = encodeFrameMalloc(fb, CAM_QUALITY, &jpegSize);

    // 立即释放 fb（无论 encode 成功与否）
    esp_camera_fb_return(fb);

    // 释放锁 —— malloc buffer 已包含完整 JPEG，可在锁外发送
    xSemaphoreGive(s_cameraMutex);

    if (jpegBuf == nullptr)
    {
        Serial.printf(
            "[CAPTURE] encode failed (heap=%u)\n",
            (unsigned)ESP.getFreeHeap()
        );
        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "JPEG encode failed"
        );
        return ESP_FAIL;
    }

    // ------------------------------------------------------------
    // 锁外发送：一次性 httpd_resp_send（Content-Length 已知）
    //
    // 相比 chunked 编码，一次性发送对 TCP socket 占用更短，
    // 也不会与 /stream 的 chunked 编码交错导致 socket buffer 竞争。
    // ------------------------------------------------------------

    esp_err_t res = httpd_resp_set_type(req, "image/jpeg");

    if (res != ESP_OK)
    {
        Serial.printf("[CAPTURE] set type failed: 0x%x\n", res);
        free(jpegBuf);
        return res;
    }

    httpd_resp_set_hdr(req, "Content-Disposition", "inline");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    res = httpd_resp_send(req, (const char *)jpegBuf, (ssize_t)jpegSize);

    if (res != ESP_OK)
    {
        Serial.printf("[CAPTURE] send failed: 0x%x\n", res);
        free(jpegBuf);
        return res;
    }

    free(jpegBuf);
    jpegBuf = nullptr;

    uint32_t totalUs = (uint32_t)(esp_timer_get_time() - tStart);
    Serial.printf(
        "[CAPTURE] done jpeg=%uB detect=%s conf=%.2f wait=%luus total=%luus\n",
        (unsigned int)jpegSize,
        det.detected ? "1" : "0",
        (double)det.confidence,
        (unsigned long)waitUs,
        (unsigned long)totalUs
    );

    return ESP_OK;
}


// ============================================================================
// GET /stream
//
// MJPEG Stream
//
// 格式：
//
//   --frame
//   Content-Type: image/jpeg
//   Content-Length: xxxx
//
//   JPEG DATA
//
//   --frame
//   ...
//
// ============================================================================

static esp_err_t streamHandler(httpd_req_t *req)
{
    Serial.println("[STREAM] client connected");

    if (s_cameraMutex == nullptr)
    {
        Serial.println("[STREAM] mutex not initialized");
        return ESP_FAIL;
    }

    // ------------------------------------------------------------
    // Multipart header
    // ------------------------------------------------------------

    esp_err_t res = httpd_resp_set_type(
        req,
        "multipart/x-mixed-replace; boundary=" BOUNDARY
    );

    if (res != ESP_OK)
    {
        Serial.printf("[STREAM] set content type failed: 0x%x\n", res);
        return res;
    }

    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    // ------------------------------------------------------------
    // Loop state
    // ------------------------------------------------------------

    uint32_t frameCount       = 0;
    uint32_t lastLogMs        = millis();
    uint32_t fpsWindowStartMs = millis();
    uint32_t fpsWindowFrames  = 0;

    // 节流诊断状态（每 PERSON_DETECT_INTERVAL 帧才更新一次）
    PersonDebugInfo dbg = {0, 0, 0, 0, false, false, 0};
    uint32_t        lastDetectUs = 0;
    uint32_t        lastEncodeUs = 0;

    while (true)
    {
        // ------------------------------------------------------------
        // 持锁区间：fb_get → detect(可选) → overlay → encodeFrameToBuf → fb_return
        //
        // 每 PERSON_DETECT_INTERVAL 帧才做一次 detectPerson，
        // 其他帧复用 s_lastPersonDetection（保持 overlay 连续）。
        // 这样 detect 耗时被均摊，等效 fps 提升 ~2-3x。
        // ------------------------------------------------------------

        if (xSemaphoreTake(s_cameraMutex, pdMS_TO_TICKS(500)) != pdTRUE)
        {
            Serial.println("[STREAM] camera lock timeout (concurrent capture?)");
            break;
        }

        camera_fb_t *fb = esp_camera_fb_get();

        if (fb == nullptr)
        {
            Serial.println("[STREAM] camera frame failed");
            xSemaphoreGive(s_cameraMutex);
            break;
        }

        bool         doDetect = (frameCount % PERSON_DETECT_INTERVAL == 0);
        PersonDetection det;

        if (doDetect)
        {
            det = {false, 0, 0, 0, 0};
            uint32_t t0 = esp_timer_get_time();
            bool ok = detectPerson(fb->buf, &det, &dbg);
            uint32_t t1 = esp_timer_get_time();
            lastDetectUs = (uint32_t)(t1 - t0);

            if (!ok)
            {
                Serial.println("[STREAM] detectPerson returned false");
            }

            // 更新共享状态（供 /capture 与后续节流帧复用）
            s_lastPersonDetection = det;
            s_lastPersonValid     = true;
        }
        else
        {
            // 节流帧：复用上一次检测结果，保持 overlay 稳定显示
            if (s_lastPersonValid)
            {
                det = s_lastPersonDetection;
            }
            else
            {
                det = {false, 0, 0, 0, 0};
            }
        }

        // Overlay（每帧都画，用当帧或缓存的检测结果）
        uint16_t *rgbBuf = (uint16_t *)fb->buf;
        drawPersonOverlay(rgbBuf, 160, 120, det);

        // 编码到 malloc buffer（与 /capture 的 s_jpegBuf 隔离）
        size_t    jpegSize = 0;
        uint8_t  *jpegBuf  = nullptr;
        uint32_t  t2 = esp_timer_get_time();
        jpegBuf = encodeFrameMalloc(fb, CAM_QUALITY, &jpegSize);
        uint32_t  t3 = esp_timer_get_time();
        lastEncodeUs = (uint32_t)(t3 - t2);

        // 立即释放 fb
        esp_camera_fb_return(fb);

        // 释放锁 —— malloc buffer 已安全保存本帧数据，
        // 与 /capture 的 s_jpegBuf 无竞争。
        xSemaphoreGive(s_cameraMutex);

        if (jpegBuf == nullptr)
        {
            Serial.printf(
                "[STREAM] jpeg encode failed (heap=%u)\n",
                (unsigned)ESP.getFreeHeap()
            );
            break;
        }

        // ------------------------------------------------------------
        // 锁外发送：Frame Header + JPEG body + Frame End
        //
        // 每步都检查返回码，任一失败视为客户端断开，立即 break。
        // 释放 jpegBuf 后回到下一帧。
        // ------------------------------------------------------------

        char partHeader[128];
        int headerLen = snprintf(
            partHeader,
            sizeof(partHeader),
            "--" BOUNDARY "\r\n"
            "Content-Type: image/jpeg\r\n"
            "Content-Length: %u\r\n"
            "\r\n",
            (unsigned int)jpegSize
        );

        if (headerLen <= 0 || headerLen >= (int)sizeof(partHeader))
        {
            Serial.println("[STREAM] header build failed");
            break;
        }

        res = httpd_resp_send_chunk(req, partHeader, headerLen);
        if (res != ESP_OK)
        {
            Serial.printf("[STREAM] header send failed (client disconnect?): 0x%x\n", res);
            break;
        }

        res = httpd_resp_send_chunk(req, (const char *)jpegBuf, jpegSize);
        if (res != ESP_OK)
        {
            Serial.printf("[STREAM] JPEG send failed (client disconnect?): 0x%x\n", res);
            free(jpegBuf);
            break;
        }

        res = httpd_resp_send_chunk(req, "\r\n", 2);
        if (res != ESP_OK)
        {
            Serial.printf("[STREAM] frame end failed: 0x%x\n", res);
            free(jpegBuf);
            break;
        }

        // 帧发送完成，释放本帧 malloc buffer
        free(jpegBuf);
        jpegBuf = nullptr;

        // ------------------------------------------------------------
        // 帧计数
        // ------------------------------------------------------------

        frameCount++;
        fpsWindowFrames++;

        // ------------------------------------------------------------
        // 每 10 帧输出一次诊断日志（~1 Hz，非每帧）
        // ------------------------------------------------------------

        if ((frameCount % 10) == 0)
        {
            uint32_t nowMs = millis();
            uint32_t windowMs = nowMs - fpsWindowStartMs;
            float fps =
                (windowMs > 0)
                    ? ((float)fpsWindowFrames * 1000.0f / (float)windowMs)
                    : 0.0f;
            fpsWindowStartMs = nowMs;
            fpsWindowFrames = 0;

            uint32_t freeHeap = ESP.getFreeHeap();
            size_t  largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);

            Serial.printf(
                "[STREAM] f=%u fps=%.1f det=%luus enc=%luus jpeg=%uB free=%uB max=%uB p=%d\n",
                (unsigned int)frameCount,
                fps,
                (unsigned long)lastDetectUs,
                (unsigned long)lastEncodeUs,
                (unsigned int)jpegSize,
                (unsigned int)freeHeap,
                (unsigned int)largestBlock,
                det.detected ? 1 : 0
            );

            // Person 诊断（仅在检测帧更新过的 dbg）
            Serial.printf(
                "[PERSON] skin=%u cand=%d score=%u conf=%.2f x=%.2f y=%.2f w=%.2f h=%.2f tC=%d tL=%d sm=%d\n",
                (unsigned int)dbg.skin_pixels_total,
                (int)dbg.component_count,
                (unsigned int)dbg.best_score,
                det.confidence,
                det.center_x,
                det.center_y,
                det.width,
                det.height,
                (int)dbg.temporal_confirm,
                (int)dbg.temporal_loss,
                dbg.smoothing_active ? 1 : 0
            );
        }

        // ------------------------------------------------------------
        // FPS 上限（默认 10）
        // ------------------------------------------------------------

        uint32_t nowMs = millis();
        uint32_t frameInterval = 1000 / CAM_FRAMERATE;
        uint32_t elapsed = nowMs - lastLogMs;

        if (elapsed < frameInterval)
        {
            delay(frameInterval - elapsed);
        }
        lastLogMs = millis();
    }

    // ------------------------------------------------------------
    // 结束 Chunked Response（幂等，失败也不重试）
    // ------------------------------------------------------------

    httpd_resp_send_chunk(req, nullptr, 0);

    Serial.printf(
        "[STREAM] client disconnected, frames=%u\n",
        (unsigned int)frameCount
    );

    return ESP_OK;
}


// ============================================================================
// Camera 同步层初始化
//
// 创建二值互斥量 s_cameraMutex。
// 用于串行化 esp_camera_fb_get / fb_return / detect / overlay / encode。
//
// 返回 true 表示 mutex 创建成功。
// ============================================================================

static bool initCameraSync()
{
    if (s_cameraMutex != nullptr)
    {
        return true;
    }

    s_cameraMutex = xSemaphoreCreateMutex();

    if (s_cameraMutex == nullptr)
    {
        Serial.println("[CAM] xSemaphoreCreateMutex() failed");
        return false;
    }

    memset(&s_lastPersonDetection, 0, sizeof(s_lastPersonDetection));
    s_lastPersonValid = false;

    Serial.println("[CAM] sync layer initialized (mutex + lastPersonDetection)");
    return true;
}


// ============================================================================
// 摄像头初始化
// ============================================================================

static bool initCamera()
{
    Serial.println(
        "[CAM] configuring camera..."
    );


    // ------------------------------------------------------------
    // Camera configuration
    // ------------------------------------------------------------

    camera_config_t config = {};


    // ------------------------------------------------------------
    // LEDC
    // ------------------------------------------------------------

    config.ledc_channel =
        LEDC_CHANNEL_0;

    config.ledc_timer =
        LEDC_TIMER_0;


    // ------------------------------------------------------------
    // Camera Data
    // ------------------------------------------------------------

    config.pin_d0 =
        Y2_GPIO_NUM;

    config.pin_d1 =
        Y3_GPIO_NUM;

    config.pin_d2 =
        Y4_GPIO_NUM;

    config.pin_d3 =
        Y5_GPIO_NUM;

    config.pin_d4 =
        Y6_GPIO_NUM;

    config.pin_d5 =
        Y7_GPIO_NUM;

    config.pin_d6 =
        Y8_GPIO_NUM;

    config.pin_d7 =
        Y9_GPIO_NUM;


    // ------------------------------------------------------------
    // Camera Clock
    // ------------------------------------------------------------

    config.pin_xclk =
        XCLK_GPIO_NUM;

    config.pin_pclk =
        PCLK_GPIO_NUM;

    config.pin_vsync =
        VSYNC_GPIO_NUM;

    config.pin_href =
        HREF_GPIO_NUM;


    // ------------------------------------------------------------
    // SCCB
    // ------------------------------------------------------------

    config.pin_sccb_sda =
        SIOD_GPIO_NUM;

    config.pin_sccb_scl =
        SIOC_GPIO_NUM;


    // ------------------------------------------------------------
    // Power / Reset
    // ------------------------------------------------------------

    config.pin_pwdn =
        PWDN_GPIO_NUM;

    config.pin_reset =
        RESET_GPIO_NUM;


    // ------------------------------------------------------------
    // XCLK
    // ------------------------------------------------------------

    config.xclk_freq_hz =
        20000000;


    // ------------------------------------------------------------
    // Image
    // ------------------------------------------------------------

    config.pixel_format =
        CAM_PIXFORMAT;

    config.frame_size =
        CAM_FRAMESIZE;

    config.jpeg_quality =
        CAM_QUALITY;


    // ------------------------------------------------------------
    // Frame Buffer
    //
    // Step 12-2-B：
    //   fb_count = 2：双缓冲，避免 esp_camera_get_image() 因
    //   framebuffer 被占用而阻塞，显著提升 MJPEG 帧率稳定性。
    //   QQVGA RGB565 = 38400 B/帧，双缓冲共 76800 B，Internal DRAM 可承受。
    //
    //   grab_mode = CAMERA_GRAB_LATEST：
    //   拿到最新帧即返回，旧帧直接丢弃。
    //   避免 Wi-Fi/网络抖动时旧帧积压导致延迟暴涨。
    // ------------------------------------------------------------

    config.fb_count = 2;

    config.grab_mode = CAMERA_GRAB_LATEST;


    // ------------------------------------------------------------
    // 初始化
    // ------------------------------------------------------------

    Serial.println(
        "[CAM] calling esp_camera_init()..."
    );


    esp_err_t err =
        esp_camera_init(
            &config
        );


    if (err != ESP_OK)
    {
        Serial.printf(
            "[CAM] ESP_ERR 0x%x\n",
            err
        );

        return false;
    }


    Serial.println(
        "[CAM] esp_camera_init() OK"
    );


    // ------------------------------------------------------------
    // Sensor Settings
    // ------------------------------------------------------------

    sensor_t *s =
        esp_camera_sensor_get();


    if (s != nullptr)
    {
        s->set_quality(
            s,
            CAM_QUALITY
        );


        s->set_framesize(
            s,
            CAM_FRAMESIZE
        );


        s->set_brightness(
            s,
            0
        );


        s->set_contrast(
            s,
            0
        );


        s->set_saturation(
            s,
            0
        );
    }


    // ------------------------------------------------------------
    // 输出 Sensor 信息
    // ------------------------------------------------------------

    if (s != nullptr)
    {
        Serial.printf(
            "[CAM] sensor PID: 0x%02x\n",
            s->id.PID
        );
    }


    Serial.println(
        "[CAM] frame size: QQVGA 160x120 RGB565"
    );


    Serial.printf(
        "[CAM] JPEG quality: %d\n",
        CAM_QUALITY
    );


    Serial.printf(
        "[CAM] framebuffer count: %d\n",
        (int)config.fb_count
    );


    return true;
}


// ============================================================================
// Wi-Fi 初始化
// ============================================================================

static void initWifi()
{
    Serial.printf(
        "[WIFI] connecting to SSID: %s\n",
        WIFI_SSID
    );


    // ------------------------------------------------------------
    // Wi-Fi Mode
    // ------------------------------------------------------------

    WiFi.mode(
        WIFI_STA
    );


    WiFi.setHostname(
        "esp32-cam"
    );


    // ------------------------------------------------------------
    // Start connection
    // ------------------------------------------------------------

    WiFi.begin(
        WIFI_SSID,
        WIFI_PASS
    );


    // ------------------------------------------------------------
    // 等待连接
    // ------------------------------------------------------------

    uint32_t startMs =
        millis();


    const uint32_t timeoutMs =
        20000;


    while (
        WiFi.status() != WL_CONNECTED &&
        (millis() - startMs) < timeoutMs
    )
    {
        delay(500);


        LED_ON;

        delay(100);


        LED_OFF;
    }


    // ------------------------------------------------------------
    // 成功
    // ------------------------------------------------------------

    if (
        WiFi.status() == WL_CONNECTED
    )
    {
        // Step 12-2-B：
        //   关闭 Wi-Fi modem sleep，降低持续 MJPEG 流时的 TCP 延迟抖动。
        //   代价：功耗略增（板子由 USB 供电时不影响）。
        WiFi.setSleep(
            false
        );

        // Wi-Fi 连接成功：
        // LED 保持常灭，避免污染摄像头视野。
        // 状态仅通过串口日志 [WIFI]/[HTTP] 输出确认。

        IPAddress ip =
            WiFi.localIP();


        Serial.println();


        Serial.println(
            "=============================================="
        );


        Serial.println(
            " ESP32-CAM ONLINE"
        );


        Serial.printf(
            " IP         : %u.%u.%u.%u\n",
            ip[0],
            ip[1],
            ip[2],
            ip[3]
        );


        Serial.printf(
            " Home       : http://%u.%u.%u.%u/\n",
            ip[0],
            ip[1],
            ip[2],
            ip[3]
        );


        Serial.printf(
            " Capture    : http://%u.%u.%u.%u/capture\n",
            ip[0],
            ip[1],
            ip[2],
            ip[3]
        );


        Serial.printf(
            " Stream     : http://%u.%u.%u.%u/stream\n",
            ip[0],
            ip[1],
            ip[2],
            ip[3]
        );


        Serial.println(
            "=============================================="
        );
    }


    // ------------------------------------------------------------
    // 失败
    // ------------------------------------------------------------

    else
    {
        Serial.println(
            "[WIFI] FAILED to connect"
        );


        Serial.println(
            "[WIFI] Will retry in background"
        );


        if (WIFI_IS_PLACEHOLDER)
        {
            Serial.println(
                "[WIFI] NOTE: SSID is still placeholder"
            );
        }
    }
}


// ============================================================================
// HTTP Server
// ============================================================================

static void initHttpServer()
{
    // ============================================================
    // Server 1
    //
    // Port 80
    //
    // /
    // /capture
    // ============================================================

    httpd_config_t camConfig =
        HTTPD_DEFAULT_CONFIG();


    camConfig.server_port =
        HTTP_PORT;


    // ------------------------------------------------------------
    // HTTP server control port
    // ------------------------------------------------------------

    camConfig.ctrl_port =
        HTTP_CTRL_PORT;


    camConfig.max_uri_handlers =
        8;


    camConfig.lru_purge_enable =
        true;


    // ------------------------------------------------------------
    // Start main HTTP server
    // ------------------------------------------------------------

    esp_err_t res =
        httpd_start(
            &g_camera_httpd,
            &camConfig
        );


    if (res != ESP_OK)
    {
        Serial.printf(
            "[HTTP] main server start failed: 0x%x\n",
            res
        );

        return;
    }


    Serial.printf(
        "[HTTP] main server started on port %d\n",
        HTTP_PORT
    );


    // ------------------------------------------------------------
    // /
    // ------------------------------------------------------------

    httpd_uri_t idx =
    {
        .uri = "/",
        .method = HTTP_GET,
        .handler = indexHandler,
        .user_ctx = nullptr
    };


    res =
        httpd_register_uri_handler(
            g_camera_httpd,
            &idx
        );


    Serial.printf(
        "[HTTP] register / : 0x%x\n",
        res
    );


    // ------------------------------------------------------------
    // /capture
    // ------------------------------------------------------------

    httpd_uri_t cap =
    {
        .uri = "/capture",
        .method = HTTP_GET,
        .handler = captureHandler,
        .user_ctx = nullptr
    };


    res =
        httpd_register_uri_handler(
            g_camera_httpd,
            &cap
        );


    Serial.printf(
        "[HTTP] register /capture : 0x%x\n",
        res
    );


    // ------------------------------------------------------------
    // /stream
    //
    // 与 / 和 /capture 共用同一个 HTTP server (Port 80)
    // ------------------------------------------------------------

    httpd_uri_t stream =
    {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = streamHandler,
        .user_ctx = nullptr
    };


    res =
        httpd_register_uri_handler(
            g_camera_httpd,
            &stream
        );


    if (res != ESP_OK)
    {
        Serial.printf(
            "[HTTP] stream handler register failed: 0x%x\n",
            res
        );

        return;
    }


    Serial.printf(
        "[HTTP] register /stream : 0x%x\n",
        res
    );


    Serial.println(
        "[HTTP] all handlers ready"
    );
}


// ============================================================================
// LED 初始化
// ============================================================================

static void initLed()
{
    pinMode(
        LED_GPIO_NUM,
        OUTPUT
    );


    LED_OFF;
}


// ============================================================================
// setup
// ============================================================================

void setup()
{
    // ------------------------------------------------------------
    // Serial
    // ------------------------------------------------------------

    Serial.begin(
        115200
    );


    delay(500);


    Serial.println();


    Serial.println(
        "=============================================="
    );


    Serial.println(
        " ESP32-CAM · Step 12-1 · OV2640 base test"
    );


    Serial.println(
        "=============================================="
    );


    // ------------------------------------------------------------
    // Step 12-2-B：PSRAM 实测诊断
    //
    //   目的：确认当前 ESP32-CAM 硬件是否真的有 PSRAM。
    //   注意：PlatformIO 内置 board "esp32cam" 默认带 -DBOARD_HAS_PSRAM
    //   编译宏，因此启动日志可能显示 "psramInit(): PSRAM enabled"，
    //   这仅是编译期标志，不代表硬件真的存在 PSRAM。
    //   下面输出的实际返回值才是硬件真实状态。
    // ------------------------------------------------------------

    Serial.printf(
        "[PSRAM] found   : %s\n",
        psramFound() ? "YES" : "NO"
    );

    Serial.printf(
        "[PSRAM] size    : %u bytes\n",
        (unsigned)ESP.getPsramSize()
    );

    Serial.printf(
        "[PSRAM] free    : %u bytes\n",
        (unsigned)ESP.getFreePsram()
    );

    Serial.printf(
        "[MEM]   heap    : %u bytes\n",
        (unsigned)ESP.getFreeHeap()
    );

    Serial.println();

    // ------------------------------------------------------------
    // LED
    // ------------------------------------------------------------

    initLed();


    // ------------------------------------------------------------
    // Wi-Fi
    // ------------------------------------------------------------

    initWifi();


    // ------------------------------------------------------------
    // Camera
    // ------------------------------------------------------------

    Serial.println(
        "[CAM] initializing OV2640 ..."
    );


    if (!initCamera())
    {
        Serial.println(
            "[CAM] FAILED to init OV2640."
        );


        Serial.println(
            "[CAM] Check camera module / cable."
        );


        // 不主动重启
        return;
    }


    Serial.println(
        "[CAM] OV2640 init OK"
    );


    // ------------------------------------------------------------
    // Camera Sync Layer (Task C · 2026-09-21)
    //
    // 必须在 initHttpServer() 之前完成 —— handler 会用到 s_cameraMutex。
    // ------------------------------------------------------------

    if (!initCameraSync())
    {
        Serial.println(
            "[CAM] sync layer init FAILED (mutex)."
        );

        // 不主动重启 —— 即使 mutex 失败，/stream 与 /capture 也会
        // 在 s_cameraMutex == nullptr 检查时安全返回，不会 crash。
    }


    // ------------------------------------------------------------
    // HTTP
    // ------------------------------------------------------------

    if (
        WiFi.status() == WL_CONNECTED
    )
    {
        initHttpServer();
    }
    else
    {
        Serial.println(
            "[HTTP] skipped: Wi-Fi not connected"
        );
    }
}


// ============================================================================
// loop
// ============================================================================

void loop()
{
    // ------------------------------------------------------------
    // Wi-Fi 重连
    // ------------------------------------------------------------

    if (
        WiFi.status() != WL_CONNECTED
    )
    {
        static uint32_t lastRetry = 0;


        if (
            millis() - lastRetry >
            10000
        )
        {
            lastRetry =
                millis();


            Serial.println(
                "[WIFI] retrying ..."
            );


            WiFi.disconnect();


            WiFi.begin(
                WIFI_SSID,
                WIFI_PASS
            );
        }
    }


    // 注：
    //   板载 Flash LED 仅用于 Wi-Fi 连接阶段的指示。
    //   Wi-Fi 连接成功后保持常灭，避免干扰视频观察。
    //   需要外部指示灯时可另行接入 GPIO 引脚。

    delay(
        100
    );
}

