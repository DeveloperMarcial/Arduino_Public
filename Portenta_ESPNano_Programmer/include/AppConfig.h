#pragma once

#include <Arduino.h>
#include <IPAddress.h>

#ifndef PORTENTA_NETWORK_USE_WIFI
#define PORTENTA_NETWORK_USE_WIFI 0
#endif

#ifndef PORTENTA_WIFI_SSID
#define PORTENTA_WIFI_SSID ""
#endif

#ifndef PORTENTA_WIFI_PASS
#define PORTENTA_WIFI_PASS ""
#endif

namespace AppConfig {

constexpr bool kUseWifi = PORTENTA_NETWORK_USE_WIFI != 0;
constexpr const char *kWifiSsid = PORTENTA_WIFI_SSID;
constexpr const char *kWifiPass = PORTENTA_WIFI_PASS;
constexpr uint32_t kStatusBaud = PORTENTA_STATUS_BAUD;
constexpr uint16_t kHttpPort = PORTENTA_REMOTE_FLASH_HTTP_PORT;
constexpr uint32_t kNetworkConnectTimeoutMs = 30000;
constexpr uint32_t kHttpReadTimeoutMs = 2000;
constexpr size_t kHttpRequestBufferSize = 2048;
constexpr size_t kHttpResponseBufferSize = 4096;
constexpr size_t kHttpBodyBufferSize = 1536;
constexpr size_t kMaxHeaderCount = 16;
constexpr size_t kMaxPathSegments = 8;

// Verified against the Arduino mbed PORTENTA_H7_M7 variant:
// - J1-33/J1-35 UART1 TX/RX are exposed through Serial1.
// - J2-76 D20 is core pin PC_3.
// - J2-78 D21 is core pin PA_4.
constexpr PinName kEsp32EnablePin = PC_3; // PC_3 = 0x23.
constexpr PinName kEsp32BootPin   = PA_4; // PA_4 = 0x04.

constexpr size_t kMaxImagesPerSession = 8;
constexpr size_t kMaxImageNameLength = 32;
constexpr size_t kMaxSessionIdLength = 24;
constexpr size_t kChunkBufferSize = 1024;
constexpr size_t kMaxTrackedChunksPerImage = 8192;
constexpr size_t kChunkBitmapBytes = kMaxTrackedChunksPerImage / 8;
constexpr uint32_t kDefaultFlashBaud = 460800;
constexpr uint32_t kInitialSyncBaud = 115200;
constexpr uint32_t kBootHoldMs = 250;
constexpr uint32_t kResetPulseMs = 250;
constexpr uint32_t kBootloaderSettleMs = 15000    ;

inline IPAddress defaultEthernetIp() {
  return IPAddress(192, 168, 1, 177);
}

inline IPAddress defaultEthernetDns() {
  return IPAddress(192, 168, 1, 1);
}

inline IPAddress defaultEthernetGateway() {
  return IPAddress(192, 168, 1, 1);
}

inline IPAddress defaultEthernetSubnet() {
  return IPAddress(255, 255, 255, 0);
}

inline arduino::UART &espSerial() {
  return Serial1;
}

}  // namespace AppConfig
