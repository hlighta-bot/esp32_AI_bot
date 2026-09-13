#ifndef WIFI_CLIENT_H
#define WIFI_CLIENT_H

#ifndef WIFI_CONNECT_TIMEOUT_MS
#define WIFI_CONNECT_TIMEOUT_MS 5000
#endif

#ifndef WIFI_RETRY_INTERVAL_MS
#define WIFI_RETRY_INTERVAL_MS 2000
#endif

#ifndef WIFI_LOCK_TIMEOUT_MS
#define WIFI_LOCK_TIMEOUT_MS 100
#endif

#ifndef TCP_READ_TIMEOUT_MS
#define TCP_READ_TIMEOUT_MS 100
#endif

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>

/*
 * Project/network configuration.
 *
 * This header contains:
 *   WIFI_CONNECT_TIMEOUT_MS
 *   WIFI_RETRY_INTERVAL_MS
 *   WIFI_LOCK_TIMEOUT_MS
 *   TCP_READ_TIMEOUT_MS
 *   PC_PORT
 *   PC_HOST
 *   WIFI_SSID
 *   WIFI_PASS
 */
#include "../secrets.local.h"


class WifiClient
{
public:
    WifiClient();

    void begin();
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

private:
    IPAddress _pcIp;
    uint16_t _pcPort;

    WiFiClient _client;

    bool _started;

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