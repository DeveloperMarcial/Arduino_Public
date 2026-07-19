#pragma once

#include <Arduino.h>

#include "AppConfig.h"

enum class SessionState : uint8_t {
  Idle,
  Created,
  ManifestReceived,
  Uploading,
  ReadyToFlash,
  Flashing,
  Completed,
  Failed
};

struct ImageSlot {
  char name[33] = {};
  char sha256[65] = {};
  char stagedMd5[33] = {};
  char flashMd5[33] = {};
  uint32_t offset = 0;
  uint32_t size = 0;
  uint32_t receivedBytes = 0;
  bool flashImage = true;
  bool complete = false;
  bool verified = false;
  bool flashVerified = false;
};

struct StubLoaderConfig {
  bool enabled = false;
  char textImageName[33] = {};
  uint32_t textStart = 0;
  uint32_t textSize = 0;
  char dataImageName[33] = {};
  uint32_t dataStart = 0;
  uint32_t dataSize = 0;
  uint32_t entry = 0;
};

struct FlashManifest {
  char target[16] = {};
  uint32_t flashSize = 0;
  uint32_t baud = 0;
  uint32_t chunkSize = 1024;
  bool eraseFirst = false;
  StubLoaderConfig stub {};
  size_t imageCount = 0;
  ImageSlot images[AppConfig::kMaxImagesPerSession] = {};
};

struct SessionStatus {
  char sessionId[25] = {};
  SessionState state = SessionState::Idle;
  char detail[96] = {};
  uint8_t progress = 0;
};
