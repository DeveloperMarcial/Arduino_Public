#include "Md5.h"

#include <cstring>

namespace {

#define MD5_F(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))
#define MD5_G(x, y, z) ((y) ^ ((z) & ((x) ^ (y))))
#define MD5_H(x, y, z) ((x) ^ (y) ^ (z))
#define MD5_I(x, y, z) ((y) ^ ((x) | ~(z)))

#define MD5_STEP(f, a, b, c, d, x, t, s) \
  (a) += f((b), (c), (d)) + (x) + (t);   \
  (a) = (((a) << (s)) | (((a) & 0xffffffffU) >> (32 - (s)))); \
  (a) += (b);

#define MD5_SET(n) \
  (context.block[(n)] = static_cast<uint32_t>(ptr[(n) * 4]) | \
                        (static_cast<uint32_t>(ptr[(n) * 4 + 1]) << 8) | \
                        (static_cast<uint32_t>(ptr[(n) * 4 + 2]) << 16) | \
                        (static_cast<uint32_t>(ptr[(n) * 4 + 3]) << 24))

#define MD5_GET(n) (context.block[(n)])

}  // namespace

const void *Md5::body(Md5Context &context, const void *data, size_t length) {
  const uint8_t *ptr = static_cast<const uint8_t *>(data);
  uint32_t a = context.a;
  uint32_t b = context.b;
  uint32_t c = context.c;
  uint32_t d = context.d;

  do {
    const uint32_t savedA = a;
    const uint32_t savedB = b;
    const uint32_t savedC = c;
    const uint32_t savedD = d;

    MD5_STEP(MD5_F, a, b, c, d, MD5_SET(0), 0xd76aa478U, 7)
    MD5_STEP(MD5_F, d, a, b, c, MD5_SET(1), 0xe8c7b756U, 12)
    MD5_STEP(MD5_F, c, d, a, b, MD5_SET(2), 0x242070dbU, 17)
    MD5_STEP(MD5_F, b, c, d, a, MD5_SET(3), 0xc1bdceeeU, 22)
    MD5_STEP(MD5_F, a, b, c, d, MD5_SET(4), 0xf57c0fafU, 7)
    MD5_STEP(MD5_F, d, a, b, c, MD5_SET(5), 0x4787c62aU, 12)
    MD5_STEP(MD5_F, c, d, a, b, MD5_SET(6), 0xa8304613U, 17)
    MD5_STEP(MD5_F, b, c, d, a, MD5_SET(7), 0xfd469501U, 22)
    MD5_STEP(MD5_F, a, b, c, d, MD5_SET(8), 0x698098d8U, 7)
    MD5_STEP(MD5_F, d, a, b, c, MD5_SET(9), 0x8b44f7afU, 12)
    MD5_STEP(MD5_F, c, d, a, b, MD5_SET(10), 0xffff5bb1U, 17)
    MD5_STEP(MD5_F, b, c, d, a, MD5_SET(11), 0x895cd7beU, 22)
    MD5_STEP(MD5_F, a, b, c, d, MD5_SET(12), 0x6b901122U, 7)
    MD5_STEP(MD5_F, d, a, b, c, MD5_SET(13), 0xfd987193U, 12)
    MD5_STEP(MD5_F, c, d, a, b, MD5_SET(14), 0xa679438eU, 17)
    MD5_STEP(MD5_F, b, c, d, a, MD5_SET(15), 0x49b40821U, 22)

    MD5_STEP(MD5_G, a, b, c, d, MD5_GET(1), 0xf61e2562U, 5)
    MD5_STEP(MD5_G, d, a, b, c, MD5_GET(6), 0xc040b340U, 9)
    MD5_STEP(MD5_G, c, d, a, b, MD5_GET(11), 0x265e5a51U, 14)
    MD5_STEP(MD5_G, b, c, d, a, MD5_GET(0), 0xe9b6c7aaU, 20)
    MD5_STEP(MD5_G, a, b, c, d, MD5_GET(5), 0xd62f105dU, 5)
    MD5_STEP(MD5_G, d, a, b, c, MD5_GET(10), 0x02441453U, 9)
    MD5_STEP(MD5_G, c, d, a, b, MD5_GET(15), 0xd8a1e681U, 14)
    MD5_STEP(MD5_G, b, c, d, a, MD5_GET(4), 0xe7d3fbc8U, 20)
    MD5_STEP(MD5_G, a, b, c, d, MD5_GET(9), 0x21e1cde6U, 5)
    MD5_STEP(MD5_G, d, a, b, c, MD5_GET(14), 0xc33707d6U, 9)
    MD5_STEP(MD5_G, c, d, a, b, MD5_GET(3), 0xf4d50d87U, 14)
    MD5_STEP(MD5_G, b, c, d, a, MD5_GET(8), 0x455a14edU, 20)
    MD5_STEP(MD5_G, a, b, c, d, MD5_GET(13), 0xa9e3e905U, 5)
    MD5_STEP(MD5_G, d, a, b, c, MD5_GET(2), 0xfcefa3f8U, 9)
    MD5_STEP(MD5_G, c, d, a, b, MD5_GET(7), 0x676f02d9U, 14)
    MD5_STEP(MD5_G, b, c, d, a, MD5_GET(12), 0x8d2a4c8aU, 20)

    MD5_STEP(MD5_H, a, b, c, d, MD5_GET(5), 0xfffa3942U, 4)
    MD5_STEP(MD5_H, d, a, b, c, MD5_GET(8), 0x8771f681U, 11)
    MD5_STEP(MD5_H, c, d, a, b, MD5_GET(11), 0x6d9d6122U, 16)
    MD5_STEP(MD5_H, b, c, d, a, MD5_GET(14), 0xfde5380cU, 23)
    MD5_STEP(MD5_H, a, b, c, d, MD5_GET(1), 0xa4beea44U, 4)
    MD5_STEP(MD5_H, d, a, b, c, MD5_GET(4), 0x4bdecfa9U, 11)
    MD5_STEP(MD5_H, c, d, a, b, MD5_GET(7), 0xf6bb4b60U, 16)
    MD5_STEP(MD5_H, b, c, d, a, MD5_GET(10), 0xbebfbc70U, 23)
    MD5_STEP(MD5_H, a, b, c, d, MD5_GET(13), 0x289b7ec6U, 4)
    MD5_STEP(MD5_H, d, a, b, c, MD5_GET(0), 0xeaa127faU, 11)
    MD5_STEP(MD5_H, c, d, a, b, MD5_GET(3), 0xd4ef3085U, 16)
    MD5_STEP(MD5_H, b, c, d, a, MD5_GET(6), 0x04881d05U, 23)
    MD5_STEP(MD5_H, a, b, c, d, MD5_GET(9), 0xd9d4d039U, 4)
    MD5_STEP(MD5_H, d, a, b, c, MD5_GET(12), 0xe6db99e5U, 11)
    MD5_STEP(MD5_H, c, d, a, b, MD5_GET(15), 0x1fa27cf8U, 16)
    MD5_STEP(MD5_H, b, c, d, a, MD5_GET(2), 0xc4ac5665U, 23)

    MD5_STEP(MD5_I, a, b, c, d, MD5_GET(0), 0xf4292244U, 6)
    MD5_STEP(MD5_I, d, a, b, c, MD5_GET(7), 0x432aff97U, 10)
    MD5_STEP(MD5_I, c, d, a, b, MD5_GET(14), 0xab9423a7U, 15)
    MD5_STEP(MD5_I, b, c, d, a, MD5_GET(5), 0xfc93a039U, 21)
    MD5_STEP(MD5_I, a, b, c, d, MD5_GET(12), 0x655b59c3U, 6)
    MD5_STEP(MD5_I, d, a, b, c, MD5_GET(3), 0x8f0ccc92U, 10)
    MD5_STEP(MD5_I, c, d, a, b, MD5_GET(10), 0xffeff47dU, 15)
    MD5_STEP(MD5_I, b, c, d, a, MD5_GET(1), 0x85845dd1U, 21)
    MD5_STEP(MD5_I, a, b, c, d, MD5_GET(8), 0x6fa87e4fU, 6)
    MD5_STEP(MD5_I, d, a, b, c, MD5_GET(15), 0xfe2ce6e0U, 10)
    MD5_STEP(MD5_I, c, d, a, b, MD5_GET(6), 0xa3014314U, 15)
    MD5_STEP(MD5_I, b, c, d, a, MD5_GET(13), 0x4e0811a1U, 21)
    MD5_STEP(MD5_I, a, b, c, d, MD5_GET(4), 0xf7537e82U, 6)
    MD5_STEP(MD5_I, d, a, b, c, MD5_GET(11), 0xbd3af235U, 10)
    MD5_STEP(MD5_I, c, d, a, b, MD5_GET(2), 0x2ad7d2bbU, 15)
    MD5_STEP(MD5_I, b, c, d, a, MD5_GET(9), 0xeb86d391U, 21)

    a += savedA;
    b += savedB;
    c += savedC;
    d += savedD;

    ptr += 64;
    length -= 64;
  } while (length >= 64);

  context.a = a;
  context.b = b;
  context.c = c;
  context.d = d;
  return ptr;
}

void Md5::init(Md5Context &context) {
  context.a = 0x67452301U;
  context.b = 0xefcdab89U;
  context.c = 0x98badcfeU;
  context.d = 0x10325476U;
  context.lo = 0U;
  context.hi = 0U;
  memset(context.block, 0, sizeof(context.block));
  memset(context.buffer, 0, sizeof(context.buffer));
}

void Md5::update(Md5Context &context, const void *data, size_t length) {
  const uint32_t savedLo = context.lo;
  if ((context.lo = (savedLo + static_cast<uint32_t>(length)) & 0x1fffffffU) < savedLo) {
    ++context.hi;
  }
  context.hi += static_cast<uint32_t>(length >> 29);

  uint32_t used = savedLo & 0x3fU;
  if (used != 0U) {
    const uint32_t available = 64U - used;
    if (length < available) {
      memcpy(&context.buffer[used], data, length);
      return;
    }

    memcpy(&context.buffer[used], data, available);
    data = static_cast<const uint8_t *>(data) + available;
    length -= available;
    body(context, context.buffer, 64U);
  }

  if (length >= 64U) {
    data = body(context, data, length & ~(static_cast<size_t>(0x3fU)));
    length &= 0x3fU;
  }

  memcpy(context.buffer, data, length);
}

void Md5::finish(Md5Context &context, uint8_t output[16]) {
  uint32_t used = context.lo & 0x3fU;
  context.buffer[used++] = 0x80U;

  uint32_t available = 64U - used;
  if (available < 8U) {
    memset(&context.buffer[used], 0, available);
    body(context, context.buffer, 64U);
    used = 0U;
    available = 64U;
  }

  memset(&context.buffer[used], 0, available - 8U);

  context.lo <<= 3;
  context.buffer[56] = static_cast<uint8_t>(context.lo);
  context.buffer[57] = static_cast<uint8_t>(context.lo >> 8);
  context.buffer[58] = static_cast<uint8_t>(context.lo >> 16);
  context.buffer[59] = static_cast<uint8_t>(context.lo >> 24);
  context.buffer[60] = static_cast<uint8_t>(context.hi);
  context.buffer[61] = static_cast<uint8_t>(context.hi >> 8);
  context.buffer[62] = static_cast<uint8_t>(context.hi >> 16);
  context.buffer[63] = static_cast<uint8_t>(context.hi >> 24);

  body(context, context.buffer, 64U);

  output[0] = static_cast<uint8_t>(context.a);
  output[1] = static_cast<uint8_t>(context.a >> 8);
  output[2] = static_cast<uint8_t>(context.a >> 16);
  output[3] = static_cast<uint8_t>(context.a >> 24);
  output[4] = static_cast<uint8_t>(context.b);
  output[5] = static_cast<uint8_t>(context.b >> 8);
  output[6] = static_cast<uint8_t>(context.b >> 16);
  output[7] = static_cast<uint8_t>(context.b >> 24);
  output[8] = static_cast<uint8_t>(context.c);
  output[9] = static_cast<uint8_t>(context.c >> 8);
  output[10] = static_cast<uint8_t>(context.c >> 16);
  output[11] = static_cast<uint8_t>(context.c >> 24);
  output[12] = static_cast<uint8_t>(context.d);
  output[13] = static_cast<uint8_t>(context.d >> 8);
  output[14] = static_cast<uint8_t>(context.d >> 16);
  output[15] = static_cast<uint8_t>(context.d >> 24);

  memset(&context, 0, sizeof(context));
}
