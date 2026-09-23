// ============================================================
// config_web.h - SoftAP + captive portal + Web config page
// ============================================================
//
// Step 2A: SoftAP + DNS + Captive Portal + WebServer
// Step 2B-1: Display-only config page with current RuntimeConfig
// Step 2C-1: NVS save, Wi-Fi reset, factory reset, reboot
//
// ConfigWeb owns SoftAP, DNSServer and WebServer.
// ConfigWeb -> DeviceConfig -> RuntimeConfig -> NVS
// ============================================================

#ifndef CONFIG_WEB_H
#define CONFIG_WEB_H

#include <WebServer.h>
#include <DNSServer.h>

class DeviceConfig;

class ConfigWeb
{
public:
    ConfigWeb();

    bool begin(DeviceConfig& config);
    void beginHTTP(DeviceConfig& config);
    void loop();
    void loopHTTP();
    void stop();

    const char* getApSsid() const;

    // 暴露底层 WebServer 引用，供其他模块（如 RobotEventServer）
    // 复用同一个 WebServer(80) 实例注册路由，避免重复绑定端口。
    WebServer& server();

private:
    void setupRoutes();
    void handleRoot();
    void handleCaptivePortal();
    void handleSave();
    void handleReset();
    void handleFactoryReset();
    void handleReboot();

    static String sendResultPage(bool success, const String& message);
    static void safeCopy(const String& src, char* dst, size_t dstSize);
    static bool parseU32(const String& text, uint32_t& out);

    WebServer    _server;
    DNSServer    _dnsServer;
    char         _ssid[32];
    DeviceConfig* _config;
    uint32_t     _rebootAt;
};

#endif  // CONFIG_WEB_H
