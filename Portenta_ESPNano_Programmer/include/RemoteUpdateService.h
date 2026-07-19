#pragma once

#include <ArduinoJson.h>

#include "AppConfig.h"

#if PORTENTA_NETWORK_USE_WIFI
#include <WiFi.h>
using PortentaNetworkClient = arduino::WiFiClient;
using PortentaNetworkServer = arduino::WiFiServer;
#else
#include <Ethernet.h>
using PortentaNetworkClient = arduino::EthernetClient;
using PortentaNetworkServer = arduino::EthernetServer;
#endif

#include "Esp32RomFlasher.h"
#include "FlashProtocol.h"
#include "StagingStore.h"

class RemoteUpdateService {
 public:
  bool begin();
  void loop();

  bool createSession(SessionStatus &outStatus);
  bool applyManifest(const char *sessionId, const JsonDocument &manifest, SessionStatus &outStatus);
  bool uploadChunk(const char *sessionId, const char *imageName, uint32_t chunkIndex, const uint8_t *data, size_t length, SessionStatus &outStatus);
  bool flashSession(const char *sessionId, SessionStatus &outStatus);
  SessionStatus status() const;

 private:
  bool sessionMatches(const char *sessionId) const;
  void setState(SessionState state, const char *detail, uint8_t progress);
  bool finalizeIfReady();
  void pollHttpServer();
  void setNetworkState(bool ready, const char *detail);
  bool sendChunkMap(PortentaNetworkClient &client, const char *sessionId, const char *imageName) const;

  SessionStatus status_ {};
  FlashManifest manifest_ {};
  StagingStore staging_ {};
  Esp32RomFlasher flasher_ {};
  PortentaNetworkServer server_ {AppConfig::kHttpPort};
  bool networkReady_ = false;
};
