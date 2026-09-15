// ============================================================
// device_config.cpp - Device configuration (NVS + defaults)
// ============================================================
//
// Step 1: RuntimeConfig + DeviceConfig + Preferences/NVS
//
// Flow:
//   1. begin() opens Preferences namespace "voice_ai"
//   2. Check if cfg_ver exists and matches CFG_VER
//   3. If valid NVS config → load from NVS
//   4. If no/invalid NVS   → load defaults from secrets.h
//   5. save() writes all fields in one NVS session
//
// ============================================================

#include "device_config.h"

// secrets.h defines WIFI_SSID, WIFI_PASS, PC_HOST, PC_PORT
// Path: src/config/ → src/secrets.h  (one level up)
#include "../secrets.h"

#include <Preferences.h>

// ============================================================
// Constructor
// ============================================================

DeviceConfig::DeviceConfig()
    : _hasNVSConfig(false)
{
    // Initialize config to zero
    memset(&_config, 0, sizeof(RuntimeConfig));

    _config.vad_rms = DEFAULT_VAD_RMS;
    _config.vad_min_ms = DEFAULT_VAD_MIN_MS;
    _config.vad_sil_ms = DEFAULT_VAD_SIL_MS;
    _config.cfg_ver = 0;
}

// ============================================================
// begin
// ============================================================

bool DeviceConfig::begin()
{
    Preferences prefs;

    if (!prefs.begin(NVS_NAMESPACE, true))
    {
        Serial.println("[config] NVS open failed");
        loadDefaults();
        return false;
    }

    // Check if a valid config version exists
    if (prefs.isKey(NVS_KEY_CFG_VER))
    {
        uint16_t ver = prefs.getUShort(NVS_KEY_CFG_VER);

        if (ver == CFG_VER)
        {
            loadFromNVS();
            prefs.end();
            return true;
        }
        else
        {
            Serial.printf(
                "[config] NVS version mismatch (%u vs %u), loading defaults\n",
                (unsigned)ver,
                (unsigned)CFG_VER
            );
        }
    }

    // No valid NVS config → load defaults from secrets.h only (no NVS write)
    // This ensures isConfigValid() returns false until user explicitly saves via Web
    loadDefaults();

    prefs.end();
    return true;
}

// ============================================================
// getConfig
// ============================================================

const RuntimeConfig& DeviceConfig::getConfig() const
{
    return _config;
}

// ============================================================
// isConfigValid
// ============================================================

bool DeviceConfig::isConfigValid() const
{
    // No NVS config loaded → not valid
    if (!_hasNVSConfig)
    {
        return false;
    }

    // Must have matching config version
    if (_config.cfg_ver != CFG_VER)
    {
        return false;
    }

    // WiFi SSID must not be empty
    if (_config.wifi_ssid[0] == '\0')
    {
        return false;
    }

    return true;
}

// ============================================================
// save
// ============================================================

bool DeviceConfig::save(const RuntimeConfig& newConfig)
{
    Preferences prefs;

    if (!prefs.begin(NVS_NAMESPACE, false))
    {
        Serial.println("[config] NVS save open failed");
        return false;
    }

    // Write all fields in one session
    prefs.putString(NVS_KEY_SSID,  newConfig.wifi_ssid);
    prefs.putString(NVS_KEY_PWD,   newConfig.wifi_pass);
    prefs.putString(NVS_KEY_HOST,  newConfig.pc_host);
    prefs.putUShort(NVS_KEY_PORT,  newConfig.pc_port);
    prefs.putInt(NVS_KEY_VAD_RMS, (int32_t)newConfig.vad_rms);
    prefs.putInt(NVS_KEY_VAD_MIN, (int32_t)newConfig.vad_min_ms);
    prefs.putInt(NVS_KEY_VAD_SIL, (int32_t)newConfig.vad_sil_ms);
    prefs.putUShort(NVS_KEY_CFG_VER, newConfig.cfg_ver);

    prefs.end();

    // Update in-memory config
    _config = newConfig;
    _hasNVSConfig = true;

    Serial.println("[config] saved to NVS");
    return true;
}

// ============================================================
// resetWifi
// ============================================================

bool DeviceConfig::resetWifi()
{
    Preferences prefs;

    if (!prefs.begin(NVS_NAMESPACE, false))
    {
        Serial.println("[config] NVS resetWifi open failed");
        return false;
    }

    // Remove WiFi keys
    prefs.remove(NVS_KEY_SSID);
    prefs.remove(NVS_KEY_PWD);

    prefs.end();

    // Reload defaults (secrets.h)
    loadDefaults();
    _hasNVSConfig = false;

    Serial.println("[config] WiFi credentials reset");
    return true;
}

// ============================================================
// factoryReset
// ============================================================

bool DeviceConfig::factoryReset()
{
    Preferences prefs;

    if (!prefs.begin(NVS_NAMESPACE, false))
    {
        Serial.println("[config] NVS factoryReset open failed");
        return false;
    }

    // Remove all keys
    prefs.remove(NVS_KEY_SSID);
    prefs.remove(NVS_KEY_PWD);
    prefs.remove(NVS_KEY_HOST);
    prefs.remove(NVS_KEY_PORT);
    prefs.remove(NVS_KEY_VAD_RMS);
    prefs.remove(NVS_KEY_VAD_MIN);
    prefs.remove(NVS_KEY_VAD_SIL);
    prefs.remove(NVS_KEY_CFG_VER);

    prefs.end();

    // Reload defaults (secrets.h)
    loadDefaults();
    _hasNVSConfig = false;

    Serial.println("[config] factory reset complete");
    return true;
}

// ============================================================
// loadFromNVS
// ============================================================

void DeviceConfig::loadFromNVS()
{
    Preferences prefs;

    if (!prefs.begin(NVS_NAMESPACE, true))
    {
        Serial.println("[config] NVS load open failed");
        loadDefaults();
        return;
    }

    // WiFi
    prefs.getString(NVS_KEY_SSID,  _config.wifi_ssid, WIFI_SSID_MAX_LEN + 1);
    prefs.getString(NVS_KEY_PWD,   _config.wifi_pass, WIFI_PASS_MAX_LEN + 1);

    // PC
    prefs.getString(NVS_KEY_HOST,  _config.pc_host, HOST_MAX_LEN + 1);
    _config.pc_port = prefs.getUShort(NVS_KEY_PORT);

    // VAD
    _config.vad_rms = (uint32_t)prefs.getInt(NVS_KEY_VAD_RMS);
    _config.vad_min_ms = (uint32_t)prefs.getInt(NVS_KEY_VAD_MIN);
    _config.vad_sil_ms = (uint32_t)prefs.getInt(NVS_KEY_VAD_SIL);

    // Version
    _config.cfg_ver = prefs.getUShort(NVS_KEY_CFG_VER);

    prefs.end();

    _hasNVSConfig = true;

    Serial.printf(
        "[config] loaded from NVS: ssid=%s host=%s port=%u vad_rms=%u\n",
        _config.wifi_ssid,
        _config.pc_host,
        (unsigned)_config.pc_port,
        (unsigned)_config.vad_rms
    );
}

// ============================================================
// loadDefaults
// ============================================================

void DeviceConfig::loadDefaults()
{
    // From secrets.h (which includes secrets.local.h if present)
    strncpy(_config.wifi_ssid, WIFI_SSID, WIFI_SSID_MAX_LEN);
    _config.wifi_ssid[WIFI_SSID_MAX_LEN] = '\0';

    strncpy(_config.wifi_pass, WIFI_PASS, WIFI_PASS_MAX_LEN);
    _config.wifi_pass[WIFI_PASS_MAX_LEN] = '\0';

    strncpy(_config.pc_host, PC_HOST, HOST_MAX_LEN);
    _config.pc_host[HOST_MAX_LEN] = '\0';

    _config.pc_port = PC_PORT;

    _config.vad_rms = DEFAULT_VAD_RMS;
    _config.vad_min_ms = DEFAULT_VAD_MIN_MS;
    _config.vad_sil_ms = DEFAULT_VAD_SIL_MS;

    _config.cfg_ver = CFG_VER;

    _hasNVSConfig = false;

    Serial.printf(
        "[config] loaded defaults: ssid=%s host=%s port=%u vad_rms=%u\n",
        _config.wifi_ssid,
        _config.pc_host,
        (unsigned)_config.pc_port,
        (unsigned)_config.vad_rms
    );
}
