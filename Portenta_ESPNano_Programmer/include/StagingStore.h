#pragma once

#include <Arduino.h>

#include "AppConfig.h"
#include "FlashProtocol.h"

class StagingStore {
 public:
  struct ChunkInfo {
    uint32_t chunkSize = 0;
    uint32_t totalChunks = 0;
    uint32_t receivedChunks = 0;
  };

  bool begin();
  bool reset(const FlashManifest &manifest);
  bool appendChunk(const char *imageName, uint32_t chunkIndex, const uint8_t *data, size_t length);
  bool hasImage(const char *imageName) const;
  bool readImageChunk(const char *imageName, size_t offset, uint8_t *buffer, size_t capacity, size_t &bytesRead) const;
  size_t imageSize(const char *imageName) const;
  bool chunkInfo(const char *imageName, ChunkInfo &outInfo) const;
  bool chunkReceived(const char *imageName, uint32_t chunkIndex) const;
  bool verifyImage(const char *imageName);
  bool calculateImageMd5(const char *imageName, char *output, size_t outputCapacity) const;
  bool imageVerified(const char *imageName) const;

 private:
  const ImageSlot *findImage(const char *imageName) const;
  ptrdiff_t findImageIndex(const char *imageName) const;
  ImageSlot *findImageMutable(const char *imageName);
  bool makeImagePath(const char *imageName, char *path, size_t capacity) const;
  bool setChunkBit(size_t imageIndex, uint32_t chunkIndex, bool value);
  bool getChunkBit(size_t imageIndex, uint32_t chunkIndex) const;

  FlashManifest manifest_ {};
  ChunkInfo chunkInfo_[AppConfig::kMaxImagesPerSession] = {};
  uint8_t chunkBitmap_[AppConfig::kMaxImagesPerSession][AppConfig::kChunkBitmapBytes] = {};
};
