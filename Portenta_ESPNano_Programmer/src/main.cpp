#include <Arduino.h>

#include "AppConfig.h"
#include "RemoteUpdateService.h"

RemoteUpdateService g_service;

void setup() {
  Serial.begin(AppConfig::kStatusBaud);
  while (!Serial && millis() < 3000) {
  }

  Serial.println();
  Serial.println("Portenta H7 remote ESP32 programmer starting");
#if PORTENTA_NETWORK_USE_WIFI
  Serial.println("HTTP transport binding enabled on Portenta WiFi");
#else
  Serial.println("HTTP transport binding enabled on Portenta Ethernet");
#endif

  if (!g_service.begin()) {
    Serial.println("Service initialization failed");
    const SessionStatus status = g_service.status();
    Serial.print("Failure detail: ");
    Serial.println(status.detail);
    for (;;) {
      delay(1000);
    }
  }
}

void loop() {
  g_service.loop();

  static uint32_t lastHeartbeat = 0;
  if (millis() - lastHeartbeat >= 2000) {
    lastHeartbeat = millis();
    const SessionStatus status = g_service.status();
    Serial.print("State=");
    Serial.print(static_cast<int>(status.state));
    Serial.print(" Progress=");
    Serial.print(status.progress);
    Serial.print(" Detail=");
    Serial.println(status.detail);
  }
}
