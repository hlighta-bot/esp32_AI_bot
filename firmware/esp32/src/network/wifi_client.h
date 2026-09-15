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

private:
    char     _ssid[32];
    char     _password[65];

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