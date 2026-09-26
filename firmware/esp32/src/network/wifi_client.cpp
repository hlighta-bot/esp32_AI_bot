#include "wifi_client.h"

#include <lwip/netdb.h>


WifiClient::WifiClient()
    : _ssid()
    , _password()
    , _pcIp()
    , _pcPort(0)
    , _started(false)
    , _tcpEstablished(false)
    , _mdnsStarted(false)
    , _mutex(nullptr)
    , _nextWifiRetryMs(0)
    , _nextTcpRetryMs(0)
    , _lastTcpProbeMs(0)
{
    _ssid[0] = '\0';
    _password[0] = '\0';
}


void WifiClient::begin(const char* ssid, const char* password,
                       const char* pcHost, uint16_t pcPort)
{
    if (_started) {
        return;
    }

    if (!_mutex) {
        _mutex = xSemaphoreCreateMutex();

        if (!_mutex) {
            Serial.println("[wifi] mutex create failed");
            return;
        }
    }

    // Store configuration from RuntimeConfig
    strncpy(_ssid, ssid, sizeof(_ssid) - 1);
    _ssid[sizeof(_ssid) - 1] = '\0';
    strncpy(_password, password, sizeof(_password) - 1);
    _password[sizeof(_password) - 1] = '\0';
    _pcPort = pcPort;

    WiFi.mode(WIFI_STA);

    WiFi.setHostname("esp32-voice-ai");

    /*
     * Disable Wi-Fi power saving.
     * This is important for continuous audio/TCP streaming.
     */
    WiFi.setSleep(false);


    /*
     * Resolve PC host.
     *
     * Usually already an IP address, so fromString() normally succeeds.
     */
    if (!_pcIp.fromString(pcHost)) {

        Serial.printf(
            "[wifi] resolving host: %s\n",
            pcHost
        );

        struct hostent *host = gethostbyname(pcHost);

        if (host &&
            host->h_addr_list &&
            host->h_addr_list[0])
        {
            _pcIp = IPAddress(
                reinterpret_cast<uint8_t *>(
                    host->h_addr_list[0]
                )
            );

            Serial.printf(
                "[wifi] resolved PC: %s\n",
                _pcIp.toString().c_str()
            );
        }
        else
        {
            Serial.printf(
                "[wifi] failed to resolve host: %s\n",
                pcHost
            );
        }
    }


    /*
     * Do NOT call MDNS.begin() here.
     *
     * At this point WiFi is not yet connected; starting the mDNS
     * responder before we own a link-local address means the
     * responder won't receive any 224.0.0.251:5353 multicast
     * queries, so a remote ESP32-CAM calling MDNS.queryHost()
     * on us will fail with "Query Failed".
     *
     * Move MDNS.begin() into tryConnectWifi(), which runs only
     * after WiFi.status() == WL_CONNECTED.
     */


    _started = true;

    _tcpEstablished = false;

    Serial.printf(
        "[wifi] initialized: ssid=%s pc=%s:%u\n",
        _ssid,
        _pcIp.toString().c_str(),
        _pcPort
    );
}


void WifiClient::run()
{
    if (!_started) {
        return;
    }


    const uint32_t now = millis();


    /*
     * Wi-Fi itself is disconnected.
     *
     * 非阻塞节流：只有到达 _nextWifiRetryMs 才尝试，
     * 避免每轮 loop 都阻塞在 WiFi.begin()。
     */
    if (WiFi.status() != WL_CONNECTED) {

        /*
         * TCP is necessarily invalid when Wi-Fi is gone.
         */
        if (_tcpEstablished) {
            _tcpEstablished = false;
            Serial.println("[NET] server disconnected (wifi lost)");
        }

        if (now >= _nextWifiRetryMs) {
            tryConnectWifi();
            _nextWifiRetryMs = now + WIFI_RETRY_INTERVAL_MS;
        }

        return;
    }


    /*
     * Wi-Fi 已连接。首次连接时启动 mDNS responder。
     *
     * 原来 mDNS 初始化在 tryConnectWifi() 的阻塞等待之后，
     * 改为非阻塞后 tryConnectWifi() 不再等待连接结果，
     * 因此 mDNS 启动移到 run() 中，在检测到 WL_CONNECTED
     * 之后执行。_mdnsStarted 保证只启动一次。
     */
    if (!_mdnsStarted)
    {
        Serial.println("[NET] wifi connected");
        Serial.printf("[NET] IP: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("[NET] RSSI: %d dBm\n", WiFi.RSSI());

        const bool mdnsOk = MDNS.begin("esp32-voice-ai");
        Serial.printf("[MDNS] begin(\"esp32-voice-ai\") returned: %s\n",
                      mdnsOk ? "true" : "false");

        if (mdnsOk)
        {
            const bool svcOk = MDNS.addService("tcp", "tcp", _pcPort);
            Serial.printf("[MDNS] addService(tcp/tcp,%u) returned: %s\n",
                          (unsigned)_pcPort,
                          svcOk ? "true" : "false");
            Serial.printf("[MDNS] hostname=esp32-voice-ai.local\n");
            Serial.printf("[MDNS] localIP=%s\n",
                          WiFi.localIP().toString().c_str());
        }
        else
        {
            Serial.println("[MDNS] start failed (resp=0/false)");
        }

        _mdnsStarted = true;
    }

    /*
     * Low-frequency TCP peer-close probe.
     *
     * 背景：_tcpEstablished 只会在 Wi-Fi 断开或 read/write
     * 显式报错时清零。当服务端被干净关闭（例如 python
     * wifi_server.py Ctrl+C），lwip 会收到 FIN，但如果此时
     * ESP32 没有业务 I/O（SLEEPING 状态、不录音、不下载），
     * 上层永远不会察觉，导致 _tcpEstablished 长期为 true，
     * 即便服务端已经消失。
     *
     * 探针策略：在 _tcpEstablished == true 时，每
     * TCP_PROBE_INTERVAL_MS (5000ms) 调用一次
     * _client.connected()。该调用内部会做一次
     * recv(fd, 0, MSG_DONTWAIT)，使 lwip 有机会消化
     * 已经到达但对应用层尚不可见的 FIN / RST，然后把
     * 内部 _connected 置为 false。
     *
     * 探针本身：
     *   - 不主动发送任何探测包（不发 keepalive）；
     *   - 不调用 tryConnectTcp()（避免与"合法上传中短暂
     *     抖动 → 立即重连"的老问题冲突）；
     *   - 只负责标记 _tcpEstablished=false 并把
     *     _nextTcpRetryMs 设置到 3 秒后，让现有的
     *     retry 分支完成实际的重连工作。
     */
    if (_tcpEstablished && (now - _lastTcpProbeMs) >= TCP_PROBE_INTERVAL_MS)
    {
        _lastTcpProbeMs = now;

        if (!_client.connected())
        {
            _tcpEstablished = false;

            /*
             * 关闭失效 socket，避免后续 retry 时残留状态。
             * tryConnectTcp() 里也会再调一次 _client.stop()，
             * 但这里显式清理，让语义更清晰。
             */
            _client.stop();

            Serial.println("[NET] server disconnected");
            Serial.println("[NET] reconnect in 3s");

            /*
             * 触发现有 retry 机制。3 秒后同一个 run() 下方
             * 的 (!_tcpEstablished) 分支会调用 tryConnectTcp()。
             */
            _nextTcpRetryMs = now + WIFI_RETRY_INTERVAL_MS;
        }
    }

    /*
     * IMPORTANT:
     *
     * Do NOT call _client.connected() unconditionally here.
     *
     * The previous implementation did this:
     *
     *     if (!_client.connected())
     *         tryConnectTcp();
     *
     * This caused the ESP32 to reconnect in the middle of
     * a perfectly valid upload.
     *
     * We now trust our own TCP state and only reconnect when
     * a real TCP operation fails, OR when the low-frequency
     * probe above (running at most every 5 s) reports that
     * the peer has closed.
     *
     * 非阻塞节流：TCP 连接失败后等 WIFI_RETRY_INTERVAL_MS
     * 再重试，期间不阻塞 loop()。
     */
    if (!_tcpEstablished) {

        if (now >= _nextTcpRetryMs) {
            tryConnectTcp();
            _nextTcpRetryMs = now + WIFI_RETRY_INTERVAL_MS;
        }
    }
}


bool WifiClient::tryConnectWifi()
{
    if (WiFi.status() == WL_CONNECTED) {
        return true;
    }

    /*
     * Wi-Fi is not connected, therefore any previous TCP
     * state is invalid.
     */
    _tcpEstablished = false;

    Serial.printf("[NET] wifi connecting to %s\n", _ssid);

    /*
     * 非阻塞：只调用 WiFi.begin() 然后立即返回。
     * 连接结果由 run() 在后续 loop 中通过 WiFi.status() 检测。
     * _nextWifiRetryMs 节流防止频繁调用 WiFi.begin()。
     */
    WiFi.begin(_ssid, _password);

    return false;
}


bool WifiClient::tryConnectTcp()
{
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }


    /*
     * If our own state says the TCP connection is alive,
     * don't reconnect.
     */
    if (_tcpEstablished) {
        return true;
    }


    /*
     * Make sure any previous socket is closed before
     * establishing a new connection.
     */
    _client.stop();


    Serial.printf("[NET] connecting to server %s:%u\n",
                  _pcIp.toString().c_str(),
                  _pcPort);


    if (!_client.connect(
            _pcIp,
            _pcPort,
            WIFI_CONNECT_TIMEOUT_MS))
    {
        Serial.println("[NET] server connection failed");

        _tcpEstablished = false;

        /*
         * 不再 delay()。run() 通过 _nextTcpRetryMs 节流，
         * 3 秒后才会再次尝试，期间 loop() 不阻塞。
         */
        return false;
    }


    /*
     * Disable Nagle algorithm.
     *
     * This is useful for our small protocol headers and
     * ACK packets.
     */
    _client.setNoDelay(true);


    /*
     * From this point on we consider the TCP session
     * established.
     *
     * We intentionally do NOT immediately call
     * _client.connected().
     */
    _tcpEstablished = true;

    Serial.println("[NET] server connected");

    return true;
}


bool WifiClient::isConnected()
{
    /*
     * Wi-Fi must still be connected.
     */
    if (WiFi.status() != WL_CONNECTED) {

        return false;
    }


    /*
     * Use our own TCP state.
     *
     * Do NOT call:
     *
     *     _client.connected()
     *
     * here.
     */
    return _tcpEstablished;
}


void WifiClient::disconnect()
{
    _tcpEstablished = false;

    _client.stop();

    Serial.println("[NET] server disconnected");
}


int WifiClient::read(
    uint8_t *buf,
    size_t max_len
)
{
    if (buf == nullptr || max_len == 0) {
        return 0;
    }


    if (!_mutex ||
        xSemaphoreTake(
            _mutex,
            pdMS_TO_TICKS(
                WIFI_LOCK_TIMEOUT_MS
            )
        ) != pdTRUE)
    {
        return 0;
    }


    _client.setTimeout(
        TCP_READ_TIMEOUT_MS
    );


    int n = _client.read(
        buf,
        max_len
    );


    /*
     * A negative value indicates an actual read error.
     *
     * A zero result can also simply mean that no data was
     * available, so don't treat zero as a disconnect.
     */
    if (n < 0) {

        _tcpEstablished = false;

        Serial.println(
            "[tcp] read failed, TCP marked disconnected"
        );
    }


    xSemaphoreGive(_mutex);


    return n;
}


int WifiClient::available()
{
    if (!_mutex ||
        xSemaphoreTake(
            _mutex,
            pdMS_TO_TICKS(
                WIFI_LOCK_TIMEOUT_MS
            )
        ) != pdTRUE)
    {
        return 0;
    }


    int n = _client.available();


    xSemaphoreGive(_mutex);


    return n;
}


int WifiClient::write(
    const uint8_t *buf,
    size_t len
)
{
    if (buf == nullptr || len == 0) {
        return 0;
    }


    if (!_mutex ||
        xSemaphoreTake(
            _mutex,
            pdMS_TO_TICKS(
                WIFI_LOCK_TIMEOUT_MS
            )
        ) != pdTRUE)
    {
        return 0;
    }


    int n = _client.write(
        buf,
        len
    );


    /*
     * This is now the important place where we detect
     * an actual TCP failure.
     *
     * If the socket really stopped working, write()
     * should fail.
     */
    if (n != static_cast<int>(len)) {

        Serial.printf(
            "[tcp] write failed: %d/%u bytes\n",
            n,
            (unsigned)len
        );

        _tcpEstablished = false;
    }


    xSemaphoreGive(_mutex);


    return n;
}


void WifiClient::flush()
{
    if (!_mutex ||
        xSemaphoreTake(
            _mutex,
            pdMS_TO_TICKS(
                WIFI_LOCK_TIMEOUT_MS
            )
        ) != pdTRUE)
    {
        return;
    }


    /*
     * Arduino-ESP32 3.x:
     *
     * flush() is TX-related.
     *
     * It must NOT be used to clear the RX buffer.
     *
     * The previous custom implementation that repeatedly
     * called _client.read() here was wrong because it could
     * consume protocol data such as PLAY/ACK.
     */
    _client.flush();


    xSemaphoreGive(_mutex);
}