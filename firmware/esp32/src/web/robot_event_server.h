// ============================================================
// robot_event_server.h
//
// 目的：
//     ESP32-S3 侧的 HTTP 接收器：接收 ESP32-CAM 上报的
//     人物检测事件，触发"你好！"问候播放。
//
// 接口：
//     POST /robot/event
//     Body: {"event":"person_detected"}   // 首次检测到人物 → 播放"你好！"
//           {"event":"person_lost"}       // 人离开 → 不播放，仅切换状态
//
// 状态机：
//     PERSON_NOT_PRESENT --person_detected--> PERSON_PRESENT  (触发播放)
//     PERSON_PRESENT     --person_lost-----> PERSON_NOT_PRESENT
//     重复的 person_detected / person_lost 不重复触发。
//
// 线程模型（关键）：
//     HTTP 回调里只做：
//         1) 解析 JSON（长度上限 64 字节）
//         2) 更新 s_state
//         3) 置位 s_playTriggered
//     不在回调里阻塞播放。
//
//     实际播放由 main.cpp 的 loop() 检测 s_playTriggered
//     后调用 playChunk() 完成，见 main.cpp。
//
// WebServer 复用（重要）：
//     本类不再自己创建 WebServer(80)，而是复用 ConfigWeb
//     已启动的同一个 WebServer 实例注册 /robot/event 路由。
//     原因：两个 WebServer 实例同时绑定 port 80 会互相抢占，
//     导致后来 start 的那个拿不到请求 → 404。
//
//     调用方式（见 main.cpp）：
//         g_web.beginHTTP(config);        // 先启动 WebServer(80)
//         g_robotEvent.begin(g_web.server()); // 借用同一实例注册路由
//
// 不引入任何第三方 JSON 库；仅依赖 Arduino String。
// 不修改 wifi_server.py，不影响 TCP voice AI 主链路。
// ============================================================

#ifndef ROBOT_EVENT_SERVER_H
#define ROBOT_EVENT_SERVER_H

#include <Arduino.h>
#include <WebServer.h>

class RobotEventServer
{
public:
    RobotEventServer();

    // 借用外部已启动的 WebServer 实例注册 /robot/event 路由。
    // 不再自己 new WebServer(80)，也不调用 _server.begin()。
    // 必须在 sharedServer 已调用过 begin() 之后调用，
    // 否则路由注册后会因 handleClient() 未被调用而永不生效。
    void begin(WebServer& sharedServer);

    // 主循环调用（保持与旧接口一致）。
    // 由于与 ConfigWeb 共享同一个 WebServer 实例，
    // ConfigWeb::loopHTTP() 已经负责 handleClient()，
    // 此处为空实现仅为保持 main.cpp 调用不变。
    void loop();

    // 状态机
    enum class State
    {
        PersonNotPresent,
        PersonPresent,
    };

    State state() const;

    // 一次性消费播放触发标志。
    // 返回 true 表示"检测到有人"这一瞬间被触发过，
    // 由主循环调用 playChunk() 播放"你好！"。
    bool consumePlayTrigger();

private:
    // HTTP handler（注册到 sharedServer 上）
    void handleRobotEvent();

    // 事件处理（供 handleRobotEvent 与测试用）
    void handleEvent(const String& eventName);

    // 借用外部 WebServer 引用；本类不拥有该实例
    WebServer*    _server;
    State         s_state;
    bool          s_playTriggered;
};

#endif  // ROBOT_EVENT_SERVER_H
