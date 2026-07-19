#pragma once

#include <Arduino.h>

struct Md5Context {
  uint32_t lo = 0;
  uint32_t hi = 0;
  uint32_t a = 0;
  uint32_t b = 0;
  uint32_t c = 0;
  uint32_t d = 0;
  uint8_t buffer[64] = {};
  uint32_t block[16] = {};
};

class Md5 {
 public:
  static void init(Md5Context &context);
  static void update(Md5Context &context, const void *data, size_t length);
  static void finish(Md5Context &context, uint8_t output[16]);

 private:
  static const void *body(Md5Context &context, const void *data, size_t length);
};
