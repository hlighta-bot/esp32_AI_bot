// ============================================================
// secrets.h - Wi-Fi 与网络配置模板
// ============================================================
//
// 首次使用请复制为 secrets.local.h 并填入你的路由器信息：
//   cp secrets.h secrets.local.h
//
// secrets.local.h 已在 .gitignore 中，不会入库。
// 未提供 secrets.local.h 时，使用本文件作为回退。
// ============================================================

#ifndef SECRETS_H
#define SECRETS_H

#ifdef __has_include
#  if __has_include("secrets.local.h")
#    include "secrets.local.h"
#  else
    // ---- 默认值：请改成你的路由器 ----
    #define WIFI_SSID       "your_wifi_ssid"
    #define WIFI_PASS       "your_wifi_password"
    #define PC_HOST         "192.168.1.20"   // PC 或 aidlux 静态 IP
    #define PC_PORT         8888
    #define ESP32_HOSTNAME  "esp32-voice"
    #undef  SECRETS_H_DEFAULT
  #endif
#endif

#ifndef WIFI_SSID
#define WIFI_SSID       "your_wifi_ssid"
#endif

#ifndef WIFI_PASS
#define WIFI_PASS       "your_wifi_password"
#endif

#ifndef PC_HOST
#define PC_HOST         "192.168.1.20"
#endif

#ifndef PC_PORT
#define PC_PORT         8888
#endif

#ifndef ESP32_HOSTNAME
#define ESP32_HOSTNAME  "esp32-voice"
#endif

#endif  // SECRETS_H
