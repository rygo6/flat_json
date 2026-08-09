////////////////////////////////////////////////////////////////////////////////
// @author: ryan
// flat_json.cpp — Flat, caller-owned arena JSON parsing and serialization.
////////////////////////////////////////////////////////////////////////////////

// Copyright 2024 Mozilla Foundation
//
// Project lineage:
//   - Cosmopolitan tool/net/ljson.c (2022), by Justine Tunney and
//     Gautham Venkatasubramanian.
//   - The Mozilla-sponsored C++ port used by Mozilla-Ocho/llamafile and
//     published as jart/json.cpp by Justine Tunney and contributors (2024).
//   - This immutable flat-arena parse/serialization derivative.
//
// See THIRD_PARTY_NOTICES.md for complete provenance.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "flat_json.hpp"
#include "jtckdint.h"

#include <algorithm>
#include <cmath>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <span>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <type_traits>
#include <unistd.h>
#include <wchar.h>

#if !defined(__x86_64__) && !defined(_M_X64) && !defined(__aarch64__) && !defined(_M_ARM64)
#error flat json supports only x86-64 and ARM64
#endif
static_assert(sizeof(void*) == 8, "flat json requires a 64-bit target");

////////////////////////////////////////////////////////////////////////////////
// Embedded google/double-conversion
////////////////////////////////////////////////////////////////////////////////
//
// Origin: https://github.com/google/double-conversion
// Upstream commit: 75b48d66ac835da2c1678926f7d61d6cb2992922
// Upstream commit date: 2024-05-21
// License: BSD-3-Clause
//
// Local changes retained while amalgamating:
//   * remove internal quoted includes after dependency-order amalgamation;
//   * retain only shortest float/double formatting and JSON decimal parsing;
//   * write digits directly in the flat arena and use its uncommitted tail for
//     exact bignum workspace instead of stack or heap buffers;
//   * use one exact conversion path on x86-64 and ARM64, without
//     architecture-dependent floating-point shortcuts;
//   * compact the retained implementation into one double_conversion namespace.
//
// The complete upstream license follows.
//
// Copyright 2006-2011, the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

////////////////////////////////////////////////////////////////////////////////
// double-conversion/utils.h (amalgamated)
////////////////////////////////////////////////////////////////////////////////

#define DOUBLE_CONVERSION_ASSERT(condition) JSN_REQUIRE(condition, #condition)
#define DOUBLE_CONVERSION_UNREACHABLE() JSN_PANIC("Unreachable double-conversion path.")

// Keep upstream's split spelling for its 64-bit constants.
#define DOUBLE_CONVERSION_UINT64_2PART_C(a, b) (((static_cast<uint64_t>(a) << 32) + 0x##b##u))

////////////////////////////////////////////////////////////////////////////////
namespace double_conversion {
////////////////////////////////////////////////////////////////////////////////

// The type-based aliasing rule allows the compiler to assume that pointers of
// different types (for some definition of different) never alias each other.
// Thus the following code does not work:
//
// float f = foo();
// int fbits = *(int*)(&f);
//
// The compiler 'knows' that the int pointer can't refer to f since the types
// don't match, so the compiler may cache f in a register, leaving random data
// in fbits.  Using C++ style casts makes no difference, however a pointer to
// char data is assumed to alias any other pointer.  This is the 'memcpy
// exception'.
//
// Bit_cast uses the memcpy exception to move the bits from a variable of one
// type of a variable of another type.  Of course the end result is likely to
// be implementation dependent.  Most compilers (gcc-4.2 and MSVC 2005)
// will completely optimize BitCast away.
//
template<typename Destination, typename Source> Destination BitCast(const Source& source)
{
  // Compile time assertion: sizeof(Destination) == sizeof(Source)
  // A compile error here means your Destination and Source have different sizes.
  static_assert(sizeof(Destination) == sizeof(Source), "source and destination size mismatch");

  Destination destination;
  memmove(&destination, &source, sizeof(destination));
  return destination;
}

////////////////////////////////////////////////////////////////////////////////
// double-conversion/diy-fp.h (amalgamated)
////////////////////////////////////////////////////////////////////////////////

// This "Do It Yourself Floating Point" class implements a floating-point number
// with a uint64 significand and an int exponent. Normalized DiyFp numbers will
// have the most significant bit of the significand set.
// Multiplication does not normalize its result.
// DiyFp store only non-negative numbers and are not designed to contain special
// doubles (NaN and Infinity).
struct DiyFp
{
  static const int SignificandSize = 64;

  DiyFp() : significandValue(0), exponentValue(0)
  {
  }
  DiyFp(const uint64_t inputSignificand, const int32_t inputExponent) : significandValue(inputSignificand), exponentValue(inputExponent)
  {
  }

  // this *= other.
  void Multiply(const DiyFp& other)
  {
    // Simply "emulates" a 128 bit multiplication.
    // However: the resulting number only Contains 64 bits. The least
    // significant 64 bits are only used for rounding the most significant 64
    // bits.
    const uint64_t Mask32 = 0xFFFFFFFFU;
    const uint64_t significandHigh = significandValue >> 32;
    const uint64_t significandLow = significandValue & Mask32;
    const uint64_t otherHigh = other.significandValue >> 32;
    const uint64_t otherLow = other.significandValue & Mask32;
    const uint64_t highProduct = significandHigh * otherHigh;
    const uint64_t lowHighProduct = significandLow * otherHigh;
    const uint64_t highLowProduct = significandHigh * otherLow;
    const uint64_t lowProduct = significandLow * otherLow;
    // By adding 1U << 31 to temporary we round the final result.
    // Halfway cases will be rounded up.
    const uint64_t temporary = (lowProduct >> 32) + (highLowProduct & Mask32) + (lowHighProduct & Mask32) + (1U << 31);
    exponentValue += other.exponentValue + 64;
    significandValue = highProduct + (highLowProduct >> 32) + (lowHighProduct >> 32) + (temporary >> 32);
  }

  void Normalize()
  {
    DOUBLE_CONVERSION_ASSERT(significandValue != 0);
    uint64_t significand = significandValue;
    int32_t exponent = exponentValue;

    // This method is mainly called for normalizing boundaries. In general,
    // boundaries need to be shifted by 10 bits, and we optimize for this case.
    const uint64_t TenMostSignificantBits = DOUBLE_CONVERSION_UINT64_2PART_C(0xFFC00000, 00000000);
    while ((significand & TenMostSignificantBits) == 0) {
      significand <<= 10;
      exponent -= 10;
    }
    while ((significand & Uint64MSB) == 0) {
      significand <<= 1;
      exponent--;
    }
    significandValue = significand;
    exponentValue = exponent;
  }

  uint64_t Significand() const
  {
    return significandValue;
  }
  int32_t Exponent() const
  {
    return exponentValue;
  }

  void SetSignificand(uint64_t newValue)
  {
    significandValue = newValue;
  }
  void SetExponent(int32_t newValue)
  {
    exponentValue = newValue;
  }

  static const uint64_t Uint64MSB = DOUBLE_CONVERSION_UINT64_2PART_C(0x80000000, 00000000);

  uint64_t significandValue;
  int32_t exponentValue;
};

////////////////////////////////////////////////////////////////////////////////
// double-conversion/ieee.h (amalgamated)
////////////////////////////////////////////////////////////////////////////////

// We assume that doubles and uint64_t have the same endianness.
static uint64_t DoubleToUint64(double value) { return BitCast<uint64_t>(value); }
static double Uint64ToDouble(uint64_t bits) { return BitCast<double>(bits); }
static uint32_t FloatToUint32(float value) { return BitCast<uint32_t>(value); }

// Helper functions for doubles.
struct Double
{
  static const uint64_t SignMask = DOUBLE_CONVERSION_UINT64_2PART_C(0x80000000, 00000000);
  static const uint64_t ExponentMask = DOUBLE_CONVERSION_UINT64_2PART_C(0x7FF00000, 00000000);
  static const uint64_t SignificandMask = DOUBLE_CONVERSION_UINT64_2PART_C(0x000FFFFF, FFFFFFFF);
  static const uint64_t HiddenBit = DOUBLE_CONVERSION_UINT64_2PART_C(0x00100000, 00000000);
  static const int PhysicalSignificandSize = 52; // Excludes the hidden bit.
  static const int SignificandSize = 53;
  static const int ExponentBias = 0x3FF + PhysicalSignificandSize;
  static const int MaxExponent = 0x7FF - ExponentBias;

  explicit Double(double value) : bits(DoubleToUint64(value))
  {
  }
  explicit Double(uint64_t inputBits) : bits(inputBits)
  {
  }
  explicit Double(DiyFp diyFp) : bits(DiyFpToUint64(diyFp))
  {
  }

  // Returns the double's bit as uint64.
  uint64_t AsUint64() const
  {
    return bits;
  }

  // Returns the next greater double. Returns +infinity on input +infinity.
  double NextDouble() const
  {
    if (bits == InfinityBits)
      return Double(InfinityBits).Value();
    if (Sign() < 0 && Significand() == 0) {
      // -0.0
      return 0.0;
    }
    if (Sign() < 0) {
      return Double(bits - 1).Value();
    } else {
      return Double(bits + 1).Value();
    }
  }

  int Exponent() const
  {
    if (IsDenormal())
      return DenormalExponent;

    uint64_t d64 = AsUint64();
    int biasedExponent = static_cast<int>((d64 & ExponentMask) >> PhysicalSignificandSize);
    return biasedExponent - ExponentBias;
  }

  uint64_t Significand() const
  {
    uint64_t d64 = AsUint64();
    uint64_t significand = d64 & SignificandMask;
    if (!IsDenormal()) {
      return significand + HiddenBit;
    } else {
      return significand;
    }
  }

  // Returns true if the double is a denormal.
  bool IsDenormal() const
  {
    uint64_t d64 = AsUint64();
    return (d64 & ExponentMask) == 0;
  }

  // We consider denormals not to be special.
  // Hence only Infinity and NaN are special.
  bool IsSpecial() const
  {
    uint64_t d64 = AsUint64();
    return (d64 & ExponentMask) == ExponentMask;
  }

  bool IsNan() const
  {
    uint64_t d64 = AsUint64();
    return ((d64 & ExponentMask) == ExponentMask) && ((d64 & SignificandMask) != 0);
  }

  bool IsInfinite() const
  {
    uint64_t d64 = AsUint64();
    return ((d64 & ExponentMask) == ExponentMask) && ((d64 & SignificandMask) == 0);
  }

  int Sign() const
  {
    uint64_t d64 = AsUint64();
    return (d64 & SignMask) == 0 ? 1 : -1;
  }

  // Precondition: the value encoded by this Double must be greater or equal
  // than +0.0.
  DiyFp UpperBoundary() const
  {
    DOUBLE_CONVERSION_ASSERT(Sign() > 0);
    return DiyFp(Significand() * 2 + 1, Exponent() - 1);
  }

  bool LowerBoundaryIsCloser() const
  {
    // The boundary is closer if the significand is of the form f == 2^p-1 then
    // the lower boundary is closer.
    // Think of v = 1000e10 and v- = 9999e9.
    // Then the boundary (== (v - v-)/2) is not just at a distance of 1e9 but
    // at a distance of 1e8.
    // The only exception is for the smallest normal: the largest denormal is
    // at the same distance as its successor.
    // Note: denormals have the same exponent as the smallest normals.
    bool physicalSignificandIsZero = ((AsUint64() & SignificandMask) == 0);
    return physicalSignificandIsZero && (Exponent() != DenormalExponent);
  }

  double Value() const
  {
    return Uint64ToDouble(bits);
  }

  // Returns the significand size for a given order of magnitude.
  // If v = f*2^e with 2^p-1 <= f <= 2^p then p+e is v's order of magnitude.
  // This function returns the number of significant binary digits v will have
  // once it's encoded into a double. In almost all cases this is equal to
  // SignificandSize. The only exceptions are denormals. They start with
  // leading zeroes and their effective significand-size is hence smaller.
  static int SignificandSizeForOrderOfMagnitude(int order)
  {
    if (order >= (DenormalExponent + SignificandSize)) {
      return SignificandSize;
    }
    if (order <= DenormalExponent)
      return 0;
    return order - DenormalExponent;
  }

  static double Infinity()
  {
    return Double(InfinityBits).Value();
  }

  static const int DenormalExponent = -ExponentBias + 1;
  static const uint64_t InfinityBits = DOUBLE_CONVERSION_UINT64_2PART_C(0x7FF00000, 00000000);
  const uint64_t bits;

  static uint64_t DiyFpToUint64(DiyFp diyFp)
  {
    uint64_t significand = diyFp.Significand();
    int exponent = diyFp.Exponent();
    while (significand > HiddenBit + SignificandMask) {
      significand >>= 1;
      exponent++;
    }
    if (exponent >= MaxExponent) {
      return InfinityBits;
    }
    if (exponent < DenormalExponent) {
      return 0;
    }
    while (exponent > DenormalExponent && (significand & HiddenBit) == 0) {
      significand <<= 1;
      exponent--;
    }
    uint64_t biasedExponent;
    if (exponent == DenormalExponent && (significand & HiddenBit) == 0) {
      biasedExponent = 0;
    } else {
      biasedExponent = static_cast<uint64_t>(exponent + ExponentBias);
    }
    return (significand & SignificandMask) | (biasedExponent << PhysicalSignificandSize);
  }

  Double(const Double&) = delete;
  Double& operator=(const Double&) = delete;
};

struct Single
{
  static const uint32_t ExponentMask = 0x7F800000;
  static const uint32_t SignificandMask = 0x007FFFFF;
  static const uint32_t HiddenBit = 0x00800000;
  static const int PhysicalSignificandSize = 23; // Excludes the hidden bit.
  static const int SignificandSize = 24;

  explicit Single(float value) : bits(FloatToUint32(value))
  {
  }

  // Returns the single's bit as uint64.
  uint32_t AsUint32() const
  {
    return bits;
  }

  int Exponent() const
  {
    if (IsDenormal())
      return DenormalExponent;

    uint32_t d32 = AsUint32();
    int biasedExponent = static_cast<int>((d32 & ExponentMask) >> PhysicalSignificandSize);
    return biasedExponent - ExponentBias;
  }

  uint32_t Significand() const
  {
    uint32_t d32 = AsUint32();
    uint32_t significand = d32 & SignificandMask;
    if (!IsDenormal()) {
      return significand + HiddenBit;
    } else {
      return significand;
    }
  }

  // Returns true if the single is a denormal.
  bool IsDenormal() const
  {
    uint32_t d32 = AsUint32();
    return (d32 & ExponentMask) == 0;
  }

  bool LowerBoundaryIsCloser() const
  {
    // The boundary is closer if the significand is of the form f == 2^p-1 then
    // the lower boundary is closer.
    // Think of v = 1000e10 and v- = 9999e9.
    // Then the boundary (== (v - v-)/2) is not just at a distance of 1e9 but
    // at a distance of 1e8.
    // The only exception is for the smallest normal: the largest denormal is
    // at the same distance as its successor.
    // Note: denormals have the same exponent as the smallest normals.
    bool physicalSignificandIsZero = ((AsUint32() & SignificandMask) == 0);
    return physicalSignificandIsZero && (Exponent() != DenormalExponent);
  }

  static const int ExponentBias = 0x7F + PhysicalSignificandSize;
  static const int DenormalExponent = -ExponentBias + 1;

  const uint32_t bits;

  Single(const Single&) = delete;
  Single& operator=(const Single&) = delete;
};

////////////////////////////////////////////////////////////////////////////////
// double-conversion/bignum.h (amalgamated)
////////////////////////////////////////////////////////////////////////////////

struct Bignum
{
  using Chunk = uint32_t;
  using DoubleChunk = uint64_t;

  // 3584 = 128 * 28. We can represent 2^3584 > 10^1000 accurately.
  // This bignum can encode much bigger numbers, since it Contains an
  // exponent.
  static const int MaxSignificantBits = 3584;

  static const int BigitSize = 28;
  static const int BigitCapacity = MaxSignificantBits / BigitSize;

  explicit Bignum(Chunk* pStorage) : usedBigits(0), bigitExponent(0), pBigitsBuffer(pStorage)
  {
  }

  void AssignUInt16(const uint16_t value);
  void AssignUInt64(uint64_t value);
  void AssignBignum(const Bignum& other);

  void AssignDecimalString(std::span<const char> value);

  void AssignPowerUInt16(uint16_t base, const int exponent);

  void AddUInt64(const uint64_t operand);
  // Precondition: this >= other.
  void SubtractBignum(const Bignum& other);

  void Square();
  void ShiftLeft(const int shiftAmount);
  void MultiplyByUInt32(const uint32_t factor);
  void MultiplyByUInt64(const uint64_t factor);
  void MultiplyByPowerOfTen(const int exponent);
  void Times10()
  {
    return MultiplyByUInt32(10);
  }
  // Pseudocode:
  //  int result = this / other;
  //  this = this % other;
  // In the worst case this function is in O(this/other).
  uint16_t DivideModuloIntBignum(const Bignum& other);

  // Returns
  //  -1 if a < b,
  //   0 if a == b, and
  //  +1 if a > b.
  static int Compare(const Bignum& a, const Bignum& b);
  static bool Equal(const Bignum& a, const Bignum& b)
  {
    return Compare(a, b) == 0;
  }
  static bool LessEqual(const Bignum& a, const Bignum& b)
  {
    return Compare(a, b) <= 0;
  }
  static bool Less(const Bignum& a, const Bignum& b)
  {
    return Compare(a, b) < 0;
  }
  // Returns Compare(a + b, c);
  static int PlusCompare(const Bignum& a, const Bignum& b, const Bignum& c);
  static const int ChunkSize = sizeof(Chunk) * 8;
  static const int DoubleChunkSize = sizeof(DoubleChunk) * 8;
  // With bigit size of 28 we loose some bits, but a double still fits easily
  // into two chunks, and more importantly we can use the Comba multiplication.
  static const Chunk BigitMask = (1 << BigitSize) - 1;
  // Storage is supplied by the flat arena. Bignums cannot grow beyond it.

  static void EnsureCapacity(const int size)
  {
    if (size > BigitCapacity) {
      DOUBLE_CONVERSION_UNREACHABLE();
    }
  }
  void Align(const Bignum& other);
  void Clamp();
  bool IsClamped() const
  {
    return usedBigits == 0 || RawBigit(usedBigits - 1) != 0;
  }
  void Zero()
  {
    usedBigits = 0;
    bigitExponent = 0;
  }
  // Requires this to have enough capacity (no tests done).
  // Updates usedBigits if necessary.
  // shiftAmount must be < BigitSize.
  void BigitsShiftLeft(const int shiftAmount);
  // BigitLength includes the "hidden" bigits encoded in the exponent.
  int BigitLength() const
  {
    return usedBigits + bigitExponent;
  }
  Chunk& RawBigit(const int index);
  const Chunk& RawBigit(const int index) const;
  Chunk BigitOrZero(const int index) const;
  void SubtractTimes(const Bignum& other, const int factor);

  // The Bignum's value is value(pBigitsBuffer) * 2^(bigitExponent * BigitSize),
  // where the value of the buffer consists of the lower BigitSize bits of
  // the first usedBigits Chunks in pBigitsBuffer, first chunk has lowest
  // significant bits.
  int16_t usedBigits;
  int16_t bigitExponent;
  Chunk* pBigitsBuffer;

  Bignum(const Bignum&) = delete;
  Bignum& operator=(const Bignum&) = delete;
};

////////////////////////////////////////////////////////////////////////////////
// double-conversion/bignum-dtoa.h (amalgamated)
////////////////////////////////////////////////////////////////////////////////

// Converts the given double 'v' to ascii.
// The result should be interpreted as buffer * 10^(point-length).
// The buffer will be null-terminated.
//
// The input v must be > 0 and different from NaN, and Infinity.
//
// Produces the least amount of digits for which the internal
//   identity requirement is still satisfied. If the digits are printed
//   (together with the correct exponent) then reading this number will give
//   'v' again. The buffer will choose the representation that is closest to
//   'v'. If there are two at the same distance, than the number is round up.
// 'BignumDtoa' expects the given buffer to be big enough to hold all digits
// and a terminating null-character.
void BignumDtoa(double value, bool single, Bignum::Chunk* pWorkspace, std::span<char> buffer, int* pLength, int* pDecimalPoint);

////////////////////////////////////////////////////////////////////////////////
// double-conversion/cached-powers.h (amalgamated)
////////////////////////////////////////////////////////////////////////////////

// Not all powers of ten are cached. Neighboring decimal exponents differ by this distance.
static const int DecimalExponentDistance = 8;
static const int MinDecimalExponent = -348;
static const int MaxDecimalExponent = 340;

void GetCachedPowerForDecimalExponent(int requestedExponent, DiyFp* pPower, int* pFoundExponent);

////////////////////////////////////////////////////////////////////////////////
// double-conversion/strtod.h (amalgamated)
////////////////////////////////////////////////////////////////////////////////

// Converts the parser's already-trimmed arena digit span.
double StrtodTrimmed(std::span<const char> trimmed, int exponent, Bignum::Chunk* pWorkspace);

////////////////////////////////////////////////////////////////////////////////
// double-conversion/bignum.cc (amalgamated)
////////////////////////////////////////////////////////////////////////////////
Bignum::Chunk& Bignum::RawBigit(const int index)
{
  DOUBLE_CONVERSION_ASSERT(static_cast<unsigned>(index) < BigitCapacity);
  return pBigitsBuffer[index];
}

const Bignum::Chunk& Bignum::RawBigit(const int index) const
{
  DOUBLE_CONVERSION_ASSERT(static_cast<unsigned>(index) < BigitCapacity);
  return pBigitsBuffer[index];
}

template<typename S> static int BitSize(const S value)
{
  (void)value; // Mark variable as used.
  return 8 * sizeof(value);
}

// Guaranteed to lie in one Bigit.
void Bignum::AssignUInt16(const uint16_t value)
{
  DOUBLE_CONVERSION_ASSERT(BigitSize >= BitSize(value));
  Zero();
  if (value > 0) {
    RawBigit(0) = value;
    usedBigits = 1;
  }
}

void Bignum::AssignUInt64(uint64_t value)
{
  Zero();
  for (int i = 0; value > 0; ++i) {
    RawBigit(i) = value & BigitMask;
    value >>= BigitSize;
    ++usedBigits;
  }
}

void Bignum::AssignBignum(const Bignum& other)
{
  bigitExponent = other.bigitExponent;
  for (int i = 0; i < other.usedBigits; ++i) {
    RawBigit(i) = other.RawBigit(i);
  }
  usedBigits = other.usedBigits;
}

static uint64_t ReadUInt64(std::span<const char> buffer, const int from, const int digitsToRead)
{
  uint64_t result = 0;
  for (int i = from; i < from + digitsToRead; ++i) {
    const int digit = buffer[i] - '0';
    DOUBLE_CONVERSION_ASSERT(0 <= digit && digit <= 9);
    result = result * 10 + digit;
  }
  return result;
}

void Bignum::AssignDecimalString(std::span<const char> value)
{
  // 2^64 = 18446744073709551616 > 10^19
  static const int MaxUint64DecimalDigits = 19;
  Zero();
  int length = (int)value.size();
  unsigned pos = 0;
  // Let's just say that each digit needs 4 bits.
  while (length >= MaxUint64DecimalDigits) {
    const uint64_t digits = ReadUInt64(value, pos, MaxUint64DecimalDigits);
    pos += MaxUint64DecimalDigits;
    length -= MaxUint64DecimalDigits;
    MultiplyByPowerOfTen(MaxUint64DecimalDigits);
    AddUInt64(digits);
  }
  const uint64_t digits = ReadUInt64(value, pos, length);
  MultiplyByPowerOfTen(length);
  AddUInt64(digits);
  Clamp();
}

void Bignum::AddUInt64(const uint64_t operand)
{
  if (operand == 0)
    return;
  DOUBLE_CONVERSION_ASSERT(bigitExponent == 0);
  uint64_t carry = operand;
  int index = 0;
  while (carry) {
    EnsureCapacity(index + 1);
    if (index == usedBigits)
      RawBigit(usedBigits++) = 0;
    uint64_t sum = RawBigit(index) + (carry & BigitMask);
    RawBigit(index) = static_cast<Chunk>(sum & BigitMask);
    carry = (carry >> BigitSize) + (sum >> BigitSize);
    ++index;
  }
}

void Bignum::SubtractBignum(const Bignum& other)
{
  DOUBLE_CONVERSION_ASSERT(IsClamped());
  DOUBLE_CONVERSION_ASSERT(other.IsClamped());
  // We require this to be bigger than other.
  DOUBLE_CONVERSION_ASSERT(LessEqual(other, *this));

  Align(other);

  const int offset = other.bigitExponent - bigitExponent;
  Chunk borrow = 0;
  int i;
  for (i = 0; i < other.usedBigits; ++i) {
    DOUBLE_CONVERSION_ASSERT((borrow == 0) || (borrow == 1));
    const Chunk difference = RawBigit(i + offset) - other.RawBigit(i) - borrow;
    RawBigit(i + offset) = difference & BigitMask;
    borrow = difference >> (ChunkSize - 1);
  }
  while (borrow != 0) {
    const Chunk difference = RawBigit(i + offset) - borrow;
    RawBigit(i + offset) = difference & BigitMask;
    borrow = difference >> (ChunkSize - 1);
    ++i;
  }
  Clamp();
}

void Bignum::ShiftLeft(const int shiftAmount)
{
  if (usedBigits == 0) {
    return;
  }
  bigitExponent += static_cast<int16_t>(shiftAmount / BigitSize);
  const int localShift = shiftAmount % BigitSize;
  EnsureCapacity(usedBigits + 1);
  BigitsShiftLeft(localShift);
}

void Bignum::MultiplyByUInt32(const uint32_t factor)
{
  if (factor == 1) {
    return;
  }
  if (factor == 0) {
    Zero();
    return;
  }
  if (usedBigits == 0) {
    return;
  }
  // The product of a bigit with the factor is of size BigitSize + 32.
  // Assert that this number + 1 (for the carry) fits into double chunk.
  DOUBLE_CONVERSION_ASSERT(DoubleChunkSize >= BigitSize + 32 + 1);
  DoubleChunk carry = 0;
  for (int i = 0; i < usedBigits; ++i) {
    const DoubleChunk product = static_cast<DoubleChunk>(factor) * RawBigit(i) + carry;
    RawBigit(i) = static_cast<Chunk>(product & BigitMask);
    carry = (product >> BigitSize);
  }
  while (carry != 0) {
    EnsureCapacity(usedBigits + 1);
    RawBigit(usedBigits) = carry & BigitMask;
    usedBigits++;
    carry >>= BigitSize;
  }
}

void Bignum::MultiplyByUInt64(const uint64_t factor)
{
  if (factor == 1) {
    return;
  }
  if (factor == 0) {
    Zero();
    return;
  }
  if (usedBigits == 0) {
    return;
  }
  DOUBLE_CONVERSION_ASSERT(BigitSize < 32);
  uint64_t carry = 0;
  const uint64_t low = factor & 0xFFFFFFFF;
  const uint64_t high = factor >> 32;
  for (int i = 0; i < usedBigits; ++i) {
    const uint64_t productLow = low * RawBigit(i);
    const uint64_t productHigh = high * RawBigit(i);
    const uint64_t tmp = (carry & BigitMask) + productLow;
    RawBigit(i) = tmp & BigitMask;
    carry = (carry >> BigitSize) + (tmp >> BigitSize) + (productHigh << (32 - BigitSize));
  }
  while (carry != 0) {
    EnsureCapacity(usedBigits + 1);
    RawBigit(usedBigits) = carry & BigitMask;
    usedBigits++;
    carry >>= BigitSize;
  }
}

void Bignum::MultiplyByPowerOfTen(const int exponent)
{
  static const uint64_t Five27 = DOUBLE_CONVERSION_UINT64_2PART_C(0x6765c793, fa10079d);
  static const uint16_t Five1 = 5;
  static const uint16_t Five2 = Five1 * 5;
  static const uint16_t Five3 = Five2 * 5;
  static const uint16_t Five4 = Five3 * 5;
  static const uint16_t Five5 = Five4 * 5;
  static const uint16_t Five6 = Five5 * 5;
  static const uint32_t Five7 = Five6 * 5;
  static const uint32_t Five8 = Five7 * 5;
  static const uint32_t Five9 = Five8 * 5;
  static const uint32_t Five10 = Five9 * 5;
  static const uint32_t Five11 = Five10 * 5;
  static const uint32_t Five12 = Five11 * 5;
  static const uint32_t Five13 = Five12 * 5;
  static const uint32_t kFive1_to_12[] = {Five1, Five2, Five3, Five4, Five5, Five6, Five7, Five8, Five9, Five10, Five11, Five12};

  DOUBLE_CONVERSION_ASSERT(exponent >= 0);

  if (exponent == 0) {
    return;
  }
  if (usedBigits == 0) {
    return;
  }
  // We shift by exponent at the end just before returning.
  int remainingExponent = exponent;
  while (remainingExponent >= 27) {
    MultiplyByUInt64(Five27);
    remainingExponent -= 27;
  }
  while (remainingExponent >= 13) {
    MultiplyByUInt32(Five13);
    remainingExponent -= 13;
  }
  if (remainingExponent > 0) {
    MultiplyByUInt32(kFive1_to_12[remainingExponent - 1]);
  }
  ShiftLeft(exponent);
}

void Bignum::Square()
{
  DOUBLE_CONVERSION_ASSERT(IsClamped());
  const int productLength = 2 * usedBigits;
  EnsureCapacity(productLength);

  // Comba multiplication: compute each column separately.
  // Example: r = a2a1a0 * b2b1b0.
  //    r =  1    * a0b0 +
  //        10    * (a1b0 + a0b1) +
  //        100   * (a2b0 + a1b1 + a0b2) +
  //        1000  * (a2b1 + a1b2) +
  //        10000 * a2b2
  //
  // In the worst case we have to accumulate nb-digits products of digit*digit.
  //
  // Assert that the additional number of bits in a DoubleChunk are enough to
  // sum up usedDigits of Bigit*Bigit.
  JSN_REQUIRE((1 << (2 * (ChunkSize - BigitSize))) > usedBigits,
              "double-conversion bignum multiplication overflow.");
  DoubleChunk accumulator = 0;
  // First shift the digits so we don't overwrite them.
  const int copyOffset = usedBigits;
  for (int i = 0; i < usedBigits; ++i) {
    RawBigit(copyOffset + i) = RawBigit(i);
  }
  // We have two loops to avoid some 'if's in the loop.
  for (int i = 0; i < usedBigits; ++i) {
    // Process temporary digit i with power i.
    // The sum of the two indices must be equal to i.
    int bigitIndex1 = i;
    int bigitIndex2 = 0;
    // Sum all of the sub-products.
    while (bigitIndex1 >= 0) {
      const Chunk chunk1 = RawBigit(copyOffset + bigitIndex1);
      const Chunk chunk2 = RawBigit(copyOffset + bigitIndex2);
      accumulator += static_cast<DoubleChunk>(chunk1) * chunk2;
      bigitIndex1--;
      bigitIndex2++;
    }
    RawBigit(i) = static_cast<Chunk>(accumulator) & BigitMask;
    accumulator >>= BigitSize;
  }
  for (int i = usedBigits; i < productLength; ++i) {
    int bigitIndex1 = usedBigits - 1;
    int bigitIndex2 = i - bigitIndex1;
    // Invariant: sum of both indices is again equal to i.
    // Inner loop runs 0 times on last iteration, emptying accumulator.
    while (bigitIndex2 < usedBigits) {
      const Chunk chunk1 = RawBigit(copyOffset + bigitIndex1);
      const Chunk chunk2 = RawBigit(copyOffset + bigitIndex2);
      accumulator += static_cast<DoubleChunk>(chunk1) * chunk2;
      bigitIndex1--;
      bigitIndex2++;
    }
    // The overwritten RawBigit(i) will never be read in further loop iterations,
    // because bigitIndex1 and bigitIndex2 are always greater
    // than i - usedBigits.
    RawBigit(i) = static_cast<Chunk>(accumulator) & BigitMask;
    accumulator >>= BigitSize;
  }
  // Since the result was guaranteed to lie inside the number the
  // accumulator must be 0 now.
  DOUBLE_CONVERSION_ASSERT(accumulator == 0);

  // Don't forget to update the usedDigits and the exponent.
  usedBigits = static_cast<int16_t>(productLength);
  bigitExponent *= 2;
  Clamp();
}

void Bignum::AssignPowerUInt16(uint16_t base, const int powerExponent)
{
  DOUBLE_CONVERSION_ASSERT(base != 0);
  DOUBLE_CONVERSION_ASSERT(powerExponent >= 0);
  if (powerExponent == 0) {
    AssignUInt16(1);
    return;
  }
  Zero();
  int shifts = 0;
  // We expect base to be in range 2-32, and most often to be 10.
  // It does not make much sense to implement different algorithms for counting
  // the bits.
  while ((base & 1) == 0) {
    base >>= 1;
    shifts++;
  }
  int bitSize = 0;
  int temporaryBase = base;
  while (temporaryBase != 0) {
    temporaryBase >>= 1;
    bitSize++;
  }
  const int finalSize = bitSize * powerExponent;
  // 1 extra bigit for the shifting, and one for rounded finalSize.
  EnsureCapacity(finalSize / BigitSize + 2);

  // Left to Right exponentiation.
  int mask = 1;
  while (powerExponent >= mask)
    mask <<= 1;

  // The mask is now pointing to the bit above the most significant 1-bit of
  // powerExponent.
  // Get rid of first 1-bit;
  mask >>= 2;
  uint64_t thisValue = base;

  bool delayedMultiplication = false;
  const uint64_t max32Bits = 0xFFFFFFFF;
  while (mask != 0 && thisValue <= max32Bits) {
    thisValue = thisValue * thisValue;
    // Verify that there is enough space in thisValue to perform the
    // multiplication.  The first bitSize bits must be 0.
    if ((powerExponent & mask) != 0) {
      DOUBLE_CONVERSION_ASSERT(bitSize > 0);
      const uint64_t baseBitsMask = ~((static_cast<uint64_t>(1) << (64 - bitSize)) - 1);
      const bool highBitsZero = (thisValue & baseBitsMask) == 0;
      if (highBitsZero) {
        thisValue *= base;
      } else {
        delayedMultiplication = true;
      }
    }
    mask >>= 1;
  }
  AssignUInt64(thisValue);
  if (delayedMultiplication) {
    MultiplyByUInt32(base);
  }

  // Now do the same thing as a bignum.
  while (mask != 0) {
    Square();
    if ((powerExponent & mask) != 0) {
      MultiplyByUInt32(base);
    }
    mask >>= 1;
  }

  // And finally add the saved shifts.
  ShiftLeft(shifts * powerExponent);
}

// Precondition: this/other < 16bit.
uint16_t Bignum::DivideModuloIntBignum(const Bignum& other)
{
  DOUBLE_CONVERSION_ASSERT(IsClamped());
  DOUBLE_CONVERSION_ASSERT(other.IsClamped());
  DOUBLE_CONVERSION_ASSERT(other.usedBigits > 0);

  // Easy case: if we have less digits than the divisor than the result is 0.
  // Note: this handles the case where this == 0, too.
  if (BigitLength() < other.BigitLength()) {
    return 0;
  }

  Align(other);

  uint16_t result = 0;

  // Start by removing multiples of 'other' until both numbers have the same
  // number of digits.
  while (BigitLength() > other.BigitLength()) {
    // This naive approach is extremely inefficient if `this` divided by other
    // is big. This function is implemented for doubleToString where
    // the result should be small (less than 10).
    DOUBLE_CONVERSION_ASSERT(other.RawBigit(other.usedBigits - 1) >= ((1 << BigitSize) / 16));
    DOUBLE_CONVERSION_ASSERT(RawBigit(usedBigits - 1) < 0x10000);
    // Remove the multiples of the first digit.
    // Example this = 23 and other equals 9. -> Remove 2 multiples.
    result += static_cast<uint16_t>(RawBigit(usedBigits - 1));
    SubtractTimes(other, RawBigit(usedBigits - 1));
  }

  DOUBLE_CONVERSION_ASSERT(BigitLength() == other.BigitLength());

  // Both bignums are at the same length now.
  // Since other has more than 0 digits we know that the access to
  // RawBigit(usedBigits - 1) is safe.
  const Chunk thisBigit = RawBigit(usedBigits - 1);
  const Chunk otherBigit = other.RawBigit(other.usedBigits - 1);

  if (other.usedBigits == 1) {
    // Shortcut for easy (and common) case.
    int quotient = thisBigit / otherBigit;
    RawBigit(usedBigits - 1) = thisBigit - otherBigit * quotient;
    DOUBLE_CONVERSION_ASSERT(quotient < 0x10000);
    result += static_cast<uint16_t>(quotient);
    Clamp();
    return result;
  }

  const int divisionEstimate = thisBigit / (otherBigit + 1);
  DOUBLE_CONVERSION_ASSERT(divisionEstimate < 0x10000);
  result += static_cast<uint16_t>(divisionEstimate);
  SubtractTimes(other, divisionEstimate);

  if (otherBigit * (divisionEstimate + 1) > thisBigit) {
    // No need to even try to subtract. Even if other's remaining digits were 0
    // another subtraction would be too much.
    return result;
  }

  while (LessEqual(other, *this)) {
    SubtractBignum(other);
    result++;
  }
  return result;
}

Bignum::Chunk Bignum::BigitOrZero(const int index) const
{
  if (index >= BigitLength()) {
    return 0;
  }
  if (index < bigitExponent) {
    return 0;
  }
  return RawBigit(index - bigitExponent);
}

int Bignum::Compare(const Bignum& a, const Bignum& b)
{
  DOUBLE_CONVERSION_ASSERT(a.IsClamped());
  DOUBLE_CONVERSION_ASSERT(b.IsClamped());
  const int bigitLengthA = a.BigitLength();
  const int bigitLengthB = b.BigitLength();
  if (bigitLengthA < bigitLengthB) {
    return -1;
  }
  if (bigitLengthA > bigitLengthB) {
    return +1;
  }
  for (int i = bigitLengthA - 1; i >= (std::min)(a.bigitExponent, b.bigitExponent); --i) {
    const Chunk bigitA = a.BigitOrZero(i);
    const Chunk bigitB = b.BigitOrZero(i);
    if (bigitA < bigitB) {
      return -1;
    }
    if (bigitA > bigitB) {
      return +1;
    }
    // Otherwise they are equal up to this digit. Try the next digit.
  }
  return 0;
}

int Bignum::PlusCompare(const Bignum& a, const Bignum& b, const Bignum& c)
{
  DOUBLE_CONVERSION_ASSERT(a.IsClamped());
  DOUBLE_CONVERSION_ASSERT(b.IsClamped());
  DOUBLE_CONVERSION_ASSERT(c.IsClamped());
  if (a.BigitLength() < b.BigitLength()) {
    return PlusCompare(b, a, c);
  }
  if (a.BigitLength() + 1 < c.BigitLength()) {
    return -1;
  }
  if (a.BigitLength() > c.BigitLength()) {
    return +1;
  }
  // The exponent encodes 0-bigits. So if there are more 0-digits in 'a' than
  // 'b' has digits, then the bigit-length of 'a'+'b' must be equal to the one
  // of 'a'.
  if (a.bigitExponent >= b.BigitLength() && a.BigitLength() < c.BigitLength()) {
    return -1;
  }

  Chunk borrow = 0;
  // Starting at minExponent all digits are == 0. So no need to compare them.
  const int minExponent = (std::min)((std::min)(a.bigitExponent, b.bigitExponent), c.bigitExponent);
  for (int i = c.BigitLength() - 1; i >= minExponent; --i) {
    const Chunk chunkA = a.BigitOrZero(i);
    const Chunk chunkB = b.BigitOrZero(i);
    const Chunk chunkC = c.BigitOrZero(i);
    const Chunk sum = chunkA + chunkB;
    if (sum > chunkC + borrow) {
      return +1;
    } else {
      borrow = chunkC + borrow - sum;
      if (borrow > 1) {
        return -1;
      }
      borrow <<= BigitSize;
    }
  }
  if (borrow == 0) {
    return 0;
  }
  return -1;
}

void Bignum::Clamp()
{
  while (usedBigits > 0 && RawBigit(usedBigits - 1) == 0) {
    usedBigits--;
  }
  if (usedBigits == 0) {
    // Zero.
    bigitExponent = 0;
  }
}

void Bignum::Align(const Bignum& other)
{
  if (bigitExponent > other.bigitExponent) {
    // If "X" represents a "hidden" bigit (by the exponent) then we are in the
    // following case (a == this, b == other):
    // a:  aaaaaaXXXX   or a:   aaaaaXXX
    // b:     bbbbbbX      b: bbbbbbbbXX
    // We replace some of the hidden digits (X) of a with 0 digits.
    // a:  aaaaaa000X   or a:   aaaaa0XX
    const int zeroBigits = bigitExponent - other.bigitExponent;
    EnsureCapacity(usedBigits + zeroBigits);
    for (int i = usedBigits - 1; i >= 0; --i) {
      RawBigit(i + zeroBigits) = RawBigit(i);
    }
    for (int i = 0; i < zeroBigits; ++i) {
      RawBigit(i) = 0;
    }
    usedBigits += static_cast<int16_t>(zeroBigits);
    bigitExponent -= static_cast<int16_t>(zeroBigits);

    DOUBLE_CONVERSION_ASSERT(usedBigits >= 0);
    DOUBLE_CONVERSION_ASSERT(bigitExponent >= 0);
  }
}

void Bignum::BigitsShiftLeft(const int shiftAmount)
{
  DOUBLE_CONVERSION_ASSERT(shiftAmount < BigitSize);
  DOUBLE_CONVERSION_ASSERT(shiftAmount >= 0);
  Chunk carry = 0;
  for (int i = 0; i < usedBigits; ++i) {
    const Chunk newCarry = RawBigit(i) >> (BigitSize - shiftAmount);
    RawBigit(i) = ((RawBigit(i) << shiftAmount) + carry) & BigitMask;
    carry = newCarry;
  }
  if (carry != 0) {
    RawBigit(usedBigits) = carry;
    usedBigits++;
  }
}

void Bignum::SubtractTimes(const Bignum& other, const int factor)
{
  DOUBLE_CONVERSION_ASSERT(bigitExponent <= other.bigitExponent);
  if (factor < 3) {
    for (int i = 0; i < factor; ++i) {
      SubtractBignum(other);
    }
    return;
  }
  Chunk borrow = 0;
  const int exponentDifference = other.bigitExponent - bigitExponent;
  for (int i = 0; i < other.usedBigits; ++i) {
    const DoubleChunk product = static_cast<DoubleChunk>(factor) * other.RawBigit(i);
    const DoubleChunk remove = borrow + product;
    const Chunk difference = RawBigit(i + exponentDifference) - (remove & BigitMask);
    RawBigit(i + exponentDifference) = difference & BigitMask;
    borrow = static_cast<Chunk>((difference >> (ChunkSize - 1)) + (remove >> BigitSize));
  }
  for (int i = other.usedBigits + exponentDifference; i < usedBigits; ++i) {
    if (borrow == 0) {
      return;
    }
    const Chunk difference = RawBigit(i) - borrow;
    RawBigit(i) = difference & BigitMask;
    borrow = difference >> (ChunkSize - 1);
  }
  Clamp();
}

////////////////////////////////////////////////////////////////////////////////
// double-conversion/bignum-dtoa.cc (amalgamated)
////////////////////////////////////////////////////////////////////////////////
static int NormalizedExponent(uint64_t significand, int exponent)
{
  DOUBLE_CONVERSION_ASSERT(significand != 0);
  while ((significand & Double::HiddenBit) == 0) {
    significand = significand << 1;
    exponent = exponent - 1;
  }
  return exponent;
}

// Forward declarations:
// Returns an estimation of k such that 10^(k-1) <= v < 10^k.
static int EstimatePower(int exponent);
// Computes v / 10^estimatedPower exactly, as a ratio of two bignums, pNumerator
// and pDenominator.
static void InitialScaledStartValues(uint64_t significand, int exponent, bool lowerBoundaryIsCloser, int estimatedPower, Bignum* pNumerator, Bignum* pDenominator, Bignum* pDeltaMinus,
                                     Bignum* pDeltaPlus);
// Multiplies pNumerator/pDenominator so that its values lies in the range 1-10.
// Returns pDecimalPoint s.t.
//  v = pNumerator'/pDenominator' * 10^(pDecimalPoint-1)
//     where pNumerator' and pDenominator' are the values of pNumerator and
//     pDenominator after the call to this function.
static void FixupMultiply10(int estimatedPower, bool isEven, int* pDecimalPoint, Bignum* pNumerator, Bignum* pDenominator, Bignum* pDeltaMinus, Bignum* pDeltaPlus);
// Generates digits from the left to the right and stops when the generated
// digits yield the shortest decimal representation of v.
static void GenerateShortestDigits(Bignum* pNumerator, Bignum* pDenominator, Bignum* pDeltaMinus, Bignum* pDeltaPlus, bool isEven, std::span<char> buffer, int* pLength);
void BignumDtoa(double value, bool single, Bignum::Chunk* pWorkspace, std::span<char> buffer, int* pLength, int* pDecimalPoint)
{
  DOUBLE_CONVERSION_ASSERT(value > 0);
  DOUBLE_CONVERSION_ASSERT(!Double(value).IsSpecial());
  uint64_t significand;
  int exponent;
  bool lowerBoundaryIsCloser;
  if (single) {
    float floatValue = static_cast<float>(value);
    DOUBLE_CONVERSION_ASSERT(floatValue == value);
    significand = Single(floatValue).Significand();
    exponent = Single(floatValue).Exponent();
    lowerBoundaryIsCloser = Single(floatValue).LowerBoundaryIsCloser();
  } else {
    significand = Double(value).Significand();
    exponent = Double(value).Exponent();
    lowerBoundaryIsCloser = Double(value).LowerBoundaryIsCloser();
  }
  bool isEven = (significand & 1) == 0;
  int normalizedExponent = NormalizedExponent(significand, exponent);
  // estimatedPower might be too low by 1.
  int estimatedPower = EstimatePower(normalizedExponent);

  Bignum numerator(pWorkspace + 0 * Bignum::BigitCapacity);
  Bignum denominator(pWorkspace + 1 * Bignum::BigitCapacity);
  Bignum deltaMinus(pWorkspace + 2 * Bignum::BigitCapacity);
  Bignum deltaPlus(pWorkspace + 3 * Bignum::BigitCapacity);
  // Make sure the bignum can grow large enough. The smallest double equals
  // 4e-324. In this case the pDenominator needs fewer than 324*4 binary digits.
  // The maximum double is 1.7976931348623157e308 which needs fewer than
  // 308*4 binary digits.
  DOUBLE_CONVERSION_ASSERT(Bignum::MaxSignificantBits >= 324 * 4);
  InitialScaledStartValues(significand, exponent, lowerBoundaryIsCloser, estimatedPower, &numerator, &denominator, &deltaMinus, &deltaPlus);
  // We now have v = (pNumerator / pDenominator) * 10^estimatedPower.
  FixupMultiply10(estimatedPower, isEven, pDecimalPoint, &numerator, &denominator, &deltaMinus, &deltaPlus);
  // We now have v = (pNumerator / pDenominator) * 10^(pDecimalPoint-1), and
  //  1 <= (pNumerator + pDeltaPlus) / pDenominator < 10
  GenerateShortestDigits(&numerator, &denominator, &deltaMinus, &deltaPlus, isEven, buffer, pLength);
  buffer[*pLength] = '\0';
}

// The procedure starts generating digits from the left to the right and stops
// when the generated digits yield the shortest decimal representation of v. A
// decimal representation of v is a number lying closer to v than to any other
// double, so it converts to v when read.
//
// This is true if d, the decimal representation, is between m- and m+, the
// upper and lower boundaries. d must be strictly between them if !isEven.
//           m- := (pNumerator - pDeltaMinus) / pDenominator
//           m+ := (pNumerator + pDeltaPlus) / pDenominator
//
// Precondition: 0 <= (pNumerator+pDeltaPlus) / pDenominator < 10.
//   If 1 <= (pNumerator+pDeltaPlus) / pDenominator < 10 then no leading 0 digit
//   will be produced. This should be the standard precondition.
static void GenerateShortestDigits(Bignum* pNumerator, Bignum* pDenominator, Bignum* pDeltaMinus, Bignum* pDeltaPlus, bool isEven, std::span<char> buffer, int* pLength)
{
  // Small optimization: if pDeltaMinus and pDeltaPlus are the same just reuse
  // one of the two bignums.
  if (Bignum::Equal(*pDeltaMinus, *pDeltaPlus)) {
    pDeltaPlus = pDeltaMinus;
  }
  *pLength = 0;
  for (;;) {
    uint16_t digit;
    digit = pNumerator->DivideModuloIntBignum(*pDenominator);
    DOUBLE_CONVERSION_ASSERT(digit <= 9); // digit is a uint16_t and therefore always positive.
    // digit = pNumerator / pDenominator (integer division).
    // pNumerator = pNumerator % pDenominator.
    buffer[(*pLength)++] = static_cast<char>(digit + '0');

    // Can we stop already?
    // If the remainder of the division is less than the distance to the lower
    // boundary we can stop. In this case we simply round down (discarding the
    // remainder).
    // Similarly we test if we can round up (using the upper boundary).
    bool inDeltaRoomMinus;
    bool inDeltaRoomPlus;
    if (isEven) {
      inDeltaRoomMinus = Bignum::LessEqual(*pNumerator, *pDeltaMinus);
    } else {
      inDeltaRoomMinus = Bignum::Less(*pNumerator, *pDeltaMinus);
    }
    if (isEven) {
      inDeltaRoomPlus = Bignum::PlusCompare(*pNumerator, *pDeltaPlus, *pDenominator) >= 0;
    } else {
      inDeltaRoomPlus = Bignum::PlusCompare(*pNumerator, *pDeltaPlus, *pDenominator) > 0;
    }
    if (!inDeltaRoomMinus && !inDeltaRoomPlus) {
      // Prepare for next iteration.
      pNumerator->Times10();
      pDeltaMinus->Times10();
      // We optimized pDeltaPlus to be equal to pDeltaMinus (if they share the
      // same value). So don't multiply pDeltaPlus if they point to the same
      // object.
      if (pDeltaMinus != pDeltaPlus) {
        pDeltaPlus->Times10();
      }
    } else if (inDeltaRoomMinus && inDeltaRoomPlus) {
      // Let's see if 2*pNumerator < pDenominator.
      // If yes, then the next digit would be < 5 and we can round down.
      int compare = Bignum::PlusCompare(*pNumerator, *pNumerator, *pDenominator);
      if (compare < 0) {
        // Remaining digits are less than .5. -> Round down (== do nothing).
      } else if (compare > 0) {
        // Remaining digits are more than .5 of pDenominator. -> Round up.
        // Note that the last digit could not be a '9' as otherwise the whole
        // loop would have stopped earlier.
        // We still have an assert here in case the preconditions were not
        // satisfied.
        DOUBLE_CONVERSION_ASSERT(buffer[(*pLength) - 1] != '9');
        buffer[(*pLength) - 1]++;
      } else {
        // Halfway case.
        // TODO(floitsch): need a way to solve half-way cases.
        //   For now let's round towards even (since this is what Gay seems to
        //   do).

        if ((buffer[(*pLength) - 1] - '0') % 2 == 0) {
          // Round down => Do nothing.
        } else {
          DOUBLE_CONVERSION_ASSERT(buffer[(*pLength) - 1] != '9');
          buffer[(*pLength) - 1]++;
        }
      }
      return;
    } else if (inDeltaRoomMinus) {
      // Round down (== do nothing).
      return;
    } else { // inDeltaRoomPlus
      // Round up.
      // Note again that the last digit could not be '9' since this would have
      // stopped the loop earlier.
      // We still have an DOUBLE_CONVERSION_ASSERT here, in case the preconditions were not
      // satisfied.
      DOUBLE_CONVERSION_ASSERT(buffer[(*pLength) - 1] != '9');
      buffer[(*pLength) - 1]++;
      return;
    }
  }
}

// Let v = pNumerator / pDenominator < 10.
// Then we generate 'count' digits of d = x.xxxxx... (without the decimal point)
// from left to right. Once 'count' digits have been produced we decide whether
// to round up or down. Remainders of exactly .5 round upwards. Numbers such
// as 9.999999 propagate a carry all the way, and change the
// exponent (pDecimalPoint), when rounding upwards.
// Returns an estimation of k such that 10^(k-1) <= v < 10^k where
// v = f * 2^exponent and 2^52 <= f < 2^53.
// v is hence a normalized double with the given exponent. The output is an
// approximation for the exponent of the decimal approximation .digits * 10^k.
//
// The result might undershoot by 1 in which case 10^k <= v < 10^k+1.
// Note: this property holds for v's upper boundary m+ too.
//    10^k <= m+ < 10^k+1.
//   (see explanation below).
//
// Examples:
//  EstimatePower(0)   => 16
//  EstimatePower(-52) => 0
//
// Note: e >= 0 => EstimatedPower(e) > 0. No similar claim can be made for e<0.
static int EstimatePower(int exponent)
{
  // This function estimates log10 of v where v = f*2^e (with e == exponent).
  // Note that 10^floor(log10(v)) <= v, but v <= 10^ceil(log10(v)).
  // Note that f is bounded by its container size. Let p = 53 (the double's
  // significand size). Then 2^(p-1) <= f < 2^p.
  //
  // Given that log10(v) == log2(v)/log2(10) and e+(len(f)-1) is quite close
  // to log2(v) the function is simplified to (e+(len(f)-1)/log2(10)).
  // The computed number undershoots by less than 0.631 (when we compute log3
  // and not log10).
  //
  // Optimization: since we only need an approximated result this computation
  // can be performed on 64 bit integers. On x86/x64 architecture the speedup is
  // not really measurable, though.
  //
  // Since we want to avoid overshooting we decrement by 1e10 so that
  // floating-point imprecisions don't affect us.
  //
  // Explanation for v's boundary m+: the computation takes advantage of
  // the fact that 2^(p-1) <= f < 2^p. Boundaries still satisfy this requirement
  // (even for denormals where the delta can be much more important).

  const double Log10Two = 0.30102999566398114; // 1/lg(10)

  // For doubles len(f) == 53 (don't forget the hidden bit).
  const int SignificandSize = Double::SignificandSize;
  double estimate = ceil((exponent + SignificandSize - 1) * Log10Two - 1e-10);
  return static_cast<int>(estimate);
}

// See comments for InitialScaledStartValues.
static void InitialScaledStartValuesPositiveExponent(uint64_t significand, int exponent, int estimatedPower, Bignum* pNumerator, Bignum* pDenominator, Bignum* pDeltaMinus, Bignum* pDeltaPlus)
{
  // A positive exponent implies a positive power.
  DOUBLE_CONVERSION_ASSERT(estimatedPower >= 0);
  // Since the estimatedPower is positive we simply multiply the pDenominator
  // by 10^estimatedPower.

  // pNumerator = v.
  pNumerator->AssignUInt64(significand);
  pNumerator->ShiftLeft(exponent);
  // pDenominator = 10^estimatedPower.
  pDenominator->AssignPowerUInt16(10, estimatedPower);

  // Introduce a common pDenominator so that the deltas to the boundaries are
  // integers.
  pDenominator->ShiftLeft(1);
  pNumerator->ShiftLeft(1);
  // Let v = f * 2^e, then m+ - v = 1/2 * 2^e; With the common
  // pDenominator (of 2) pDeltaPlus equals 2^e.
  pDeltaPlus->AssignUInt16(1);
  pDeltaPlus->ShiftLeft(exponent);
  // Same for pDeltaMinus. The adjustments if f == 2^p-1 are done later.
  pDeltaMinus->AssignUInt16(1);
  pDeltaMinus->ShiftLeft(exponent);
}

// See comments for InitialScaledStartValues
static void InitialScaledStartValuesNegativeExponentPositivePower(uint64_t significand, int exponent, int estimatedPower, Bignum* pNumerator, Bignum* pDenominator, Bignum* pDeltaMinus,
                                                                  Bignum* pDeltaPlus)
{
  // v = f * 2^e with e < 0, and with estimatedPower >= 0.
  // This means that e is close to 0 (have a look at how estimatedPower is
  // computed).

  // pNumerator = significand
  //  since v = significand * 2^exponent this is equivalent to
  //  pNumerator = v * / 2^-exponent
  pNumerator->AssignUInt64(significand);
  // pDenominator = 10^estimatedPower * 2^-exponent (with exponent < 0)
  pDenominator->AssignPowerUInt16(10, estimatedPower);
  pDenominator->ShiftLeft(-exponent);

  // Introduce a common pDenominator so that the deltas to the boundaries are
  // integers.
  pDenominator->ShiftLeft(1);
  pNumerator->ShiftLeft(1);
  // Given that the pDenominator already includes v's exponent, the distance
  // to each boundary is simply 1.
  pDeltaPlus->AssignUInt16(1);
  pDeltaMinus->AssignUInt16(1);
}

// See comments for InitialScaledStartValues
static void InitialScaledStartValuesNegativeExponentNegativePower(uint64_t significand, int exponent, int estimatedPower, Bignum* pNumerator, Bignum* pDenominator, Bignum* pDeltaMinus,
                                                                  Bignum* pDeltaPlus)
{
  // Instead of multiplying the pDenominator with 10^estimatedPower we
  // multiply all values (pNumerator and deltas) by 10^-estimatedPower.

  // Use pNumerator as temporary container for pPowerTen.
  Bignum* pPowerTen = pNumerator;
  pPowerTen->AssignPowerUInt16(10, -estimatedPower);

  // Since pPowerTen == pNumerator we must make a copy of 10^estimatedPower
  // before we complete the computation of the pNumerator.
  // pDeltaPlus = pDeltaMinus = 10^estimatedPower
  pDeltaPlus->AssignBignum(*pPowerTen);
  pDeltaMinus->AssignBignum(*pPowerTen);

  // pNumerator = significand * 2 * 10^-estimatedPower
  //  since v = significand * 2^exponent this is equivalent to
  // pNumerator = v * 10^-estimatedPower * 2 * 2^-exponent.
  // Remember: pNumerator has been abused as pPowerTen. So no need to assign it
  //  to itself.
  DOUBLE_CONVERSION_ASSERT(pNumerator == pPowerTen);
  pNumerator->MultiplyByUInt64(significand);

  // pDenominator = 2 * 2^-exponent with exponent < 0.
  pDenominator->AssignUInt16(1);
  pDenominator->ShiftLeft(-exponent);

  // Introduce a common pDenominator so that the deltas to the boundaries are
  // integers.
  pNumerator->ShiftLeft(1);
  pDenominator->ShiftLeft(1);
  // The adjustments if f == 2^p-1 (lower boundary is closer) are done later.
}

// Let v = significand * 2^exponent.
// Computes v / 10^estimatedPower exactly, as a ratio of two bignums, pNumerator
// and pDenominator. The functions GenerateShortestDigits and
// GenerateCountedDigits will then convert this ratio to its decimal
// representation d, with the required accuracy.
// Then d * 10^estimatedPower is the representation of v.
// (Note: the fraction and the estimatedPower might get adjusted before
// generating the decimal representation.)
//
// The initial start values consist of:
//  - a scaled pNumerator: s.t. pNumerator/pDenominator == v / 10^estimatedPower.
//  - a scaled (common) pDenominator.
//  optionally (used by GenerateShortestDigits to decide if it has the shortest
//  decimal converting back to v):
//  - v - m-: the distance to the lower boundary.
//  - m+ - v: the distance to the upper boundary.
//
// v, m+, m-, and therefore v - m- and m+ - v all share the same pDenominator.
//
// Let ep == estimatedPower, then the returned values will satisfy:
//  v / 10^ep = pNumerator / pDenominator.
//  v's boundaries m- and m+:
//    m- / 10^ep == v / 10^ep - pDeltaMinus / pDenominator
//    m+ / 10^ep == v / 10^ep + pDeltaPlus / pDenominator
//  Or in other words:
//    m- == v - pDeltaMinus * 10^ep / pDenominator;
//    m+ == v + pDeltaPlus * 10^ep / pDenominator;
//
// Since 10^(k-1) <= v < 10^k    (with k == estimatedPower)
//  or       10^k <= v < 10^(k+1)
//  we then have 0.1 <= pNumerator/pDenominator < 1
//           or    1 <= pNumerator/pDenominator < 10
//
// It is then easy to kickstart the digit-generation routine.
//
// The boundary deltas select the shortest representation that rounds to v.

static void InitialScaledStartValues(uint64_t significand, int exponent, bool lowerBoundaryIsCloser, int estimatedPower, Bignum* pNumerator, Bignum* pDenominator, Bignum* pDeltaMinus,
                                     Bignum* pDeltaPlus)
{
  if (exponent >= 0) {
    InitialScaledStartValuesPositiveExponent(significand, exponent, estimatedPower, pNumerator, pDenominator, pDeltaMinus, pDeltaPlus);
  } else if (estimatedPower >= 0) {
    InitialScaledStartValuesNegativeExponentPositivePower(significand, exponent, estimatedPower, pNumerator, pDenominator, pDeltaMinus, pDeltaPlus);
  } else {
    InitialScaledStartValuesNegativeExponentNegativePower(significand, exponent, estimatedPower, pNumerator, pDenominator, pDeltaMinus, pDeltaPlus);
  }

  if (lowerBoundaryIsCloser) {
    // The lower boundary is closer at half the distance of "normal" numbers.
    // Increase the common pDenominator and adapt all but the pDeltaMinus.
    pDenominator->ShiftLeft(1); // *2
    pNumerator->ShiftLeft(1);   // *2
    pDeltaPlus->ShiftLeft(1);   // *2
  }
}

// This routine multiplies pNumerator/pDenominator so that its values lies in the
// range 1-10. That is after a call to this function we have:
//    1 <= (pNumerator + pDeltaPlus) /pDenominator < 10.
// Let pNumerator the input before modification and pNumerator' the argument
// after modification, then the output-parameter pDecimalPoint is such that
//  pNumerator / pDenominator * 10^estimatedPower ==
//    pNumerator' / pDenominator' * 10^(pDecimalPoint - 1)
// In some cases estimatedPower was too low, and this is already the case. We
// then simply adjust the power so that 10^(k-1) <= v < 10^k (with k ==
// estimatedPower) but do not touch the pNumerator or pDenominator.
// Otherwise the routine multiplies the pNumerator and the deltas by 10.
static void FixupMultiply10(int estimatedPower, bool isEven, int* pDecimalPoint, Bignum* pNumerator, Bignum* pDenominator, Bignum* pDeltaMinus, Bignum* pDeltaPlus)
{
  bool inRange;
  if (isEven) {
    // For IEEE doubles half-way cases (in decimal system numbers ending with 5)
    // are rounded to the closest floating-point number with even significand.
    inRange = Bignum::PlusCompare(*pNumerator, *pDeltaPlus, *pDenominator) >= 0;
  } else {
    inRange = Bignum::PlusCompare(*pNumerator, *pDeltaPlus, *pDenominator) > 0;
  }
  if (inRange) {
    // Since pNumerator + pDeltaPlus >= pDenominator we already have
    // 1 <= pNumerator/pDenominator < 10. Simply update the estimatedPower.
    *pDecimalPoint = estimatedPower + 1;
  } else {
    *pDecimalPoint = estimatedPower;
    pNumerator->Times10();
    if (Bignum::Equal(*pDeltaMinus, *pDeltaPlus)) {
      pDeltaMinus->Times10();
      pDeltaPlus->AssignBignum(*pDeltaMinus);
    } else {
      pDeltaMinus->Times10();
      pDeltaPlus->Times10();
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
// double-conversion/cached-powers.cc (amalgamated)
////////////////////////////////////////////////////////////////////////////////
struct CachedPower
{
  uint64_t significand;
  int16_t binaryExponent;
  int16_t decimalExponent;
};

static const CachedPower CachedPowers[] = {
  {DOUBLE_CONVERSION_UINT64_2PART_C(0xfa8fd5a0, 081c0288), -1220, -348}, {DOUBLE_CONVERSION_UINT64_2PART_C(0xbaaee17f, a23ebf76), -1193, -340},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0x8b16fb20, 3055ac76), -1166, -332}, {DOUBLE_CONVERSION_UINT64_2PART_C(0xcf42894a, 5dce35ea), -1140, -324},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0x9a6bb0aa, 55653b2d), -1113, -316}, {DOUBLE_CONVERSION_UINT64_2PART_C(0xe61acf03, 3d1a45df), -1087, -308},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0xab70fe17, c79ac6ca), -1060, -300}, {DOUBLE_CONVERSION_UINT64_2PART_C(0xff77b1fc, bebcdc4f), -1034, -292},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0xbe5691ef, 416bd60c), -1007, -284}, {DOUBLE_CONVERSION_UINT64_2PART_C(0x8dd01fad, 907ffc3c), -980, -276},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0xd3515c28, 31559a83), -954, -268},  {DOUBLE_CONVERSION_UINT64_2PART_C(0x9d71ac8f, ada6c9b5), -927, -260},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0xea9c2277, 23ee8bcb), -901, -252},  {DOUBLE_CONVERSION_UINT64_2PART_C(0xaecc4991, 4078536d), -874, -244},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0x823c1279, 5db6ce57), -847, -236},  {DOUBLE_CONVERSION_UINT64_2PART_C(0xc2109436, 4dfb5637), -821, -228},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0x9096ea6f, 3848984f), -794, -220},  {DOUBLE_CONVERSION_UINT64_2PART_C(0xd77485cb, 25823ac7), -768, -212},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0xa086cfcd, 97bf97f4), -741, -204},  {DOUBLE_CONVERSION_UINT64_2PART_C(0xef340a98, 172aace5), -715, -196},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0xb23867fb, 2a35b28e), -688, -188},  {DOUBLE_CONVERSION_UINT64_2PART_C(0x84c8d4df, d2c63f3b), -661, -180},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0xc5dd4427, 1ad3cdba), -635, -172},  {DOUBLE_CONVERSION_UINT64_2PART_C(0x936b9fce, bb25c996), -608, -164},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0xdbac6c24, 7d62a584), -582, -156},  {DOUBLE_CONVERSION_UINT64_2PART_C(0xa3ab6658, 0d5fdaf6), -555, -148},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0xf3e2f893, dec3f126), -529, -140},  {DOUBLE_CONVERSION_UINT64_2PART_C(0xb5b5ada8, aaff80b8), -502, -132},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0x87625f05, 6c7c4a8b), -475, -124},  {DOUBLE_CONVERSION_UINT64_2PART_C(0xc9bcff60, 34c13053), -449, -116},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0x964e858c, 91ba2655), -422, -108},  {DOUBLE_CONVERSION_UINT64_2PART_C(0xdff97724, 70297ebd), -396, -100},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0xa6dfbd9f, b8e5b88f), -369, -92},   {DOUBLE_CONVERSION_UINT64_2PART_C(0xf8a95fcf, 88747d94), -343, -84},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0xb9447093, 8fa89bcf), -316, -76},   {DOUBLE_CONVERSION_UINT64_2PART_C(0x8a08f0f8, bf0f156b), -289, -68},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0xcdb02555, 653131b6), -263, -60},   {DOUBLE_CONVERSION_UINT64_2PART_C(0x993fe2c6, d07b7fac), -236, -52},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0xe45c10c4, 2a2b3b06), -210, -44},   {DOUBLE_CONVERSION_UINT64_2PART_C(0xaa242499, 697392d3), -183, -36},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0xfd87b5f2, 8300ca0e), -157, -28},   {DOUBLE_CONVERSION_UINT64_2PART_C(0xbce50864, 92111aeb), -130, -20},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0x8cbccc09, 6f5088cc), -103, -12},   {DOUBLE_CONVERSION_UINT64_2PART_C(0xd1b71758, e219652c), -77, -4},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0x9c400000, 00000000), -50, 4},      {DOUBLE_CONVERSION_UINT64_2PART_C(0xe8d4a510, 00000000), -24, 12},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0xad78ebc5, ac620000), 3, 20},       {DOUBLE_CONVERSION_UINT64_2PART_C(0x813f3978, f8940984), 30, 28},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0xc097ce7b, c90715b3), 56, 36},      {DOUBLE_CONVERSION_UINT64_2PART_C(0x8f7e32ce, 7bea5c70), 83, 44},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0xd5d238a4, abe98068), 109, 52},     {DOUBLE_CONVERSION_UINT64_2PART_C(0x9f4f2726, 179a2245), 136, 60},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0xed63a231, d4c4fb27), 162, 68},     {DOUBLE_CONVERSION_UINT64_2PART_C(0xb0de6538, 8cc8ada8), 189, 76},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0x83c7088e, 1aab65db), 216, 84},     {DOUBLE_CONVERSION_UINT64_2PART_C(0xc45d1df9, 42711d9a), 242, 92},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0x924d692c, a61be758), 269, 100},    {DOUBLE_CONVERSION_UINT64_2PART_C(0xda01ee64, 1a708dea), 295, 108},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0xa26da399, 9aef774a), 322, 116},    {DOUBLE_CONVERSION_UINT64_2PART_C(0xf209787b, b47d6b85), 348, 124},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0xb454e4a1, 79dd1877), 375, 132},    {DOUBLE_CONVERSION_UINT64_2PART_C(0x865b8692, 5b9bc5c2), 402, 140},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0xc83553c5, c8965d3d), 428, 148},    {DOUBLE_CONVERSION_UINT64_2PART_C(0x952ab45c, fa97a0b3), 455, 156},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0xde469fbd, 99a05fe3), 481, 164},    {DOUBLE_CONVERSION_UINT64_2PART_C(0xa59bc234, db398c25), 508, 172},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0xf6c69a72, a3989f5c), 534, 180},    {DOUBLE_CONVERSION_UINT64_2PART_C(0xb7dcbf53, 54e9bece), 561, 188},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0x88fcf317, f22241e2), 588, 196},    {DOUBLE_CONVERSION_UINT64_2PART_C(0xcc20ce9b, d35c78a5), 614, 204},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0x98165af3, 7b2153df), 641, 212},    {DOUBLE_CONVERSION_UINT64_2PART_C(0xe2a0b5dc, 971f303a), 667, 220},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0xa8d9d153, 5ce3b396), 694, 228},    {DOUBLE_CONVERSION_UINT64_2PART_C(0xfb9b7cd9, a4a7443c), 720, 236},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0xbb764c4c, a7a44410), 747, 244},    {DOUBLE_CONVERSION_UINT64_2PART_C(0x8bab8eef, b6409c1a), 774, 252},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0xd01fef10, a657842c), 800, 260},    {DOUBLE_CONVERSION_UINT64_2PART_C(0x9b10a4e5, e9913129), 827, 268},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0xe7109bfb, a19c0c9d), 853, 276},    {DOUBLE_CONVERSION_UINT64_2PART_C(0xac2820d9, 623bf429), 880, 284},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0x80444b5e, 7aa7cf85), 907, 292},    {DOUBLE_CONVERSION_UINT64_2PART_C(0xbf21e440, 03acdd2d), 933, 300},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0x8e679c2f, 5e44ff8f), 960, 308},    {DOUBLE_CONVERSION_UINT64_2PART_C(0xd433179d, 9c8cb841), 986, 316},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0x9e19db92, b4e31ba9), 1013, 324},   {DOUBLE_CONVERSION_UINT64_2PART_C(0xeb96bf6e, badf77d9), 1039, 332},
  {DOUBLE_CONVERSION_UINT64_2PART_C(0xaf87023b, 9bf0ee6b), 1066, 340},
};

static const int CachedPowersOffset = 348; // -1 * the first decimalExponent.
void GetCachedPowerForDecimalExponent(int requestedExponent, DiyFp* pPower, int* pFoundExponent)
{
  DOUBLE_CONVERSION_ASSERT(MinDecimalExponent <= requestedExponent);
  DOUBLE_CONVERSION_ASSERT(requestedExponent < MaxDecimalExponent + DecimalExponentDistance);
  int index = (requestedExponent + CachedPowersOffset) / DecimalExponentDistance;
  CachedPower cachedPower = CachedPowers[index];
  *pPower = DiyFp(cachedPower.significand, cachedPower.binaryExponent);
  *pFoundExponent = cachedPower.decimalExponent;
  DOUBLE_CONVERSION_ASSERT(*pFoundExponent <= requestedExponent);
  DOUBLE_CONVERSION_ASSERT(requestedExponent < *pFoundExponent + DecimalExponentDistance);
}

////////////////////////////////////////////////////////////////////////////////
// double-conversion/strtod.cc (amalgamated)
////////////////////////////////////////////////////////////////////////////////
// 2^64 = 18446744073709551616 > 10^19
static const int MaxUint64DecimalDigits = 19;

// Max double: 1.7976931348623157 x 10^308
// Min non-zero double: 4.9406564584124654 x 10^-324
// Any x >= 10^309 is interpreted as +infinity.
// Any x <= 10^-324 is interpreted as 0.
// Note that 2.5e-324 (despite being smaller than the min double) will be read
// as non-zero (equal to the min non-zero double).
static const int MaxDecimalPower = 309;
static const int MinDecimalPower = -324;

// 2^64 = 18446744073709551616
static const uint64_t MaxUint64 = DOUBLE_CONVERSION_UINT64_2PART_C(0xFFFFFFFF, FFFFFFFF);

// Maximum number of significant digits in the decimal representation.
// In fact the value is 772 (see conversions.cc), but to give us some margin
// we round up to 780.
static const int MaxSignificantDecimalDigits = 780;

// Reads digits from the buffer and converts them to a uint64.
// Reads in as many digits as fit into a uint64.
// When the string starts with "1844674407370955161" no further digit is read.
// Since 2^64 = 18446744073709551616 it would still be possible read another
// digit if it was less or equal than 6, but this would complicate the code.
static uint64_t ReadUint64(std::span<const char> buffer, int* pNumberOfReadDigits)
{
  uint64_t result = 0;
  int i = 0;
  while (i < (int)buffer.size() && result <= (MaxUint64 / 10 - 1)) {
    int digit = buffer[i++] - '0';
    DOUBLE_CONVERSION_ASSERT(0 <= digit && digit <= 9);
    result = 10 * result + digit;
  }
  *pNumberOfReadDigits = i;
  return result;
}

// Reads a DiyFp from the buffer.
// The returned DiyFp is not necessarily normalized.
// If remainingDecimals is zero then the returned DiyFp is accurate.
// Otherwise it has been rounded and has error of at most 1/2 ulp.
static void ReadDiyFp(std::span<const char> buffer, DiyFp* pResult, int* pRemainingDecimals)
{
  int readDigits;
  uint64_t significand = ReadUint64(buffer, &readDigits);
  if ((int)buffer.size() == readDigits) {
    *pResult = DiyFp(significand, 0);
    *pRemainingDecimals = 0;
  } else {
    // Round the significand.
    if (buffer[readDigits] >= '5') {
      significand++;
    }
    // Compute the binary exponent.
    int exponent = 0;
    *pResult = DiyFp(significand, exponent);
    *pRemainingDecimals = (int)buffer.size() - readDigits;
  }
}

// Returns 10^exponent as an exact DiyFp.
// The given exponent must be in the range [1; DecimalExponentDistance[.
static DiyFp AdjustmentPowerOfTen(int exponent)
{
  DOUBLE_CONVERSION_ASSERT(0 < exponent);
  DOUBLE_CONVERSION_ASSERT(exponent < DecimalExponentDistance);
  // Simply hardcode the remaining powers for the given decimal exponent
  // distance.
  DOUBLE_CONVERSION_ASSERT(DecimalExponentDistance == 8);
  switch (exponent)
  {
    case 1:
      return DiyFp(DOUBLE_CONVERSION_UINT64_2PART_C(0xa0000000, 00000000), -60);
    case 2:
      return DiyFp(DOUBLE_CONVERSION_UINT64_2PART_C(0xc8000000, 00000000), -57);
    case 3:
      return DiyFp(DOUBLE_CONVERSION_UINT64_2PART_C(0xfa000000, 00000000), -54);
    case 4:
      return DiyFp(DOUBLE_CONVERSION_UINT64_2PART_C(0x9c400000, 00000000), -50);
    case 5:
      return DiyFp(DOUBLE_CONVERSION_UINT64_2PART_C(0xc3500000, 00000000), -47);
    case 6:
      return DiyFp(DOUBLE_CONVERSION_UINT64_2PART_C(0xf4240000, 00000000), -44);
    case 7:
      return DiyFp(DOUBLE_CONVERSION_UINT64_2PART_C(0x98968000, 00000000), -40);
    default:
      DOUBLE_CONVERSION_UNREACHABLE();
  }
}

// If the function returns true then the result is the correct double.
// Otherwise it is either the correct double or the double that is just below
// the correct double.
static bool DiyFpStrtod(std::span<const char> buffer, int exponent, double* pResult)
{
  DiyFp input;
  int remainingDecimals;
  ReadDiyFp(buffer, &input, &remainingDecimals);
  // Since we may have dropped some digits the input is not accurate.
  // If remainingDecimals is different than 0 than the error is at most
  // .5 ulp (unit in the last place).
  // We don't want to deal with fractions and therefore keep a common
  // pDenominator.
  const int DenominatorLog = 3;
  const int Denominator = 1 << DenominatorLog;
  // Move the remaining decimals into the exponent.
  exponent += remainingDecimals;
  uint64_t error = (remainingDecimals == 0 ? 0 : Denominator / 2);

  int oldExponent = input.Exponent();
  input.Normalize();
  error <<= oldExponent - input.Exponent();

  DOUBLE_CONVERSION_ASSERT(exponent <= MaxDecimalExponent);
  if (exponent < MinDecimalExponent) {
    *pResult = 0.0;
    return true;
  }
  DiyFp cachedPower;
  int cachedDecimalExponent;
  GetCachedPowerForDecimalExponent(exponent, &cachedPower, &cachedDecimalExponent);

  if (cachedDecimalExponent != exponent) {
    int adjustmentExponent = exponent - cachedDecimalExponent;
    DiyFp adjustmentPower = AdjustmentPowerOfTen(adjustmentExponent);
    input.Multiply(adjustmentPower);
    if (MaxUint64DecimalDigits - (int)buffer.size() >= adjustmentExponent) {
      // The product of input with the adjustment power fits into a 64 bit
      // integer.
      DOUBLE_CONVERSION_ASSERT(DiyFp::SignificandSize == 64);
    } else {
      // The adjustment power is exact. There is hence only an error of 0.5.
      error += Denominator / 2;
    }
  }

  input.Multiply(cachedPower);
  // The error introduced by a multiplication of a*b equals
  //   errorA + errorB + errorA*errorB/2^64 + 0.5
  // Substituting a with 'input' and b with 'cachedPower' we have
  //   errorB = 0.5  (all cached powers have an error of less than 0.5 ulp),
  //   errorAB = 0 or 1 / Denominator > errorA*errorB/ 2^64
  int errorB = Denominator / 2;
  int errorAB = (error == 0 ? 0 : 1); // We round up to 1.
  int fixedError = Denominator / 2;
  error += errorB + errorAB + fixedError;

  oldExponent = input.Exponent();
  input.Normalize();
  error <<= oldExponent - input.Exponent();

  // See if the double's significand changes if we add/subtract the error.
  int orderOfMagnitude = DiyFp::SignificandSize + input.Exponent();
  int effectiveSignificandSize = Double::SignificandSizeForOrderOfMagnitude(orderOfMagnitude);
  int precisionDigitsCount = DiyFp::SignificandSize - effectiveSignificandSize;
  if (precisionDigitsCount + DenominatorLog >= DiyFp::SignificandSize) {
    // This can only happen for very small denormals. In this case the
    // half-way multiplied by the pDenominator exceeds the range of an uint64.
    // Simply shift everything to the right.
    int shiftAmount = (precisionDigitsCount + DenominatorLog) - DiyFp::SignificandSize + 1;
    input.SetSignificand(input.Significand() >> shiftAmount);
    input.SetExponent(input.Exponent() + shiftAmount);
    // We add 1 for the lost precision of error, and Denominator for
    // the lost precision of input.Significand().
    error = (error >> shiftAmount) + 1 + Denominator;
    precisionDigitsCount -= shiftAmount;
  }
  // We use uint64_ts now. This only works if the DiyFp uses uint64_ts too.
  DOUBLE_CONVERSION_ASSERT(DiyFp::SignificandSize == 64);
  DOUBLE_CONVERSION_ASSERT(precisionDigitsCount < 64);
  uint64_t one64 = 1;
  uint64_t precisionBitsMask = (one64 << precisionDigitsCount) - 1;
  uint64_t precisionBits = input.Significand() & precisionBitsMask;
  uint64_t halfway = one64 << (precisionDigitsCount - 1);
  precisionBits *= Denominator;
  halfway *= Denominator;
  DiyFp roundedInput(input.Significand() >> precisionDigitsCount, input.Exponent() + precisionDigitsCount);
  if (precisionBits >= halfway + error) {
    roundedInput.SetSignificand(roundedInput.Significand() + 1);
  }
  // If the lastBits are too close to the half-way case than we are too
  // inaccurate and round down. In this case we return false so that we can
  // fall back to a more precise algorithm.

  *pResult = Double(roundedInput).Value();
  if (halfway - error < precisionBits && precisionBits < halfway + error) {
    // Too imprecise. The caller will have to fall back to a slower version.
    // However the returned number is guaranteed to be either the correct
    // double, or the next-lower double.
    return false;
  } else {
    return true;
  }
}

// Returns
//   - -1 if buffer*10^exponent < diyFp.
//   -  0 if buffer*10^exponent == diyFp.
//   - +1 if buffer*10^exponent > diyFp.
// Preconditions:
//   buffer.length() + exponent <= MaxDecimalPower + 1
//   buffer.length() + exponent > MinDecimalPower
//   buffer.length() <= MaxDecimalSignificantDigits
static int CompareBufferWithDiyFp(std::span<const char> buffer, int exponent, DiyFp diyFp, Bignum::Chunk* pWorkspace)
{
  DOUBLE_CONVERSION_ASSERT((int)buffer.size() + exponent <= MaxDecimalPower + 1);
  DOUBLE_CONVERSION_ASSERT((int)buffer.size() + exponent > MinDecimalPower);
  DOUBLE_CONVERSION_ASSERT((int)buffer.size() <= MaxSignificantDecimalDigits);
  // Make sure that the Bignum will be able to hold all our numbers.
  // Our Bignum implementation has a separate field for exponents. Shifts will
  // consume at most one bigit (< 64 bits).
  // ln(10) == 3.3219...
  DOUBLE_CONVERSION_ASSERT(((MaxDecimalPower + 1) * 333 / 100) < Bignum::MaxSignificantBits);
  Bignum bufferBignum(pWorkspace + 0 * Bignum::BigitCapacity);
  Bignum diyFpBignum(pWorkspace + 1 * Bignum::BigitCapacity);
  bufferBignum.AssignDecimalString(buffer);
  diyFpBignum.AssignUInt64(diyFp.Significand());
  if (exponent >= 0) {
    bufferBignum.MultiplyByPowerOfTen(exponent);
  } else {
    diyFpBignum.MultiplyByPowerOfTen(-exponent);
  }
  if (diyFp.Exponent() > 0) {
    diyFpBignum.ShiftLeft(diyFp.Exponent());
  } else {
    bufferBignum.ShiftLeft(-diyFp.Exponent());
  }
  return Bignum::Compare(bufferBignum, diyFpBignum);
}

// Returns true if the guess is the correct double.
// Returns false, when guess is either correct or the next-lower double.
static bool ComputeGuess(std::span<const char> trimmed, int exponent, double* pGuess)
{
  if (trimmed.empty()) {
    *pGuess = 0.0;
    return true;
  }
  if (exponent + (int)trimmed.size() - 1 >= MaxDecimalPower) {
    *pGuess = Double::Infinity();
    return true;
  }
  if (exponent + (int)trimmed.size() <= MinDecimalPower) {
    *pGuess = 0.0;
    return true;
  }

  // DiyFp supplies a neighboring finite guess. Every nontrivial finite value
  // is then checked by the same exact bignum rounding path below.
  DiyFpStrtod(trimmed, exponent, pGuess);
  if (*pGuess == Double::Infinity()) {
    return true;
  }
  return false;
}

static bool IsDigit(const char digit) { return ('0' <= digit) && (digit <= '9'); }

static bool IsNonZeroDigit(const char digit) { return ('1' <= digit) && (digit <= '9'); }

#ifdef __has_cpp_attribute
#if __has_cpp_attribute(maybe_unused)
[[maybe_unused]]
#endif
#endif
static bool AssertTrimmedDigits(std::span<const char> buffer)
{
  for (int i = 0; i < (int)buffer.size(); ++i) {
    if (!IsDigit(buffer[i])) {
      return false;
    }
  }
  return buffer.empty() || (IsNonZeroDigit(buffer[0]) && IsNonZeroDigit(buffer[buffer.size() - 1]));
}

double StrtodTrimmed(std::span<const char> trimmed, int exponent, Bignum::Chunk* pWorkspace)
{
  DOUBLE_CONVERSION_ASSERT((int)trimmed.size() <= MaxSignificantDecimalDigits);
  DOUBLE_CONVERSION_ASSERT(AssertTrimmedDigits(trimmed));
  double guess;
  const bool isCorrect = ComputeGuess(trimmed, exponent, &guess);
  if (isCorrect) {
    return guess;
  }
  DiyFp upperBoundary = Double(guess).UpperBoundary();
  int comparison = CompareBufferWithDiyFp(trimmed, exponent, upperBoundary, pWorkspace);
  if (comparison < 0) {
    return guess;
  } else if (comparison > 0) {
    return Double(guess).NextDouble();
  } else if ((Double(guess).Significand() & 1) == 0) {
    // Round towards even.
    return guess;
  } else {
    return Double(guess).NextDouble();
  }
}

////////////////////////////////////////////////////////////////////////////////
}  // namespace double_conversion
////////////////////////////////////////////////////////////////////////////////

#define KEY 1
#define COMMA 2
#define COLON 4
#define ARRAY 8
#define OBJECT 16
#define DEPTH 20

#define UTF16_MASK 0xfc00
#define UTF16_MOAR 0xd800 // 0xD800..0xDBFF
#define UTF16_CONT 0xdc00 // 0xDC00..0xDFFF

#define READ32LE(pData) ((uint_least32_t)(255 & (pData)[3]) << 030 | (uint_least32_t)(255 & (pData)[2]) << 020 | (uint_least32_t)(255 & (pData)[1]) << 010 | (uint_least32_t)(255 & (pData)[0]) << 000)

#define THOM_PIKE_CONT(value) (0200 == (0300 & (value)))
#define THOM_PIKE_BYTE(value) ((value) & (((1 << THOM_PIKE_MSB(value)) - 1) | 3))
#define THOM_PIKE_LEN(value) (7 - THOM_PIKE_MSB(value))
#define THOM_PIKE_MSB(value) ((255 & (value)) < 252 ? Bsr(255 & ~(value)) : 1)
#define THOM_PIKE_MERGE(left, right) ((left) << 6 | (077 & (right)))

#define IS_SURROGATE(codePoint) ((0xf800 & (codePoint)) == 0xd800)
#define IS_HIGH_SURROGATE(codePoint) (((codePoint) & UTF16_MASK) == UTF16_MOAR)
#define IS_LOW_SURROGATE(codePoint) (((codePoint) & UTF16_MASK) == UTF16_CONT)
#define MERGE_UTF16(high, low) ((((high) - 0xD800) << 10) + ((low) - 0xDC00) + 0x10000)
#define ENCODE_UTF16(codePoint)                                                                                                                                                                        \
  ((0x0000 <= (codePoint) && (codePoint) <= 0xFFFF) || (0xE000 <= (codePoint) && (codePoint) <= 0xFFFF) ? (codePoint)                                                                                  \
   : 0x10000 <= (codePoint) && (codePoint) <= 0x10FFFF ? (((((codePoint) - 0x10000) >> 10) + 0xD800) | (unsigned)((((codePoint) - 0x10000) & 1023) + 0xDC00) << 16)                                    \
                                                       : 0xFFFD)

////////////////////////////////////////////////////////////////////////////////
namespace flat {
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// Arena ownership
////////////////////////////////////////////////////////////////////////////////

static constexpr size_t EscapeLiteralCount = 128;
static constexpr size_t HexByteCount = 256;
static constexpr size_t Utf8MaximumSequenceSize = 4;
static constexpr size_t Utf16EscapeSize = 6;

struct ArenaHeader
{
  size_t used;
  size_t back;
};

static u32 Hash32(const char* pKey, size_t length)
{
  u32 hash = 0x9747b28c;
  u32 word = 0;
  size_t offset = 0;
  while (offset + 4 <= length) {
    memcpy(&word, pKey + offset, 4);
    word *= 0xcc9e2d51u;
    word = word << 15 | word >> 17;
    word *= 0x1b873593u;
    hash ^= word;
    hash = hash << 13 | hash >> 19;
    hash = hash * 5 + 0xe6546b64u;
    offset += 4;
  }
  word = 0;
  switch (length & 3)
  {
    case 3:
      word ^= (u32)(unsigned char)pKey[offset + 2] << 16;
      [[fallthrough]];
    case 2:
      word ^= (u32)(unsigned char)pKey[offset + 1] << 8;
      [[fallthrough]];
    case 1:
      word ^= (u32)(unsigned char)pKey[offset];
      word *= 0xcc9e2d51u;
      word = word << 15 | word >> 17;
      word *= 0x1b873593u;
      hash ^= word;
  }
  hash ^= (u32)length;
  hash ^= hash >> 16;
  hash *= 0x85ebca6bu;
  hash ^= hash >> 13;
  hash *= 0xc2b2ae35u;
  hash ^= hash >> 16;
  return hash;
}

static void InitializeArena(ArenaBuffer* pArena, void* pBuffer, size_t size)
{
  pArena->pBase = (char*)pBuffer;
  ArenaHeader* pHeader = (ArenaHeader*)pArena->pBase;
  pHeader->used = sizeof(ArenaHeader);
  pHeader->back = size;
}

static u32 BackAlloc(ArenaBuffer arena, size_t byteCount, size_t alignment = 8)
{
  ArenaHeader* pHeader = (ArenaHeader*)arena.pBase;
  JSN_REQUIRE(byteCount <= pHeader->back, "Arena allocation of %zu bytes exceeds capacity.", byteCount);
  size_t offset = (pHeader->back - byteCount) & ~(alignment - 1);
  JSN_REQUIRE(offset >= pHeader->used, "Arena is full.");
  pHeader->back = offset;
  return (u32)offset;
}

template<typename T, typename Like>
using ConstLike = std::conditional_t<std::is_const_v<std::remove_reference_t<Like>>, const T, T>;

struct PackedString
{
  u32 size;
  u32 dataOffset;
};

struct ArrayIndex
{
  u32 size;
  auto Offsets(this auto& self) { return (ConstLike<u32, decltype(self)>*)(&self + 1); }
};

struct ObjectEntry
{
  u32 hash;
  u32 keyOffset;
  u32 valueOffset;
};

struct ObjectIndex
{
  u32 size;

  auto Entries(this auto& self) { return (ConstLike<ObjectEntry, decltype(self)>*)(&self + 1); }
};

MappedBuffer::MappedBuffer(void* pMapping, size_t byteCount, size_t byteOffset)
  : mapped(pMapping), size(byteCount), cursor(byteOffset), _writable(true)
{
  JSN_REQUIRE(pMapping, "MappedBuffer cannot wrap null memory.");
  JSN_REQUIRE(byteCount, "MappedBuffer cannot wrap an empty memory range.");
  JSN_REQUIRE(byteOffset <= byteCount, "MappedBuffer offset %zu exceeds its %zu-byte capacity.", byteOffset, byteCount);
}

MappedBuffer::MappedBuffer(const char* pPath)
{
  JSN_REQUIRE(pPath, "MappedBuffer input path cannot be null.");
  _descriptor = open(pPath, O_RDONLY | O_CLOEXEC);
  if (_descriptor < 0) {
    int error = errno;
    JSN_ERR("Could not open JSON input '%s': %s (%d)\n", pPath, strerror(error), error);
    return;
  }

  struct stat fileInfo = {};
  if (fstat(_descriptor, &fileInfo)) {
    int error = errno;
    JSN_ERR("Could not inspect JSON input '%s': %s (%d)\n", pPath, strerror(error), error);
    if (close(_descriptor)) {
      error = errno;
      JSN_WARN("Could not close JSON input '%s' after inspection failed: %s (%d)\n", pPath, strerror(error), error);
    }
    _descriptor = -1;
    return;
  }

  if (fileInfo.st_size <= 0) {
    JSN_WARN("JSON input '%s' is empty.\n", pPath);
    if (close(_descriptor)) {
      int error = errno;
      JSN_WARN("Could not close empty JSON input '%s': %s (%d)\n", pPath, strerror(error), error);
    }
    _descriptor = -1;
    return;
  }

  size = (size_t)fileInfo.st_size;
  mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, _descriptor, 0);
  if (mapped == MAP_FAILED) {
    int error = errno;
    JSN_ERR("Could not map JSON input '%s': %s (%d)\n", pPath, strerror(error), error);
    mapped = nullptr;
    size = 0;
    if (close(_descriptor)) {
      error = errno;
      JSN_WARN("Could not close JSON input '%s' after mapping failed: %s (%d)\n", pPath, strerror(error), error);
    }
    _descriptor = -1;
  }
}

MappedBuffer::MappedBuffer(const char* pPath, size_t capacity)
{
  JSN_REQUIRE(pPath, "MappedBuffer output path cannot be null.");
  JSN_REQUIRE(capacity, "MappedBuffer output capacity cannot be zero.");
  JSN_REQUIRE(capacity <= LLONG_MAX, "MappedBuffer output capacity exceeds the 64-bit file limit.");
  _descriptor = open(pPath, O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  if (_descriptor < 0) {
    int error = errno;
    JSN_ERR("Could not open JSON output '%s': %s (%d)\n", pPath, strerror(error), error);
    return;
  }

  if (ftruncate(_descriptor, (off_t)capacity)) {
    int error = errno;
    JSN_ERR("Could not size JSON output '%s' to %zu bytes: %s (%d)\n", pPath, capacity, strerror(error), error);
    if (close(_descriptor)) {
      error = errno;
      JSN_WARN("Could not close JSON output '%s' after sizing failed: %s (%d)\n", pPath, strerror(error), error);
    }
    _descriptor = -1;
    return;
  }

  mapped = mmap(nullptr, capacity, PROT_READ | PROT_WRITE, MAP_SHARED, _descriptor, 0);
  if (mapped == MAP_FAILED) {
    int error = errno;
    JSN_ERR("Could not map JSON output '%s': %s (%d)\n", pPath, strerror(error), error);
    mapped = nullptr;
    if (ftruncate(_descriptor, 0)) {
      error = errno;
      JSN_WARN("Could not restore JSON output '%s' after mapping failed: %s (%d)\n", pPath, strerror(error), error);
    }
    if (close(_descriptor)) {
      error = errno;
      JSN_WARN("Could not close JSON output '%s' after mapping failed: %s (%d)\n", pPath, strerror(error), error);
    }
    _descriptor = -1;
    return;
  }
  size = capacity;
  _writable = true;
}

MappedBuffer::~MappedBuffer()
{
  if (_descriptor < 0)
    return;

  if (mapped) {
    if (_writable && cursor && msync(mapped, cursor, MS_SYNC)) {
      int error = errno;
      JSN_WARN("Could not flush JSON output descriptor %d: %s (%d)\n", _descriptor, strerror(error), error);
    }
    if (munmap(mapped, size)) {
      int error = errno;
      JSN_WARN("Could not unmap JSON descriptor %d: %s (%d)\n", _descriptor, strerror(error), error);
    }
  }

  if (_writable && ftruncate(_descriptor, (off_t)(cursor ? cursor - 1 : 0))) {
    int error = errno;
    JSN_WARN("Could not trim JSON output descriptor %d to %zu bytes: %s (%d)\n",
             _descriptor, cursor ? cursor - 1 : 0, strerror(error), error);
  }
  if (close(_descriptor)) {
    int error = errno;
    JSN_WARN("Could not close JSON descriptor %d: %s (%d)\n", _descriptor, strerror(error), error);
  }
}

HeapArena::HeapArena(size_t capacity)
{
  JSN_REQUIRE(capacity >= sizeof(ArenaHeader) + 8, "HeapArena capacity is too small.");
  void* pBuffer = malloc(capacity);
  JSN_REQUIRE(pBuffer, "HeapArena allocation of %zu bytes failed.", capacity);
  InitializeArena(this, pBuffer, capacity);
}

HeapArena::~HeapArena() { free(pBase); }

///////////////////////////////////////////////////////
// ArenaBuffer::Attach
//  Initializes an arena over caller-owned memory.
///////////////////////////////////////////////////////
ArenaBuffer ArenaBuffer::Attach(void* pBuffer, size_t size)
{
  JSN_REQUIRE(pBuffer, "Cannot attach an arena to null memory.");
  uintptr_t address = ((uintptr_t)pBuffer + 7) & ~(uintptr_t)7;
  size_t skew = address - (uintptr_t)pBuffer;
  JSN_REQUIRE(size >= skew + sizeof(ArenaHeader) + 8, "Arena buffer is too small.");
  char* pAlignedBase = (char*)address;
  ArenaBuffer arena;
  InitializeArena(&arena, pAlignedBase, size - skew);
  return arena;
}

////////////////////////////////////////////////////////////////////////////////
// Direct arena output
////////////////////////////////////////////////////////////////////////////////

struct OutputBuffer
{
  char* pData = nullptr;
  size_t size = 0;
  size_t capacity = 0;
  ArenaHeader* pArena = nullptr;

  explicit OutputBuffer(ArenaBuffer arena)
  {
    JSN_REQUIRE(arena.pBase, "JSON output arena cannot be null.");
    pArena = (ArenaHeader*)arena.pBase;
    size_t offset = (pArena->used + 7) & ~(size_t)7;
    JSN_REQUIRE(offset <= pArena->back, "Arena has no room for JSON output.");
    pData = arena.pBase + offset;
    capacity = pArena->back - offset;
  }

  explicit OutputBuffer(std::span<char> output) : pData(output.data()), capacity(output.size()) {}

  [[noreturn]] void Grow(size_t byteCount) { JSN_PANIC("JSON output needs %zu more bytes.", byteCount); }

  void Add(char value)
  {
    if (size == capacity)
      Grow(1);
    pData[size++] = value;
  }

  void Append(const char* pSource, size_t count)
  {
    if (size + count > capacity)
      Grow(count);
    memcpy(pData + size, pSource, count);
    size += count;
  }

  template<size_t Size> void Append(const char (&text)[Size])
  {
    Append(text, Size - 1);
  }

  char* Reserve(size_t count)
  {
    if (size + count > capacity)
      Grow(count);
    return pData + size;
  }

  void Commit(size_t count)
  {
    if (size + count > capacity)
      Grow(count);
    size += count;
  }

  const char* Finish(size_t* pByteCount = nullptr)
  {
    Add('\0');
    if (pArena)
      pArena->used = (size_t)(pData - (char*)pArena) + size;
    if (pByteCount)
      *pByteCount = size;
    return pData;
  }
};

static void MarshalJson(const Json& value, OutputBuffer& buffer, bool pretty, int indent);
static void WriteJsonString(OutputBuffer& buffer, const char* pData, size_t size);
static void WriteEscapedString(OutputBuffer& buffer, const char* pData, size_t size);

static const char EscapeLiteral[EscapeLiteralCount] = {
  9, 9, 9, 9, 9, 9, 9, 9, 9, 1, 2, 9, 4, 3, 9,
  9, // 0x00
  9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
  9, // 0x10
  0, 0, 7, 0, 0, 0, 9, 9, 0, 0, 0, 0, 0, 0, 0,
  6, // 0x20
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9, 9, 9,
  0, // 0x30
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, // 0x40
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0,
  0, // 0x50
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, // 0x60
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  9, // 0x70
};

alignas(signed char) static const signed char HexToInt[HexByteCount] = {
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, // 0x00
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, // 0x10
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, // 0x20
  0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  -1, -1, -1, -1, -1,
  -1, // 0x30
  -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, // 0x40
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, // 0x50
  -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, // 0x60
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, // 0x70
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, // 0x80
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, // 0x90
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, // 0xa0
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, // 0xb0
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, // 0xc0
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, // 0xd0
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, // 0xe0
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, // 0xf0
};

static int Bsr(int value)
{
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_clz(value) ^ (sizeof(int) * CHAR_BIT - 1);
#else
  int result = 0;
  if (value & 0xFFFF0000u) {
    value >>= 16;
    result |= 16;
  }
  if (value & 0xFF00) {
    value >>= 8;
    result |= 8;
  }
  if (value & 0xF0) {
    value >>= 4;
    result |= 4;
  }
  if (value & 0xC) {
    value >>= 2;
    result |= 2;
  }
  if (value & 0x2) {
    result |= 1;
  }
  return result;
#endif
}

////////////////////////////////////////////////////////////////////////////////
// Number conversion
////////////////////////////////////////////////////////////////////////////////

static int ClampExponent(long long value)
{
  const int limit = INT_MAX / 2;
  if (value > limit)
    return limit;
  if (value < -limit)
    return -limit;
  return value;
}

///////////////////////////////////////////////////////
// StringToDouble
//  Parses one decimal directly with scratch space in the arena tail.
///////////////////////////////////////////////////////
static bool StringToDouble(ArenaBuffer arena, const char* pStart, const char* pEnd, const char** ppOutputEnd, double* pOutputValue)
{
  static constexpr int MaxSignificantDigits = 772;
  const char* pCursor = pStart;
  bool negative = false;
  if (pCursor < pEnd && *pCursor == '-') {
    negative = true;
    ++pCursor;
  }
  if (pCursor == pEnd)
    return false;

  const char* pInteger = pCursor;
  const char* pIntegerEnd;
  if (*pCursor == '0') {
    pIntegerEnd = ++pCursor;
  } else if ('1' <= *pCursor && *pCursor <= '9') {
    do {
      ++pCursor;
    } while (pCursor < pEnd && '0' <= *pCursor && *pCursor <= '9');
    pIntegerEnd = pCursor;
  } else {
    return false;
  }

  const char* pFraction = pCursor;
  const char* pFractionEnd = pCursor;
  if (pCursor < pEnd && *pCursor == '.') {
    pFraction = ++pCursor;
    if (pCursor == pEnd || *pCursor < '0' || '9' < *pCursor)
      return false;
    do {
      ++pCursor;
    } while (pCursor < pEnd && '0' <= *pCursor && *pCursor <= '9');
    pFractionEnd = pCursor;
  }

  int explicitExponent = 0;
  if (pCursor < pEnd && (*pCursor == 'e' || *pCursor == 'E')) {
    ++pCursor;
    bool exponentNegative = false;
    if (pCursor < pEnd && (*pCursor == '+' || *pCursor == '-')) {
      exponentNegative = *pCursor == '-';
      ++pCursor;
    }
    if (pCursor == pEnd || *pCursor < '0' || '9' < *pCursor)
      return false;
    const int limit = INT_MAX / 2;
    do {
      int digit = *pCursor++ - '0';
      if (explicitExponent > (limit - digit) / 10)
        explicitExponent = limit;
      else
        explicitExponent = explicitExponent * 10 + digit;
    } while (pCursor < pEnd && '0' <= *pCursor && *pCursor <= '9');
    if (exponentNegative)
      explicitExponent = -explicitExponent;
  }

  int significantDigits = 0;
  int trailingZeroes = 0;
  int insignificantIntegerDigits = 0;
  int decimalExponent = 0;
  bool nonzeroDigitDropped = false;

  const char* pScan = pInteger;
  if (pScan < pIntegerEnd && *pScan == '0')
    ++pScan;
  for (; pScan < pIntegerEnd; ++pScan) {
    if (significantDigits < MaxSignificantDigits) {
      ++significantDigits;
      trailingZeroes = *pScan == '0' ? trailingZeroes + 1 : 0;
    } else {
      if (insignificantIntegerDigits < INT_MAX / 2)
        ++insignificantIntegerDigits;
      nonzeroDigitDropped |= *pScan != '0';
    }
  }

  pScan = pFraction;
  if (!significantDigits) {
    while (pScan < pFractionEnd && *pScan == '0') {
      decimalExponent = ClampExponent((long long)decimalExponent - 1);
      ++pScan;
    }
  }
  for (; pScan < pFractionEnd; ++pScan) {
    if (significantDigits < MaxSignificantDigits) {
      ++significantDigits;
      decimalExponent = ClampExponent((long long)decimalExponent - 1);
      trailingZeroes = *pScan == '0' ? trailingZeroes + 1 : 0;
    } else {
      nonzeroDigitDropped |= *pScan != '0';
    }
  }

  decimalExponent = ClampExponent((long long)decimalExponent + insignificantIntegerDigits + explicitExponent);
  int keptDigits = significantDigits;
  if (nonzeroDigitDropped) {
    decimalExponent = ClampExponent((long long)decimalExponent - 1);
    trailingZeroes = 0;
  } else {
    keptDigits -= trailingZeroes;
    decimalExponent = ClampExponent((long long)decimalExponent + trailingZeroes);
  }

  int scratchSize = keptDigits + (nonzeroDigitDropped ? 1 : 0);
  ArenaHeader* pHeader = (ArenaHeader*)arena.pBase;
  JSN_REQUIRE(pHeader->used + (size_t)scratchSize <= pHeader->back,
              "Arena has no room for number parsing scratch space.");
  char* pDigits = arena.pBase + pHeader->used;
  int digitPosition = 0;

  pScan = pInteger;
  if (pScan < pIntegerEnd && *pScan == '0')
    ++pScan;
  for (; pScan < pIntegerEnd && digitPosition < keptDigits; ++pScan)
    pDigits[digitPosition++] = *pScan;
  pScan = pFraction;
  if (!digitPosition) {
    while (pScan < pFractionEnd && *pScan == '0')
      ++pScan;
  }
  for (; pScan < pFractionEnd && digitPosition < keptDigits; ++pScan)
    pDigits[digitPosition++] = *pScan;
  if (nonzeroDigitDropped)
    pDigits[digitPosition++] = '1';

  double converted = 0;
  if (digitPosition) {
    uintptr_t workspaceAddress = ((uintptr_t)(pDigits + scratchSize) + alignof(double_conversion::Bignum::Chunk) - 1) & ~(uintptr_t)(alignof(double_conversion::Bignum::Chunk) - 1);
    size_t workspaceSize = 2 * double_conversion::Bignum::BigitCapacity * sizeof(double_conversion::Bignum::Chunk);
    JSN_REQUIRE(workspaceAddress + workspaceSize <= (uintptr_t)(arena.pBase + pHeader->back),
                "Arena has no room for exact number conversion.");
    converted = double_conversion::StrtodTrimmed(std::span<const char>(pDigits, digitPosition), decimalExponent, (double_conversion::Bignum::Chunk*)workspaceAddress);
  }
  *ppOutputEnd = pCursor;
  *pOutputValue = negative ? -converted : converted;
  return true;
}

static char* UnsignedLongToString(char* pOutput, unsigned long long value)
{
  size_t length = 0;
  do {
    pOutput[length++] = value % 10 + '0';
    value /= 10;
  } while (value > 0);
  pOutput[length] = '\0';
  for (size_t left = 0, right = length - 1; left < right; ++left, --right) {
    char temporary = pOutput[left];
    pOutput[left] = pOutput[right];
    pOutput[right] = temporary;
  }
  return pOutput + length;
}

static char* LongToString(char* pOutput, long long value)
{
  unsigned long long magnitude = value;
  if (value < 0) {
    *pOutput++ = '-';
    magnitude = 0 - magnitude;
  }
  return UnsignedLongToString(pOutput, magnitude);
}

static void WriteLong(OutputBuffer& buffer, long long value)
{
  char* pOutput = buffer.Reserve(32);
  buffer.Commit(LongToString(pOutput, value) - pOutput);
}

///////////////////////////////////////////////////////
// WriteDouble
//  Writes the shortest round-trippable number directly into the arena.
///////////////////////////////////////////////////////
static void WriteDouble(OutputBuffer& buffer, double value, bool single)
{
  double_conversion::Double inspected(value);
  if (inspected.IsNan()) {
    buffer.Append("null");
    return;
  }
  if (inspected.IsInfinite()) {
    if (value < 0)
      buffer.Add('-');
    buffer.Append("1e5000");
    return;
  }

  // Shortest digits and all exact bignum workspace live in the uncommitted
  // arena tail. This intentionally uses one universal conversion path.
  char* pOutput = buffer.Reserve(32);
  bool negative = inspected.Sign() < 0;
  if (negative)
    value = -value;
  int length;
  int point;
  if (value == 0) {
    pOutput[0] = '0';
    length = 1;
    point = 1;
  } else {
    std::span<char> digits(pOutput, 18);
    uintptr_t workspaceAddress = ((uintptr_t)(pOutput + 32) + alignof(double_conversion::Bignum::Chunk) - 1) & ~(uintptr_t)(alignof(double_conversion::Bignum::Chunk) - 1);
    size_t workspaceSize = 4 * double_conversion::Bignum::BigitCapacity * sizeof(double_conversion::Bignum::Chunk);
    buffer.Reserve(workspaceAddress - (uintptr_t)pOutput + workspaceSize);
    double_conversion::BignumDtoa(value, single, (double_conversion::Bignum::Chunk*)workspaceAddress, digits, &length, &point);
  }

  int exponent = point - 1;
  if (-6 <= exponent && exponent < 21) {
    if (point <= 0) {
      int prefix = 2 - point;
      memmove(pOutput + prefix, pOutput, length);
      pOutput[0] = '0';
      pOutput[1] = '.';
      memset(pOutput + 2, '0', -point);
      length += prefix;
    } else if (point >= length) {
      memset(pOutput + length, '0', point - length);
      length = point;
    } else {
      memmove(pOutput + point + 1, pOutput + point, length - point);
      pOutput[point] = '.';
      ++length;
    }
  } else {
    if (length > 1) {
      memmove(pOutput + 2, pOutput + 1, length - 1);
      pOutput[1] = '.';
      ++length;
    }
    pOutput[length++] = 'e';
    pOutput[length++] = exponent < 0 ? '-' : '+';
    unsigned int magnitude = exponent < 0 ? -exponent : exponent;
    length = UnsignedLongToString(pOutput + length, magnitude) - pOutput;
  }

  // JSON has one spelling for zero, so suppress DoubleToAscii's sign for
  // negative zero just as the former UNIQUE_ZERO converter did.
  if (negative && value != 0.0) {
    memmove(pOutput + 1, pOutput, length);
    pOutput[0] = '-';
    ++length;
  }
  buffer.Commit(length);
}

////////////////////////////////////////////////////////////////////////////////
// Immutable value access
////////////////////////////////////////////////////////////////////////////////

const char* Json::GetString() const
{
  JSN_REQUIRE(IsString(), "JSON value is not a string.");
  const PackedString* pString = (const PackedString*)((const char*)this + stringOffset);
  return (const char*)pString + pString->dataOffset;
}

size_t Json::GetSize() const
{
  switch (type)
  {
    case TYPE_STRING:
      return ((const PackedString*)((const char*)this + stringOffset))->size;
    case TYPE_ARRAY:
      return ((const ArrayIndex*)((const char*)this + arrayOffset))->size;
    case TYPE_OBJECT:
      return ((const ObjectIndex*)((const char*)this + objectOffset))->size;
    default:
      JSN_PANIC("JSON value has no size.");
  }
}

double Json::GetNumber() const
{
  switch (type)
  {
    case TYPE_LONG:
      return longValue;
    case TYPE_FLOAT:
      return floatValue;
    case TYPE_DOUBLE:
      return doubleValue;
    default:
      JSN_PANIC("JSON value is not a number.");
  }
}

long long Json::GetLong() const
{
  switch (type)
  {
    case TYPE_LONG:
      return longValue;
    default:
      JSN_PANIC("JSON value is not a long.");
  }
}

bool Json::GetBool() const
{
  switch (type)
  {
    case TYPE_BOOL:
      return boolValue;
    default:
      JSN_PANIC("JSON value is not a bool.");
  }
}

float Json::GetFloat() const
{
  switch (type)
  {
    case TYPE_FLOAT:
      return floatValue;
    case TYPE_DOUBLE:
      return doubleValue;
    default:
      JSN_PANIC("JSON value is not a floating-point number.");
  }
}

double Json::GetDouble() const
{
  switch (type)
  {
    case TYPE_FLOAT:
      return floatValue;
    case TYPE_DOUBLE:
      return doubleValue;
    default:
      JSN_PANIC("JSON value is not a floating-point number.");
  }
}

///////////////////////////////////////////////////////
// Json::Contains
//  Finds an object key through its immutable hash index.
///////////////////////////////////////////////////////
bool Json::Contains(std::span<const char> key) const
{
  if (!IsObject())
    return false;
  const ObjectIndex* pObject = (const ObjectIndex*)((const char*)this + objectOffset);
  u32 hash = Hash32(key.data(), key.size());
  for (u32 i = 0; i < pObject->size; ++i) {
    const ObjectEntry& entry = pObject->Entries()[i];
    const PackedString* pName = (const PackedString*)((const char*)pObject + entry.keyOffset);
    if (entry.hash == hash && pName->size == key.size() && !memcmp((const char*)pName + pName->dataOffset, key.data(), key.size()))
      return true;
  }
  return false;
}

const Json& Json::operator[](size_t index) const
{
  JSN_REQUIRE(IsArray(), "JSON value is not an array.");
  const ArrayIndex& array = *(const ArrayIndex*)((const char*)this + arrayOffset);
  JSN_REQUIRE(index < array.size, "JSON index %zu is outside array of size %u.", index, array.size);
  return *(const Json*)((const char*)&array + array.Offsets()[index]);
}

const Json& Json::operator[](std::span<const char> key) const
{
  JSN_REQUIRE(IsObject(), "JSON value is not an object.");
  const ObjectIndex* pObject = (const ObjectIndex*)((const char*)this + objectOffset);
  u32 hash = Hash32(key.data(), key.size());
  for (u32 i = 0; i < pObject->size; ++i) {
    const ObjectEntry& entry = pObject->Entries()[i];
    const PackedString* pName = (const PackedString*)((const char*)pObject + entry.keyOffset);
    if (entry.hash == hash && pName->size == key.size() && !memcmp((const char*)pName + pName->dataOffset, key.data(), key.size()))
      return *(const Json*)((const char*)pObject + entry.valueOffset);
  }
  JSN_PANIC("JSON object does not contain requested key.");
}

const char* Json::ToString(ArenaBuffer output) const
{
  OutputBuffer buffer(output);
  MarshalJson(*this, buffer, false, 0);
  return buffer.Finish();
}

const char* Json::ToStringPretty(ArenaBuffer output) const
{
  OutputBuffer buffer(output);
  MarshalJson(*this, buffer, true, 0);
  return buffer.Finish();
}

////////////////////////////////////////////////////////////////////////////////
// JSON serialization
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////
// MarshalJson
//  Serializes an immutable parsed node into the arena output tail.
///////////////////////////////////////////////////////
static void MarshalJson(const Json& value, OutputBuffer& buffer, bool pretty, int indent)
{
  switch (value.type)
  {
    case Json::TYPE_NULL:
      buffer.Append("null");
      break;
    case Json::TYPE_STRING: {
      const PackedString* pString = (const PackedString*)((const char*)&value + value.stringOffset);
      WriteJsonString(buffer, (const char*)pString + pString->dataOffset, pString->size);
      break;
    }
    case Json::TYPE_BOOL:
      if (value.boolValue)
        buffer.Append("true");
      else
        buffer.Append("false");
      break;
    case Json::TYPE_LONG:
      WriteLong(buffer, value.longValue);
      break;
    case Json::TYPE_FLOAT:
      WriteDouble(buffer, value.floatValue, true);
      break;
    case Json::TYPE_DOUBLE:
      WriteDouble(buffer, value.doubleValue, false);
      break;
    case Json::TYPE_ARRAY: {
      const ArrayIndex& array = *(const ArrayIndex*)((const char*)&value + value.arrayOffset);
      buffer.Add('[');
      for (u32 i = 0; i < array.size; ++i) {
        if (i) {
          buffer.Add(',');
          if (pretty)
            buffer.Add(' ');
        }
        const Json& child = *(const Json*)((const char*)&array + array.Offsets()[i]);
        MarshalJson(child, buffer, pretty, indent);
      }
      buffer.Add(']');
      break;
    }
    case Json::TYPE_OBJECT: {
      const ObjectIndex& object = *(const ObjectIndex*)((const char*)&value + value.objectOffset);
      buffer.Add('{');
      for (u32 i = 0; i < object.size; ++i) {
        if (i)
          buffer.Add(',');
        if (pretty && object.size > 1) {
          buffer.Add('\n');
          ++indent;
          for (int indentationIndex = 0; indentationIndex < indent; ++indentationIndex)
            buffer.Append("  ");
        }
        const ObjectEntry& entry = object.Entries()[i];
        const PackedString* pName = (const PackedString*)((const char*)&object + entry.keyOffset);
        WriteJsonString(buffer, (const char*)pName + pName->dataOffset, pName->size);
        buffer.Add(':');
        if (pretty)
          buffer.Add(' ');
        const Json& child = *(const Json*)((const char*)&object + entry.valueOffset);
        MarshalJson(child, buffer, pretty, indent);
        if (pretty && object.size > 1)
          --indent;
      }
      if (pretty && object.size > 1) {
        buffer.Add('\n');
        for (int indentationIndex = 0; indentationIndex < indent; ++indentationIndex)
          buffer.Append("  ");
        ++indent;
      }
      buffer.Add('}');
      break;
    }
    default:
      JSN_PANIC("Unhandled JSON type.");
  }
}

static void WriteJsonString(OutputBuffer& buffer, const char* pData, size_t size)
{
  buffer.Add('"');
  WriteEscapedString(buffer, pData, size);
  buffer.Add('"');
}

///////////////////////////////////////////////////////
// WriteEscapedString
//  Escapes a bounded UTF-8 string into JSON syntax.
///////////////////////////////////////////////////////
static void WriteEscapedString(OutputBuffer& buffer, const char* pData, size_t size)
{
  for (size_t offset = 0; offset < size;) {
    wint_t codePoint = pData[offset++] & 255;
    if (codePoint >= 0300) {
      wint_t mergedCodePoint = THOM_PIKE_BYTE(codePoint);
      size_t continuationCount = THOM_PIKE_LEN(codePoint) - 1;
      if (offset + continuationCount <= size) {
        for (size_t i = 0;;) {
          wint_t continuation = pData[offset + i] & 0xff;
          if (!THOM_PIKE_CONT(continuation))
            break;
          mergedCodePoint = THOM_PIKE_MERGE(mergedCodePoint, continuation);
          if (++i == continuationCount) {
            codePoint = mergedCodePoint;
            offset += i;
            break;
          }
        }
      }
    }
    switch (0 <= codePoint && codePoint <= 127 ? EscapeLiteral[codePoint] : 9)
    {
      case 0:
        buffer.Add(codePoint);
        break;
      case 1:
        buffer.Append("\\t");
        break;
      case 2:
        buffer.Append("\\n");
        break;
      case 3:
        buffer.Append("\\r");
        break;
      case 4:
        buffer.Append("\\f");
        break;
      case 5:
        buffer.Append("\\\\");
        break;
      case 6:
        buffer.Append("\\/");
        break;
      case 7:
        buffer.Append("\\\"");
        break;
      case 9: {
        unsigned long long utf16 = ENCODE_UTF16(codePoint);
        do {
          char escape[Utf16EscapeSize];
          escape[0] = '\\';
          escape[1] = 'u';
          escape[2] = "0123456789abcdef"[(utf16 & 0xF000) >> 014];
          escape[3] = "0123456789abcdef"[(utf16 & 0x0F00) >> 010];
          escape[4] = "0123456789abcdef"[(utf16 & 0x00F0) >> 004];
          escape[5] = "0123456789abcdef"[(utf16 & 0x000F) >> 000];
          buffer.Append(escape, Utf16EscapeSize);
        } while ((utf16 >>= 16));
        break;
      }
      default:
        JSN_PANIC("Unhandled character escape code during string serialization.");
    }
  }
}

///////////////////////////////////////////////////////
// MarshalValue
//  Serializes an initializer-list value tree without intermediate storage.
///////////////////////////////////////////////////////
static void MarshalValue(const JsonValue& value, OutputBuffer& buffer, bool pretty, int indent)
{
  switch (value.type)
  {
    case JsonValue::TYPE_NULL:
      buffer.Append("null");
      break;
    case JsonValue::TYPE_BOOL:
      if (value.boolValue)
        buffer.Append("true");
      else
        buffer.Append("false");
      break;
    case JsonValue::TYPE_LONG:
      WriteLong(buffer, value.longValue);
      break;
    case JsonValue::TYPE_FLOAT:
      WriteDouble(buffer, value.floatValue, true);
      break;
    case JsonValue::TYPE_DOUBLE:
      WriteDouble(buffer, value.doubleValue, false);
      break;
    case JsonValue::TYPE_STRING:
      WriteJsonString(buffer, value.stringValue.pData, value.stringValue.size);
      break;
    case JsonValue::TYPE_ARRAY: {
      const JsonValue* pValues = (const JsonValue*)value.listValue.pData;
      buffer.Add('[');
      for (size_t i = 0; i < value.listValue.size; ++i) {
        if (i) {
          buffer.Add(',');
          if (pretty)
            buffer.Add(' ');
        }
        MarshalValue(pValues[i], buffer, pretty, indent);
      }
      buffer.Add(']');
      break;
    }
    case JsonValue::TYPE_OBJECT: {
      const JsonMember* pMembers = (const JsonMember*)value.listValue.pData;
      size_t count = value.listValue.size;
      buffer.Add('{');
      for (size_t i = 0; i < count; ++i) {
        if (i)
          buffer.Add(',');
        if (pretty && count > 1) {
          buffer.Add('\n');
          for (int indentationIndex = 0; indentationIndex < indent + 1; ++indentationIndex)
            buffer.Append("  ");
        }
        WriteJsonString(buffer, pMembers[i].key.data(), pMembers[i].key.size());
        buffer.Add(':');
        if (pretty)
          buffer.Add(' ');
        MarshalValue(pMembers[i].value, buffer, pretty, indent + 1);
      }
      if (pretty && count > 1) {
        buffer.Add('\n');
        for (int indentationIndex = 0; indentationIndex < indent; ++indentationIndex)
          buffer.Append("  ");
      }
      buffer.Add('}');
      break;
    }
    default:
      JSN_PANIC("Unhandled JSON write type.");
  }
}

const char* WriteJson(const JsonValue& value, ArenaBuffer output)
{
  OutputBuffer buffer(output);
  MarshalValue(value, buffer, false, 0);
  return buffer.Finish();
}

const char* WriteJsonPretty(const JsonValue& value, ArenaBuffer output)
{
  OutputBuffer buffer(output);
  MarshalValue(value, buffer, true, 0);
  return buffer.Finish();
}

const char* WriteJson(const JsonValue& value, std::span<char> output, size_t* pByteCount)
{
  OutputBuffer buffer(output);
  MarshalValue(value, buffer, false, 0);
  return buffer.Finish(pByteCount);
}

const char* WriteJsonPretty(const JsonValue& value, std::span<char> output, size_t* pByteCount)
{
  OutputBuffer buffer(output);
  MarshalValue(value, buffer, true, 0);
  return buffer.Finish(pByteCount);
}

const char* WriteJson(const Json& value, ArenaBuffer output) { return value.ToString(output); }

const char* WriteJsonPretty(const Json& value, ArenaBuffer output) { return value.ToStringPretty(output); }

const char* WriteJson(const Json& value, std::span<char> output, size_t* pByteCount)
{
  OutputBuffer buffer(output);
  MarshalJson(value, buffer, false, 0);
  return buffer.Finish(pByteCount);
}

const char* WriteJsonPretty(const Json& value, std::span<char> output, size_t* pByteCount)
{
  OutputBuffer buffer(output);
  MarshalJson(value, buffer, true, 0);
  return buffer.Finish(pByteCount);
}

template<typename Value>
static const char* WriteMappedJson(const Value& value, MappedBuffer& output, bool pretty)
{
  JSN_REQUIRE(output.mapped && output._writable && output.cursor <= output.size,
              "MappedBuffer is not valid writable output.");
  size_t byteCount = 0;
  std::span<char> remaining((char*)output.mapped + output.cursor, output.size - output.cursor);
  const char* pText = pretty ? WriteJsonPretty(value, remaining, &byteCount)
                             : WriteJson(value, remaining, &byteCount);
  output.cursor += byteCount;
  return pText;
}

const char* WriteJson(const JsonValue& value, MappedBuffer& output) { return WriteMappedJson(value, output, false); }

const char* WriteJsonPretty(const JsonValue& value, MappedBuffer& output) { return WriteMappedJson(value, output, true); }

const char* WriteJson(const Json& value, MappedBuffer& output) { return WriteMappedJson(value, output, false); }

const char* WriteJsonPretty(const Json& value, MappedBuffer& output) { return WriteMappedJson(value, output, true); }

////////////////////////////////////////////////////////////////////////////////
// Immutable backward parser
////////////////////////////////////////////////////////////////////////////////

static u32 StoreNode(ArenaBuffer arena, Json node, size_t subtreeEnd)
{
  u32 offset = BackAlloc(arena, sizeof(Json), alignof(Json));
  JSN_ASSERT(subtreeEnd >= offset && subtreeEnd - offset <= UINT32_MAX);
  node.span = (u32)(subtreeEnd - offset);
  switch (node.type)
  {
    case Json::TYPE_STRING:
      JSN_ASSERT(node.stringOffset >= offset);
      node.stringOffset -= offset;
      break;
    case Json::TYPE_ARRAY:
      JSN_ASSERT(node.arrayOffset >= offset);
      node.arrayOffset -= offset;
      break;
    case Json::TYPE_OBJECT:
      JSN_ASSERT(node.objectOffset >= offset);
      node.objectOffset -= offset;
      break;
    default:
      break;
  }
  new (arena.pBase + offset) Json(node);
  return offset;
}

static void ReverseBytes(char* pData, size_t size)
{
  for (size_t i = 0; i < size / 2; ++i) {
    char temporary = pData[i];
    pData[i] = pData[size - i - 1];
    pData[size - i - 1] = temporary;
  }
}

static bool IsUtf8ContinuationByte(unsigned byte) { return 0x80 <= byte && byte <= 0xbf; }

// Returns the byte length of one canonical UTF-8 scalar, or zero if invalid.
///////////////////////////////////////////////////////
// JsonUtf8SequenceLength
//  Validates one canonical UTF-8 scalar and returns its byte count.
///////////////////////////////////////////////////////
static size_t JsonUtf8SequenceLength(const char* pStart, const char* pEnd)
{
  size_t available = pEnd - pStart;
  unsigned leadingByte = pStart[0] & 255;
  if (leadingByte <= 0x7f)
    return 1;
  if (0xc2 <= leadingByte && leadingByte <= 0xdf) {
    return available >= 2 && IsUtf8ContinuationByte(pStart[1] & 255) ? 2 : 0;
  }
  if (0xe0 <= leadingByte && leadingByte <= 0xef) {
    if (available < 3 || !IsUtf8ContinuationByte(pStart[2] & 255))
      return 0;
    unsigned secondByte = pStart[1] & 255;
    if (leadingByte == 0xe0)
      return 0xa0 <= secondByte && secondByte <= 0xbf ? 3 : 0;
    if (leadingByte == 0xed)
      return 0x80 <= secondByte && secondByte <= 0x9f ? 3 : 0;
    return IsUtf8ContinuationByte(secondByte) ? 3 : 0;
  }
  if (0xf0 <= leadingByte && leadingByte <= 0xf4) {
    if (available < 4 || !IsUtf8ContinuationByte(pStart[2] & 255) || !IsUtf8ContinuationByte(pStart[3] & 255))
      return 0;
    unsigned secondByte = pStart[1] & 255;
    if (leadingByte == 0xf0)
      return 0x90 <= secondByte && secondByte <= 0xbf ? 4 : 0;
    if (leadingByte == 0xf4)
      return 0x80 <= secondByte && secondByte <= 0x8f ? 4 : 0;
    return IsUtf8ContinuationByte(secondByte) ? 4 : 0;
  }
  return 0;
}

///////////////////////////////////////////////////////
// ParseJson
//  Parses one subtree backward into immutable arena records.
///////////////////////////////////////////////////////
static Json::Status ParseJson(u32& nodeOffset, ArenaBuffer arena, const char*& pCursor, const char* pEnd, int context, int depth)
{
  using enum Json::Status;
  using enum Json::Type;
  char encodedBytes[Utf8MaximumSequenceSize];
  long long integerValue;
  const char* pNumberStart;
  int hexA, hexB, hexC, hexD, character, sign, byteCount, lowSurrogate;
  if (!depth)
    return MALFORMED;
  size_t subtreeEnd = ((ArenaHeader*)arena.pBase)->back;
  Json node;
  for (pNumberStart = pCursor, sign = +1; pCursor < pEnd;) {
    switch ((character = *pCursor++ & 255))
    {
      case ' ':
      case '\n':
      case '\r':
      case '\t':
        pNumberStart = pCursor;
        break;

      case ',':
        if (context & COMMA) {
          context = 0;
          pNumberStart = pCursor;
          break;
        }
        return MALFORMED;

      case ':':
        if (context & COLON) {
          context = 0;
          pNumberStart = pCursor;
          break;
        }
        return MALFORMED;

      case 'n':
        if (context & (KEY | COLON | COMMA))
          return MALFORMED;
        if (pCursor + 3 <= pEnd && READ32LE(pCursor - 1) == READ32LE("null")) {
          pCursor += 3;
          nodeOffset = StoreNode(arena, node, subtreeEnd);
          return SUCCESS;
        }
        return MALFORMED;

      case 'f':
        if (context & (KEY | COLON | COMMA))
          return MALFORMED;
        if (pCursor + 4 <= pEnd && READ32LE(pCursor) == READ32LE("alse")) {
          pCursor += 4;
          node.type = TYPE_BOOL;
          node.boolValue = false;
          nodeOffset = StoreNode(arena, node, subtreeEnd);
          return SUCCESS;
        }
        return MALFORMED;

      case 't':
        if (context & (KEY | COLON | COMMA))
          return MALFORMED;
        if (pCursor + 3 <= pEnd && READ32LE(pCursor - 1) == READ32LE("true")) {
          pCursor += 3;
          node.type = TYPE_BOOL;
          node.boolValue = true;
          nodeOffset = StoreNode(arena, node, subtreeEnd);
          return SUCCESS;
        }
        return MALFORMED;

      case '-':
        if (context & (COLON | COMMA | KEY))
          return MALFORMED;
        if (pCursor < pEnd && isdigit(*pCursor)) {
          sign = -1;
          break;
        }
        return MALFORMED;

      case '0':
        if (context & (COLON | COMMA | KEY))
          return MALFORMED;
        if (pCursor < pEnd) {
          if (*pCursor == '.') {
            if (pCursor + 1 == pEnd || !isdigit(pCursor[1]))
              return MALFORMED;
            goto UseDouble;
          }
          if (*pCursor == 'e' || *pCursor == 'E')
            goto UseDouble;
        }
        node.type = TYPE_LONG;
        node.longValue = 0;
        nodeOffset = StoreNode(arena, node, subtreeEnd);
        return SUCCESS;

      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        if (context & (COLON | COMMA | KEY))
          return MALFORMED;
        for (integerValue = (character - '0') * sign; pCursor < pEnd; ++pCursor) {
          character = *pCursor & 255;
          if (isdigit(character)) {
            if (ckd_mul(&integerValue, integerValue, 10) || ckd_add(&integerValue, integerValue, (character - '0') * sign))
              goto UseDouble;
          } else if (character == '.') {
            if (pCursor + 1 == pEnd || !isdigit(pCursor[1]))
              return MALFORMED;
            goto UseDouble;
          } else if (character == 'e' || character == 'E') {
            goto UseDouble;
          } else {
            break;
          }
        }
        node.type = TYPE_LONG;
        node.longValue = integerValue;
        nodeOffset = StoreNode(arena, node, subtreeEnd);
        return SUCCESS;

      UseDouble:
        node.type = TYPE_DOUBLE;
        const char* pNumberEnd;
        if (!StringToDouble(arena, pNumberStart, pEnd, &pNumberEnd, &node.doubleValue))
          return MALFORMED;
        pCursor = pNumberEnd;
        nodeOffset = StoreNode(arena, node, subtreeEnd);
        return SUCCESS;

      case '[': {
        if (context & (COLON | COMMA | KEY))
          return MALFORMED;
        u32 elementCount = 0;
        u32 lastChildOffset = 0;
        for (context = ARRAY;;) {
          u32 childOffset;
          Json::Status status = ParseJson(childOffset, arena, pCursor, pEnd, context, depth - 1);
          if (status == ABSENT_VALUE) {
            size_t indexSize = sizeof(ArrayIndex) + (size_t)elementCount * sizeof(u32);
            u32 indexOffset = BackAlloc(arena, indexSize, alignof(ArrayIndex));
            ArrayIndex* pIndex = new (arena.pBase + indexOffset) ArrayIndex{elementCount};
            u32 cursorOffset = lastChildOffset;
            for (u32 i = elementCount; i--;) {
              JSN_ASSERT(cursorOffset >= indexOffset);
              pIndex->Offsets()[i] = cursorOffset - indexOffset;
              cursorOffset += ((const Json*)(arena.pBase + cursorOffset))->span;
            }
            node.type = TYPE_ARRAY;
            node.arrayOffset = indexOffset;
            nodeOffset = StoreNode(arena, node, subtreeEnd);
            return SUCCESS;
          }
          if (status != SUCCESS)
            return status;
          lastChildOffset = childOffset;
          ++elementCount;
          context = ARRAY | COMMA;
        }
      }

      case ']':
        return context & ARRAY ? ABSENT_VALUE : MALFORMED;

      case '}':
        return context & OBJECT ? ABSENT_VALUE : MALFORMED;

      case '{': {
        if (context & (COLON | COMMA | KEY))
          return MALFORMED;
        u32 memberCount = 0;
        u32 lastValueOffset = 0;
        for (context = KEY | OBJECT;;) {
          u32 keyOffset;
          Json::Status status = ParseJson(keyOffset, arena, pCursor, pEnd, context, depth - 1);
          if (status == ABSENT_VALUE) {
            size_t indexSize = sizeof(ObjectIndex) + (size_t)memberCount * sizeof(ObjectEntry);
            u32 indexOffset = BackAlloc(arena, indexSize, alignof(ObjectIndex));
            ObjectIndex* pIndex = new (arena.pBase + indexOffset) ObjectIndex{memberCount};
            u32 cursorOffset = lastValueOffset;
            for (u32 i = memberCount; i--;) {
              const Json* pValue = (const Json*)(arena.pBase + cursorOffset);
              u32 valueOffset = cursorOffset;
              cursorOffset += pValue->span;
              const Json* pKey = (const Json*)(arena.pBase + cursorOffset);
              JSN_ASSERT(pKey->type == TYPE_STRING);
              const PackedString* pName = (const PackedString*)((const char*)pKey + pKey->stringOffset);
              u32 nameOffset = (u32)((const char*)pName - arena.pBase);
              pIndex->Entries()[i] = {
                Hash32((const char*)pName + pName->dataOffset, pName->size),
                nameOffset - indexOffset,
                valueOffset - indexOffset,
              };
              cursorOffset += pKey->span;
            }
            node.type = TYPE_OBJECT;
            node.objectOffset = indexOffset;
            nodeOffset = StoreNode(arena, node, subtreeEnd);
            return SUCCESS;
          }
          if (status != SUCCESS)
            return status;
          const Json* pKey = (const Json*)(arena.pBase + keyOffset);
          if (!pKey->IsString())
            return MALFORMED;
          u32 valueOffset;
          status = ParseJson(valueOffset, arena, pCursor, pEnd, COLON, depth - 1);
          if (status != SUCCESS)
            return MALFORMED;
          lastValueOffset = valueOffset;
          ++memberCount;
          context = KEY | COMMA | OBJECT;
        }
      }

      case '"': {
        if (context & (COLON | COMMA))
          return MALFORMED;
        ArenaHeader* pHeader = (ArenaHeader*)arena.pBase;
        u32 nullOffset = BackAlloc(arena, 1, 1);
        arena.pBase[nullOffset] = '\0';
        auto appendBytes = [&](const char* pSource, size_t size) {
          for (size_t i = 0; i < size; ++i) {
            u32 offset = BackAlloc(arena, 1, 1);
            arena.pBase[offset] = pSource[i];
          }
        };
        for (;;) {
          if (pCursor >= pEnd)
            return MALFORMED;
          switch ((character = *pCursor++ & 255))
          {
            default: {
              const char* pSequence = pCursor - 1;
              size_t length = JsonUtf8SequenceLength(pSequence, pEnd);
              if (character < 0x20 || !length)
                return MALFORMED;
              appendBytes(pSequence, length);
              pCursor += length - 1;
              break;
            }
            case '"': {
              u32 dataOffset = (u32)pHeader->back;
              size_t size = nullOffset - dataOffset;
              ReverseBytes(arena.pBase + dataOffset, size);
              u32 stringOffset = BackAlloc(arena, sizeof(PackedString), alignof(PackedString));
              JSN_ASSERT(dataOffset >= stringOffset);
              new (arena.pBase + stringOffset) PackedString{(u32)size, dataOffset - stringOffset};
              node.type = TYPE_STRING;
              node.stringOffset = stringOffset;
              nodeOffset = StoreNode(arena, node, subtreeEnd);
              return SUCCESS;
            }
            case '\\':
              if (pCursor >= pEnd)
                return MALFORMED;
              switch ((character = *pCursor++ & 255))
              {
                case '"':
                case '/':
                case '\\': {
                  char escapedCharacter = character;
                  appendBytes(&escapedCharacter, 1);
                  break;
                }
                case 'b':
                  appendBytes("\b", 1);
                  break;
                case 'f':
                  appendBytes("\f", 1);
                  break;
                case 'n':
                  appendBytes("\n", 1);
                  break;
                case 'r':
                  appendBytes("\r", 1);
                  break;
                case 't':
                  appendBytes("\t", 1);
                  break;
                case 'u':
                  if (pCursor + 4 <= pEnd && (hexA = HexToInt[pCursor[0] & 255]) != -1 && (hexB = HexToInt[pCursor[1] & 255]) != -1 && (hexC = HexToInt[pCursor[2] & 255]) != -1 &&
                      (hexD = HexToInt[pCursor[3] & 255]) != -1) {
                    character = hexA << 12 | hexB << 8 | hexC << 4 | hexD;
                    if (!IS_SURROGATE(character)) {
                      pCursor += 4;
                    } else if (IS_HIGH_SURROGATE(character) && pCursor + 10 <= pEnd && pCursor[4] == '\\' && pCursor[5] == 'u' && (hexA = HexToInt[pCursor[6] & 255]) != -1 &&
                               (hexB = HexToInt[pCursor[7] & 255]) != -1 && (hexC = HexToInt[pCursor[8] & 255]) != -1 && (hexD = HexToInt[pCursor[9] & 255]) != -1 &&
                               IS_LOW_SURROGATE((lowSurrogate = hexA << 12 | hexB << 8 | hexC << 4 | hexD))) {
                      pCursor += 10;
                      character = MERGE_UTF16(character, lowSurrogate);
                    } else {
                      appendBytes("\\u", 2);
                      break;
                    }
                    if (character <= 0x7f) {
                      encodedBytes[0] = character;
                      byteCount = 1;
                    } else if (character <= 0x7ff) {
                      encodedBytes[0] = 0300 | (character >> 6);
                      encodedBytes[1] = 0200 | (character & 077);
                      byteCount = 2;
                    } else if (character <= 0xffff) {
                      encodedBytes[0] = 0340 | (character >> 12);
                      encodedBytes[1] = 0200 | ((character >> 6) & 077);
                      encodedBytes[2] = 0200 | (character & 077);
                      byteCount = 3;
                    } else {
                      encodedBytes[0] = 0360 | (character >> 18);
                      encodedBytes[1] = 0200 | ((character >> 12) & 077);
                      encodedBytes[2] = 0200 | ((character >> 6) & 077);
                      encodedBytes[3] = 0200 | (character & 077);
                      byteCount = 4;
                    }
                    appendBytes(encodedBytes, byteCount);
                    break;
                  }
                  return MALFORMED;
                default:
                  return MALFORMED;
              }
              break;
          }
        }
      }

      default:
        return MALFORMED;
    }
  }
  return depth == DEPTH ? ABSENT_VALUE : MALFORMED;
}

///////////////////////////////////////////////////////
// ParseJson
//  Parses one bounded JSON document and rolls back on failure.
///////////////////////////////////////////////////////
static std::pair<Json::Status, const Json*> ParseJson(ArenaBuffer arena, const char* pData, size_t size)
{
  using enum Json::Status;
  JSN_REQUIRE(arena.pBase, "JSON parse arena cannot be null.");
  JSN_REQUIRE(pData || !size, "JSON input cannot be null when its size is nonzero.");
  if (!pData)
    return {ABSENT_VALUE, nullptr};

  ArenaHeader* pHeader = (ArenaHeader*)arena.pBase;
  size_t backMark = pHeader->back;
  const char* pCursor = pData;
  const char* pEnd = pData + size;
  u32 rootOffset = 0;
  Json::Status status = ParseJson(rootOffset, arena, pCursor, pEnd, 0, DEPTH);
  while (status == SUCCESS && pCursor < pEnd && (*pCursor == ' ' || *pCursor == '\n' || *pCursor == '\r' || *pCursor == '\t'))
    ++pCursor;
  if (status != SUCCESS || pCursor != pEnd) {
    pHeader->back = backMark;
    return {MALFORMED, nullptr};
  }
  return {SUCCESS, (const Json*)(arena.pBase + rootOffset)};
}

std::pair<Json::Status, const Json*> Json::Parse(ArenaBuffer arena, const char* pData, size_t size) { return ParseJson(arena, pData, size); }

std::pair<Json::Status, const Json*> Json::Parse(ArenaBuffer arena, const MappedBuffer& input)
{
  if (!input.IsValid())
    return {ABSENT_VALUE, nullptr};
  return ParseJson(arena, (const char*)input.mapped, input.size);
}

const char* Json::StatusToString(Status status)
{
  switch (status)
  {
    case SUCCESS:
      return "success";
    case MALFORMED:
    case ABSENT_VALUE:
      return "JSON Malformed. Cannot Parse.";
    default:
      JSN_PANIC("Unhandled JSON parse status.");
  }
}

////////////////////////////////////////////////////////////////////////////////
}  // namespace flat
////////////////////////////////////////////////////////////////////////////////
