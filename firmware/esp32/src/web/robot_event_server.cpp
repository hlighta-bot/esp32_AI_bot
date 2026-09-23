// ============================================================
// robot_event_server.cpp
//
// 实现说明：
//   - 复用 ConfigWeb 已启动的同一个 WebServer(80) 实例注册路由，
//     本类不再自己 new WebServer 或调用 begin()，避免端口冲突。
//   - POST /robot/event 的回调中：
//       1) 校验 clientContentLength()（上限 64 字节）
//       2) 通过 WebServer 内置的 "plain" 参数取出 JSON body
//          （Content-Type: application/json 时，WebServer 会自动
//           把整个 body 读入 _currentArgs，key="plain"，
//           见 Parsing.cpp:207-218）
//       3) 用极简字符串扫描提取 "event":"..." 值
//       4) 更新状态机，必要时置位 s_playTriggered
//       5) 返回 HTTP 200 / 400 JSON 响应
//     回调不阻塞播放；播放由 main.cpp loop() 消费 s_playTriggered
//     后调用 playChunk() 完成。
//
// 关键点：不要在 handler 里读 client.read()。
//        WebServer::_parseRequest() 已经用 readBytesWithTimeout
//        把 body 消费完了；handler 里 client.read() 拿到的是空。
//        要用 WebServer 已解析出的 arg("plain")。
//
// 主循环调用：
//   g_web.loopHTTP() 已经在同一个 WebServer 上 handleClient()，
//   因此本类的 loop() 是空实现，仅为兼容旧接口。
//
// 不引入第三方 JSON 库；用轻量字符串扫描兼容 {"event":"..."} 格式。
// ============================================================

#include "robot_event_server.h"

#include <Arduino.h>

namespace {

// 事件名常量（与 ESP32-CAM 侧 robot_event.cpp 保持一致）
const char* EVENT_PERSON_DETECTED = "person_detected";
const char* EVENT_PERSON_LOST     = "person_lost";

// Body 长度上限（防恶意/异常长 body）
constexpr size_t MAX_BODY_LEN = 64;

// ------------------------------------------------------------
// 极简 JSON "event" 字段提取
//
// 支持格式（大小写不敏感）：
//     {"event":"person_detected"}
//     { "event" : "person_detected" }
//
// 返回 true 表示找到 event 字段并写出到 out。
// ------------------------------------------------------------
bool extractEventField(const String& body, String& out)
{
    // 定位 "event"
    String lower = body;
    lower.toLowerCase();

    int pos = lower.indexOf("\"event\"");
    if (pos < 0)
    {
        return false;
    }

    // 跳过 "event" 后的空白
    pos += 7;  // strlen("\"event\"")
    while (pos < (int)body.length() &&
           (body[pos] == ' ' || body[pos] == '\t' ||
            body[pos] == '\r' || body[pos] == '\n'))
    {
        pos++;
    }

    // 期望 ':'
    if (pos >= (int)body.length() || body[pos] != ':')
    {
        return false;
    }
    pos++;

    // 跳过 ':' 后空白
    while (pos < (int)body.length() &&
           (body[pos] == ' ' || body[pos] == '\t' ||
            body[pos] == '\r' || body[pos] == '\n'))
    {
        pos++;
    }

    // 期望 '"'
    if (pos >= (int)body.length() || body[pos] != '"')
    {
        return false;
    }
    pos++;

    // 读取到下一个 '"'
    int start = pos;
    while (pos < (int)body.length() && body[pos] != '"')
    {
        pos++;
    }
    if (pos >= (int)body.length())
    {
        return false;
    }

    out = body.substring(start, pos);
    return true;
}

}  // namespace


// ============================================================
// 构造
// ============================================================

RobotEventServer::RobotEventServer()
    : _server(nullptr)
    , s_state(State::PersonNotPresent)
    , s_playTriggered(false)
{
}


// ============================================================
// begin - 借用外部已启动的 WebServer 注册路由
// ============================================================

void RobotEventServer::begin(WebServer& sharedServer)
{
    _server = &sharedServer;

    _server->on(
        "/robot/event",
        HTTP_POST,
        [this]() { handleRobotEvent(); }
    );

    Serial.println(
        "[robot-event] route registered on shared WebServer: POST /robot/event"
    );
}


// ============================================================
// loop - 空实现（由 ConfigWeb::loopHTTP() 统一 handleClient）
// ============================================================

void RobotEventServer::loop()
{
    // 与 ConfigWeb 共享同一个 WebServer 实例，
    // handleClient() 已在 ConfigWeb::loopHTTP() 里执行，
    // 此处再调一次会重复处理 client（虽无害但浪费 CPU）。
    // 保留空实现仅为兼容 main.cpp 现有调用。
}


// ============================================================
// state
// ============================================================

RobotEventServer::State RobotEventServer::state() const
{
    return s_state;
}


// ============================================================
// consumePlayTrigger
//
// 一次性读取：读取后清零。
// ============================================================

bool RobotEventServer::consumePlayTrigger()
{
    bool trigger = s_playTriggered;
    s_playTriggered = false;
    return trigger;
}


// ============================================================
// handleEvent - 状态机
// ============================================================

void RobotEventServer::handleEvent(const String& eventName)
{
    if (eventName == EVENT_PERSON_DETECTED)
    {
        Serial.printf(
            "[robot-event] person_detected (state=%s)\n",
            (s_state == State::PersonPresent) ? "PRESENT" : "NOT_PRESENT"
        );

        if (s_state == State::PersonNotPresent)
        {
            s_state = State::PersonPresent;
            s_playTriggered = true;
            Serial.println("[robot-event] STATE: NOT_PRESENT -> PRESENT (play '你好！')");
        }
        // else: 已经 PRESENT，忽略重复事件
    }
    else if (eventName == EVENT_PERSON_LOST)
    {
        Serial.printf(
            "[robot-event] person_lost (state=%s)\n",
            (s_state == State::PersonPresent) ? "PRESENT" : "NOT_PRESENT"
        );

        if (s_state == State::PersonPresent)
        {
            s_state = State::PersonNotPresent;
            Serial.println("[robot-event] STATE: PRESENT -> NOT_PRESENT (no play)");
        }
        // else: 已经 NOT_PRESENT，忽略重复事件
    }
    else
    {
        Serial.printf("[robot-event] unknown event: '%s'\n", eventName.c_str());
    }
}


// ============================================================
// handleRobotEvent - HTTP 回调
//
// 关键点：不阻塞播放。仅解析 + 状态切换 + 置位 flag。
//
// Body 获取方式：
//   Arduino-ESP32 WebServer 对 Content-Type: application/json
//   的 POST 请求，会在 _parseRequest() 内用 readBytesWithTimeout
//   把整个 body 消费完毕，并把内容存到 _currentArgs，
//   key="plain"（见 Parsing.cpp:206-218）。
//   handler 里通过 _server->arg("plain") 直接取回 body 即可，
//   不要再从 _server->client() 手动 read（那时 buffer 已空）。
//
// 64 字节限制：
//   1) 入口检查 clientContentLength() > MAX_BODY_LEN → 400
//   2) arg("plain") 返回后，body.length() > MAX_BODY_LEN → 400
//   （双重保险，覆盖 Content-Length 缺失或与实际不符的情况）
// ============================================================

void RobotEventServer::handleRobotEvent()
{
    if (_server == nullptr)
    {
        return;
    }

    // ------------------------------------------------------------
    // 1. 校验 Content-Length（0 或超长都拒绝）
    // ------------------------------------------------------------
    int contentLen = _server->clientContentLength();
    if (contentLen <= 0)
    {
        Serial.printf(
            "[robot-event] bad Content-Length: %d\n",
            contentLen
        );
        _server->send(
            400,
            "application/json",
            "{\"status\":\"error\",\"message\":\"bad content length\"}"
        );
        return;
    }
    if ((size_t)contentLen > MAX_BODY_LEN)
    {
        Serial.printf(
            "[robot-event] Content-Length %d > MAX %zu\n",
            contentLen,
            MAX_BODY_LEN
        );
        _server->send(
            400,
            "application/json",
            "{\"status\":\"error\",\"message\":\"body too large\"}"
        );
        return;
    }

    // ------------------------------------------------------------
    // 2. 从 WebServer 内置的 plain 参数取 body
    //    （WebServer 已把 body 读完并解析好，此处直接取用）
    // ------------------------------------------------------------
    String body = _server->arg("plain");

    if (body.length() == 0)
    {
        Serial.printf(
            "[robot-event] empty body (Content-Length=%d)\n",
            contentLen
        );
        _server->send(
            400,
            "application/json",
            "{\"status\":\"error\",\"message\":\"empty body\"}"
        );
        return;
    }

    // 二次长度检查：Content-Length 可能少报了实际 body
    if (body.length() > MAX_BODY_LEN)
    {
        Serial.printf(
            "[robot-event] body.length() %u > MAX %zu\n",
            body.length(),
            MAX_BODY_LEN
        );
        _server->send(
            400,
            "application/json",
            "{\"status\":\"error\",\"message\":\"body too large\"}"
        );
        return;
    }

    Serial.printf(
        "[robot-event] POST /robot/event body=%s\n",
        body.c_str()
    );

    // ------------------------------------------------------------
    // 3. 解析 event 字段
    // ------------------------------------------------------------
    String eventName;
    if (!extractEventField(body, eventName) || eventName.length() == 0)
    {
        Serial.println("[robot-event] parse failed");
        _server->send(
            400,
            "application/json",
            "{\"status\":\"error\",\"message\":\"unknown event\"}"
        );
        return;
    }

    // ------------------------------------------------------------
    // 4. 状态机
    // ------------------------------------------------------------
    handleEvent(eventName);

    // ------------------------------------------------------------
    // 5. 校验事件名是否合法
    //    - person_detected / person_lost 已知 → 200
    //    - 其他 → 400（此时状态机也没做任何修改）
    // ------------------------------------------------------------
    if (
        eventName != EVENT_PERSON_DETECTED &&
        eventName != EVENT_PERSON_LOST
    )
    {
        _server->send(
            400,
            "application/json",
            "{\"status\":\"error\",\"message\":\"unknown event\"}"
        );
        return;
    }

    _server->send(
        200,
        "application/json",
        "{\"status\":\"ok\"}"
    );
}
