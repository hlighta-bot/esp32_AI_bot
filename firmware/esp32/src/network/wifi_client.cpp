#include "wifi_client.h"

#include <lwip/netdb.h>

#include "../secrets.local.h"


WifiClient::WifiClient()
    : _pcIp()
    , _pcPort(PC_PORT)
    , _started(false)
    , _tcpEstablished(false)
    , _mutex(nullptr)
{
}


void WifiClient::begin()
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

    WiFi.mode(WIFI_STA);

    WiFi.setHostname("esp32-voice-ai");

    /*
     * Disable Wi-Fi power saving.
     * This is important for continuous audio/TCP streaming.
     */
    WiFi.setSleep(false);


    /*
     * Resolve PC_HOST.
     *
     * Usually PC_HOST is already an IP address, so this
     * normally succeeds in fromString().
     */
    if (!_pcIp.fromString(PC_HOST)) {

        Serial.printf(
            "[wifi] resolving host: %s\n",
            PC_HOST
        );

        struct hostent *host = gethostbyname(PC_HOST);

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
                PC_HOST
            );
        }
    }


    /*
     * Start mDNS.
     */
    if (MDNS.begin("esp32-voice-ai")) {

        Serial.println("[wifi] mDNS started");

        MDNS.addService(
            "tcp",
            "tcp",
            PC_PORT
        );
    }
    else
    {
        Serial.println("[wifi] mDNS start failed");
    }


    _started = true;

    _tcpEstablished = false;

    Serial.println("[wifi] initialized");
}


void WifiClient::run()
{
    if (!_started) {
        return;
    }


    /*
     * Wi-Fi itself is disconnected.
     */
    if (WiFi.status() != WL_CONNECTED) {

        /*
         * TCP is necessarily invalid when Wi-Fi is gone.
         */
        _tcpEstablished = false;

        tryConnectWifi();

        return;
    }


    /*
     * IMPORTANT:
     *
     * Do NOT call _client.connected() here.
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
     * a real TCP operation fails.
     */
    if (!_tcpEstablished) {
        tryConnectTcp();
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


    Serial.printf(
        "[wifi] connecting to %s\n",
        WIFI_SSID
    );

    WiFi.begin(
        WIFI_SSID,
        WIFI_PASS
    );


    const uint32_t start = millis();


    while (
        WiFi.status() != WL_CONNECTED &&
        millis() - start < WIFI_CONNECT_TIMEOUT_MS
    )
    {
        delay(100);
    }


    if (WiFi.status() != WL_CONNECTED) {

        Serial.println(
            "[wifi] connection timeout"
        );

        return false;
    }


    Serial.println(
        "[wifi] connected"
    );


    Serial.printf(
        "[wifi] IP: %s\n",
        WiFi.localIP().toString().c_str()
    );


    Serial.printf(
        "[wifi] RSSI: %d dBm\n",
        WiFi.RSSI()
    );


    return true;
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


    Serial.printf(
        "[tcp] connecting to %s:%u\n",
        _pcIp.toString().c_str(),
        _pcPort
    );


    if (!_client.connect(
            _pcIp,
            _pcPort,
            WIFI_CONNECT_TIMEOUT_MS))
    {
        Serial.println(
            "[tcp] connection failed"
        );

        _tcpEstablished = false;

        delay(WIFI_RETRY_INTERVAL_MS);

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


    Serial.println(
        "[tcp] connected"
    );


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

    Serial.println(
        "[tcp] disconnected"
    );
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