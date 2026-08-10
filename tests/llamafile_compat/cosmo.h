#pragma once

#include <stdint.h>

#define UTF16_MASK 0xfc00
#define UTF16_MOAR 0xd800
#define UTF16_CONT 0xdc00

#define READ32LE(S) \
  ((uint_least32_t)(255 & (S)[3]) << 030 | \
   (uint_least32_t)(255 & (S)[2]) << 020 | \
   (uint_least32_t)(255 & (S)[1]) << 010 | \
   (uint_least32_t)(255 & (S)[0]) << 000)

inline int Bsr(int value) { return __builtin_clz(value) ^ 31; }

#define ThomPikeCont(x) (0200 == (0300 & (x)))
#define ThomPikeByte(x) ((x) & (((1 << ThomPikeMsb(x)) - 1) | 3))
#define ThomPikeLen(x) (7 - ThomPikeMsb(x))
#define ThomPikeMsb(x) ((255 & (x)) < 252 ? Bsr(255 & ~(x)) : 1)
#define ThomPikeMerge(x, y) ((x) << 6 | (077 & (y)))

#define IsSurrogate(wc) ((0xf800 & (wc)) == 0xd800)
#define IsHighSurrogate(wc) (((wc) & UTF16_MASK) == UTF16_MOAR)
#define IsLowSurrogate(wc) (((wc) & UTF16_MASK) == UTF16_CONT)
#define MergeUtf16(hi, lo) ((((hi) - 0xD800) << 10) + ((lo) - 0xDC00) + 0x10000)
#define EncodeUtf16(wc) \
  ((0x0000 <= (wc) && (wc) <= 0xFFFF) || (0xE000 <= (wc) && (wc) <= 0xFFFF) \
     ? (wc) \
   : 0x10000 <= (wc) && (wc) <= 0x10FFFF \
     ? (((((wc) - 0x10000) >> 10) + 0xD800) | \
        (unsigned)((((wc) - 0x10000) & 1023) + 0xDC00) << 16) \
     : 0xFFFD)

inline char* FormatInt64(char* pOutput, int64_t value)
{
  char* pCursor = pOutput;
  uint64_t magnitude;
  if (value < 0) {
    *pCursor++ = '-';
    magnitude = 0 - (uint64_t)value;
  } else {
    magnitude = value;
  }
  char* pDigits = pCursor;
  do {
    *pCursor++ = (char)('0' + magnitude % 10);
    magnitude /= 10;
  } while (magnitude);
  for (char* pLeft = pDigits, *pRight = pCursor - 1; pLeft < pRight; ++pLeft, --pRight) {
    char temporary = *pLeft;
    *pLeft = *pRight;
    *pRight = temporary;
  }
  return pCursor;
}
