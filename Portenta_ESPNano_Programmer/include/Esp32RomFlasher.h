#pragma once

#include <Arduino.h>
#include "FlashProtocol.h"
#include "StagingStore.h"

class Esp32RomFlasher {
 public:
  bool begin();
  bool flash(FlashManifest &manifest, const StagingStore &store, SessionStatus &status);

 private:
  struct CommandResponse {
    uint8_t direction = 0;
    uint8_t command = 0;
    uint16_t size = 0;
    uint32_t value = 0;
    uint8_t data[64] = {};
    size_t dataLength = 0;
    uint8_t status[2] = {};
    size_t statusLength = 0;
  };

  void enterBootloader();
  void rebootNormal();
  void flushInput();
  bool sendSyncCommand(uint32_t timeoutMs);
  bool syncBootloader(SessionStatus &status);
  bool configureFlash(const FlashManifest &manifest, SessionStatus &status);
  bool changeBaudRate(uint32_t baudRate, SessionStatus &status);
  bool ensureEraseSupport(const FlashManifest &manifest, const StagingStore &store, SessionStatus &status);
  bool startStub(const FlashManifest &manifest, const StagingStore &store, SessionStatus &status);
  bool uploadStubSegment(const char *label,
                         const char *imageName,
                         uint32_t loadAddress,
                         uint32_t segmentSize,
                         const StagingStore &store,
                         SessionStatus &status);
  bool waitForStubReady(SessionStatus &status);
  bool eraseFlashWithStub(SessionStatus &status);
  bool verifyFlashImage(ImageSlot &image, const StagingStore &store, SessionStatus &status, size_t imageIndex, size_t imageCount);
  bool writeImageBlocks(const ImageSlot &image, const StagingStore &store, SessionStatus &status, size_t imageIndex, size_t imageCount);
  bool writeImage(const ImageSlot &image, const StagingStore &store, SessionStatus &status, size_t imageIndex, size_t imageCount);
  bool readCommandFrame(uint8_t expectedCommand,
                        CommandResponse &response,
                        uint32_t timeoutMs,
                        bool expectStatusBytes,
                        size_t expectedDataLength = 0U);
  bool calculateFlashMd5(uint32_t offset, uint32_t length, char *output, size_t outputCapacity);
  bool sendCommand(uint8_t command, const uint8_t *payload, size_t payloadLength, uint32_t checksum, CommandResponse &response, uint32_t timeoutMs);
  bool readResponse(uint8_t expectedCommand, CommandResponse &response, uint32_t timeoutMs);
  bool readFrame(uint8_t *frame, size_t capacity, size_t &frameLength, uint32_t timeoutMs);
  void logCommandResponse(const char *prefix, const CommandResponse &response);
  void writeSlipEscaped(uint8_t value);
  uint8_t checksum(const uint8_t *data, size_t length) const;
  uint32_t alignedSize(uint32_t value, uint32_t alignment) const;
  uint32_t md5TimeoutMs(uint32_t size) const;
  void setStatus(SessionStatus &status, const char *detail, uint8_t progress);
};
