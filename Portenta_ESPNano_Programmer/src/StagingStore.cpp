#include "StagingStore.h"

#include <BlockDevice.h>
#include <LittleFileSystem.h>
#include <MBRBlockDevice.h>
#include <mbedtls/sha256.h>

#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#include "AppConfig.h"
#include "Md5.h"

namespace {

mbed::BlockDevice *g_qspi = mbed::BlockDevice::get_default_instance();
mbed::MBRBlockDevice g_stagingPartition(g_qspi, 4);
mbed::LittleFileSystem g_stagingFs("staging");
bool g_storageReady = false;

bool ensureDirectory(const char *path) {
  struct stat info {};
  if (stat(path, &info) == 0) {
    return S_ISDIR(info.st_mode);
  }

  return mkdir(path, 0777) == 0;
}

constexpr size_t kSha256HexLength = 64;
constexpr size_t kMd5HexLength = 32;

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

}  // namespace

bool StagingStore::begin() {
  memset(&manifest_, 0, sizeof(manifest_));
  memset(chunkInfo_, 0, sizeof(chunkInfo_));
  memset(chunkBitmap_, 0, sizeof(chunkBitmap_));

  if (!g_storageReady) {
    int result = g_qspi->init();
    if (result != 0) {
      Serial.println("Failed to initialize Portenta QSPI storage.");
      return false;
    }

    result = g_stagingPartition.init();
    if (result != 0) {
      Serial.println("Failed to open QSPI user-data partition 4 for staging.");
      Serial.println("Run the Arduino STM32H747_System > QSPIFormat sketch, then WiFiFirmwareUpdater.");
      g_qspi->deinit();
      return false;
    }

    result = g_stagingFs.mount(&g_stagingPartition);
    if (result != 0) {
      Serial.println("Formatting QSPI user-data partition 4 for firmware staging.");
      result = g_stagingFs.reformat(&g_stagingPartition);
      if (result != 0) {
        Serial.println("Failed to format QSPI user-data partition 4 for staging.");
        g_stagingPartition.deinit();
        g_qspi->deinit();
        return false;
      }

      result = g_stagingFs.mount(&g_stagingPartition);
      if (result != 0) {
        Serial.println("Failed to mount QSPI user-data partition 4 after formatting.");
        g_stagingPartition.deinit();
        g_qspi->deinit();
        return false;
      }
    }

    g_storageReady = true;
  }

  return ensureDirectory(PORTENTA_STAGING_DIR);
}

bool StagingStore::reset(const FlashManifest &manifest) {
  manifest_ = manifest;
  memset(chunkInfo_, 0, sizeof(chunkInfo_));
  memset(chunkBitmap_, 0, sizeof(chunkBitmap_));
  for (size_t i = 0; i < AppConfig::kMaxImagesPerSession; ++i) {
    manifest_.images[i].receivedBytes = 0;
    manifest_.images[i].complete = false;
    manifest_.images[i].verified = false;
  }

  if (manifest_.chunkSize == 0U) {
    return false;
  }

  for (size_t i = 0; i < manifest_.imageCount; ++i) {
    chunkInfo_[i].chunkSize = manifest_.chunkSize;
    chunkInfo_[i].totalChunks = (manifest_.images[i].size + manifest_.chunkSize - 1U) / manifest_.chunkSize;
    if (chunkInfo_[i].totalChunks > AppConfig::kMaxTrackedChunksPerImage) {
      return false;
    }
  }

  if (!ensureDirectory(PORTENTA_STAGING_DIR)) {
    return false;
  }

  for (size_t i = 0; i < manifest_.imageCount; ++i) {
    char path[160] = {};
    if (makeImagePath(manifest_.images[i].name, path, sizeof(path))) {
      remove(path);
    }
  }

  return true;
}

bool StagingStore::appendChunk(const char *imageName, uint32_t chunkIndex, const uint8_t *data, size_t length) {
  const ptrdiff_t imageIndex = findImageIndex(imageName);
  if (imageIndex < 0) {
    return false;
  }

  ImageSlot &slot = manifest_.images[imageIndex];
  ChunkInfo &info = chunkInfo_[imageIndex];
  if (chunkIndex >= info.totalChunks) {
    return false;
  }

  const uint32_t chunkOffset = chunkIndex * info.chunkSize;
  const uint32_t remainingBytes = slot.size - chunkOffset;
  const size_t expectedLength = (remainingBytes < info.chunkSize) ? remainingBytes : info.chunkSize;
  if ((length == 0U) || (length != expectedLength)) {
    return false;
  }

  char path[160] = {};
  if (!makeImagePath(imageName, path, sizeof(path))) {
    return false;
  }

  FILE *handle = fopen(path, "rb+");
  if (handle == nullptr) {
    handle = fopen(path, "wb+");
  }
  if (handle == nullptr) {
    return false;
  }

  if (fseek(handle, static_cast<long>(chunkOffset), SEEK_SET) != 0) {
    fclose(handle);
    return false;
  }

  const size_t written = fwrite(data, 1, length, handle);
  fflush(handle);
  fclose(handle);
  if (written != length) {
    return false;
  }

  if (!getChunkBit(imageIndex, chunkIndex)) {
    if (!setChunkBit(imageIndex, chunkIndex, true)) {
      return false;
    }
    slot.receivedBytes += static_cast<uint32_t>(written);
    info.receivedChunks += 1U;
  }

  slot.complete = (info.receivedChunks == info.totalChunks);
  slot.verified = false;
  return true;
}

bool StagingStore::hasImage(const char *imageName) const {
  const ImageSlot *slot = findImage(imageName);
  return (slot != nullptr) && slot->complete;
}

bool StagingStore::readImageChunk(const char *imageName, size_t offset, uint8_t *buffer, size_t capacity, size_t &bytesRead) const {
  const ImageSlot *slot = findImage(imageName);
  if ((slot == nullptr) || (offset >= slot->receivedBytes)) {
    bytesRead = 0;
    return false;
  }

  char path[160] = {};
  if (!makeImagePath(imageName, path, sizeof(path))) {
    bytesRead = 0;
    return false;
  }

  FILE *handle = fopen(path, "rb");
  if (handle == nullptr) {
    bytesRead = 0;
    return false;
  }

  if (fseek(handle, static_cast<long>(offset), SEEK_SET) != 0) {
    fclose(handle);
    bytesRead = 0;
    return false;
  }

  const size_t available = static_cast<size_t>(slot->receivedBytes) - offset;
  bytesRead = (available < capacity) ? available : capacity;
  const size_t actualRead = fread(buffer, 1, bytesRead, handle);
  fclose(handle);

  bytesRead = actualRead;
  return actualRead > 0U;
}

size_t StagingStore::imageSize(const char *imageName) const {
  const ImageSlot *slot = findImage(imageName);
  return (slot == nullptr) ? 0U : static_cast<size_t>(slot->size);
}

bool StagingStore::chunkInfo(const char *imageName, ChunkInfo &outInfo) const {
  const ptrdiff_t imageIndex = findImageIndex(imageName);
  if (imageIndex < 0) {
    return false;
  }

  outInfo = chunkInfo_[imageIndex];
  return true;
}

bool StagingStore::chunkReceived(const char *imageName, uint32_t chunkIndex) const {
  const ptrdiff_t imageIndex = findImageIndex(imageName);
  if (imageIndex < 0) {
    return false;
  }

  return getChunkBit(static_cast<size_t>(imageIndex), chunkIndex);
}

bool StagingStore::verifyImage(const char *imageName) {
  ImageSlot *slot = findImageMutable(imageName);
  if ((slot == nullptr) || !slot->complete || (slot->sha256[0] == '\0')) {
    return false;
  }

  char path[160] = {};
  if (!makeImagePath(imageName, path, sizeof(path))) {
    return false;
  }

  FILE *handle = fopen(path, "rb");
  if (handle == nullptr) {
    return false;
  }

  mbedtls_sha256_context context;
  mbedtls_sha256_init(&context);
  if (mbedtls_sha256_starts_ret(&context, 0) != 0) {
    mbedtls_sha256_free(&context);
    fclose(handle);
    return false;
  }

  uint8_t buffer[AppConfig::kChunkBufferSize] = {};
  size_t totalRead = 0;
  bool ok = true;
  while (totalRead < slot->size) {
    const size_t requestLength = std::min<size_t>(sizeof(buffer), static_cast<size_t>(slot->size) - totalRead);
    const size_t bytesRead = fread(buffer, 1, requestLength, handle);
    if (bytesRead != requestLength) {
      ok = false;
      break;
    }

    if (mbedtls_sha256_update_ret(&context, buffer, bytesRead) != 0) {
      ok = false;
      break;
    }
    totalRead += bytesRead;
  }

  fclose(handle);

  uint8_t digest[32] = {};
  if (ok && (mbedtls_sha256_finish_ret(&context, digest) != 0)) {
    ok = false;
  }
  mbedtls_sha256_free(&context);

  if (!ok || (totalRead != slot->size)) {
    slot->verified = false;
    return false;
  }

  char digestHex[kSha256HexLength + 1U] = {};
  bytesToHex(digest, sizeof(digest), digestHex, sizeof(digestHex));
  slot->verified = (strcmp(slot->sha256, digestHex) == 0);
  return slot->verified;
}

bool StagingStore::calculateImageMd5(const char *imageName, char *output, size_t outputCapacity) const {
  const ImageSlot *slot = findImage(imageName);
  if ((slot == nullptr) || !slot->complete || (output == nullptr) || (outputCapacity < (kMd5HexLength + 1U))) {
    return false;
  }

  char path[160] = {};
  if (!makeImagePath(imageName, path, sizeof(path))) {
    return false;
  }

  FILE *handle = fopen(path, "rb");
  if (handle == nullptr) {
    return false;
  }

  Md5Context context;
  Md5::init(context);

  uint8_t buffer[AppConfig::kChunkBufferSize] = {};
  size_t totalRead = 0;
  bool ok = true;
  while (totalRead < slot->size) {
    const size_t requestLength = std::min<size_t>(sizeof(buffer), static_cast<size_t>(slot->size) - totalRead);
    const size_t bytesRead = fread(buffer, 1, requestLength, handle);
    if (bytesRead != requestLength) {
      ok = false;
      break;
    }

    Md5::update(context, buffer, bytesRead);
    totalRead += bytesRead;
  }

  fclose(handle);

  uint8_t digest[16] = {};
  if (ok) {
    Md5::finish(context, digest);
  }

  if (!ok || (totalRead != slot->size)) {
    if (outputCapacity > 0U) {
      output[0] = '\0';
    }
    return false;
  }

  bytesToHex(digest, sizeof(digest), output, outputCapacity);
  return true;
}

bool StagingStore::imageVerified(const char *imageName) const {
  const ImageSlot *slot = findImage(imageName);
  return (slot != nullptr) && slot->verified;
}

const ImageSlot *StagingStore::findImage(const char *imageName) const {
  const ptrdiff_t imageIndex = findImageIndex(imageName);
  if (imageIndex < 0) {
    return nullptr;
  }
  return &manifest_.images[imageIndex];
}

ptrdiff_t StagingStore::findImageIndex(const char *imageName) const {
  for (size_t i = 0; i < manifest_.imageCount; ++i) {
    if (strcmp(manifest_.images[i].name, imageName) == 0) {
      return static_cast<ptrdiff_t>(i);
    }
  }

  return -1;
}

ImageSlot *StagingStore::findImageMutable(const char *imageName) {
  const ptrdiff_t imageIndex = findImageIndex(imageName);
  if (imageIndex < 0) {
    return nullptr;
  }
  return &manifest_.images[imageIndex];
}

bool StagingStore::makeImagePath(const char *imageName, char *path, size_t capacity) const {
  if ((imageName == nullptr) || (strchr(imageName, '/') != nullptr) || (strchr(imageName, '\\') != nullptr)) {
    return false;
  }

  const int written = snprintf(path, capacity, "%s/%s", PORTENTA_STAGING_DIR, imageName);
  return (written > 0) && (static_cast<size_t>(written) < capacity);
}

bool StagingStore::setChunkBit(size_t imageIndex, uint32_t chunkIndex, bool value) {
  if ((imageIndex >= AppConfig::kMaxImagesPerSession) || (chunkIndex >= AppConfig::kMaxTrackedChunksPerImage)) {
    return false;
  }

  const size_t byteIndex = chunkIndex / 8U;
  const uint8_t bitMask = static_cast<uint8_t>(1U << (chunkIndex % 8U));
  if (value) {
    chunkBitmap_[imageIndex][byteIndex] |= bitMask;
  } else {
    chunkBitmap_[imageIndex][byteIndex] &= static_cast<uint8_t>(~bitMask);
  }

  return true;
}

bool StagingStore::getChunkBit(size_t imageIndex, uint32_t chunkIndex) const {
  if ((imageIndex >= AppConfig::kMaxImagesPerSession) || (chunkIndex >= AppConfig::kMaxTrackedChunksPerImage)) {
    return false;
  }

  const size_t byteIndex = chunkIndex / 8U;
  const uint8_t bitMask = static_cast<uint8_t>(1U << (chunkIndex % 8U));
  return (chunkBitmap_[imageIndex][byteIndex] & bitMask) != 0U;
}
