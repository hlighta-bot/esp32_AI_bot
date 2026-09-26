#ifndef WIFI_CLIENT_H
#define WIFI_CLIENT_H

#ifndef WIFI_CONNECT_TIMEOUT_MS
#define WIFI_CONNECT_TIMEOUT_MS 5000
#endif

#ifndef WIFI_RETRY_INTERVAL_MS
#define WIFI_RETRY_INTERVAL_MS 3000
#endif

#ifndef WIFI_LOCK_TIMEOUT_MS
#define WIFI_LOCK_TIMEOUT_MS 100
#endif

#ifndef TCP_READ_TIMEOUT_MS
#define TCP_READ_TIMEOUT_MS 100
#endif

#ifndef TCP_PROBE_INTERVAL_MS
#define TCP_PROBE_INTERVAL_MS 5000
#endif

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>

class WifiClient
{
public:
    WifiClient();

    void begin(const char* ssid, const char* password,
               const char* pcHost, uint16_t pcPort);
    void run();

    bool isConnected();

    void disconnect();

    int read(uint8_t *buf, size_t max_len);
    int available();

    int write(const uint8_t *buf, size_t len);
    void flush();

private:
    bool tryConnectWifi();
    bool tryConnectTcp();

    /*
     * 非阻塞重连节流。
     *
     * Wi-Fi 与 TCP 重试分别记录下次允许尝试的时间，
     * 避免每轮 loop 都阻塞在 connect() 里。
     */
    uint32_t _nextWifiRetryMs;
    uint32_t _nextTcpRetryMs;

    /*
     * Timestamp of the last low-frequency TCP probe.
     *
     * 探针语义：每 TCP_PROBE_INTERVAL_MS 调用一次
     * _client.connected()。该调用在 lwip 层触发一次
     * recv(fd, 0, MSG_DONTWAIT)，让 lwip 有机会处理
     * 已经到达但对应用层尚不可见的 TCP FIN / RST，
     * 并据此把 _connected 置为 false。
     *
     * 探针本身不发起任何主动探测包，也不直接调用
     * tryConnectTcp()。它只负责"发现 peer 已关闭 →
     * 标记 _tcpEstablished=false → 设置 _nextTcpRetryMs
     * 让现有 retry 机制接管实际重连"。
     */
    uint32_t _lastTcpProbeMs;

private:
    char     _ssid[32];
    char     _password[65];

    IPAddress _pcIp;
    uint16_t _pcPort;

    WiFiClient _client;

    bool _started;

    /*
     * mDNS responder started flag.
     *
     * MDNS.begin() 必须在 WiFi 连上 (WL_CONNECTED) 之后调用，
     * 否则 responder 无法接收 224.0.0.251:5353 组播包，
     * 外部设备（例如 ESP32-CAM）通过 MDNS.queryHost() 查询
     * "esp32-voice-ai" 时会超时得到 0.0.0.0。
     *
     * begin() 里只做 WiFi.setHostname()；真正的 MDNS.begin()
     * 放到 tryConnectWifi() 在拿到 IP 之后调用，用这个 flag
     * 保证整个生命周期只启动一次（不会每次 Wi-Fi 重连都 restart）。
     */
    bool _mdnsStarted;

    /*
     * Our own TCP connection state.
     *
     * IMPORTANT:
     * We intentionally do NOT use
     * WiFiClient::connected() as the primary state.
     */
    bool _tcpEstablished;

    SemaphoreHandle_t _mutex;
};

#endif