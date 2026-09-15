// ============================================================
// config_web.cpp - SoftAP + captive portal + Web config page
// ============================================================
//
// Step 2A: SoftAP + DNS captive portal + HTTP test page
// Step 2B-1: Display-only config page with current RuntimeConfig
// Step 2C-1: NVS save, Wi-Fi reset, factory reset, reboot
//
//   - SoftAP: 192.168.4.1 / 255.255.255.0
//   - SSID: ESP32-Voice-XXXX from MAC address
//   - DNS wildcard resolves to SoftAP IP
//   - GET / serves mobile-friendly config form
//   - POST /save saves to NVS via DeviceConfig
//   - POST /reset removes Wi-Fi credentials only
//   - POST /factory-reset clears all config
//   - POST /reboot restarts the device
//   - Captive portal detection URLs redirect to /
//
// Data flow:
//   ConfigWeb -> DeviceConfig -> RuntimeConfig -> NVS
//
// No password is ever printed in logs or responses.
// ============================================================

#include "config_web.h"

#include "../config/device_config.h"

#include <WiFi.h>

// Fixed development AP password. No cloud/API keys are stored in web config.
#define CONFIG_WEB_AP_PASSWORD "ESP32Voice"

#define CONFIG_WEB_AP_IP      IPAddress(192, 168, 4, 1)
#define CONFIG_WEB_AP_GW      IPAddress(192, 168, 4, 1)
#define CONFIG_WEB_AP_SUBNET  IPAddress(255, 255, 255, 0)

// Non-blocking reboot delay (ms)
#define REBOOT_DELAY_MS       2000

// ============================================================
// HTML escaping helper
// ============================================================

static String htmlEscape(const char* text)
{
    String result;
    if (text == nullptr)
    {
        return result;
    }

    // Use \x26 for ampersand to avoid source-level encoding issues
    for (const char* p = text; *p; p++)
    {
        if (*p == '\x26')    { result += '\x26'; result += "amp;"; }
        else if (*p == '<')  { result += '\x26'; result += "lt;"; }
        else if (*p == '>')  { result += '\x26'; result += "gt;"; }
        else if (*p == '"')  { result += '\x26'; result += "quot;"; }
        else if (*p == '\'') { result += '\x26'; result += "#x27;"; }
        else                 { result += *p; }
    }
    return result;
}

// ============================================================
// safeCopy - strncpy + guaranteed null termination
// ============================================================

void ConfigWeb::safeCopy(const String& src, char* dst, size_t dstSize)
{
    if (dstSize == 0) return;
    size_t len = src.length();
    if (len >= dstSize) len = dstSize - 1;
    memcpy(dst, src.c_str(), len);
    dst[len] = '\0';
}

// ============================================================
// parseU32 - parse non-negative integer, reject overflow
// ============================================================

bool ConfigWeb::parseU32(const String& text, uint32_t& out)
{
    if (text.isEmpty()) return false;

    // Must contain only digits (no sign, no whitespace)
    for (size_t i = 0; i < text.length(); i++)
    {
        if (!isdigit((unsigned char)text[i])) return false;
    }

    char buf[16];
    text.toCharArray(buf, sizeof(buf));

    errno = 0;
    unsigned long val = strtoul(buf, NULL, 10);
    if (errno == ERANGE) return false;

    out = (uint32_t)val;
    return true;
}

// ============================================================
// sendResultPage - generate mobile-friendly result HTML
// ============================================================

String ConfigWeb::sendResultPage(bool success, const String& message)
{
    String page;
    page.reserve(2048);

    page += "<!doctype html>";
    page += "<html lang=\"en\">";
    page += "<head>";
    page += "<meta charset=\"utf-8\">";
    page += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
    page += "<title>ESP32 Voice AI - Result</title>";
    page += "<style>";
    page += "*{box-sizing:border-box;margin:0;padding:0;}";
    page += "body{font-family:system-ui,-apple-system,sans-serif;background:#f0f2f5;color:#1a1a2e;padding:16px;line-height:1.5;display:flex;justify-content:center;align-items:center;min-height:100vh;}";
    page += ".result-card{max-width:400px;width:100%;background:#fff;border-radius:14px;padding:32px 20px;text-align:center;box-shadow:0 2px 8px rgba(0,0,0,0.06);}";
    page += ".icon{font-size:48px;margin-bottom:12px;}";
    page += ".title{font-size:18px;font-weight:700;margin-bottom:8px;}";
    page += ".msg{font-size:14px;color:#6b7280;line-height:1.5;}";
    page += ".spinner{display:inline-block;width:24px;height:24px;border:3px solid #e5e7eb;border-top-color:#3b82f6;border-radius:50%;animation:spin 1s linear infinite;margin-top:16px;}";
    page += "@keyframes spin{to{transform:rotate(360deg)}}";
    page += "</style>";
    page += "</head>";
    page += "<body>";
    page += "<div class=\"result-card\">";

    if (success)
    {
        page += "<div class=\"icon\">&#10003;</div>";
        page += "<div class=\"title\">Success</div>";
        page += "<p class=\"msg\">" + htmlEscape(message.c_str()) + "</p>";
        page += "<div class=\"spinner\"></div>";
    }
    else
    {
        page += "<div class=\"icon\">&#10007;</div>";
        page += "<div class=\"title\">Error</div>";
        page += "<p class=\"msg\">" + htmlEscape(message.c_str()) + "</p>";
        page += "<a href=\"/\" style=\"display:inline-block;margin-top:16px;padding:10px 20px;background:#3b82f6;color:#fff;border-radius:8px;text-decoration:none;font-weight:600;\">Back to Configuration</a>";
    }

    page += "</div>";
    page += "</body>";
    page += "</html>";

    return page;
}

// ============================================================
// Constructor
// ============================================================

ConfigWeb::ConfigWeb()
    : _config(nullptr)
    , _rebootAt(0)
{
    _ssid[0] = '\0';
}

// ============================================================
// begin
// ============================================================

bool ConfigWeb::begin(DeviceConfig& config)
{
    _config = &config;

    if (WiFi.getMode() != WIFI_OFF)
    {
        WiFi.mode(WIFI_OFF);
        delay(100);
    }

    WiFi.mode(WIFI_AP);

    snprintf(_ssid, sizeof(_ssid), "ESP32-Voice-%s", WiFi.macAddress().c_str());

    if (!WiFi.softAPConfig(CONFIG_WEB_AP_GW, CONFIG_WEB_AP_IP, CONFIG_WEB_AP_SUBNET))
    {
        Serial.println("[config-web] SoftAP config failed");
        return false;
    }

    if (!WiFi.softAP(_ssid, CONFIG_WEB_AP_PASSWORD))
    {
        Serial.println("[config-web] SoftAP start failed");
        return false;
    }

    if (!_dnsServer.start(CONFIG_WEB_AP_IP, "_", CONFIG_WEB_AP_IP))
    {
        Serial.println("[config-web] DNS captive portal start failed");
        return false;
    }

    // HTTP Server（路由 + 监听）与 AP/DNS 分离启动，
    // 便于 Normal Mode 下仅启用 HTTP 而不切换 Wi-Fi 模式。
    beginHTTP(config);

    Serial.printf(
        "[config-web] SoftAP ready: ssid=%s ip=%s password=%s\n",
        _ssid,
        CONFIG_WEB_AP_IP.toString().c_str(),
        CONFIG_WEB_AP_PASSWORD
    );

    return true;
}

// ============================================================
// beginHTTP - start HTTP config server only (STA mode)
//
// Normal Mode 下调用：只启动 WebServer，不切换 Wi-Fi 模式，
// 不启动 SoftAP / DNS。
//
// Config Mode 由 begin() 内部调用（AP 已就绪后）。
// 同一套页面与保存逻辑，无重复 HTML。
// ============================================================

void ConfigWeb::beginHTTP(DeviceConfig& config)
{
    _config = &config;

    setupRoutes();
    _server.begin();

    Serial.println(
        "[config-web] HTTP config server started (STA mode)."
    );
}

// ============================================================
// loop - handles non-blocking reboot
// ============================================================

void ConfigWeb::loop()
{
    _dnsServer.processNextRequest();
    loopHTTP();
}

// ============================================================
// loopHTTP - poll HTTP server only (STA mode)
//
// Normal Mode 下调用：仅轮询 WebServer + 非阻塞重启检查，
// 不处理 DNS（DNS 仅 Config Mode 需要）。
// ============================================================

void ConfigWeb::loopHTTP()
{
    _server.handleClient();

    // Non-blocking reboot check
    if (_rebootAt != 0 && millis() >= _rebootAt)
    {
        _rebootAt = 0;
        Serial.println("[config-web] rebooting");
        ESP.restart();
    }
}

// ============================================================
// stop
// ============================================================

void ConfigWeb::stop()
{
    _server.stop();
    _dnsServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
}

// ============================================================
// getApSsid
// ============================================================

const char* ConfigWeb::getApSsid() const
{
    return _ssid;
}

// ============================================================
// setupRoutes
// ============================================================

void ConfigWeb::setupRoutes()
{
    // Main config page
    _server.on("/", HTTP_GET, [this]() { handleRoot(); });

    // POST handlers
    _server.on("/save", HTTP_POST, [this]() { handleSave(); });
    _server.on("/reset", HTTP_POST, [this]() { handleReset(); });
    _server.on("/factory-reset", HTTP_POST, [this]() { handleFactoryReset(); });
    _server.on("/reboot", HTTP_POST, [this]() { handleReboot(); });

    // Captive portal detection URLs
    _server.on("/generate_204", HTTP_GET, [this]() { handleCaptivePortal(); });
    _server.on("/hotspot-detect.html", HTTP_GET, [this]() { handleCaptivePortal(); });
    _server.on("/connecttest.txt", HTTP_GET, [this]() { handleCaptivePortal(); });
    _server.on("/ncsi.txt", HTTP_GET, [this]() { handleCaptivePortal(); });
}

// ============================================================
// handleRoot - main config page with form
// ============================================================

void ConfigWeb::handleRoot()
{
    const RuntimeConfig& cfg = _config->getConfig();

    String escSsid   = htmlEscape(cfg.wifi_ssid);
    String escPass   = htmlEscape(cfg.wifi_pass);
    String escHost   = htmlEscape(cfg.pc_host);
    String strPort   = String(cfg.pc_port);
    String strRms    = String(cfg.vad_rms);
    String strMin    = String(cfg.vad_min_ms);
    String strSil    = String(cfg.vad_sil_ms);

    String page;
    page.reserve(4096);

    page += "<!doctype html>";
    page += "<html lang=\"en\">";
    page += "<head>";
    page += "<meta charset=\"utf-8\">";
    page += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
    page += "<title>ESP32 Voice AI - Configuration</title>";
    page += "<style>";
    page += "*{box-sizing:border-box;margin:0;padding:0;}";
    page += "body{font-family:system-ui,-apple-system,sans-serif;background:#f0f2f5;color:#1a1a2e;padding:16px;line-height:1.5;}";
    page += ".container{max-width:480px;margin:0 auto;}";
    page += "h1{font-size:20px;font-weight:700;text-align:center;margin-bottom:4px;}";
    page += ".subtitle{font-size:14px;color:#6b7280;text-align:center;margin-bottom:20px;}";
    page += ".status-bar{background:#e0f2e9;border:1px solid #a7f3d0;border-radius:10px;padding:10px 14px;font-size:13px;color:#065f46;margin-bottom:20px;display:flex;justify-content:space-between;align-items:center;}";
    page += ".status-label{font-weight:600;}";
    page += ".status-ip{font-family:monospace;font-size:13px;}";
    page += ".card{background:#fff;border-radius:14px;padding:16px;margin-bottom:14px;box-shadow:0 2px 8px rgba(0,0,0,0.06);}";
    page += ".card-title{font-size:15px;font-weight:700;color:#1a1a2e;margin-bottom:12px;padding-bottom:8px;border-bottom:1px solid #e5e7eb;}";
    page += ".field{margin-bottom:14px;}";
    page += ".field:last-child{margin-bottom:0;}";
    page += "label{display:block;font-size:14px;font-weight:600;color:#374151;margin-bottom:4px;}";
    page += "input[type=text],input[type=password],input[type=number],input[type=url]{width:100%;padding:10px 12px;font-size:16px;border:1px solid #d1d5db;border-radius:8px;background:#fff;color:#1a1a2e;outline:none;transition:border-color 0.2s;}";
    page += "input[type=text]:focus,input[type=password]:focus,input[type=number]:focus,input[type=url]:focus{border-color:#3b82f6;box-shadow:0 0 0 2px rgba(59,130,246,0.15);}";
    page += ".hint{font-size:12px;color:#9ca3af;margin-top:4px;line-height:1.4;}";
    page += ".check-row{display:flex;align-items:center;gap:8px;margin-top:6px;}";
    page += ".check-row input[type=checkbox]{width:18px;height:18px;cursor:pointer;}";
    page += ".check-row label{margin:0;font-size:13px;font-weight:500;cursor:pointer;color:#6b7280;}";
    page += ".btn{width:100%;padding:14px;font-size:16px;font-weight:700;border:none;border-radius:10px;cursor:pointer;transition:background 0.2s;display:block;text-align:center;margin-bottom:8px;}";
    page += ".btn:last-child{margin-bottom:0;}";
    page += ".btn-save{background:#3b82f6;color:#fff;}";
    page += ".btn-save:active{background:#2563eb;}";
    page += ".btn-reset{background:#f59e0b;color:#fff;}";
    page += ".btn-reset:active{background:#d97706;}";
    page += ".btn-danger{background:#ef4444;color:#fff;}";
    page += ".btn-danger:active{background:#dc2626;}";
    page += ".btn-reboot{background:#6b7280;color:#fff;}";
    page += ".btn-reboot:active{background:#4b5563;}";
    page += ".actions{margin-top:14px;}";
    page += "</style>";
    page += "</head>";
    page += "<body>";
    page += "<div class=\"container\">";

    // Header
    page += "<h1>ESP32 Voice AI</h1>";
    page += "<p class=\"subtitle\">Firmware Configuration Mode</p>";

    // Status bar
    page += "<div class=\"status-bar\">";
    page += "<span class=\"status-label\">Configuration Mode</span>";
    page += "<span class=\"status-ip\">AP: 192.168.4.1</span>";
    page += "</div>";

    // Main config form
    page += "<form method=\"POST\" action=\"/save\" id=\"configForm\">";

    // --- Wi-Fi Section ---
    page += "<div class=\"card\">";
    page += "<div class=\"card-title\">Wi-Fi</div>";

    page += "<div class=\"field\">";
    page += "<label for=\"ssid\">SSID</label>";
    page += "<input type=\"text\" id=\"ssid\" name=\"ssid\" value=\"" + escSsid + "\" maxlength=\"32\" autocomplete=\"off\" placeholder=\"Network name\" required>";
    page += "</div>";

    page += "<div class=\"field\">";
    page += "<label for=\"password\">Password</label>";
    page += "<input type=\"password\" id=\"password\" name=\"password\" value=\"" + escPass + "\" maxlength=\"64\" autocomplete=\"off\" placeholder=\"Wi-Fi password\">";
    page += "<p class=\"hint\">Leave empty to keep current password.</p>";
    page += "<div class=\"check-row\">";
    page += "<input type=\"checkbox\" id=\"showpw\" onchange=\"document.getElementById('password').type=this.checked?'text':'password'\">";
    page += "<label for=\"showpw\">Show password</label>";
    page += "</div>";
    page += "</div>";

    page += "</div>"; // end card

    // --- PC Server Section ---
    page += "<div class=\"card\">";
    page += "<div class=\"card-title\">PC Server</div>";

    page += "<div class=\"field\">";
    page += "<label for=\"pc_host\">Host</label>";
    page += "<input type=\"text\" id=\"pc_host\" name=\"pc_host\" value=\"" + escHost + "\" maxlength=\"44\" autocomplete=\"off\" placeholder=\"e.g. 192.168.1.100\" required>";
    page += "</div>";

    page += "<div class=\"field\">";
    page += "<label for=\"pc_port\">Port</label>";
    page += "<input type=\"number\" id=\"pc_port\" name=\"pc_port\" value=\"" + strPort + "\" min=\"1\" max=\"65535\" inputmode=\"numeric\" placeholder=\"8080\" required>";
    page += "</div>";

    page += "</div>"; // end card

    // --- VAD Section ---
    page += "<div class=\"card\">";
    page += "<div class=\"card-title\">Voice Detection</div>";

    page += "<div class=\"field\">";
    page += "<label for=\"vad_rms\">RMS Threshold</label>";
    page += "<input type=\"number\" id=\"vad_rms\" name=\"vad_rms\" value=\"" + strRms + "\" min=\"0\" max=\"4294967295\" inputmode=\"numeric\" placeholder=\"400\">";
    page += "<p class=\"hint\">Higher value requires louder speech to trigger.</p>";
    page += "</div>";

    page += "<div class=\"field\">";
    page += "<label for=\"vad_min\">Minimum Voice Time (ms)</label>";
    page += "<input type=\"number\" id=\"vad_min\" name=\"vad_min\" value=\"" + strMin + "\" min=\"0\" max=\"4294967295\" inputmode=\"numeric\" placeholder=\"200\">";
    page += "<p class=\"hint\">Minimum duration of detected speech.</p>";
    page += "</div>";

    page += "<div class=\"field\">";
    page += "<label for=\"vad_sil\">Silence Time (ms)</label>";
    page += "<input type=\"number\" id=\"vad_sil\" name=\"vad_sil\" value=\"" + strSil + "\" min=\"0\" max=\"4294967295\" inputmode=\"numeric\" placeholder=\"700\">";
    page += "<p class=\"hint\">How long silence must continue before recording stops.</p>";
    page += "</div>";

    page += "</div>"; // end card

    // Save button
    page += "<button type=\"submit\" class=\"btn btn-save\">Save Configuration</button>";
    page += "</form>"; // end main form

    // Action buttons section
    page += "<div class=\"actions\">";

    page += "<form method=\"POST\" action=\"/reset\" id=\"resetForm\" onsubmit=\"return confirm('Reset Wi-Fi credentials? Device will reboot.')\">";
    page += "<button type=\"submit\" class=\"btn btn-reset\">Reset Wi-Fi</button>";
    page += "</form>";

    page += "<form method=\"POST\" action=\"/factory-reset\" id=\"factoryForm\" onsubmit=\"return confirm('Factory reset? This clears ALL settings.')\">";
    page += "<button type=\"submit\" class=\"btn btn-danger\">Factory Reset</button>";
    page += "</form>";

    page += "<form method=\"POST\" action=\"/reboot\" id=\"rebootForm\">";
    page += "<button type=\"submit\" class=\"btn btn-reboot\">Reboot</button>";
    page += "</form>";

    page += "</div>"; // end actions

    page += "</div>"; // end container
    page += "</body>";
    page += "</html>";

    _server.send(200, "text/html", page);
}

// ============================================================
// handleSave - POST /save
// ============================================================

void ConfigWeb::handleSave()
{
    const RuntimeConfig& current = _config->getConfig();

    // Copy current config as base (empty password keeps current)
    RuntimeConfig newConfig;
    memcpy(&newConfig, &current, sizeof(RuntimeConfig));

    // --- Parse and validate SSID ---
    if (!_server.hasArg("ssid"))
    {
        _server.send(400, "text/html", sendResultPage(false, "SSID is required."));
        return;
    }
    String ssidVal = _server.arg("ssid");
    if (ssidVal.isEmpty())
    {
        _server.send(400, "text/html", sendResultPage(false, "SSID is required."));
        return;
    }
    if (ssidVal.length() > WIFI_SSID_MAX_LEN)
    {
        _server.send(400, "text/html", sendResultPage(false, "SSID too long (max 32 characters)."));
        return;
    }
    safeCopy(ssidVal, newConfig.wifi_ssid, WIFI_SSID_MAX_LEN + 1);

    // --- Parse and validate Password ---
    // Empty password = keep current (safe default)
    if (_server.hasArg("password"))
    {
        String passVal = _server.arg("password");
        if (passVal.length() > 0)
        {
            if (passVal.length() > WIFI_PASS_MAX_LEN)
            {
                _server.send(400, "text/html", sendResultPage(false, "Password too long (max 64 characters)."));
                return;
            }
            safeCopy(passVal, newConfig.wifi_pass, WIFI_PASS_MAX_LEN + 1);
        }
    }

    // --- Parse and validate PC Host ---
    if (!_server.hasArg("pc_host"))
    {
        _server.send(400, "text/html", sendResultPage(false, "PC host is required."));
        return;
    }
    String hostVal = _server.arg("pc_host");
    if (hostVal.isEmpty())
    {
        _server.send(400, "text/html", sendResultPage(false, "PC host is required."));
        return;
    }
    if (hostVal.length() > HOST_MAX_LEN)
    {
        _server.send(400, "text/html", sendResultPage(false, "Host too long (max 44 characters)."));
        return;
    }
    safeCopy(hostVal, newConfig.pc_host, HOST_MAX_LEN + 1);

    // --- Parse and validate PC Port ---
    uint32_t portVal;
    if (!_server.hasArg("pc_port") || !parseU32(_server.arg("pc_port"), portVal))
    {
        _server.send(400, "text/html", sendResultPage(false, "Invalid PC port."));
        return;
    }
    if (portVal < 1 || portVal > 65535)
    {
        _server.send(400, "text/html", sendResultPage(false, "Invalid PC port (must be 1-65535)."));
        return;
    }
    newConfig.pc_port = (uint16_t)portVal;

    // --- Parse and validate VAD RMS ---
    if (!_server.hasArg("vad_rms") || !parseU32(_server.arg("vad_rms"), newConfig.vad_rms))
    {
        _server.send(400, "text/html", sendResultPage(false, "Invalid RMS threshold."));
        return;
    }

    // --- Parse and validate VAD Min Voice ---
    if (!_server.hasArg("vad_min") || !parseU32(_server.arg("vad_min"), newConfig.vad_min_ms))
    {
        _server.send(400, "text/html", sendResultPage(false, "Invalid minimum voice time."));
        return;
    }

    // --- Parse and validate VAD Silence ---
    if (!_server.hasArg("vad_sil") || !parseU32(_server.arg("vad_sil"), newConfig.vad_sil_ms))
    {
        _server.send(400, "text/html", sendResultPage(false, "Invalid silence time."));
        return;
    }

    // --- Ensure cfg_ver is set ---
    newConfig.cfg_ver = CFG_VER;

    // --- Save to NVS ---
    if (!_config->save(newConfig))
    {
        _server.send(500, "text/html", sendResultPage(false, "Save failed. Try again."));
        return;
    }

    Serial.printf(
        "[config-web] config saved: ssid=%s host=%s port=%u vad_rms=%u\n",
        newConfig.wifi_ssid,
        newConfig.pc_host,
        (unsigned)newConfig.pc_port,
        (unsigned)newConfig.vad_rms
    );

    // --- Schedule reboot after response is sent ---
    _rebootAt = millis() + REBOOT_DELAY_MS;

    _server.send(200, "text/html",
        sendResultPage(true, "Configuration saved. Device will reboot in 2 seconds."));
}

// ============================================================
// handleReset - POST /reset (Wi-Fi credentials only)
// ============================================================

void ConfigWeb::handleReset()
{
    if (!_config->resetWifi())
    {
        _server.send(500, "text/html", sendResultPage(false, "Reset failed. Try again."));
        return;
    }

    Serial.println("[config-web] Wi-Fi credentials reset");

    // Schedule reboot - device re-enters config mode
    _rebootAt = millis() + REBOOT_DELAY_MS;

    _server.send(200, "text/html",
        sendResultPage(true, "Wi-Fi settings reset. Device will reboot in 2 seconds."));
}

// ============================================================
// handleFactoryReset - POST /factory-reset
// ============================================================

void ConfigWeb::handleFactoryReset()
{
    if (!_config->factoryReset())
    {
        _server.send(500, "text/html", sendResultPage(false, "Factory reset failed. Try again."));
        return;
    }

    Serial.println("[config-web] factory reset complete");

    _rebootAt = millis() + REBOOT_DELAY_MS;

    _server.send(200, "text/html",
        sendResultPage(true, "Factory reset complete. Device will reboot in 2 seconds."));
}

// ============================================================
// handleReboot - POST /reboot
// ============================================================

void ConfigWeb::handleReboot()
{
    Serial.println("[config-web] reboot requested");

    // Schedule reboot with shorter delay
    _rebootAt = millis() + 1000;

    _server.send(200, "text/html",
        sendResultPage(true, "Device will reboot in 1 second."));
}

// ============================================================
// handleCaptivePortal
// ============================================================

void ConfigWeb::handleCaptivePortal()
{
    _server.sendHeader("Location", "/", true);
    _server.sendHeader("Cache-Control", "no-store", true);
    _server.send(302, "text/plain", "");
}
