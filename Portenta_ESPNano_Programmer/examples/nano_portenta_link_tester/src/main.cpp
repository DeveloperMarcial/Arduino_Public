#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#include <driver/gpio.h>
#include <ctype.h>
#include <strings.h>

namespace {

constexpr uint32_t kUsbBaud = 115200;
constexpr uint32_t kPortentaBaud = 115200;
constexpr gpio_num_t kBootGpio = GPIO_NUM_1;  // shared with the Nano ESP32's green LED (B1 / BOOT strap)
constexpr uint32_t kHeartbeatIntervalMs = 1000;
constexpr uint32_t kUsbCommandIdleMs = 200;
constexpr size_t kLineBufferSize = 96;
constexpr size_t kLoopbackBufferSize = 64;
constexpr char kPrefsNamespace[] = "linktester";
constexpr char kPrefsBootCountKey[] = "boot_count";

HardwareSerial PortentaUart(1);
Preferences gPreferences;
char gLineBuffer[kLineBufferSize] = {};
size_t gLineLength = 0;
char gUsbLineBuffer[kLineBufferSize] = {};
size_t gUsbLineLength = 0;
uint32_t gLastUsbRxMs = 0;
uint32_t gLastHeartbeatMs = 0;
int gLastBootLevel = HIGH;
uint32_t gBootCount = 0;

char *trimAsciiWhitespace(char *text) {
  while ((*text != '\0') && isspace(static_cast<unsigned char>(*text))) {
    ++text;
  }

  char *end = text + strlen(text);
  while ((end > text) && isspace(static_cast<unsigned char>(*(end - 1)))) {
    --end;
  }

  *end = '\0';
  return text;
}

const char *resetReasonToString(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_UNKNOWN: return "unknown";
    case ESP_RST_POWERON: return "power_on";
    case ESP_RST_EXT: return "external_pin";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt_watchdog";
    case ESP_RST_TASK_WDT: return "task_watchdog";
    case ESP_RST_WDT: return "other_watchdog";
    case ESP_RST_DEEPSLEEP: return "deep_sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
#ifdef ESP_RST_USB
    case ESP_RST_USB: return "usb";
#endif
#ifdef ESP_RST_JTAG
    case ESP_RST_JTAG: return "jtag";
#endif
#ifdef ESP_RST_EFUSE
    case ESP_RST_EFUSE: return "efuse";
#endif
#ifdef ESP_RST_PWR_GLITCH
    case ESP_RST_PWR_GLITCH: return "power_glitch";
#endif
#ifdef ESP_RST_CPU_LOCKUP
    case ESP_RST_CPU_LOCKUP: return "cpu_lockup";
#endif
    default: return "unmapped";
  }
}

int readBootLevel() 
{
  return gpio_get_level(kBootGpio) ? HIGH : LOW;
}

void primeBootPinHigh() {
  digitalWrite(static_cast<uint8_t>(kBootGpio), HIGH);
  pinMode(static_cast<uint8_t>(kBootGpio), OUTPUT);
  delay(10);
  pinMode(static_cast<uint8_t>(kBootGpio), INPUT_PULLUP);
}

void setRgb(bool redOn, bool greenOn, bool blueOn) {
  // GPIO0 is shared with the Nano ESP32's green LED (B1 / BOOT strap), so
  // this tester must not drive LED_GREEN while it is trying to observe that
  // same signal. We only use red and blue here.
  analogWrite(LEDR, redOn ? 0 : 255);
  analogWrite(LEDB, blueOn ? 0 : 255);
  (void)greenOn;
}

void sendToAll(const char *message) {
  Serial.println(message);
  PortentaUart.println(message);
}

void sendStatus(const char *prefix) {
  char message[160] = {};
  snprintf(message,
           sizeof(message),
           "%s boot_count=%lu reset_reason=%s uptime_ms=%lu gpio0=%s",
           prefix,
           static_cast<unsigned long>(gBootCount),
           resetReasonToString(esp_reset_reason()),
           static_cast<unsigned long>(millis()),
           readBootLevel() == LOW ? "LOW" : "HIGH");
  sendToAll(message);
}

void loadAndIncrementBootCount() {
  if (!gPreferences.begin(kPrefsNamespace, false)) {
    gBootCount = 1;
    return;
  }

  gBootCount = gPreferences.getUInt(kPrefsBootCountKey, 0) + 1;
  gPreferences.putUInt(kPrefsBootCountKey, gBootCount);
}

void clearBootCount() {
  if (gPreferences.isKey(kPrefsBootCountKey)) {
    gPreferences.remove(kPrefsBootCountKey);
  }

  gBootCount = 0;
}

void flushPortentaUartInput() {
  while (PortentaUart.available() > 0) {
    PortentaUart.read();
  }
}

void runUartLoopbackTest() {
  static constexpr char kProbe[] = "NANO_LOOPBACK";
  char received[kLoopbackBufferSize] = {};
  size_t receivedLength = 0;

  flushPortentaUartInput();
  PortentaUart.println(kProbe);
  PortentaUart.flush();

  const uint32_t deadline = millis() + 150;
  while (millis() < deadline) {
    while (PortentaUart.available() > 0) {
      const int raw = PortentaUart.read();
      if (raw < 0) {
        break;
      }

      const char c = static_cast<char>(raw);
      if ((c == '\r') || (c == '\n')) {
        if (receivedLength == 0) {
          continue;
        }

        received[receivedLength] = '\0';
        if (strcmp(received, kProbe) == 0) {
          sendToAll("OK uart_loopback");
        } else {
          char message[96] = {};
          snprintf(message, sizeof(message), "ERR uart_loopback_mismatch=%s", received);
          sendToAll(message);
        }
        return;
      }

      if (receivedLength + 1 < sizeof(received)) {
        received[receivedLength++] = c;
      }
    }
    delay(1);
  }

  sendToAll("ERR uart_loopback_timeout");
}

void handleCommand(const char *line) {
  if (strcasecmp(line, "PING") == 0) {
    sendStatus("PONG");
    return;
  }

  if (strcasecmp(line, "STATUS") == 0) {
    sendStatus("STATUS");
    return;
  }

  if (strcasecmp(line, "CLEARCOUNT") == 0) {
    clearBootCount();
    sendToAll("OK boot_count_cleared");
    return;
  }

  if (strcasecmp(line, "UART LOOPBACK") == 0) {
    runUartLoopbackTest();
    return;
  }

  if (strcasecmp(line, "LED RED") == 0) {
    setRgb(true, false, false);
    sendToAll("OK LED RED");
    return;
  }

  if (strcasecmp(line, "LED GREEN") == 0) {
    sendToAll("ERR LED GREEN unavailable because it shares GPIO0/B1");
    return;
  }

  if (strcasecmp(line, "LED BLUE") == 0) {
    setRgb(false, false, true);
    sendToAll("OK LED BLUE");
    return;
  }

  if (strcasecmp(line, "LED OFF") == 0) {
    setRgb(false, false, false);
    sendToAll("OK LED OFF");
    return;
  }

  char message[144] = {};
  snprintf(message, sizeof(message), "ERR unknown_command=%s", line);
  sendToAll(message);
}

void pollPortentaUart() {
  while (PortentaUart.available() > 0) {
    const int raw = PortentaUart.read();
    if (raw < 0) {
      break;
    }

    const char c = static_cast<char>(raw);
    if ((c == '\r') || (c == '\n')) {
      if (gLineLength == 0) {
        continue;
      }

      gLineBuffer[gLineLength] = '\0';
      handleCommand(trimAsciiWhitespace(gLineBuffer));
      gLineLength = 0;
      continue;
    }

    if (gLineLength + 1 < sizeof(gLineBuffer)) {
      gLineBuffer[gLineLength++] = c;
    } else {
      gLineLength = 0;
      sendToAll("ERR line_too_long");
    }
  }
}

void pollUsbCommands() {
  while (Serial.available() > 0) {
    const int raw = Serial.read();
    if (raw < 0) {
      break;
    }

    const char c = static_cast<char>(raw);
    gLastUsbRxMs = millis();
    if ((c == '\r') || (c == '\n')) {
      if (gUsbLineLength == 0) {
        continue;
      }

      gUsbLineBuffer[gUsbLineLength] = '\0';
      handleCommand(trimAsciiWhitespace(gUsbLineBuffer));
      gUsbLineLength = 0;
      continue;
    }

    if (gUsbLineLength + 1 < sizeof(gUsbLineBuffer)) {
      gUsbLineBuffer[gUsbLineLength++] = c;
    } else {
      gUsbLineLength = 0;
      sendToAll("ERR usb_line_too_long");
    }
  }
}

void flushUsbCommandIfIdle() {
  if (gUsbLineLength == 0) {
    return;
  }

  if ((millis() - gLastUsbRxMs) < kUsbCommandIdleMs) {
    return;
  }

  gUsbLineBuffer[gUsbLineLength] = '\0';
  handleCommand(trimAsciiWhitespace(gUsbLineBuffer));
  gUsbLineLength = 0;
}

void reportBootPinChangeIfNeeded() {
  const int currentLevel = readBootLevel();
  if (currentLevel == gLastBootLevel) {
    return;
  }

  gLastBootLevel = currentLevel;

  char message[64] = {};
  snprintf(message, sizeof(message), "EVENT gpio0=%s", currentLevel == LOW ? "LOW" : "HIGH");
  sendToAll(message);
}

void sendHeartbeatIfNeeded() {
  const uint32_t now = millis();
  if ((now - gLastHeartbeatMs) < kHeartbeatIntervalMs) {
    return;
  }

  gLastHeartbeatMs = now;
  sendStatus("HB");
}

}  // namespace

void setup() {
  pinMode(LEDR, OUTPUT);
  pinMode(LEDB, OUTPUT);
  setRgb(false, false, true);

  primeBootPinHigh();

  Serial.begin(kUsbBaud);
  PortentaUart.begin(kPortentaBaud, SERIAL_8N1, RX, TX);
  loadAndIncrementBootCount();

  delay(250);
  gLastBootLevel = readBootLevel();

  sendToAll("Nano Portenta link tester ready");
  sendStatus("BOOT");
  sendToAll("Commands: PING, STATUS, CLEARCOUNT, UART LOOPBACK, LED RED, LED GREEN, LED BLUE, LED OFF");
}

void loop() {
  pollUsbCommands();
  flushUsbCommandIfIdle();
  pollPortentaUart();
  reportBootPinChangeIfNeeded();
  sendHeartbeatIfNeeded();
  delay(5);
}
