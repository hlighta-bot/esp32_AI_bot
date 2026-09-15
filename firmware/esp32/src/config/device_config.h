// ============================================================
// device_config.h - Device configuration (NVS + defaults)
// ============================================================
//
// Step 1: RuntimeConfig + DeviceConfig + Preferences/NVS
//
// RuntimeConfig holds all device configuration:
//   - WiFi SSID / Password
//   - PC Host / Port
//   - VAD RMS / Min Voice MS / Silence MS
//   - Config version
//
// DeviceConfig loads from NVS namespace "voice_ai".
// On first boot (no NVS data), falls back to secrets.h.
//
// NVS keys: cfg_ssid, cfg_pwd, cfg_host, cfg_port,
//           vad_rms, vad_min, vad_sil, cfg_ver
//
// ============================================================

#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

#include <Arduino.h>

// NVS namespace
#define NVS_NAMESPACE  "voice_ai"

// NVS keys
#define NVS_KEY_SSID   "cfg_ssid"
#define NVS_KEY_PWD    "cfg_pwd"
#define NVS_KEY_HOST   "cfg_host"
#define NVS_KEY_PORT   "cfg_port"
#define NVS_KEY_VAD_RMS "vad_rms"
#define NVS_KEY_VAD_MIN "vad_min"
#define NVS_KEY_VAD_SIL "vad_sil"
#define NVS_KEY_CFG_VER "cfg_ver"

// Config version
#define CFG_VER        1

// Default VAD thresholds (MAX9814 + software gain)
#define DEFAULT_VAD_RMS     400
#define DEFAULT_VAD_MIN_MS  200
#define DEFAULT_VAD_SIL_MS  700

// String buffer sizes
#define WIFI_SSID_MAX_LEN   32
#define WIFI_PASS_MAX_LEN   64
#define HOST_MAX_LEN        44

// ============================================================
// RuntimeConfig
// ============================================================

struct RuntimeConfig
{
    char     wifi_ssid[WIFI_SSID_MAX_LEN + 1];
    char     wifi_pass[WIFI_PASS_MAX_LEN + 1];
    char     pc_host[HOST_MAX_LEN + 1];
    uint16_t pc_port;
    uint32_t vad_rms;
    uint32_t vad_min_ms;
    uint32_t vad_sil_ms;
    uint16_t cfg_ver;
};

// ============================================================
// DeviceConfig
// ============================================================

class DeviceConfig
{
public:
    DeviceConfig();

    // Initialize: try NVS, fallback to secrets.h defaults
    bool begin();

    // Get current runtime config
    const RuntimeConfig& getConfig() const;

    // Check if config is valid (cfg_ver matches + SSID not empty)
    bool isConfigValid() const;

    // Save config to NVS (all fields in one session)
    bool save(const RuntimeConfig& newConfig);

    // Remove WiFi credentials, reload defaults
    bool resetWifi();

    // Clear all config, reload defaults
    bool factoryReset();

private:
    void loadFromNVS();
    void loadDefaults();

    RuntimeConfig _config;
    bool          _hasNVSConfig;
};

#endif  // DEVICE_CONFIG_H
