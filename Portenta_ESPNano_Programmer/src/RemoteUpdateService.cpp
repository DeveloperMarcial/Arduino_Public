#include "RemoteUpdateService.h"

#if !PORTENTA_NETWORK_USE_WIFI
#include <PortentaEthernet.h>
#include <Ethernet.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#include "AppConfig.h"

namespace {

struct HttpHeader {
  char name[32] = {};
  char value[96] = {};
};

struct HttpRequest {
  char method[8] = {};
  char path[160] = {};
  HttpHeader headers[AppConfig::kMaxHeaderCount] = {};
  size_t headerCount = 0;
  size_t contentLength = 0;
  bool contentLengthValid = false;
  char contentType[64] = {};
  uint8_t body[AppConfig::kHttpBodyBufferSize] = {};
  size_t bodyLength = 0;
};

void copyString(char *destination, size_t capacity, const char *source) {
  if (source == nullptr) {
    destination[0] = '\0';
    return;
  }
  strlcpy(destination, source, capacity);
}

const char *sessionStateName(SessionState state) {
  switch (state) {
    case SessionState::Idle:
      return "idle";
    case SessionState::Created:
      return "created";
    case SessionState::ManifestReceived:
      return "manifest_received";
    case SessionState::Uploading:
      return "uploading";
    case SessionState::ReadyToFlash:
      return "ready_to_flash";
    case SessionState::Flashing:
      return "flashing";
    case SessionState::Completed:
      return "completed";
    case SessionState::Failed:
      return "failed";
  }

  return "unknown";
}

#if PORTENTA_NETWORK_USE_WIFI
const char *wifiStatusName(uint8_t status) {
  switch (status) {
    case WL_IDLE_STATUS:
      return "WL_IDLE_STATUS: WiFi is idle";
    case WL_NO_SSID_AVAIL:
      return "WL_NO_SSID_AVAIL: SSID was not found";
    case WL_CONNECTED:
      return "WL_CONNECTED: connected";
    case WL_CONNECT_FAILED:
      return "WL_CONNECT_FAILED: connection failed, check password/security settings";
    case WL_CONNECTION_LOST:
      return "WL_CONNECTION_LOST: connection was lost";
    case WL_DISCONNECTED:
      return "WL_DISCONNECTED: disconnected or timed out";
    default:
      return "Unknown WiFi status";
  }
}
#endif

bool readRequestLine(PortentaNetworkClient &client, char *buffer, size_t capacity, size_t &lineLength) {
  lineLength = 0;
  const uint32_t deadline = millis() + AppConfig::kHttpReadTimeoutMs;

  while (millis() < deadline) {
    while (client.available() > 0) {
      const int value = client.read();
      if (value < 0) {
        break;
      }

      if (value == '\r') {
        continue;
      }

      if (value == '\n') {
        if (lineLength < capacity) {
          buffer[lineLength] = '\0';
          return true;
        }
        return false;
      }

      if (lineLength + 1 >= capacity) {
        return false;
      }

      buffer[lineLength++] = static_cast<char>(value);
    }

    if (!client.connected()) {
      break;
    }

    delay(1);
  }

  return false;
}

bool readRequest(PortentaNetworkClient &client, HttpRequest &request) {
  char line[AppConfig::kHttpRequestBufferSize] = {};
  size_t lineLength = 0;
  if (!readRequestLine(client, line, sizeof(line), lineLength)) {
    return false;
  }

  if (sscanf(line, "%7s %159s", request.method, request.path) != 2) {
    return false;
  }

  while (true) {
    if (!readRequestLine(client, line, sizeof(line), lineLength)) {
      return false;
    }

    if (lineLength == 0) {
      break;
    }

    char *separator = strchr(line, ':');
    if (separator == nullptr) {
      continue;
    }

    *separator = '\0';
    char *value = separator + 1;
    while (*value == ' ') {
      ++value;
    }

    if (request.headerCount < AppConfig::kMaxHeaderCount) {
      copyString(request.headers[request.headerCount].name, sizeof(request.headers[request.headerCount].name), line);
      copyString(request.headers[request.headerCount].value, sizeof(request.headers[request.headerCount].value), value);
      ++request.headerCount;
    }

    if (strcasecmp(line, "Content-Length") == 0) {
      request.contentLength = static_cast<size_t>(strtoul(value, nullptr, 10));
      request.contentLengthValid = true;
    } else if (strcasecmp(line, "Content-Type") == 0) {
      copyString(request.contentType, sizeof(request.contentType), value);
    }
  }

  if (request.contentLength == 0) {
    request.bodyLength = 0;
    return true;
  }

  if (request.contentLength > sizeof(request.body)) {
    return false;
  }

  const uint32_t deadline = millis() + AppConfig::kHttpReadTimeoutMs;
  while ((request.bodyLength < request.contentLength) && (millis() < deadline)) {
    const int available = client.available();
    if (available <= 0) {
      if (!client.connected()) {
        break;
      }
      delay(1);
      continue;
    }

    const size_t remaining = request.contentLength - request.bodyLength;
    const size_t capacity = (remaining < static_cast<size_t>(available)) ? remaining : static_cast<size_t>(available);
    const int bytesRead = client.read(&request.body[request.bodyLength], capacity);
    if (bytesRead <= 0) {
      delay(1);
      continue;
    }
    request.bodyLength += static_cast<size_t>(bytesRead);
  }

  return request.bodyLength == request.contentLength;
}

bool parseUint32(const char *text, uint32_t &value) {
  if ((text == nullptr) || (*text == '\0')) {
    return false;
  }

  char *end = nullptr;
  const unsigned long parsed = strtoul(text, &end, 10);
  if ((*end != '\0') || (parsed > 0xFFFFFFFFUL)) {
    return false;
  }

  value = static_cast<uint32_t>(parsed);
  return true;
}

size_t splitPath(const char *path, char segments[][48], size_t maxSegments) {
  size_t count = 0;
  const char *cursor = path;

  while ((*cursor != '\0') && (count < maxSegments)) {
    while (*cursor == '/') {
      ++cursor;
    }
    if (*cursor == '\0') {
      break;
    }

    const char *start = cursor;
    while ((*cursor != '\0') && (*cursor != '/')) {
      ++cursor;
    }

    const size_t length = static_cast<size_t>(cursor - start);
    const size_t copyLength = (length < 47U) ? length : 47U;
    memcpy(segments[count], start, copyLength);
    segments[count][copyLength] = '\0';
    ++count;
  }

  return count;
}

void sendJsonResponse(PortentaNetworkClient &client, int statusCode, const char *statusText, const JsonDocument &document) {
  char payload[AppConfig::kHttpResponseBufferSize] = {};
  const size_t payloadLength = serializeJson(document, payload, sizeof(payload));

  client.print("HTTP/1.1 ");
  client.print(statusCode);
  client.print(' ');
  client.println(statusText);
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.print("Content-Length: ");
  client.println(payloadLength);
  client.println();
  client.write(reinterpret_cast<const uint8_t *>(payload), payloadLength);
  client.flush();
}

void sendStatusPayload(PortentaNetworkClient &client, int statusCode, const char *statusText, const SessionStatus &status) {
  JsonDocument document;
  document["session_id"] = status.sessionId;
  document["state"] = sessionStateName(status.state);
  document["progress"] = status.progress;
  document["detail"] = status.detail;
  sendJsonResponse(client, statusCode, statusText, document);
}

void sendErrorResponse(PortentaNetworkClient &client, int statusCode, const char *statusText, const char *message) {
  JsonDocument document;
  document["error"] = message;
  sendJsonResponse(client, statusCode, statusText, document);
}

void sendTextResponse(PortentaNetworkClient &client, int statusCode, const char *statusText, const char *message) {
  client.print("HTTP/1.1 ");
  client.print(statusCode);
  client.print(' ');
  client.println(statusText);
  client.println("Content-Type: text/plain");
  client.println("Connection: close");
  client.print("Content-Length: ");
  client.println(strlen(message));
  client.println();
  client.print(message);
  client.flush();
}

PortentaNetworkClient acceptNetworkClient(PortentaNetworkServer &server) {
  return server.accept();
}

}  // namespace

bool RemoteUpdateService::begin() {
  copyString(status_.sessionId, sizeof(status_.sessionId), "");
  setState(SessionState::Idle, "Initializing service", 0);

  if (!staging_.begin() || !flasher_.begin()) {
    setState(SessionState::Failed, "Local service initialization failed", 0);
    return false;
  }

#if PORTENTA_NETWORK_USE_WIFI
  Serial.println("Bringing up Portenta WiFi...");
  if (AppConfig::kWifiSsid[0] == '\0') {
    setNetworkState(false, "WiFi SSID is not configured");
    return false;
  }

  Serial.print("Connecting to WiFi SSID: ");
  Serial.println(AppConfig::kWifiSsid);
  WiFi.begin(AppConfig::kWifiSsid, AppConfig::kWifiPass);
  const uint32_t start = millis();
  while ((WiFi.status() != WL_CONNECTED) && (millis() - start < AppConfig::kNetworkConnectTimeoutMs)) {
    delay(250);
  }

  if (WiFi.status() != WL_CONNECTED) {
    const uint8_t wifiStatus = WiFi.status();
    char detail[128] = {};
    snprintf(detail, sizeof(detail), "WiFi connection failed: %s", wifiStatusName(wifiStatus));
    Serial.print("WiFi connection failed; WiFi.status()=");
    Serial.print(wifiStatus);
    Serial.print(" ");
    Serial.println(wifiStatusName(wifiStatus));
    setNetworkState(false, detail);
    return false;
  }

  networkReady_ = true;
  server_.begin();

  char detail[96] = {};
  const IPAddress localIp = WiFi.localIP();
  snprintf(detail, sizeof(detail), "HTTP server listening on WiFi %u.%u.%u.%u:%u",
           localIp[0], localIp[1], localIp[2], localIp[3], AppConfig::kHttpPort);
  setState(SessionState::Idle, detail, 0);

  Serial.print("WiFi IP: ");
  Serial.println(localIp);
  Serial.print("HTTP server port: ");
  Serial.println(AppConfig::kHttpPort);
  return true;
#else
  Serial.println("Bringing up Portenta Ethernet...");
  if (Ethernet.begin() == 0) {
    Serial.println("DHCP unavailable, falling back to static Ethernet address");
    Ethernet.begin(
        AppConfig::defaultEthernetIp(),
        AppConfig::defaultEthernetDns(),
        AppConfig::defaultEthernetGateway(),
        AppConfig::defaultEthernetSubnet());
  }

  const uint32_t start = millis();
  while ((Ethernet.linkStatus() == Unknown) && (millis() - start < AppConfig::kNetworkConnectTimeoutMs)) {
    delay(100);
  }

  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    setNetworkState(false, "Ethernet hardware unavailable");
    return false;
  }

  networkReady_ = true;
  server_.begin();

  char detail[96] = {};
  const IPAddress localIp = Ethernet.localIP();
  snprintf(detail, sizeof(detail), "HTTP server listening on %u.%u.%u.%u:%u",
           localIp[0], localIp[1], localIp[2], localIp[3], AppConfig::kHttpPort);
  setState(SessionState::Idle, detail, 0);

  Serial.print("Ethernet IP: ");
  Serial.println(localIp);
  Serial.print("HTTP server port: ");
  Serial.println(AppConfig::kHttpPort);
  return true;
#endif
}

void RemoteUpdateService::loop() {
  pollHttpServer();
}

bool RemoteUpdateService::createSession(SessionStatus &outStatus) {
  const uint32_t nonce = millis();
  snprintf(status_.sessionId, sizeof(status_.sessionId), "sess-%08lx", static_cast<unsigned long>(nonce));
  memset(&manifest_, 0, sizeof(manifest_));
  setState(SessionState::Created, "Session created", 0);
  outStatus = status_;
  return true;
}

bool RemoteUpdateService::applyManifest(const char *sessionId, const JsonDocument &manifest, SessionStatus &outStatus) {
  if (!sessionMatches(sessionId)) {
    setState(SessionState::Failed, "Session ID mismatch", 0);
    outStatus = status_;
    return false;
  }

  copyString(manifest_.target, sizeof(manifest_.target), manifest["target"] | "esp32");
  manifest_.flashSize = manifest["flash_size"] | 0U;
  manifest_.baud = manifest["baud"] | AppConfig::kDefaultFlashBaud;
  manifest_.chunkSize = manifest["chunk_size"] | static_cast<uint32_t>(AppConfig::kChunkBufferSize);
  manifest_.eraseFirst = manifest["erase"] | false;
  manifest_.stub.enabled = false;

  if ((manifest_.chunkSize == 0U) || (manifest_.chunkSize > AppConfig::kHttpBodyBufferSize)) {
    setState(SessionState::Failed, "Manifest chunk_size invalid for HTTP transport", 0);
    outStatus = status_;
    return false;
  }

  JsonArrayConst images = manifest["images"].as<JsonArrayConst>();
  manifest_.imageCount = 0;
  size_t flashableImageCount = 0;
  for (JsonObjectConst image : images) {
    if (manifest_.imageCount >= AppConfig::kMaxImagesPerSession) {
      setState(SessionState::Failed, "Too many images in manifest", 0);
      outStatus = status_;
      return false;
    }

    ImageSlot &slot = manifest_.images[manifest_.imageCount++];
    copyString(slot.name, sizeof(slot.name), image["name"] | "");
    copyString(slot.sha256, sizeof(slot.sha256), image["sha256"] | "");
    slot.offset = image["offset"] | 0U;
    slot.size = image["size"] | 0U;
    slot.flashImage = image["flash"] | true;
    if (slot.flashImage) {
      ++flashableImageCount;
    }

    if (slot.sha256[0] == '\0') {
      setState(SessionState::Failed, "Manifest image missing sha256", 0);
      outStatus = status_;
      return false;
    }
  }

  JsonObjectConst stub = manifest["stub"].as<JsonObjectConst>();
  if (!stub.isNull()) {
    manifest_.stub.enabled = stub["enabled"] | false;
    copyString(manifest_.stub.textImageName, sizeof(manifest_.stub.textImageName), stub["text_image"] | "");
    manifest_.stub.textStart = stub["text_start"] | 0U;
    manifest_.stub.textSize = stub["text_size"] | 0U;
    copyString(manifest_.stub.dataImageName, sizeof(manifest_.stub.dataImageName), stub["data_image"] | "");
    manifest_.stub.dataStart = stub["data_start"] | 0U;
    manifest_.stub.dataSize = stub["data_size"] | 0U;
    manifest_.stub.entry = stub["entry"] | 0U;
  }

  if (manifest_.imageCount == 0) {
    setState(SessionState::Failed, "Manifest contained no images", 0);
    outStatus = status_;
    return false;
  }

  if (flashableImageCount == 0U) {
    setState(SessionState::Failed, "Manifest contained no flashable images", 0);
    outStatus = status_;
    return false;
  }

  if (manifest_.eraseFirst && !manifest_.stub.enabled) {
    setState(SessionState::Failed, "erase=true requires stub metadata in manifest", 0);
    outStatus = status_;
    return false;
  }

  if (!staging_.reset(manifest_)) {
    setState(SessionState::Failed, "Failed to initialize chunk staging", 0);
    outStatus = status_;
    return false;
  }
  setState(SessionState::ManifestReceived, "Manifest accepted", 5);
  outStatus = status_;
  return true;
}

bool RemoteUpdateService::uploadChunk(const char *sessionId, const char *imageName, uint32_t chunkIndex, const uint8_t *data, size_t length, SessionStatus &outStatus) {
  if (!sessionMatches(sessionId)) {
    setState(SessionState::Failed, "Session ID mismatch", 0);
    outStatus = status_;
    return false;
  }

  if (!staging_.appendChunk(imageName, chunkIndex, data, length)) {
    setState(SessionState::Failed, "Chunk rejected: wrong index or size", 0);
    outStatus = status_;
    return false;
  }

  setState(SessionState::Uploading, "Chunk staged", 25);
  finalizeIfReady();
  outStatus = status_;
  return true;
}

bool RemoteUpdateService::flashSession(const char *sessionId, SessionStatus &outStatus) {
  if (!sessionMatches(sessionId)) {
    setState(SessionState::Failed, "Session ID mismatch", 0);
    outStatus = status_;
    return false;
  }

  if (status_.state != SessionState::ReadyToFlash) {
    setState(SessionState::Failed, "Session is not ready to flash", 0);
    outStatus = status_;
    return false;
  }

  setState(SessionState::Flashing, "Starting flash sequence", 30);
  const bool ok = flasher_.flash(manifest_, staging_, status_);
  status_.state = ok ? SessionState::Completed : SessionState::Failed;
  if (ok) {
    copyString(status_.detail, sizeof(status_.detail), "Flash and readback verification successful");
    status_.progress = 100;
  }
  outStatus = status_;
  return ok;
}

SessionStatus RemoteUpdateService::status() const {
  return status_;
}

bool RemoteUpdateService::sessionMatches(const char *sessionId) const {
  return strcmp(status_.sessionId, sessionId) == 0;
}

void RemoteUpdateService::setState(SessionState state, const char *detail, uint8_t progress) {
  status_.state = state;
  copyString(status_.detail, sizeof(status_.detail), detail);
  status_.progress = progress;
}

bool RemoteUpdateService::finalizeIfReady() {
  for (size_t i = 0; i < manifest_.imageCount; ++i) {
    if (!staging_.hasImage(manifest_.images[i].name)) {
      return false;
    }

    if (!staging_.verifyImage(manifest_.images[i].name)) {
      setState(SessionState::Failed, "Staged image hash verification failed", 0);
      return false;
    }
  }

  setState(SessionState::ReadyToFlash, "All images uploaded and verified", 100);
  return true;
}

void RemoteUpdateService::pollHttpServer() {
  if (!networkReady_) {
    return;
  }

  PortentaNetworkClient client = acceptNetworkClient(server_);
  if (!client) {
    return;
  }

  HttpRequest request;
  if (!readRequest(client, request)) {
    sendErrorResponse(client, 400, "Bad Request", "Malformed HTTP request");
    delay(1);
    client.stop();
    return;
  }

  char pathSegments[AppConfig::kMaxPathSegments][48] = {};
  const size_t segmentCount = splitPath(request.path, pathSegments, AppConfig::kMaxPathSegments);
  SessionStatus responseStatus = status_;

  if ((strcmp(request.method, "POST") == 0) &&
      (segmentCount == 3) &&
      (strcmp(pathSegments[0], "api") == 0) &&
      (strcmp(pathSegments[1], "v1") == 0) &&
      (strcmp(pathSegments[2], "session") == 0)) {
    createSession(responseStatus);
    JsonDocument document;
    document["session_id"] = responseStatus.sessionId;
    document["state"] = sessionStateName(responseStatus.state);
    document["progress"] = responseStatus.progress;
    document["detail"] = responseStatus.detail;
    sendJsonResponse(client, 200, "OK", document);
  } else if ((segmentCount >= 5) &&
             (strcmp(pathSegments[0], "api") == 0) &&
             (strcmp(pathSegments[1], "v1") == 0) &&
             (strcmp(pathSegments[2], "session") == 0)) {
    const char *sessionId = pathSegments[3];

    if ((strcmp(request.method, "POST") == 0) &&
        (segmentCount == 5) &&
        (strcmp(pathSegments[4], "manifest") == 0)) {
      JsonDocument manifest;
      const DeserializationError error = deserializeJson(manifest, request.body, request.bodyLength);
      if (error) {
        sendErrorResponse(client, 400, "Bad Request", "Manifest JSON parse failed");
      } else if (!applyManifest(sessionId, manifest, responseStatus)) {
        sendStatusPayload(client, 409, "Conflict", responseStatus);
      } else {
        sendStatusPayload(client, 200, "OK", responseStatus);
      }
    } else if ((strcmp(request.method, "POST") == 0) &&
               (segmentCount == 7) &&
               (strcmp(pathSegments[4], "chunk") == 0)) {
      const char *imageName = pathSegments[5];
      uint32_t chunkIndex = 0;
      if (!request.contentLengthValid) {
        sendErrorResponse(client, 411, "Length Required", "Chunk uploads require Content-Length");
      } else if (!parseUint32(pathSegments[6], chunkIndex)) {
        sendErrorResponse(client, 400, "Bad Request", "Chunk index must be an unsigned integer");
      } else if (!uploadChunk(sessionId, imageName, chunkIndex, request.body, request.bodyLength, responseStatus)) {
        sendStatusPayload(client, 409, "Conflict", responseStatus);
      } else {
        sendStatusPayload(client, 200, "OK", responseStatus);
      }
    } else if ((strcmp(request.method, "POST") == 0) &&
               (segmentCount == 5) &&
               (strcmp(pathSegments[4], "flash") == 0)) {
      if (!flashSession(sessionId, responseStatus)) {
        sendStatusPayload(client, 409, "Conflict", responseStatus);
      } else {
        sendStatusPayload(client, 200, "OK", responseStatus);
      }
    } else if ((strcmp(request.method, "GET") == 0) &&
               (segmentCount == 5) &&
               (strcmp(pathSegments[4], "status") == 0)) {
      if (!sessionMatches(sessionId)) {
        sendErrorResponse(client, 404, "Not Found", "Unknown session");
      } else {
        JsonDocument document;
        document["session_id"] = status_.sessionId;
        document["state"] = sessionStateName(status_.state);
        document["progress"] = status_.progress;
        document["detail"] = status_.detail;
        document["target"] = manifest_.target;
        document["flash_size"] = manifest_.flashSize;
        document["baud"] = manifest_.baud;
        document["chunk_size"] = manifest_.chunkSize;
        document["erase"] = manifest_.eraseFirst;
        JsonArray images = document["images"].to<JsonArray>();
        for (size_t i = 0; i < manifest_.imageCount; ++i) {
          JsonObject image = images.add<JsonObject>();
          image["name"] = manifest_.images[i].name;
          image["sha256"] = manifest_.images[i].sha256;
          image["offset"] = manifest_.images[i].offset;
          image["size"] = manifest_.images[i].size;
          image["flash"] = manifest_.images[i].flashImage;
          image["received_bytes"] = manifest_.images[i].receivedBytes;
          image["complete"] = manifest_.images[i].complete;
          image["verified"] = manifest_.images[i].verified;
          image["staged_md5"] = manifest_.images[i].stagedMd5;
          image["flash_md5"] = manifest_.images[i].flashMd5;
          image["flash_verified"] = manifest_.images[i].flashVerified;

          StagingStore::ChunkInfo chunkInfo;
          if (staging_.chunkInfo(manifest_.images[i].name, chunkInfo)) {
            image["received_chunks"] = chunkInfo.receivedChunks;
            image["total_chunks"] = chunkInfo.totalChunks;
          }
        }
        sendJsonResponse(client, 200, "OK", document);
      }
    } else if ((strcmp(request.method, "GET") == 0) &&
               (segmentCount == 6) &&
               (strcmp(pathSegments[4], "chunks") == 0)) {
      if (!sendChunkMap(client, sessionId, pathSegments[5])) {
        sendErrorResponse(client, 404, "Not Found", "Unknown session or image");
      }
    } else {
      sendErrorResponse(client, 404, "Not Found", "Unknown API route");
    }
  } else if ((strcmp(request.method, "GET") == 0) && (strcmp(request.path, "/") == 0)) {
    sendTextResponse(client, 200, "OK", "Portenta ESP32 programmer is online\n");
  } else {
    sendErrorResponse(client, 404, "Not Found", "Unknown API route");
  }

  delay(1);
  client.flush();
  delay(10);
  client.stop();
}

void RemoteUpdateService::setNetworkState(bool ready, const char *detail) {
  networkReady_ = ready;
  setState(ready ? SessionState::Idle : SessionState::Failed, detail, 0);
  if (!ready) {
    Serial.print("Network initialization failed: ");
    Serial.println(detail);
  }
}

bool RemoteUpdateService::sendChunkMap(PortentaNetworkClient &client, const char *sessionId, const char *imageName) const {
  if (!sessionMatches(sessionId)) {
    return false;
  }

  StagingStore::ChunkInfo chunkInfo;
  if (!staging_.chunkInfo(imageName, chunkInfo)) {
    return false;
  }

  JsonDocument document;
  document["session_id"] = status_.sessionId;
  document["image"] = imageName;
  document["chunk_size"] = chunkInfo.chunkSize;
  document["total_chunks"] = chunkInfo.totalChunks;
  document["received_chunks"] = chunkInfo.receivedChunks;

  JsonArray received = document["received"].to<JsonArray>();
  for (uint32_t chunkIndex = 0; chunkIndex < chunkInfo.totalChunks; ++chunkIndex) {
    received.add(staging_.chunkReceived(imageName, chunkIndex));
  }

  sendJsonResponse(client, 200, "OK", document);
  return true;
}
