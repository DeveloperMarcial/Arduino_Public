#include "Esp32RomFlasher.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "AppConfig.h"

namespace {

constexpr uint8_t kSlipEnd = 0xC0;
constexpr uint8_t kSlipEsc = 0xDB;
constexpr uint8_t kSlipEscEnd = 0xDC;
constexpr uint8_t kSlipEscEsc = 0xDD;

constexpr uint8_t kPacketRequest = 0x00;
constexpr uint8_t kPacketResponse = 0x01;

constexpr uint8_t kCommandFlashBegin = 0x02;
constexpr uint8_t kCommandFlashData = 0x03;
constexpr uint8_t kCommandFlashEnd = 0x04;
constexpr uint8_t kCommandMemBegin = 0x05;
constexpr uint8_t kCommandMemEnd = 0x06;
constexpr uint8_t kCommandMemData = 0x07;
constexpr uint8_t kCommandSync = 0x08;
constexpr uint8_t kCommandSpiSetParams = 0x0B;
constexpr uint8_t kCommandSpiAttach = 0x0D;
constexpr uint8_t kCommandChangeBaud = 0x0F;
constexpr uint8_t kCommandSpiFlashMd5 = 0x13;
constexpr uint8_t kCommandEraseFlash = 0xD0;

constexpr uint32_t kFlashBlockSize = 1024;
constexpr uint32_t kStubBlockSize = 1024;
constexpr uint32_t kFlashSectorSize = 4096;
constexpr uint32_t kFlashPageSize = 256;
constexpr uint32_t kDefaultFlashSize = 4U * 1024U * 1024U;
constexpr uint32_t kDefaultFlashBlockEraseSize = 64U * 1024U;
constexpr uint32_t kDefaultStatusMask = 0xFFFFU;
constexpr uint32_t kChecksumMagic = 0xEFU;
constexpr uint32_t kResponseTimeoutMs = 3000;
constexpr uint32_t kSyncTimeoutMs = 2000;
constexpr uint32_t kFlashEndTimeoutMs = 250;
constexpr uint8_t kSyncAttemptCount = 20;
constexpr uint8_t kSyncAttemptsPerBootEntry = 5;
constexpr uint32_t kStubReadyTimeoutMs = 3000;
constexpr uint32_t kEraseTimeoutMs = 120000;
constexpr uint32_t kMd5TimeoutPerMbMs = 8000;
constexpr uint32_t kMinimumMd5TimeoutMs = 3000;
constexpr uint32_t kEraseRegionTimeoutPerMbMs = 30000;
constexpr uint32_t kMinimumFlashBeginTimeoutMs = 3000;
constexpr uint32_t kTemporaryRawBootReadMs = 2000;
constexpr size_t kCommandHeaderSize = 8;
constexpr size_t kFlashDataHeaderSize = 16;
constexpr char kStubReadyMessage[] = "OHAI";
constexpr size_t kMd5HexLength = 32;
constexpr uint8_t kCommandResponseRetryCount = 8;

void bytesToHex(const uint8_t *input, size_t length, char *output, size_t outputCapacity) {
  static constexpr char kHexChars[] = "0123456789abcdef";
  if (outputCapacity < (length * 2U + 1U)) {
    if (outputCapacity > 0U) {
      output[0] = '\0';
    }
    return;
  }

  for (size_t i = 0; i < length; ++i) {
    output[i * 2U] = kHexChars[(input[i] >> 4) & 0x0FU];
    output[i * 2U + 1U] = kHexChars[input[i] & 0x0FU];
  }
  output[length * 2U] = '\0';
}

void writeLe16(uint8_t *destination, uint16_t value) {
  destination[0] = static_cast<uint8_t>(value & 0xFFU);
  destination[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
}

void writeLe32(uint8_t *destination, uint32_t value) {
  destination[0] = static_cast<uint8_t>(value & 0xFFU);
  destination[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
  destination[2] = static_cast<uint8_t>((value >> 16) & 0xFFU);
  destination[3] = static_cast<uint8_t>((value >> 24) & 0xFFU);
}

uint16_t readLe16(const uint8_t *source) {
  return static_cast<uint16_t>(source[0]) |
         (static_cast<uint16_t>(source[1]) << 8);
}

uint32_t readLe32(const uint8_t *source) {
  return static_cast<uint32_t>(source[0]) |
         (static_cast<uint32_t>(source[1]) << 8) |
         (static_cast<uint32_t>(source[2]) << 16) |
         (static_cast<uint32_t>(source[3]) << 24);
}

}  // namespace

bool Esp32RomFlasher::begin() {
  pinMode(AppConfig::kEsp32EnablePin, OUTPUT);
  pinMode(AppConfig::kEsp32BootPin, OUTPUT);

  digitalWrite(AppConfig::kEsp32EnablePin, HIGH);
  digitalWrite(AppConfig::kEsp32BootPin, HIGH);

  AppConfig::espSerial().begin(AppConfig::kInitialSyncBaud);
  return true;
}

bool Esp32RomFlasher::flash(FlashManifest &manifest, const StagingStore &store, SessionStatus &status)
{
  enterBootloader();

  Serial.println("Temporary raw ESP UART bytes after bootloader reset:");
  const uint32_t rawReadDeadline = millis() + kTemporaryRawBootReadMs;
  size_t rawByteCount = 0;
  while (millis() < rawReadDeadline) {
    while (AppConfig::espSerial().available() > 0) {
      const int value = AppConfig::espSerial().read();
      if (value < 0) {
        break;
      }

      if (rawByteCount > 0U) {
        Serial.print(' ');
      }
      Serial.print("0x");
      if (value < 0x10) {
        Serial.print('0');
      }
      Serial.print(value, HEX);
      ++rawByteCount;
    }
    delay(1);
  }
  if (rawByteCount == 0U) {
    Serial.print("<none>");
  }
  Serial.println();

  if (!syncBootloader(status)) {
    rebootNormal();
    return false;
  }

  if (!ensureEraseSupport(manifest, store, status)) {
    rebootNormal();
    return false;
  }

  if (!configureFlash(manifest, status)) {
    rebootNormal();
    return false;
  }

  if ((manifest.baud != 0U) && (manifest.baud != AppConfig::kInitialSyncBaud)) {
    if (!changeBaudRate(manifest.baud, status)) {
      rebootNormal();
      return false;
    }
  }

  size_t flashImageCount = 0;
  for (size_t i = 0; i < manifest.imageCount; ++i) {
    if (manifest.images[i].flashImage) {
      ++flashImageCount;
    }
  }

  size_t flashedImages = 0;
  for (size_t i = 0; i < manifest.imageCount; ++i) {
    if (!manifest.images[i].flashImage) {
      continue;
    }

    ImageSlot &image = manifest.images[i];
    if (!writeImage(image, store, status, flashedImages, flashImageCount)) {
      rebootNormal();
      return false;
    }

    if (!verifyFlashImage(image, store, status, flashedImages, flashImageCount)) {
      rebootNormal();
      return false;
    }
    ++flashedImages;
  }

  uint8_t endPayload[4] = {};
  writeLe32(endPayload, 1U);
  CommandResponse response;
  if (!sendCommand(kCommandFlashEnd, endPayload, sizeof(endPayload), 0U, response, kFlashEndTimeoutMs)) {
    Serial.println("ESP ROM FLASH_END produced no usable reply; continuing with hardware reboot");
    logCommandResponse("ESP ROM FLASH_END response:", response);
  }

  setStatus(status, "Flash complete, rebooting target", 100);
  rebootNormal();
  return true;
}

void Esp32RomFlasher::enterBootloader() {
  AppConfig::espSerial().flush();
  flushInput();

  Serial.println("Entering ESP ROM bootloader: BOOT low, pulsing EN");
  digitalWrite(AppConfig::kEsp32BootPin, LOW);
  delay(AppConfig::kBootHoldMs);

  digitalWrite(AppConfig::kEsp32EnablePin, LOW);
  delay(AppConfig::kResetPulseMs);
  digitalWrite(AppConfig::kEsp32EnablePin, HIGH);
}

void Esp32RomFlasher::rebootNormal() {
  Serial.println("Rebooting ESP target normally");
  digitalWrite(AppConfig::kEsp32BootPin, HIGH);
  delay(AppConfig::kBootHoldMs);
  digitalWrite(AppConfig::kEsp32EnablePin, LOW);
  delay(AppConfig::kResetPulseMs);
  digitalWrite(AppConfig::kEsp32EnablePin, HIGH);
}

void Esp32RomFlasher::flushInput() {
  while (AppConfig::espSerial().available() > 0) {
    AppConfig::espSerial().read();
  }
}

bool Esp32RomFlasher::sendSyncCommand(uint32_t argTimeoutMs) {
  // ESP ROM command 0x08 is SYNC. The ROM loader expects a 36-byte payload:
  // four fixed marker bytes followed by 32 bytes of 0x55. 
  // This is the same sync pattern used by esptool.py.
  //
  // The leading bytes are a recognizable command preamble. The trailing 0x55
  // bytes produce a clean alternating bit pattern on the UART line
  // (01010101), which gives the ROM loader plenty of edges while it is trying
  // to lock onto the host and respond with a valid SLIP-framed packet.
  uint8_t syncPayload[36] = {0x07, 0x07, 0x12, 0x20};
  memset(syncPayload + 4, 0x55, sizeof(syncPayload) - 4);

  // SYNC has no checksum payload in the ESP ROM protocol, so the checksum
  // field is zero. Any valid response to this command means the target is in
  // the UART ROM bootloader and the Portenta UART path is working.
  CommandResponse response;
  //            0x08 SYNC command,     payload,                      checksum, response buffer, timeout
  const bool ok = sendCommand(kCommandSync, syncPayload, sizeof(syncPayload),       0U,        response, argTimeoutMs);

  Serial.print("ESP ROM SYNC response: ok=");
  Serial.print(ok ? "true" : "false");
  Serial.print(" direction=0x");
  Serial.print(response.direction, HEX);
  Serial.print(" command=0x");
  Serial.print(response.command, HEX);
  Serial.print(" size=");
  Serial.print(response.size);
  Serial.print(" value=0x");
  Serial.print(response.value, HEX);
  Serial.print(" status_length=");
  Serial.print(response.statusLength);
  if (response.statusLength >= 2U) {
    Serial.print(" status=[0x");
    Serial.print(response.status[0], HEX);
    Serial.print(",0x");
    Serial.print(response.status[1], HEX);
    Serial.print("]");
  }
  Serial.print(" data_length=");
  Serial.println(response.dataLength);

  return ok;
}

void Esp32RomFlasher::logCommandResponse(const char *prefix, const CommandResponse &response) {
  Serial.print(prefix);
  Serial.print(" ok=");
  const bool statusOk =
      (response.statusLength == 0U) ||
      ((response.status[0] == 0U) && (response.status[1] == 0U));
  Serial.print(statusOk ? "true" : "false");
  Serial.print(" direction=0x");
  Serial.print(response.direction, HEX);
  Serial.print(" command=0x");
  Serial.print(response.command, HEX);
  Serial.print(" size=");
  Serial.print(response.size);
  Serial.print(" value=0x");
  Serial.print(response.value, HEX);
  Serial.print(" status_length=");
  Serial.print(response.statusLength);
  if (response.statusLength >= 2U) {
    Serial.print(" status=[0x");
    Serial.print(response.status[0], HEX);
    Serial.print(",0x");
    Serial.print(response.status[1], HEX);
    Serial.print("]");
  }
  Serial.print(" data_length=");
  Serial.println(response.dataLength);
}

bool Esp32RomFlasher::syncBootloader(SessionStatus &status) {
  setStatus(status, "Synchronizing with ESP ROM bootloader", 5);
  const uint32_t settleAfterBootEntryMs = AppConfig::kBootloaderSettleMs;

  // Start each ROM sync phase from the known ESP ROM UART baud rate.
  AppConfig::espSerial().end();
  AppConfig::espSerial().begin(AppConfig::kInitialSyncBaud);
  delay(settleAfterBootEntryMs);
  flushInput();

  for (uint8_t attempt = 0; attempt < kSyncAttemptCount; ++attempt) {
    // Re-strap and reset the target periodically in case the previous attempt caught normal app boot.
    if ((attempt > 0U) && ((attempt % kSyncAttemptsPerBootEntry) == 0U)) {
      enterBootloader();
      delay(settleAfterBootEntryMs);
      flushInput();
    }

    // The ESP ROM sync command is safe to repeat; any valid response means the ROM loader is alive.
    char detail[96] = {};
    snprintf(detail, sizeof(detail), "Synchronizing with ESP ROM bootloader, attempt %u/%u",
             static_cast<unsigned>(attempt + 1U),
             static_cast<unsigned>(kSyncAttemptCount));
    setStatus(status, detail, 5);
    Serial.println(detail);

    if (sendSyncCommand(kSyncTimeoutMs)) {
      // The ESP ROM typically emits several additional SYNC replies after the
      // first successful handshake. Drain them now so the next command doesn't
      // accidentally read a leftover 0x08 response and report a false failure.
      CommandResponse extraSyncResponse;
      while (readCommandFrame(kCommandSync, extraSyncResponse, 50U, true)) {
        logCommandResponse("Discarding extra ESP ROM SYNC response:", extraSyncResponse);
      }

      setStatus(status, "ESP ROM bootloader synchronized", 10);
      Serial.println("ESP ROM bootloader synchronized");
      return true;
    }
    delay(200);
    flushInput();
  }

  // No ROM response means the target is not in UART ROM bootloader mode or the UART path is wrong.
  setStatus(status, "ESP ROM sync failed; check GND, UART TX/RX crossover, EN, GPIO0/BOOT wiring", 0);
  return false;
}

bool Esp32RomFlasher::configureFlash(const FlashManifest &manifest, SessionStatus &status) {
  setStatus(status, "Configuring ESP flash interface", 12);

  uint8_t attachPayload[8] = {};
  CommandResponse response;
  if (!sendCommand(kCommandSpiAttach, attachPayload, sizeof(attachPayload), 0U, response, kResponseTimeoutMs)) {
    logCommandResponse("ESP ROM SPI_ATTACH response:", response);
    setStatus(status, "SPI attach failed", 0);
    return false;
  }

  uint8_t paramsPayload[24] = {};
  const uint32_t flashSize = manifest.flashSize == 0U ? kDefaultFlashSize : manifest.flashSize;
  writeLe32(&paramsPayload[0], 0U);
  writeLe32(&paramsPayload[4], flashSize);
  writeLe32(&paramsPayload[8], kDefaultFlashBlockEraseSize);
  writeLe32(&paramsPayload[12], kFlashSectorSize);
  writeLe32(&paramsPayload[16], kFlashPageSize);
  writeLe32(&paramsPayload[20], kDefaultStatusMask);
  if (!sendCommand(kCommandSpiSetParams, paramsPayload, sizeof(paramsPayload), 0U, response, kResponseTimeoutMs)) {
    logCommandResponse("ESP ROM SPI_SET_PARAMS response:", response);
    setStatus(status, "SPI flash parameter setup failed", 0);
    return false;
  }

  return true;
}

bool Esp32RomFlasher::changeBaudRate(uint32_t baudRate, SessionStatus &status) {
  setStatus(status, "Switching ESP bootloader baud rate", 15);

  uint8_t payload[8] = {};
  writeLe32(&payload[0], baudRate);
  writeLe32(&payload[4], AppConfig::kInitialSyncBaud);

  CommandResponse response;
  if (!sendCommand(kCommandChangeBaud, payload, sizeof(payload), 0U, response, kResponseTimeoutMs)) {
    setStatus(status, "ESP bootloader baud-rate change failed", 0);
    return false;
  }

  AppConfig::espSerial().flush();
  AppConfig::espSerial().end();
  delay(50);
  AppConfig::espSerial().begin(baudRate);
  flushInput();

  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    uint8_t attachPayload[8] = {};
    CommandResponse probeResponse;
    if (sendCommand(kCommandSpiAttach, attachPayload, sizeof(attachPayload), 0U, probeResponse, kResponseTimeoutMs)) {
      return true;
    }
    delay(25);
    flushInput();
  }

  setStatus(status, "ESP stopped responding after baud-rate change", 0);
  return false;
}

bool Esp32RomFlasher::ensureEraseSupport(const FlashManifest &manifest, const StagingStore &store, SessionStatus &status) {
  if (!manifest.eraseFirst) {
    return true;
  }

  if (!manifest.stub.enabled) {
    setStatus(status, "Full chip erase requested, but no stub loader metadata was provided", 0);
    return false;
  }

  if (!startStub(manifest, store, status)) {
    return false;
  }

  return eraseFlashWithStub(status);
}

bool Esp32RomFlasher::startStub(const FlashManifest &manifest, const StagingStore &store, SessionStatus &status) {
  if ((manifest.stub.textImageName[0] == '\0') || (manifest.stub.dataImageName[0] == '\0') || (manifest.stub.entry == 0U)) {
    setStatus(status, "Stub metadata is incomplete", 0);
    return false;
  }

  if (!uploadStubSegment("stub text",
                         manifest.stub.textImageName,
                         manifest.stub.textStart,
                         manifest.stub.textSize,
                         store,
                         status)) {
    return false;
  }

  if (!uploadStubSegment("stub data",
                         manifest.stub.dataImageName,
                         manifest.stub.dataStart,
                         manifest.stub.dataSize,
                         store,
                         status)) {
    return false;
  }

  uint8_t endPayload[8] = {};
  writeLe32(&endPayload[0], 0U);
  writeLe32(&endPayload[4], manifest.stub.entry);

  uint8_t header[kCommandHeaderSize] = {};
  header[0] = kPacketRequest;
  header[1] = kCommandMemEnd;
  writeLe16(&header[2], sizeof(endPayload));
  writeLe32(&header[4], 0U);

  AppConfig::espSerial().write(kSlipEnd);
  for (size_t i = 0; i < sizeof(header); ++i) {
    writeSlipEscaped(header[i]);
  }
  for (size_t i = 0; i < sizeof(endPayload); ++i) {
    writeSlipEscaped(endPayload[i]);
  }
  AppConfig::espSerial().write(kSlipEnd);
  AppConfig::espSerial().flush();

  return waitForStubReady(status);
}

bool Esp32RomFlasher::uploadStubSegment(const char *label,
                                        const char *imageName,
                                        uint32_t loadAddress,
                                        uint32_t segmentSize,
                                        const StagingStore &store,
                                        SessionStatus &status) {
  const uint32_t storedSize = static_cast<uint32_t>(store.imageSize(imageName));
  if ((segmentSize == 0U) || (storedSize != segmentSize)) {
    setStatus(status, "Stub image size mismatch", 0);
    return false;
  }

  const uint32_t blockCount = alignedSize(segmentSize, kStubBlockSize) / kStubBlockSize;
  uint8_t beginPayload[16] = {};
  writeLe32(&beginPayload[0], segmentSize);
  writeLe32(&beginPayload[4], blockCount);
  writeLe32(&beginPayload[8], kStubBlockSize);
  writeLe32(&beginPayload[12], loadAddress);

  CommandResponse response;
  char detail[96] = {};
  snprintf(detail, sizeof(detail), "Loading %s into ESP RAM", label);
  setStatus(status, detail, 12);
  if (!sendCommand(kCommandMemBegin, beginPayload, sizeof(beginPayload), 0U, response, kResponseTimeoutMs)) {
    setStatus(status, "ESP MEM_BEGIN failed", 0);
    return false;
  }

  uint8_t block[kFlashDataHeaderSize + kStubBlockSize] = {};
  for (uint32_t blockIndex = 0; blockIndex < blockCount; ++blockIndex) {
    memset(block, 0, sizeof(block));

    const size_t sourceOffset = static_cast<size_t>(blockIndex) * kStubBlockSize;
    if (sourceOffset < segmentSize) {
      size_t bytesRead = 0;
      const size_t requestLength = std::min<size_t>(kStubBlockSize, static_cast<size_t>(segmentSize) - sourceOffset);
      if (!store.readImageChunk(imageName, sourceOffset, &block[kFlashDataHeaderSize], requestLength, bytesRead) ||
          (bytesRead != requestLength)) {
        setStatus(status, "Failed to read staged stub image", 0);
        return false;
      }
    }

    writeLe32(&block[0], kStubBlockSize);
    writeLe32(&block[4], blockIndex);
    writeLe32(&block[8], 0U);
    writeLe32(&block[12], 0U);

    if (!sendCommand(kCommandMemData,
                     block,
                     sizeof(block),
                     checksum(&block[kFlashDataHeaderSize], kStubBlockSize),
                     response,
                     kResponseTimeoutMs)) {
      setStatus(status, "ESP MEM_DATA failed", 0);
      return false;
    }
  }

  return true;
}

bool Esp32RomFlasher::waitForStubReady(SessionStatus &status) {
  setStatus(status, "Starting ESP flasher stub", 18);

  uint8_t frame[64] = {};
  size_t frameLength = 0;
  const uint32_t deadline = millis() + kStubReadyTimeoutMs;

  while (millis() < deadline) {
    const uint32_t remaining = deadline - millis();
    if (!readFrame(frame, sizeof(frame), frameLength, remaining)) {
      continue;
    }

    if ((frameLength == strlen(kStubReadyMessage)) &&
        (memcmp(frame, kStubReadyMessage, strlen(kStubReadyMessage)) == 0)) {
      return true;
    }
  }

  setStatus(status, "ESP flasher stub did not signal readiness", 0);
  return false;
}

bool Esp32RomFlasher::eraseFlashWithStub(SessionStatus &status) {
  setStatus(status, "Erasing ESP flash with stub loader", 20);
  CommandResponse response;
  if (!sendCommand(kCommandEraseFlash, nullptr, 0U, 0U, response, kEraseTimeoutMs)) {
    setStatus(status, "Stub erase_flash command failed", 0);
    return false;
  }

  return true;
}

bool Esp32RomFlasher::writeImage(const ImageSlot &image, const StagingStore &store, SessionStatus &status, size_t imageIndex, size_t imageCount) {
  const uint32_t writeSize = static_cast<uint32_t>(store.imageSize(image.name));
  if (writeSize == 0U) {
    setStatus(status, "Image staging buffer is empty", 0);
    return false;
  }

  const uint32_t paddedSize = alignedSize(writeSize, kFlashBlockSize);
  const uint32_t blockCount = paddedSize / kFlashBlockSize;
  const uint32_t eraseSize = alignedSize(writeSize, kFlashSectorSize);

  uint8_t beginPayload[20] = {};
  writeLe32(&beginPayload[0], eraseSize);
  writeLe32(&beginPayload[4], blockCount);
  writeLe32(&beginPayload[8], kFlashBlockSize);
  writeLe32(&beginPayload[12], image.offset);
  writeLe32(&beginPayload[16], 0U);  // encrypted_write = false for ESP32-S3 ROM format

  CommandResponse response;
  char detail[96] = {};
  snprintf(detail, sizeof(detail), "Preparing %s at 0x%08lx",
           image.name, static_cast<unsigned long>(image.offset));
  setStatus(status, detail, static_cast<uint8_t>(10U + (imageIndex * 80U) / (imageCount == 0 ? 1U : imageCount)));

  const uint32_t flashBeginTimeout =
      std::max<uint32_t>(kMinimumFlashBeginTimeoutMs,
                         static_cast<uint32_t>(
                             (static_cast<uint64_t>(writeSize) * kEraseRegionTimeoutPerMbMs) / 1000000ULL));
  if (!sendCommand(kCommandFlashBegin, beginPayload, sizeof(beginPayload), 0U, response, flashBeginTimeout)) {
    logCommandResponse("ESP ROM FLASH_BEGIN response:", response);
    setStatus(status, "ESP flash begin failed", 0);
    return false;
  }

  return writeImageBlocks(image, store, status, imageIndex, imageCount);
}

bool Esp32RomFlasher::verifyFlashImage(ImageSlot &image,
                                       const StagingStore &store,
                                       SessionStatus &status,
                                       size_t imageIndex,
                                       size_t imageCount) {
  image.stagedMd5[0] = '\0';
  image.flashMd5[0] = '\0';
  image.flashVerified = false;

  if (!store.calculateImageMd5(image.name, image.stagedMd5, sizeof(image.stagedMd5))) {
    setStatus(status, "Failed to calculate staged image MD5", 0);
    return false;
  }

  const float completedImages = static_cast<float>(imageIndex + 1U);
  const float totalImages = static_cast<float>(imageCount == 0U ? 1U : imageCount);
  char detail[96] = {};
  snprintf(detail, sizeof(detail), "Verifying %s against ESP flash", image.name);
  setStatus(status, detail, static_cast<uint8_t>(20U + (completedImages / totalImages) * 75.0F));

  if (!calculateFlashMd5(image.offset, image.size, image.flashMd5, sizeof(image.flashMd5))) {
    setStatus(status, "ESP flash readback digest failed", 0);
    return false;
  }

  image.flashVerified = strcmp(image.stagedMd5, image.flashMd5) == 0;
  if (!image.flashVerified) {
    setStatus(status, "ESP flash verification mismatch", 0);
    return false;
  }

  snprintf(detail, sizeof(detail), "Verified %s in ESP flash", image.name);
  setStatus(status, detail, static_cast<uint8_t>(25U + (completedImages / totalImages) * 75.0F));
  return true;
}

bool Esp32RomFlasher::writeImageBlocks(const ImageSlot &image, const StagingStore &store, SessionStatus &status, size_t imageIndex, size_t imageCount) {
  uint8_t block[kFlashDataHeaderSize + kFlashBlockSize] = {};
  const uint32_t writeSize = static_cast<uint32_t>(store.imageSize(image.name));
  const uint32_t paddedSize = alignedSize(writeSize, kFlashBlockSize);
  const uint32_t blockCount = paddedSize / kFlashBlockSize;

  for (uint32_t blockIndex = 0; blockIndex < blockCount; ++blockIndex) {
    memset(block, 0xFF, sizeof(block));

    const size_t sourceOffset = static_cast<size_t>(blockIndex) * kFlashBlockSize;
    size_t bytesRead = 0;
    if (sourceOffset < writeSize) {
      const size_t requestLength = std::min<size_t>(kFlashBlockSize, static_cast<size_t>(writeSize) - sourceOffset);
      if (!store.readImageChunk(image.name, sourceOffset, &block[kFlashDataHeaderSize], requestLength, bytesRead) ||
          (bytesRead != requestLength)) {
        setStatus(status, "Failed to read staged image chunk", 0);
        return false;
      }
    }

    writeLe32(&block[0], kFlashBlockSize);
    writeLe32(&block[4], blockIndex);
    writeLe32(&block[8], 0U);
    writeLe32(&block[12], 0U);

    CommandResponse response;
    if (!sendCommand(kCommandFlashData,
                     block,
                     sizeof(block),
                     checksum(&block[kFlashDataHeaderSize], kFlashBlockSize),
                     response,
                     kResponseTimeoutMs)) {
      setStatus(status, "ESP flash data block failed", 0);
      return false;
    }

    const uint32_t effectiveWritten = std::min<uint32_t>((blockIndex + 1U) * kFlashBlockSize, writeSize);
    const float imageFraction = static_cast<float>(effectiveWritten) / static_cast<float>(writeSize == 0U ? 1U : writeSize);
    const float overall = (static_cast<float>(imageIndex) + imageFraction) /
                          static_cast<float>(imageCount == 0U ? 1U : imageCount);

    char detail[96] = {};
    snprintf(detail, sizeof(detail), "Writing %s block %lu/%lu",
             image.name,
             static_cast<unsigned long>(blockIndex + 1U),
             static_cast<unsigned long>(blockCount));
    setStatus(status, detail, static_cast<uint8_t>(15U + overall * 80.0F));
  }

  return true;
}

bool Esp32RomFlasher::sendCommand(uint8_t command,
                                  const uint8_t *payload,
                                  size_t payloadLength,
                                  uint32_t packetChecksum,
                                  CommandResponse &response,
                                  uint32_t timeoutMs) {
  uint8_t header[kCommandHeaderSize] = {};
  header[0] = kPacketRequest;
  header[1] = command;
  writeLe16(&header[2], static_cast<uint16_t>(payloadLength));
  writeLe32(&header[4], packetChecksum);

  AppConfig::espSerial().write(kSlipEnd);
  for (size_t i = 0; i < sizeof(header); ++i) {
    writeSlipEscaped(header[i]);
  }
  for (size_t i = 0; i < payloadLength; ++i) {
    writeSlipEscaped(payload[i]);
  }
  AppConfig::espSerial().write(kSlipEnd);
  AppConfig::espSerial().flush();

  return readResponse(command, response, timeoutMs);
}

bool Esp32RomFlasher::readResponse(uint8_t expectedCommand, CommandResponse &response, uint32_t timeoutMs) {
  return readCommandFrame(expectedCommand, response, timeoutMs, true, 0U);
}

bool Esp32RomFlasher::readCommandFrame(uint8_t expectedCommand,
                                       CommandResponse &response,
                                       uint32_t timeoutMs,
                                       bool expectStatusBytes,
                                       size_t expectedDataLength) {
  const uint32_t deadline = millis() + timeoutMs;

  for (uint8_t attempt = 0; attempt < kCommandResponseRetryCount; ++attempt) {
    const uint32_t now = millis();
    if (now >= deadline) {
      return false;
    }

    uint8_t frame[128] = {};
    size_t frameLength = 0;
    if (!readFrame(frame, sizeof(frame), frameLength, deadline - now)) {
      return false;
    }

    if (frameLength < kCommandHeaderSize) {
      continue;
    }

    response.direction = frame[0];
    response.command = frame[1];
    response.size = readLe16(&frame[2]);
    response.value = readLe32(&frame[4]);

    const size_t availableBody = frameLength - kCommandHeaderSize;
    size_t copiedBody = 0U;
    response.statusLength = 0;
    response.status[0] = 0U;
    response.status[1] = 0U;
    if (expectStatusBytes) {
      if (availableBody < (expectedDataLength + 2U)) {
        continue;
      }
      copiedBody = expectedDataLength;
      response.status[0] = frame[kCommandHeaderSize + expectedDataLength];
      response.status[1] = frame[kCommandHeaderSize + expectedDataLength + 1U];
      response.statusLength = 2U;
    } else {
      copiedBody = availableBody;
    }

    copiedBody = std::min(copiedBody, sizeof(response.data));
    if (copiedBody > 0U) {
      memcpy(response.data, &frame[kCommandHeaderSize], copiedBody);
    }
    response.dataLength = copiedBody;

    const bool statusOk =
        (response.statusLength == 0U) ||
        ((response.status[0] == 0U) && (response.status[1] == 0U));
    if ((response.direction == kPacketResponse) &&
        (response.command == expectedCommand) &&
        statusOk) {
      return true;
    }
  }

  return false;
}

bool Esp32RomFlasher::readFrame(uint8_t *frame, size_t capacity, size_t &frameLength, uint32_t timeoutMs) {
  frameLength = 0;
  bool started = false;
  bool escaped = false;
  const uint32_t deadline = millis() + timeoutMs;

  while (millis() < deadline) {
    while (AppConfig::espSerial().available() > 0) {
      const int value = AppConfig::espSerial().read();
      if (value < 0) {
        break;
      }

      uint8_t byte = static_cast<uint8_t>(value);
      if (!started) {
        if (byte == kSlipEnd) {
          started = true;
          frameLength = 0;
          escaped = false;
        }
        continue;
      }

      if (escaped) {
        if (byte == kSlipEscEnd) {
          byte = kSlipEnd;
        } else if (byte == kSlipEscEsc) {
          byte = kSlipEsc;
        }
        escaped = false;
      } else if (byte == kSlipEsc) {
        escaped = true;
        continue;
      } else if (byte == kSlipEnd) {
        if (frameLength == 0U) {
          continue;
        }
        return true;
      }

      if (frameLength < capacity) {
        frame[frameLength++] = byte;
      }
    }

    delay(1);
  }

  return false;
}

void Esp32RomFlasher::writeSlipEscaped(uint8_t value) {
  if (value == kSlipEnd) {
    AppConfig::espSerial().write(kSlipEsc);
    AppConfig::espSerial().write(kSlipEscEnd);
  } else if (value == kSlipEsc) {
    AppConfig::espSerial().write(kSlipEsc);
    AppConfig::espSerial().write(kSlipEscEsc);
  } else {
    AppConfig::espSerial().write(value);
  }
}

uint8_t Esp32RomFlasher::checksum(const uint8_t *data, size_t length) const {
  uint8_t value = static_cast<uint8_t>(kChecksumMagic);
  for (size_t i = 0; i < length; ++i) {
    value ^= data[i];
  }
  return value;
}

uint32_t Esp32RomFlasher::alignedSize(uint32_t value, uint32_t alignment) const {
  if (alignment == 0U) {
    return value;
  }
  return ((value + alignment - 1U) / alignment) * alignment;
}

bool Esp32RomFlasher::calculateFlashMd5(uint32_t offset, uint32_t length, char *output, size_t outputCapacity) {
  if ((output == nullptr) || (outputCapacity < (kMd5HexLength + 1U))) {
    return false;
  }

  uint8_t payload[16] = {};
  writeLe32(&payload[0], offset);
  writeLe32(&payload[4], length);
  writeLe32(&payload[8], 0U);
  writeLe32(&payload[12], 0U);

  CommandResponse md5Response;
  uint8_t header[kCommandHeaderSize] = {};
  header[0] = kPacketRequest;
  header[1] = kCommandSpiFlashMd5;
  writeLe16(&header[2], sizeof(payload));
  writeLe32(&header[4], 0U);

  AppConfig::espSerial().write(kSlipEnd);
  for (size_t i = 0; i < sizeof(header); ++i) {
    writeSlipEscaped(header[i]);
  }
  for (size_t i = 0; i < sizeof(payload); ++i) {
    writeSlipEscaped(payload[i]);
  }
  AppConfig::espSerial().write(kSlipEnd);
  AppConfig::espSerial().flush();

  if (!readCommandFrame(kCommandSpiFlashMd5, md5Response, md5TimeoutMs(length), true, kMd5HexLength)) {
    logCommandResponse("ESP ROM SPI_FLASH_MD5 response:", md5Response);
    return false;
  }

  if (md5Response.dataLength == kMd5HexLength) {
    memcpy(output, md5Response.data, kMd5HexLength);
    output[kMd5HexLength] = '\0';
    return true;
  }

  if ((md5Response.dataLength == 18U) && (md5Response.data[16] == 0U) && (md5Response.data[17] == 0U)) {
    bytesToHex(md5Response.data, 16U, output, outputCapacity);
    return true;
  }

  logCommandResponse("Unexpected ESP ROM SPI_FLASH_MD5 payload:", md5Response);
  output[0] = '\0';
  return false;
}

uint32_t Esp32RomFlasher::md5TimeoutMs(uint32_t size) const {
  const uint64_t scaled = static_cast<uint64_t>(kMd5TimeoutPerMbMs) * static_cast<uint64_t>(size);
  const uint32_t timeout = static_cast<uint32_t>(scaled / 1000000ULL);
  return std::max(kMinimumMd5TimeoutMs, timeout);
}

void Esp32RomFlasher::setStatus(SessionStatus &status, const char *detail, uint8_t progress) {
  strlcpy(status.detail, detail, sizeof(status.detail));
  status.progress = progress;
}
