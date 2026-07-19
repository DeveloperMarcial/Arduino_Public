#include <Arduino.h>
#include <ctype.h>
#include <strings.h>

namespace {

constexpr PinName kEsp32EnablePin = PC_3;
constexpr PinName kEsp32BootPin = PA_4;
constexpr uint32_t kUsbBaud = 115200;
constexpr uint32_t kNanoBaud = 115200;
constexpr uint32_t kBootHoldMs = 250;
constexpr uint32_t kResetPulseMs = 250;
constexpr size_t kUsbLineSize = 64;
constexpr size_t kNanoLineSize = 192;

char gUsbLine[kUsbLineSize] = {};
size_t gUsbLineLength = 0;
char gNanoLine[kNanoLineSize] = {};
size_t gNanoLineLength = 0;
uint32_t gLastAutoStepMs = 0;
bool gAutoMode = false;
uint8_t gAutoStep = 0;

arduino::UART &NanoUart = Serial1;

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

void printHelp() {
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  help");
  Serial.println("  ping");
  Serial.println("  status");
  Serial.println("  clearcount");
  Serial.println("  gpio low");
  Serial.println("  gpio high");
  Serial.println("  gpio pulse");
  Serial.println("  gpio0 low");
  Serial.println("  gpio0 high");
  Serial.println("  gpio0 pulse");
  Serial.println("  reset");
  Serial.println("  bootloader");
  Serial.println("  uart loopback");
  Serial.println("  led red");
  Serial.println("  led green");
  Serial.println("  led blue");
  Serial.println("  led off");
  Serial.println("  auto on");
  Serial.println("  auto off");
}

void sendNanoLine(const char *line) {
  NanoUart.println(line);
  Serial.print("TX->Nano: ");
  Serial.println(line);
}

void runUartLoopbackTest() {
  const char *probe = "PORTENTA_LOOPBACK";

  while (NanoUart.available() > 0) {
    NanoUart.read();
  }

  NanoUart.println(probe);
  Serial.print("TX->Nano: ");
  Serial.println(probe);
  delay(100);

  if (NanoUart.available() == 0) {
    Serial.println("RX<-Nano: <none>");
    Serial.println("Loopback note: if you temporarily jumper Portenta D14 to D13, you should see PORTENTA_LOOPBACK echoed here.");
    return;
  }

  Serial.print("RX<-Nano: ");
  while (NanoUart.available() > 0) {
    Serial.write(NanoUart.read());
  }
  Serial.println();
}

void setBootPin(bool high) {
  digitalWrite(kEsp32BootPin, high ? HIGH : LOW);
  Serial.print("GPIO0 -> ");
  Serial.println(high ? "HIGH" : "LOW");
}

void pulseReset() {
  Serial.println("Pulsing EN low/high");
  digitalWrite(kEsp32EnablePin, LOW);
  delay(kResetPulseMs);
  digitalWrite(kEsp32EnablePin, HIGH);
}

void enterBootloaderStyleReset() {
  Serial.println("Bootloader-style sequence: GPIO0 low, pulse EN, release GPIO0");
  digitalWrite(kEsp32BootPin, LOW);
  delay(kBootHoldMs);
  pulseReset();
  delay(kBootHoldMs);
  digitalWrite(kEsp32BootPin, HIGH);
}

void handleUsbCommand(const char *line) {
  if (strcasecmp(line, "help") == 0) {
    printHelp();
    return;
  }

  if (strcasecmp(line, "ping") == 0) {
    sendNanoLine("PING");
    return;
  }

  if (strcasecmp(line, "status") == 0) {
    sendNanoLine("STATUS");
    return;
  }

  if (strcasecmp(line, "clearcount") == 0) {
    sendNanoLine("CLEARCOUNT");
    return;
  }

  if ((strcasecmp(line, "gpio0 low") == 0) || (strcasecmp(line, "gpio low") == 0)) {
    setBootPin(false);
    return;
  }

  if ((strcasecmp(line, "gpio0 high") == 0) || (strcasecmp(line, "gpio high") == 0)) {
    setBootPin(true);
    return;
  }

  if ((strcasecmp(line, "gpio0 pulse") == 0) || (strcasecmp(line, "gpio pulse") == 0)) {
    Serial.println("Pulsing GPIO0 low/high");
    digitalWrite(kEsp32BootPin, LOW);
    delay(500);
    digitalWrite(kEsp32BootPin, HIGH);
    return;
  }

  if (strcasecmp(line, "reset") == 0) {
    pulseReset();
    return;
  }

  if (strcasecmp(line, "bootloader") == 0) {
    enterBootloaderStyleReset();
    return;
  }

  if (strcasecmp(line, "uart loopback") == 0) {
    runUartLoopbackTest();
    return;
  }

  if (strcasecmp(line, "led red") == 0) {
    sendNanoLine("LED RED");
    return;
  }

  if (strcasecmp(line, "led green") == 0) {
    sendNanoLine("LED GREEN");
    return;
  }

  if (strcasecmp(line, "led blue") == 0) {
    sendNanoLine("LED BLUE");
    return;
  }

  if (strcasecmp(line, "led off") == 0) {
    sendNanoLine("LED OFF");
    return;
  }

  if (strcasecmp(line, "auto on") == 0) {
    gAutoMode = true;
    gAutoStep = 0;
    gLastAutoStepMs = 0;
    Serial.println("Auto mode enabled");
    return;
  }

  if (strcasecmp(line, "auto off") == 0) {
    gAutoMode = false;
    Serial.println("Auto mode disabled");
    return;
  }

  Serial.print("Unknown command: ");
  Serial.println(line);
}

void pollUsbCommands() {
  while (Serial.available() > 0) {
    const int raw = Serial.read();
    if (raw < 0) {
      break;
    }

    const char c = static_cast<char>(raw);
    if ((c == '\r') || (c == '\n')) {
      if (gUsbLineLength == 0) {
        continue;
      }

      gUsbLine[gUsbLineLength] = '\0';
      handleUsbCommand(trimAsciiWhitespace(gUsbLine));
      gUsbLineLength = 0;
      continue;
    }

    if (gUsbLineLength + 1 < sizeof(gUsbLine)) {
      gUsbLine[gUsbLineLength++] = c;
    } else {
      gUsbLineLength = 0;
      Serial.println("USB command too long");
    }
  }
}

void bridgeNanoToUsb() {
  while (NanoUart.available() > 0) {
    const int raw = NanoUart.read();
    if (raw < 0) {
      break;
    }

    const char c = static_cast<char>(raw);
    if ((c == '\r') || (c == '\n')) {
      if (gNanoLineLength == 0) {
        continue;
      }

      gNanoLine[gNanoLineLength] = '\0';
      Serial.print("RX<-Nano: ");
      Serial.println(gNanoLine);
      gNanoLineLength = 0;
      continue;
    }

    if (gNanoLineLength + 1 < sizeof(gNanoLine)) {
      gNanoLine[gNanoLineLength++] = c;
    } else {
      gNanoLine[gNanoLineLength] = '\0';
      Serial.print("RX<-Nano: ");
      Serial.println(gNanoLine);
      gNanoLineLength = 0;
    }
  }
}

void handleAutoMode() {
  if (!gAutoMode) {
    return;
  }

  const uint32_t now = millis();
  if ((now - gLastAutoStepMs) < 2000) {
    return;
  }

  gLastAutoStepMs = now;
  switch (gAutoStep % 4) {
    case 0:
      sendNanoLine("PING");
      break;
    case 1:
      digitalWrite(kEsp32BootPin, LOW);
      Serial.println("Auto: GPIO0 LOW");
      break;
    case 2:
      digitalWrite(kEsp32BootPin, HIGH);
      Serial.println("Auto: GPIO0 HIGH");
      break;
    case 3:
      pulseReset();
      break;
  }
  ++gAutoStep;
}

}  // namespace

void setup() {
  pinMode(kEsp32EnablePin, OUTPUT);
  pinMode(kEsp32BootPin, OUTPUT);
  digitalWrite(kEsp32EnablePin, HIGH);
  digitalWrite(kEsp32BootPin, HIGH);

  Serial.begin(kUsbBaud);
  NanoUart.begin(kNanoBaud);

  delay(500);
  Serial.println("Portenta Nano link tester ready");
  Serial.println("Using Serial1 + EN(PC_3/D20) + GPIO0(PA_4/D21)");
  printHelp();
}

void loop() {
  pollUsbCommands();
  bridgeNanoToUsb();
  handleAutoMode();
  delay(5);
}
