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
#include "flat_file.hpp"

#include <algorithm>
#include <type_traits>

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
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

#define DOUBLE_CONVERSION_ASSERT(condition) JSON_REQUIRE(condition, #condition)
#define DOUBLE_CONVERSION_UNREACHABLE() JSON_PANIC("Unreachable double-conversion path.")

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
template<typename Destination, typename Source>
Destination BitCast(const Source& source)
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
    unsigned __int128 product = (unsigned __int128)significandValue * other.significandValue;
    product += (unsigned __int128)1 << 63;
    exponentValue += other.exponentValue + 64;
    significandValue = (uint64_t)(product >> 64);
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

  void AssignDecimalString(flat::JsonSpan<const char> value);

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
void BignumDtoa(double value, bool single, Bignum::Chunk* pWorkspace, flat::JsonSpan<char> buffer, int* pLength, int* pDecimalPoint);

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
double StrtodTrimmed(flat::JsonSpan<const char> trimmed, int exponent, Bignum::Chunk* pWorkspace);
bool StrtodFast(uint64_t significand, int readDigits, int totalDigits, bool roundUp, int exponent, double* pResult);

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

template<typename S>

static int BitSize(const S value)
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

static uint64_t ReadUInt64(flat::JsonSpan<const char> buffer, const int from, const int digitsToRead)
{
  uint64_t result = 0;
  for (int i = from; i < from + digitsToRead; ++i) {
    const int digit = buffer[i] - '0';
    DOUBLE_CONVERSION_ASSERT(0 <= digit && digit <= 9);
    result = result * 10 + digit;
  }
  return result;
}

void Bignum::AssignDecimalString(flat::JsonSpan<const char> value)
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
  static_assert((1 << (2 * (ChunkSize - BigitSize))) > BigitCapacity,
                "double-conversion bignum multiplication could overflow.");
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
static void GenerateShortestDigits(Bignum* pNumerator, Bignum* pDenominator, Bignum* pDeltaMinus, Bignum* pDeltaPlus, bool isEven, flat::JsonSpan<char> buffer, int* pLength);
void BignumDtoa(double value, bool single, Bignum::Chunk* pWorkspace, flat::JsonSpan<char> buffer, int* pLength, int* pDecimalPoint)
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
static void GenerateShortestDigits(Bignum* pNumerator, Bignum* pDenominator, Bignum* pDeltaMinus, Bignum* pDeltaPlus, bool isEven, flat::JsonSpan<char> buffer, int* pLength)
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
static uint64_t ReadUint64(flat::JsonSpan<const char> buffer, int* pNumberOfReadDigits)
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
    case 1:  return DiyFp(DOUBLE_CONVERSION_UINT64_2PART_C(0xa0000000, 00000000), -60);
    case 2:  return DiyFp(DOUBLE_CONVERSION_UINT64_2PART_C(0xc8000000, 00000000), -57);
    case 3:  return DiyFp(DOUBLE_CONVERSION_UINT64_2PART_C(0xfa000000, 00000000), -54);
    case 4:  return DiyFp(DOUBLE_CONVERSION_UINT64_2PART_C(0x9c400000, 00000000), -50);
    case 5:  return DiyFp(DOUBLE_CONVERSION_UINT64_2PART_C(0xc3500000, 00000000), -47);
    case 6:  return DiyFp(DOUBLE_CONVERSION_UINT64_2PART_C(0xf4240000, 00000000), -44);
    case 7:  return DiyFp(DOUBLE_CONVERSION_UINT64_2PART_C(0x98968000, 00000000), -40);
    default: DOUBLE_CONVERSION_UNREACHABLE();
  }
}

// If the function returns true then the result is the correct double.
// Otherwise it is either the correct double or the double that is just below
// the correct double.
static bool DiyFpStrtod(uint64_t significand, int readDigits, int totalDigits, bool roundUp, int exponent, double* pResult)
{
  int remainingDecimals = totalDigits - readDigits;
  if (remainingDecimals && roundUp)
    ++significand;
  DiyFp input(significand, 0);
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
    if (MaxUint64DecimalDigits - totalDigits >= adjustmentExponent) {
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

static bool DiyFpStrtod(flat::JsonSpan<const char> buffer, int exponent, double* pResult)
{
  int readDigits;
  uint64_t significand = ReadUint64(buffer, &readDigits);
  bool roundUp = readDigits < (int)buffer.size() && buffer[readDigits] >= '5';
  return DiyFpStrtod(significand, readDigits, (int)buffer.size(), roundUp, exponent, pResult);
}

// Returns
//   - -1 if buffer*10^exponent < diyFp.
//   -  0 if buffer*10^exponent == diyFp.
//   - +1 if buffer*10^exponent > diyFp.
// Preconditions:
//   buffer.length() + exponent <= MaxDecimalPower + 1
//   buffer.length() + exponent > MinDecimalPower
//   buffer.length() <= MaxDecimalSignificantDigits
static int CompareBufferWithDiyFp(flat::JsonSpan<const char> buffer, int exponent, DiyFp diyFp, Bignum::Chunk* pWorkspace)
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
static bool ComputeGuess(flat::JsonSpan<const char> trimmed, int exponent, double* pGuess)
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

  // DiyFp uses integer arithmetic and explicit error bounds. A true result is
  // proven correctly rounded on every supported architecture; difficult cases
  // still use the exact bignum comparison below.
  if (DiyFpStrtod(trimmed, exponent, pGuess)) {
    return true;
  }
  if (*pGuess == Double::Infinity()) {
    return true;
  }
  return false;
}

bool StrtodFast(uint64_t significand, int readDigits, int totalDigits, bool roundUp, int exponent, double* pResult)
{
  DOUBLE_CONVERSION_ASSERT(totalDigits > 0);
  if (exponent + totalDigits - 1 >= MaxDecimalPower) {
    *pResult = Double::Infinity();
    return true;
  }
  if (exponent + totalDigits <= MinDecimalPower) {
    *pResult = 0.0;
    return true;
  }
  return DiyFpStrtod(significand, readDigits, totalDigits, roundUp, exponent, pResult);
}

static bool IsDigit(const char digit) { return ('0' <= digit) && (digit <= '9'); }

static bool IsNonZeroDigit(const char digit) { return ('1' <= digit) && (digit <= '9'); }

#ifdef __has_cpp_attribute
#if __has_cpp_attribute(maybe_unused)
[[maybe_unused]]
#endif
#endif
static bool AssertTrimmedDigits(flat::JsonSpan<const char> buffer)
{
  for (int i = 0; i < (int)buffer.size(); ++i) {
    if (!IsDigit(buffer[i])) {
      return false;
    }
  }
  return buffer.empty() || (IsNonZeroDigit(buffer[0]) && IsNonZeroDigit(buffer[buffer.size() - 1]));
}

double StrtodTrimmed(flat::JsonSpan<const char> trimmed, int exponent, Bignum::Chunk* pWorkspace)
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

static constexpr u32 InvalidOffset = UINT32_MAX;

JSON_INLINE static u32 BackAlloc(size_t& back, size_t used, size_t byteCount, size_t alignment = 8)
{
  JSON_ASSERT(alignment && !(alignment & (alignment - 1)), "Buffer allocation alignment must be a power of two.");
  if (byteCount > back) [[unlikely]] {
    JSON_WARN("JSON parse buffer cannot allocate %zu bytes.\n", byteCount);
    return InvalidOffset;
  }
  size_t offset = (back - byteCount) & ~(alignment - 1);
  if (offset < used) [[unlikely]] {
    JSON_WARN("JSON parse buffer is full; allocation needs %zu bytes.\n", byteCount);
    return InvalidOffset;
  }
  back = offset;
  return (u32)offset;
}

template<typename T, typename Like>
using ConstLike = std::conditional_t<std::is_const_v<std::remove_reference_t<Like>>, const T, T>;

struct ObjectEntry
{
  u32 keyOffset;
  u32 valueOffset;
};

// Match sajson's policy: scan through 100 members, then use binary search.
static constexpr u32 ObjectBinarySearchThreshold = 100;

template<typename Byte>
JSON_INLINE static auto ObjectKeySizes(Byte* pIndex) { return (ConstLike<u32, Byte>*)pIndex; }

template<typename Byte>
JSON_INLINE static auto ObjectEntries(Byte* pIndex, u32 size) { return (ConstLike<ObjectEntry, Byte>*)(ObjectKeySizes(pIndex) + size); }

template<typename Byte>
JSON_INLINE static auto ObjectSortOrder(Byte* pIndex, u32 size) { return (ConstLike<u32, Byte>*)(ObjectEntries(pIndex, size) + size); }

static_assert(sizeof(ObjectEntry) == 8);
static_assert(alignof(ObjectEntry) == alignof(u32));

////////////////////////////////////////////////////////////////////////////////
// Direct bounded output
////////////////////////////////////////////////////////////////////////////////

struct OutputBuffer
{
  char* pData = nullptr;
  size_t size = 0;
  size_t capacity = 0;
  Json::Status status = Json::SUCCESS;

  explicit OutputBuffer(JsonSpan<char> output) : pData(output.data()), capacity(output.size())
  {
    if (!pData && capacity) {
      JSON_WARN("JSON output cannot be null when its capacity is nonzero.\n");
      size = capacity;
      status = Json::INVALID_ARGUMENT;
    }
  }

  bool ReserveBytes(size_t count)
  {
    if (count <= capacity - size)
      return true;
    if (status != Json::SUCCESS)
      return false;
    JSON_WARN("JSON output needs %zu bytes but only %zu bytes remain.\n", count, capacity - size);
    size = capacity;
    status = Json::INSUFFICIENT_SPACE;
    return false;
  }

  void Add(char value)
  {
    if (!ReserveBytes(1))
      return;
    pData[size++] = value;
  }

  void Append(const char* pSource, size_t count)
  {
    if (!ReserveBytes(count))
      return;
    memcpy(pData + size, pSource, count);
    size += count;
  }

  template<size_t Size>

  void Append(const char (&text)[Size])
  {
    Append(text, Size - 1);
  }

  void AppendQuoted(const char* pSource, size_t count)
  {
    char* pOutput = Reserve(count + 2);
    if (!pOutput)
      return;
    pOutput[0] = '"';
    if (count)
      memcpy(pOutput + 1, pSource, count);
    pOutput[count + 1] = '"';
    Commit(count + 2);
  }

  char* Reserve(size_t count)
  {
    if (!ReserveBytes(count))
      return nullptr;
    return pData + size;
  }

  void Commit(size_t count)
  {
    JSON_ASSERT(count <= capacity - size, "JSON output commit exceeds reserved capacity.");
    size += count;
  }

  Json::Status Finish(size_t* pByteCount = nullptr)
  {
    if (pByteCount)
      *pByteCount = 0;
    Add('\0');
    if (status != Json::SUCCESS)
      return status;
    if (pByteCount)
      *pByteCount = size;
    return Json::SUCCESS;
  }
};

template<bool Pretty, typename Buffer>

static void MarshalJson(const Json& value, Buffer& buffer, int indent);
template<typename Buffer>
JSON_INLINE static bool MarshalJsonScalar(const Json& value, Buffer& buffer);
template<typename Buffer>
static void WriteJsonString(Buffer& buffer, JsonString string);
template<typename Buffer>
static void WriteEscapedString(Buffer& buffer, JsonString string);

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

////////////////////////////////////////////////////////////////////////////////
// Embedded fast_float Eisel-Lemire subset
////////////////////////////////////////////////////////////////////////////////
//
// Origin: https://github.com/fastfloat/fast_float
// Version: 8.2.3
// Copyright 2021 The fast_float authors
// License: MIT (complete notice in THIRD_PARTY_NOTICES.md)
//
// This retains only binary64 conversion for at-most-19-digit decimals in the
// finite binary32 decimal-exponent range. Every other number falls back to the
// exact amalgamated double-conversion path above.

namespace fast_decimal {

struct Value128
{
  uint64_t low;
  uint64_t high;
};

struct AdjustedMantissa
{
  uint64_t mantissa;
  int32_t power2;
};

static constexpr int SmallestPower = -64;
static constexpr int LargestPower = 38;
static constexpr uint64_t PowerOfFive[] = {
  0xa87fea27a539e9a5, 0x3f2398d747b36224,
  0xd29fe4b18e88640e, 0x8eec7f0d19a03aad,
  0x83a3eeeef9153e89, 0x1953cf68300424ac,
  0xa48ceaaab75a8e2b, 0x5fa8c3423c052dd7,
  0xcdb02555653131b6, 0x3792f412cb06794d,
  0x808e17555f3ebf11, 0xe2bbd88bbee40bd0,
  0xa0b19d2ab70e6ed6, 0x5b6aceaeae9d0ec4,
  0xc8de047564d20a8b, 0xf245825a5a445275,
  0xfb158592be068d2e, 0xeed6e2f0f0d56712,
  0x9ced737bb6c4183d, 0x55464dd69685606b,
  0xc428d05aa4751e4c, 0xaa97e14c3c26b886,
  0xf53304714d9265df, 0xd53dd99f4b3066a8,
  0x993fe2c6d07b7fab, 0xe546a8038efe4029,
  0xbf8fdb78849a5f96, 0xde98520472bdd033,
  0xef73d256a5c0f77c, 0x963e66858f6d4440,
  0x95a8637627989aad, 0xdde7001379a44aa8,
  0xbb127c53b17ec159, 0x5560c018580d5d52,
  0xe9d71b689dde71af, 0xaab8f01e6e10b4a6,
  0x9226712162ab070d, 0xcab3961304ca70e8,
  0xb6b00d69bb55c8d1, 0x3d607b97c5fd0d22,
  0xe45c10c42a2b3b05, 0x8cb89a7db77c506a,
  0x8eb98a7a9a5b04e3, 0x77f3608e92adb242,
  0xb267ed1940f1c61c, 0x55f038b237591ed3,
  0xdf01e85f912e37a3, 0x6b6c46dec52f6688,
  0x8b61313bbabce2c6, 0x2323ac4b3b3da015,
  0xae397d8aa96c1b77, 0xabec975e0a0d081a,
  0xd9c7dced53c72255, 0x96e7bd358c904a21,
  0x881cea14545c7575, 0x7e50d64177da2e54,
  0xaa242499697392d2, 0xdde50bd1d5d0b9e9,
  0xd4ad2dbfc3d07787, 0x955e4ec64b44e864,
  0x84ec3c97da624ab4, 0xbd5af13bef0b113e,
  0xa6274bbdd0fadd61, 0xecb1ad8aeacdd58e,
  0xcfb11ead453994ba, 0x67de18eda5814af2,
  0x81ceb32c4b43fcf4, 0x80eacf948770ced7,
  0xa2425ff75e14fc31, 0xa1258379a94d028d,
  0xcad2f7f5359a3b3e, 0x096ee45813a04330,
  0xfd87b5f28300ca0d, 0x8bca9d6e188853fc,
  0x9e74d1b791e07e48, 0x775ea264cf55347e,
  0xc612062576589dda, 0x95364afe032a819e,
  0xf79687aed3eec551, 0x3a83ddbd83f52205,
  0x9abe14cd44753b52, 0xc4926a9672793543,
  0xc16d9a0095928a27, 0x75b7053c0f178294,
  0xf1c90080baf72cb1, 0x5324c68b12dd6339,
  0x971da05074da7bee, 0xd3f6fc16ebca5e04,
  0xbce5086492111aea, 0x88f4bb1ca6bcf585,
  0xec1e4a7db69561a5, 0x2b31e9e3d06c32e6,
  0x9392ee8e921d5d07, 0x3aff322e62439fd0,
  0xb877aa3236a4b449, 0x09befeb9fad487c3,
  0xe69594bec44de15b, 0x4c2ebe687989a9b4,
  0x901d7cf73ab0acd9, 0x0f9d37014bf60a11,
  0xb424dc35095cd80f, 0x538484c19ef38c95,
  0xe12e13424bb40e13, 0x2865a5f206b06fba,
  0x8cbccc096f5088cb, 0xf93f87b7442e45d4,
  0xafebff0bcb24aafe, 0xf78f69a51539d749,
  0xdbe6fecebdedd5be, 0xb573440e5a884d1c,
  0x89705f4136b4a597, 0x31680a88f8953031,
  0xabcc77118461cefc, 0xfdc20d2b36ba7c3e,
  0xd6bf94d5e57a42bc, 0x3d32907604691b4d,
  0x8637bd05af6c69b5, 0xa63f9a49c2c1b110,
  0xa7c5ac471b478423, 0x0fcf80dc33721d54,
  0xd1b71758e219652b, 0xd3c36113404ea4a9,
  0x83126e978d4fdf3b, 0x645a1cac083126ea,
  0xa3d70a3d70a3d70a, 0x3d70a3d70a3d70a4,
  0xcccccccccccccccc, 0xcccccccccccccccd,
  0x8000000000000000, 0x0000000000000000,
  0xa000000000000000, 0x0000000000000000,
  0xc800000000000000, 0x0000000000000000,
  0xfa00000000000000, 0x0000000000000000,
  0x9c40000000000000, 0x0000000000000000,
  0xc350000000000000, 0x0000000000000000,
  0xf424000000000000, 0x0000000000000000,
  0x9896800000000000, 0x0000000000000000,
  0xbebc200000000000, 0x0000000000000000,
  0xee6b280000000000, 0x0000000000000000,
  0x9502f90000000000, 0x0000000000000000,
  0xba43b74000000000, 0x0000000000000000,
  0xe8d4a51000000000, 0x0000000000000000,
  0x9184e72a00000000, 0x0000000000000000,
  0xb5e620f480000000, 0x0000000000000000,
  0xe35fa931a0000000, 0x0000000000000000,
  0x8e1bc9bf04000000, 0x0000000000000000,
  0xb1a2bc2ec5000000, 0x0000000000000000,
  0xde0b6b3a76400000, 0x0000000000000000,
  0x8ac7230489e80000, 0x0000000000000000,
  0xad78ebc5ac620000, 0x0000000000000000,
  0xd8d726b7177a8000, 0x0000000000000000,
  0x878678326eac9000, 0x0000000000000000,
  0xa968163f0a57b400, 0x0000000000000000,
  0xd3c21bcecceda100, 0x0000000000000000,
  0x84595161401484a0, 0x0000000000000000,
  0xa56fa5b99019a5c8, 0x0000000000000000,
  0xcecb8f27f4200f3a, 0x0000000000000000,
  0x813f3978f8940984, 0x4000000000000000,
  0xa18f07d736b90be5, 0x5000000000000000,
  0xc9f2c9cd04674ede, 0xa400000000000000,
  0xfc6f7c4045812296, 0x4d00000000000000,
  0x9dc5ada82b70b59d, 0xf020000000000000,
  0xc5371912364ce305, 0x6c28000000000000,
  0xf684df56c3e01bc6, 0xc732000000000000,
  0x9a130b963a6c115c, 0x3c7f400000000000,
  0xc097ce7bc90715b3, 0x4b9f100000000000,
  0xf0bdc21abb48db20, 0x1e86d40000000000,
  0x96769950b50d88f4, 0x1314448000000000,
};

JSON_INLINE static Value128 Multiply(uint64_t a, uint64_t b)
{
  unsigned __int128 product = (unsigned __int128)a * b;
  return {(uint64_t)product, (uint64_t)(product >> 64)};
}

JSON_INLINE static Value128 ComputeProduct(int exponent, uint64_t significand)
{
  size_t index = 2 * (size_t)(exponent - SmallestPower);
  Value128 product = Multiply(significand, PowerOfFive[index]);
  if ((product.high & 0x1ff) == 0x1ff) {
    Value128 lower = Multiply(significand, PowerOfFive[index + 1]);
    product.low += lower.high;
    if (lower.high > product.low)
      ++product.high;
  }
  return product;
}

JSON_INLINE static int BinaryPower(int exponent) { return (((152170 + 65536) * exponent) >> 16) + 63; }

JSON_INLINE static bool Convert(uint64_t significand, int exponent, bool negative, double* pValue)
{
  if (exponent < SmallestPower || exponent > LargestPower)
    return false;
  AdjustedMantissa adjusted = {};
  if (!significand) {
    uint64_t bits = (uint64_t)negative << 63;
    memcpy(pValue, &bits, sizeof(bits));
    return true;
  }

  int leadingZeroes = __builtin_clzll(significand);
  uint64_t normalized = significand << leadingZeroes;
  Value128 product = ComputeProduct(exponent, normalized);
  int upperBit = product.high >> 63;
  int shift = upperBit + 9;
  adjusted.mantissa = product.high >> shift;
  adjusted.power2 = BinaryPower(exponent) + upperBit - leadingZeroes + 1023;

  if (adjusted.power2 <= 0) {
    if (-adjusted.power2 + 1 >= 64) {
      adjusted = {};
    } else {
      adjusted.mantissa >>= -adjusted.power2 + 1;
      adjusted.mantissa += adjusted.mantissa & 1;
      adjusted.mantissa >>= 1;
      adjusted.power2 = adjusted.mantissa < (1ull << 52) ? 0 : 1;
    }
  } else {
    if (product.low <= 1 && -4 <= exponent && exponent <= 23 && (adjusted.mantissa & 3) == 1 &&
        (adjusted.mantissa << shift) == product.high)
      adjusted.mantissa &= ~1ull;
    adjusted.mantissa += adjusted.mantissa & 1;
    adjusted.mantissa >>= 1;
    if (adjusted.mantissa >= 2ull << 52) {
      adjusted.mantissa = 1ull << 52;
      ++adjusted.power2;
    }
    adjusted.mantissa &= ~(1ull << 52);
    if (adjusted.power2 >= 0x7ff)
      adjusted = {0, 0x7ff};
  }

  uint64_t bits = adjusted.mantissa | (uint64_t)adjusted.power2 << 52 | (uint64_t)negative << 63;
  memcpy(pValue, &bits, sizeof(bits));
  return true;
}

}  // namespace fast_decimal

static int ClampExponent(long long value)
{
  const int limit = INT_MAX / 2;
  if (value > limit)
    return limit;
  if (value < -limit)
    return -limit;
  return value;
}

// Converts short decimals with one correctly rounded binary64 operation. The
// operands are exact integers, so this is architecture-independent on the
// library's x86-64 and ARM64 targets. Other inputs continue through DiyFp.
static bool TryShortDecimal(uint64_t significand, int exponent, double* pValue)
{
  static constexpr uint64_t PowersOfTen[] = {
    1ull,
    10ull,
    100ull,
    1000ull,
    10000ull,
    100000ull,
    1000000ull,
    10000000ull,
    100000000ull,
    1000000000ull,
    10000000000ull,
    100000000000ull,
    1000000000000ull,
    10000000000000ull,
    100000000000000ull,
    1000000000000000ull,
    10000000000000000ull,
    100000000000000000ull,
    1000000000000000000ull,
    10000000000000000000ull,
  };
  if (exponent < 0) {
    int magnitude = -exponent;
    if (magnitude > 15 || significand > 1ull << 53)
      return false;
    *pValue = (double)significand / (double)PowersOfTen[magnitude];
    return true;
  }
  if (exponent >= (int)(sizeof(PowersOfTen) / sizeof(*PowersOfTen)) || significand > UINT64_MAX / PowersOfTen[exponent])
    return false;
  *pValue = (double)(significand * PowersOfTen[exponent]);
  return true;
}

JSON_INLINE static bool AccumulateDecimalDigit(uint64_t* pSignificand, int* pDigitCount, unsigned digit)
{
  if (*pDigitCount == 19)
    return false;
  *pSignificand = *pSignificand * 10 + digit;
  ++*pDigitCount;
  return true;
}

JSON_INLINE static bool TryFastDouble(const char* pStart, const char* pEnd, const char** ppOutputEnd, double* pOutputValue)
{
  const char* pCursor = pStart;
  bool negative = false;
  if (pCursor < pEnd && *pCursor == '-') {
    negative = true;
    ++pCursor;
  }
  if (pCursor == pEnd)
    return false;

  uint64_t significand = 0;
  int digitCount = 0;

  if (*pCursor == '0') {
    ++digitCount;
    ++pCursor;
    if (pCursor < pEnd && '0' <= *pCursor && *pCursor <= '9')
      return false;
  } else if ('1' <= *pCursor && *pCursor <= '9') {
    do {
      if (!AccumulateDecimalDigit(&significand, &digitCount, *pCursor++ - '0'))
        return false;
    } while (pCursor < pEnd && '0' <= *pCursor && *pCursor <= '9');
  } else {
    return false;
  }

  int exponent = 0;
  if (pCursor < pEnd && *pCursor == '.') {
    ++pCursor;
    if (pCursor == pEnd || *pCursor < '0' || '9' < *pCursor)
      return false;
    do {
      if (!AccumulateDecimalDigit(&significand, &digitCount, *pCursor++ - '0'))
        return false;
      --exponent;
    } while (pCursor < pEnd && '0' <= *pCursor && *pCursor <= '9');
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
      explicitExponent = explicitExponent > (limit - digit) / 10 ? limit : explicitExponent * 10 + digit;
    } while (pCursor < pEnd && '0' <= *pCursor && *pCursor <= '9');
    if (exponentNegative)
      explicitExponent = -explicitExponent;
  }

  exponent = ClampExponent((long long)exponent + explicitExponent);
  if (!fast_decimal::Convert(significand, exponent, negative, pOutputValue)) {
    if (!significand) {
      *pOutputValue = negative ? -0.0 : 0.0;
    } else {
      double converted;
      int significantDigits = 0;
      for (uint64_t digits = significand; digits; digits /= 10)
        ++significantDigits;
      if (!double_conversion::StrtodFast(significand, significantDigits, significantDigits, false, exponent, &converted))
        return false;
      *pOutputValue = negative ? -converted : converted;
    }
  }
  *ppOutputEnd = pCursor;
  return true;
}

///////////////////////////////////////////////////////
// StringToDouble
//  Parses one decimal directly with scratch space in the buffer front.
///////////////////////////////////////////////////////
static constexpr int DoubleParseMaxSignificantDigits = 772;
static constexpr size_t DoubleParseScratchCapacity = DoubleParseMaxSignificantDigits + 1 + alignof(double_conversion::Bignum::Chunk) - 1 +
                                                     2 * double_conversion::Bignum::BigitCapacity * sizeof(double_conversion::Bignum::Chunk);

JSON_INLINE static Json::Status StringToDouble(char* pBase, size_t used, size_t back, const char* pStart, const char* pEnd, const char** ppOutputEnd, double* pOutputValue)
{
  using enum Json::Status;
  if (TryFastDouble(pStart, pEnd, ppOutputEnd, pOutputValue))
    return SUCCESS;
  const char* pCursor = pStart;
  bool negative = false;
  if (pCursor < pEnd && *pCursor == '-') {
    negative = true;
    ++pCursor;
  }
  if (pCursor == pEnd)
    return MALFORMED;

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
    return MALFORMED;
  }

  const char* pFraction = pCursor;
  const char* pFractionEnd = pCursor;
  if (pCursor < pEnd && *pCursor == '.') {
    pFraction = ++pCursor;
    if (pCursor == pEnd || *pCursor < '0' || '9' < *pCursor)
      return MALFORMED;
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
      return MALFORMED;
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
  uint64_t fastSignificand = 0;
  int fastReadDigits = 0;
  int firstFastDroppedDigit = 0;
  auto recordFastDigit = [&](char digit) {
    if (fastSignificand <= UINT64_MAX / 10 - 1) {
      fastSignificand = fastSignificand * 10 + digit - '0';
      ++fastReadDigits;
    } else if (!firstFastDroppedDigit) {
      firstFastDroppedDigit = digit;
    }
  };

  const char* pScan = pInteger;
  if (pScan < pIntegerEnd && *pScan == '0')
    ++pScan;
  for (; pScan < pIntegerEnd; ++pScan) {
    if (significantDigits < DoubleParseMaxSignificantDigits) {
      ++significantDigits;
      trailingZeroes = *pScan == '0' ? trailingZeroes + 1 : 0;
      recordFastDigit(*pScan);
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
    if (significantDigits < DoubleParseMaxSignificantDigits) {
      ++significantDigits;
      decimalExponent = ClampExponent((long long)decimalExponent - 1);
      trailingZeroes = *pScan == '0' ? trailingZeroes + 1 : 0;
      recordFastDigit(*pScan);
    } else {
      nonzeroDigitDropped |= *pScan != '0';
    }
  }

  decimalExponent = ClampExponent((long long)decimalExponent + insignificantIntegerDigits + explicitExponent);
  if (significantDigits && !nonzeroDigitDropped) {
    double converted;
    bool roundUp = firstFastDroppedDigit >= '5';
    bool convertedDirectly = fastReadDigits == significantDigits && TryShortDecimal(fastSignificand, decimalExponent, &converted);
    if (convertedDirectly || double_conversion::StrtodFast(fastSignificand, fastReadDigits, significantDigits, roundUp, decimalExponent, &converted)) {
      *ppOutputEnd = pCursor;
      *pOutputValue = negative ? -converted : converted;
      return SUCCESS;
    }
  }
  int keptDigits = significantDigits;
  if (nonzeroDigitDropped) {
    decimalExponent = ClampExponent((long long)decimalExponent - 1);
    trailingZeroes = 0;
  } else {
    keptDigits -= trailingZeroes;
    decimalExponent = ClampExponent((long long)decimalExponent + trailingZeroes);
  }

  int scratchSize = keptDigits + (nonzeroDigitDropped ? 1 : 0);
  if (used + (size_t)scratchSize > back) {
    JSON_WARN("JSON parse buffer has no room for number conversion scratch space.\n");
    return INSUFFICIENT_SPACE;
  }
  char* pDigits = pBase + used;
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
    if (workspaceAddress + workspaceSize > (uintptr_t)(pBase + back)) {
      JSON_WARN("JSON parse buffer has no room for exact number conversion.\n");
      return INSUFFICIENT_SPACE;
    }
    converted = double_conversion::StrtodTrimmed(JsonSpan<const char>(digitPosition, pDigits), decimalExponent, (double_conversion::Bignum::Chunk*)workspaceAddress);
  }
  *ppOutputEnd = pCursor;
  *pOutputValue = negative ? -converted : converted;
  return SUCCESS;
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

template<typename Buffer>

static void WriteLong(Buffer& buffer, long long value)
{
  char* pOutput = buffer.Reserve(32);
  if (!pOutput)
    return;
  buffer.Commit(LongToString(pOutput, value) - pOutput);
}

static void WriteLong(WritableFile& output, long long value) { output.WriteFormat("%lld", value); }

///////////////////////////////////////////////////////
// WriteDouble
//  Writes the shortest round-trippable number directly into the output sink.
///////////////////////////////////////////////////////
template<typename Buffer>
static void WriteDouble(Buffer& buffer, double value, bool single)
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
  if (-0x1p63 <= value && value < 0x1p63) {
    long long integer = (long long)value;
    if ((double)integer == value) {
      WriteLong(buffer, integer);
      return;
    }
  }

  // Shortest digits and exact bignum workspace live in the sink's uncommitted
  // conversion scratch. This intentionally uses one universal conversion path.
  char* pOutput = buffer.Reserve(32);
  if (!pOutput)
    return;
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
    JsonSpan<char> digits(18, pOutput);
    uintptr_t workspaceAddress = ((uintptr_t)(pOutput + 32) + alignof(double_conversion::Bignum::Chunk) - 1) & ~(uintptr_t)(alignof(double_conversion::Bignum::Chunk) - 1);
    size_t workspaceSize = 4 * double_conversion::Bignum::BigitCapacity * sizeof(double_conversion::Bignum::Chunk);
    if (!buffer.Reserve(workspaceAddress - (uintptr_t)pOutput + workspaceSize))
      return;
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

static void WriteExponent(WritableFile& output, unsigned int magnitude)
{
  if (magnitude >= 100)
    output.Add(magnitude / 100 + '0');
  if (magnitude >= 10)
    output.Add((magnitude / 10) % 10 + '0');
  output.Add(magnitude % 10 + '0');
}

static void WriteDouble(WritableFile& output, double value, bool single)
{
  double_conversion::Double inspected(value);
  if (inspected.IsNan()) {
    output.Append("null");
    return;
  }
  if (inspected.IsInfinite()) {
    if (value < 0)
      output.Add('-');
    output.Append("1e5000");
    return;
  }
  if (-0x1p63 <= value && value < 0x1p63) {
    long long integer = (long long)value;
    if ((double)integer == value) {
      WriteLong(output, integer);
      return;
    }
  }

  bool negative = inspected.Sign() < 0;
  if (negative)
    value = -value;

  char digits[18];
  int length;
  int point;
  if (value == 0) {
    digits[0] = '0';
    length = 1;
    point = 1;
  } else {
    double_conversion::Bignum::Chunk workspace[4 * double_conversion::Bignum::BigitCapacity];
    double_conversion::BignumDtoa(value, single, workspace, JsonSpan<char>(sizeof(digits), digits), &length, &point);
  }

  if (negative && value != 0.0)
    output.Add('-');

  int exponent = point - 1;
  if (-6 <= exponent && exponent < 21) {
    if (point <= 0) {
      output.Append("0.");
      for (int i = point; i < 0; ++i)
        output.Add('0');
      output.Append(digits, length);
    } else if (point >= length) {
      output.Append(digits, length);
      for (int i = length; i < point; ++i)
        output.Add('0');
    } else {
      output.Append(digits, point);
      output.Add('.');
      output.Append(digits + point, length - point);
    }
    return;
  }

  output.Add(digits[0]);
  if (length > 1) {
    output.Add('.');
    output.Append(digits + 1, length - 1);
  }
  output.Add('e');
  output.Add(exponent < 0 ? '-' : '+');
  WriteExponent(output, exponent < 0 ? -exponent : exponent);
}

////////////////////////////////////////////////////////////////////////////////
// Immutable value access
////////////////////////////////////////////////////////////////////////////////

JSON_INLINE static const Json* FindObjectValue(const Json& object, JsonString key)
{
  const char* pObject = (const char*)&object + object.objectOffset;
  const u32* pKeySizes = ObjectKeySizes(pObject);
  const ObjectEntry* pEntries = ObjectEntries(pObject, object.objectSize);
  if (object.objectSize <= ObjectBinarySearchThreshold) {
    u32 i = 0;
    if (object.objectSize >= 16 && key.size <= UINT32_MAX) {
      using KeySizeVector = u32 __attribute__((vector_size(16)));
      KeySizeVector wanted = {(u32)key.size, (u32)key.size, (u32)key.size, (u32)key.size};
      u32 vectorEnd = object.objectSize & ~3u;
      for (; i < vectorEnd; i += 4) {
        KeySizeVector sizes;
        memcpy(&sizes, pKeySizes + i, sizeof(sizes));
        KeySizeVector matches = sizes == wanted;
        if (!(matches[0] | matches[1] | matches[2] | matches[3]))
          continue;
        for (u32 j = i; j < i + 4; ++j) {
          if (pKeySizes[j] != key.size)
            continue;
          const ObjectEntry& entry = pEntries[j];
          const Json* pName = (const Json*)(pObject + entry.keyOffset);
          if (!memcmp(pName->GetString().pData, key.pData, key.size))
            return (const Json*)(pObject + entry.valueOffset);
        }
      }
    }
    for (; i < object.objectSize; ++i) {
      if (pKeySizes[i] != key.size)
        continue;
      const ObjectEntry& entry = pEntries[i];
      const Json* pName = (const Json*)(pObject + entry.keyOffset);
      if (!memcmp(pName->GetString().pData, key.pData, key.size))
        return (const Json*)(pObject + entry.valueOffset);
    }
    return nullptr;
  }

  const u32* pOrder = ObjectSortOrder(pObject, object.objectSize);
  u32 first = 0;
  u32 last = object.objectSize;
  while (first < last) {
    u32 middle = first + (last - first) / 2;
    u32 entryIndex = pOrder[middle];
    u32 keySize = pKeySizes[entryIndex];
    if (keySize < key.size) {
      first = middle + 1;
      continue;
    }
    if (keySize > key.size) {
      last = middle;
      continue;
    }
    const ObjectEntry& entry = pEntries[entryIndex];
    const Json* pName = (const Json*)(pObject + entry.keyOffset);
    int order = memcmp(pName->GetString().pData, key.pData, key.size);
    if (order < 0)
      first = middle + 1;
    else
      last = middle;
  }
  if (first == object.objectSize)
    return nullptr;
  u32 entryIndex = pOrder[first];
  if (pKeySizes[entryIndex] != key.size)
    return nullptr;
  const ObjectEntry& entry = pEntries[entryIndex];
  const Json* pName = (const Json*)(pObject + entry.keyOffset);
  if (memcmp(pName->GetString().pData, key.pData, key.size))
    return nullptr;
  return (const Json*)(pObject + entry.valueOffset);
}

///////////////////////////////////////////////////////
// Json::Contains
//  Finds an object key through its immutable size scan or sorted index.
///////////////////////////////////////////////////////
bool Json::Contains(JsonString key) const
{
  return IsObject() && FindObjectValue(*this, key);
}

const Json* Json::MemberAt(size_t index, JsonString* pKey) const
{
  if (!IsObject() || index >= objectSize)
    return nullptr;

  const char* pObject = (const char*)this + objectOffset;
  const ObjectEntry& entry = ObjectEntries(pObject, objectSize)[index];
  const Json* pName = (const Json*)(pObject + entry.keyOffset);
  if (pKey)
    *pKey = pName->GetString();

  return (const Json*)(pObject + entry.valueOffset);
}

const Json& Json::operator[](JsonString key) const
{
  JSON_ASSERT(IsObject(), "JSON value is not an object.");
  const Json* pValue = FindObjectValue(*this, key);
  JSON_ASSERT(pValue, "JSON object does not contain requested key.");
  return *pValue;
}

Json::Status Json::ToString(JsonSpan<char> output) const
{
  OutputBuffer buffer(output);
  MarshalJson<false>(*this, buffer, 0);
  return buffer.Finish();
}

Json::Status Json::ToStringPretty(JsonSpan<char> output) const
{
  OutputBuffer buffer(output);
  MarshalJson<true>(*this, buffer, 0);
  return buffer.Finish();
}

////////////////////////////////////////////////////////////////////////////////
// JSON serialization
////////////////////////////////////////////////////////////////////////////////

template<typename Buffer>

JSON_INLINE static bool MarshalJsonScalar(const Json& value, Buffer& buffer)
{
  switch (value.type)
  {
    case Json::TYPE_NULL:
      buffer.Append("null");
      return true;
    case Json::TYPE_STRING:
      WriteJsonString(buffer, value.GetString());
      return true;
    case Json::TYPE_PLAIN_STRING:
      buffer.AppendQuoted(value.GetString().pData, value.stringSize);
      return true;
    case Json::TYPE_BOOL:
      if (value.boolValue) buffer.Append("true");
      else                 buffer.Append("false");
      return true;
    case Json::TYPE_LONG:
      if constexpr (requires { buffer.Reserve(2); buffer.Commit(2); }) {
        if (0 <= value.longValue && value.longValue < 100) {
          char* pOutput = buffer.Reserve(2);
          if (!pOutput)
            return true;
          if (value.longValue < 10) {
            pOutput[0] = value.longValue + '0';
            buffer.Commit(1);
          } else {
            pOutput[0] = value.longValue / 10 + '0';
            pOutput[1] = value.longValue % 10 + '0';
            buffer.Commit(2);
          }
          return true;
        }
        if (0 <= value.longValue && value.longValue < 1000) {
          char* pOutput = buffer.Reserve(3);
          if (!pOutput)
            return true;
          pOutput[0] = value.longValue / 100 + '0';
          pOutput[1] = value.longValue / 10 % 10 + '0';
          pOutput[2] = value.longValue % 10 + '0';
          buffer.Commit(3);
          return true;
        }
      }
      WriteLong(buffer, value.longValue);
      return true;
    case Json::TYPE_FLOAT:
      WriteDouble(buffer, value.floatValue, true);
      return true;
    case Json::TYPE_DOUBLE:
      WriteDouble(buffer, value.doubleValue, false);
      return true;
    case Json::TYPE_ARRAY:
    case Json::TYPE_OBJECT:
      return false;
    default:
      JSON_PANIC("Unhandled JSON type.");
  }
}

///////////////////////////////////////////////////////
// MarshalJson
//  Serializes an immutable parsed node directly into caller-owned output.
///////////////////////////////////////////////////////
template<bool Pretty, typename Buffer>
static void MarshalJson(const Json& value, Buffer& buffer, int indent)
{
  if (MarshalJsonScalar(value, buffer))
    return;
  switch (value.type)
  {
    case Json::TYPE_ARRAY: {
      buffer.Add('[');
      u32 size = value.arraySize & Json::ArraySizeMask;
      for (u32 i = 0; i < size; ++i) {
        const Json& child = value[(size_t)i];
        if constexpr (!Pretty && requires { buffer.Reserve(4); buffer.Commit(4); }) {
          if (i && child.type == Json::TYPE_LONG && 0 <= child.longValue && child.longValue < 1000) {
            size_t digits = child.longValue < 10 ? 1 : child.longValue < 100 ? 2 : 3;
            char* pOutput = buffer.Reserve(digits + 1);
            if (!pOutput)
              return;
            pOutput[0] = ',';
            if (digits == 1) {
              pOutput[1] = child.longValue + '0';
            } else if (digits == 2) {
              pOutput[1] = child.longValue / 10 + '0';
              pOutput[2] = child.longValue % 10 + '0';
            } else {
              pOutput[1] = child.longValue / 100 + '0';
              pOutput[2] = child.longValue / 10 % 10 + '0';
              pOutput[3] = child.longValue % 10 + '0';
            }
            buffer.Commit(digits + 1);
            continue;
          }
        }
        if (i) {
          if constexpr (Pretty)
            buffer.Append(", ");
          else
            buffer.Add(',');
        }
        if (!MarshalJsonScalar(child, buffer))
          MarshalJson<Pretty>(child, buffer, indent);
      }
      buffer.Add(']');
      break;
    }
    case Json::TYPE_OBJECT: {
      const char* pObject = (const char*)&value + value.objectOffset;
      const ObjectEntry* pEntries = ObjectEntries(pObject, value.objectSize);
      buffer.Add('{');
      for (u32 i = 0; i < value.objectSize; ++i) {
        const ObjectEntry& entry = pEntries[i];
        const Json* pName = (const Json*)(pObject + entry.keyOffset);
        bool wroteHeader = false;
        if constexpr (!Pretty && requires { buffer.Reserve(4); buffer.Commit(4); }) {
          if (pName->type == Json::TYPE_PLAIN_STRING) {
            JsonString name = pName->GetString();
            size_t count = (i ? 1 : 0) + name.size + 3;
            char* pOutput = buffer.Reserve(count);
            if (!pOutput)
              return;
            char* pCursor = pOutput;
            if (i)
              *pCursor++ = ',';
            *pCursor++ = '"';
            memcpy(pCursor, name.pData, name.size);
            pCursor += name.size;
            *pCursor++ = '"';
            *pCursor++ = ':';
            JSON_ASSERT(pCursor == pOutput + count);
            buffer.Commit(count);
            wroteHeader = true;
          }
        }
        if (!wroteHeader) {
          if constexpr (Pretty) {
            if (value.objectSize > 1) {
              if (i)
                buffer.Append(",\n");
              else
                buffer.Add('\n');
              ++indent;
              for (int indentationIndex = 0; indentationIndex < indent; ++indentationIndex)
                buffer.Append("  ");
            } else if (i) {
              buffer.Add(',');
            }
          } else {
            if (i)
              buffer.Add(',');
          }
          bool wroteName = false;
          if constexpr (Pretty && requires { buffer.Reserve(4); buffer.Commit(4); }) {
            if (pName->type == Json::TYPE_PLAIN_STRING) {
              JsonString name = pName->GetString();
              char* pOutput = buffer.Reserve(name.size + 4);
              if (!pOutput)
                return;
              pOutput[0] = '"';
              memcpy(pOutput + 1, name.pData, name.size);
              pOutput[name.size + 1] = '"';
              pOutput[name.size + 2] = ':';
              pOutput[name.size + 3] = ' ';
              buffer.Commit(name.size + 4);
              wroteName = true;
            }
          }
          if (!wroteName) {
            if (pName->type == Json::TYPE_PLAIN_STRING)
              buffer.AppendQuoted(pName->GetString().pData, pName->stringSize);
            else
              WriteJsonString(buffer, pName->GetString());
            buffer.Add(':');
            if constexpr (Pretty)
              buffer.Add(' ');
          }
        }
        const Json& child = *(const Json*)(pObject + entry.valueOffset);
        if (!MarshalJsonScalar(child, buffer))
          MarshalJson<Pretty>(child, buffer, indent);
        if constexpr (Pretty) {
          if (value.objectSize > 1)
            --indent;
        }
      }
      if constexpr (Pretty) {
        if (value.objectSize > 1) {
          buffer.Add('\n');
          for (int indentationIndex = 0; indentationIndex < indent; ++indentationIndex)
            buffer.Append("  ");
          ++indent;
        }
      }
      buffer.Add('}');
      break;
    }
    default:
      JSON_PANIC("Unhandled JSON type.");
  }
}

template<typename Buffer>

static void WriteJsonString(Buffer& buffer, JsonString string)
{
  const char* pData = string.pData;
  size_t size = string.size;
  size_t plainSize = 0;
  while (plainSize < size) {
    unsigned char value = pData[plainSize];
    if (value < 0x20 || value >= 0x80 || value == '"' || value == '\\' || value == '/')
      break;
    ++plainSize;
  }
  if (plainSize == size) {
    buffer.AppendQuoted(pData, size);
    return;
  }

  buffer.Add('"');
  WriteEscapedString(buffer, string);
  buffer.Add('"');
}

///////////////////////////////////////////////////////
// WriteEscapedString
//  Escapes a bounded UTF-8 string into JSON syntax.
///////////////////////////////////////////////////////
template<typename Buffer>
static void WriteEscapedString(Buffer& buffer, JsonString string)
{
  const char* pData = string.pData;
  size_t size = string.size;
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
        JSON_PANIC("Unhandled character escape code during string serialization.");
    }
  }
}

///////////////////////////////////////////////////////
// MarshalValue
//  Serializes an initializer-list value tree without intermediate storage.
///////////////////////////////////////////////////////
template<bool Pretty, typename Buffer>
static void MarshalValue(const JsonValue& value, Buffer& buffer, int indent)
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
      WriteJsonString(buffer, value.stringValue);
      break;
    case JsonValue::TYPE_ARRAY: {
      const JsonValue* pValues = (const JsonValue*)value.listValue.pData;
      buffer.Add('[');
      for (size_t i = 0; i < value.listValue.size; ++i) {
        if (i) {
          buffer.Add(',');
          if constexpr (Pretty)
            buffer.Add(' ');
        }
        MarshalValue<Pretty>(pValues[i], buffer, indent);
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
        if constexpr (Pretty) {
          if (count > 1) {
            buffer.Add('\n');
            for (int indentationIndex = 0; indentationIndex < indent + 1; ++indentationIndex)
              buffer.Append("  ");
          }
        }
        WriteJsonString(buffer, pMembers[i].key);
        buffer.Add(':');
        if constexpr (Pretty)
          buffer.Add(' ');
        MarshalValue<Pretty>(pMembers[i].value, buffer, indent + 1);
      }
      if constexpr (Pretty) {
        if (count > 1) {
          buffer.Add('\n');
          for (int indentationIndex = 0; indentationIndex < indent; ++indentationIndex)
            buffer.Append("  ");
        }
      }
      buffer.Add('}');
      break;
    }
    default:
      JSON_PANIC("Unhandled JSON write type.");
  }
}

Json::Status WriteJson(const JsonValue& value, JsonSpan<char> output)
{
  OutputBuffer buffer(output);
  MarshalValue<false>(value, buffer, 0);
  return buffer.Finish();
}

Json::Status WriteJsonPretty(const JsonValue& value, JsonSpan<char> output)
{
  OutputBuffer buffer(output);
  MarshalValue<true>(value, buffer, 0);
  return buffer.Finish();
}

Json::Status WriteJson(const Json& value, JsonSpan<char> output) { return value.ToString(output); }

Json::Status WriteJsonPretty(const Json& value, JsonSpan<char> output) { return value.ToStringPretty(output); }

Json::Status WriteJson(const JsonValue& value, WritableFile& output)
{
  if (!output.IsValid()) {
    JSON_WARN("Cannot write JSON to an invalid file.\n");
    return Json::IO_ERROR;
  }
  MarshalValue<false>(value, output, 0);
  return output.Flush() ? Json::SUCCESS : Json::IO_ERROR;
}

Json::Status WriteJsonPretty(const JsonValue& value, WritableFile& output)
{
  if (!output.IsValid()) {
    JSON_WARN("Cannot write JSON to an invalid file.\n");
    return Json::IO_ERROR;
  }
  MarshalValue<true>(value, output, 0);
  return output.Flush() ? Json::SUCCESS : Json::IO_ERROR;
}

Json::Status WriteJson(const Json& value, WritableFile& output)
{
  if (!output.IsValid()) {
    JSON_WARN("Cannot write JSON to an invalid file.\n");
    return Json::IO_ERROR;
  }
  MarshalJson<false>(value, output, 0);
  return output.Flush() ? Json::SUCCESS : Json::IO_ERROR;
}

Json::Status WriteJsonPretty(const Json& value, WritableFile& output)
{
  if (!output.IsValid()) {
    JSON_WARN("Cannot write JSON to an invalid file.\n");
    return Json::IO_ERROR;
  }
  MarshalJson<true>(value, output, 0);
  return output.Flush() ? Json::SUCCESS : Json::IO_ERROR;
}

Json::Status WriteJson(const JsonValue& value, WritableFile&& output) { return WriteJson(value, output); }

Json::Status WriteJsonPretty(const JsonValue& value, WritableFile&& output) { return WriteJsonPretty(value, output); }

Json::Status WriteJson(const Json& value, WritableFile&& output) { return WriteJson(value, output); }

Json::Status WriteJsonPretty(const Json& value, WritableFile&& output) { return WriteJsonPretty(value, output); }

////////////////////////////////////////////////////////////////////////////////
// Immutable backward parser
////////////////////////////////////////////////////////////////////////////////

JSON_INLINE static Json::Status StoreNode(char* pBase, size_t used, size_t& back, Json node, size_t subtreeEnd, u32* pNodeOffset)
{
  u32 offset = BackAlloc(back, used, sizeof(Json), alignof(Json));
  if (offset == InvalidOffset)
    return Json::INSUFFICIENT_SPACE;
  JSON_ASSERT(subtreeEnd >= offset && subtreeEnd - offset <= UINT32_MAX);
  node.span = (u32)(subtreeEnd - offset);
  switch (node.type)
  {
    case Json::TYPE_STRING:
    case Json::TYPE_PLAIN_STRING:
      JSON_ASSERT(node.stringOffset >= offset);
      node.stringOffset -= offset;
      break;
    case Json::TYPE_ARRAY:
      JSON_ASSERT(node.arrayOffset >= offset);
      node.arrayOffset -= offset;
      break;
    case Json::TYPE_OBJECT:
      JSON_ASSERT(node.objectOffset >= offset);
      node.objectOffset -= offset;
      break;
    default:
      break;
  }
  new (pBase + offset) Json(node);
  *pNodeOffset = offset;
  return Json::SUCCESS;
}

JSON_INLINE static Json::Status ParseNumberNode(u32& nodeOffset, char* pBase, size_t used, size_t& back, const char*& pCursor, const char* pEnd)
{
  using enum Json::Status;
  using enum Json::Type;
  size_t subtreeEnd = back;
  const char* pStart = pCursor;
  int sign = 1;
  if (pCursor < pEnd && *pCursor == '-') {
    sign = -1;
    ++pCursor;
  }
  if (pCursor == pEnd || *pCursor < '0' || '9' < *pCursor)
    return MALFORMED;

  Json node;
  unsigned long long magnitude = 0;
  unsigned lastDigit = sign < 0 ? 8 : 7;
  if (*pCursor == '0') {
    ++pCursor;
    if (pCursor < pEnd && (*pCursor == '.' || *pCursor == 'e' || *pCursor == 'E'))
      goto UseDouble;
    node.type = TYPE_LONG;
    node.longValue = 0;
    return StoreNode(pBase, used, back, node, subtreeEnd, &nodeOffset);
  }

  magnitude = *pCursor++ - '0';
  while (pCursor < pEnd) {
    unsigned character = *pCursor & 255;
    if ('0' <= character && character <= '9') {
      unsigned digit = character - '0';
      if (magnitude > 922337203685477580ull ||
          (magnitude == 922337203685477580ull && digit > lastDigit))
        goto UseDouble;
      magnitude = magnitude * 10 + digit;
      ++pCursor;
    } else if (character == '.' || character == 'e' || character == 'E') {
      goto UseDouble;
    } else {
      break;
    }
  }
  node.type = TYPE_LONG;
  node.longValue = sign < 0 ? magnitude == 1ull << 63 ? LLONG_MIN : -(long long)magnitude : (long long)magnitude;
  return StoreNode(pBase, used, back, node, subtreeEnd, &nodeOffset);

UseDouble:
  node.type = TYPE_DOUBLE;
  const char* pNumberEnd;
  Json::Status status = StringToDouble(pBase, used, back, pStart, pEnd, &pNumberEnd, &node.doubleValue);
  if (status != SUCCESS)
    return status;
  pCursor = pNumberEnd;
  return StoreNode(pBase, used, back, node, subtreeEnd, &nodeOffset);
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

static const char* FindUnescapedStringEnd(const char* pStart, const char* pEnd)
{
  const char* pCursor = pStart;
  constexpr uint64_t ByteOnes = 0x0101010101010101ull;
  constexpr uint64_t HighBits = 0x8080808080808080ull;
  while (pEnd - pCursor >= 8) {
    uint64_t bytes;
    memcpy(&bytes, pCursor, sizeof(bytes));
    uint64_t quoteBytes = bytes ^ ByteOnes * '"';
    uint64_t slashBytes = bytes ^ ByteOnes * '\\';
    uint64_t special = ((quoteBytes - ByteOnes) & ~quoteBytes & HighBits) |
                       ((slashBytes - ByteOnes) & ~slashBytes & HighBits) |
                       ((bytes - ByteOnes * 0x20) & ~bytes & HighBits) |
                       (bytes & HighBits);
    if (special) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
      pCursor += __builtin_ctzll(special) / 8;
#else
      pCursor += __builtin_clzll(special) / 8;
#endif
      break;
    }
    pCursor += 8;
  }
  while (pCursor < pEnd) {
    unsigned byte = *pCursor & 255;
    if (byte == '"')
      return pCursor;
    if (byte == '\\' || byte < 0x20)
      return nullptr;
    if (byte >= 0x80)
      break;
    ++pCursor;
  }
  while (pCursor < pEnd) {
    unsigned byte = *pCursor & 255;
    if (byte == '"')
      return pCursor;
    if (byte == '\\' || byte < 0x20)
      return nullptr;
    size_t length = JsonUtf8SequenceLength(pCursor, pEnd);
    if (!length)
      return nullptr;
    pCursor += length;
  }
  return nullptr;
}

JSON_INLINE static bool TryParseSimpleNode(u32& nodeOffset, Json::Status& status, char* pBase, size_t used, size_t& back, const char*& pCursor, const char* pEnd)
{
  using enum Json::Status;
  using enum Json::Type;
  size_t subtreeEnd = back;
  Json node;
  switch (*pCursor)
  {
    case 'n':
      if (pEnd - pCursor < 4 || READ32LE(pCursor) != READ32LE("null"))
        return false;
      pCursor += 4;
      break;
    case 'f':
      if (pEnd - pCursor < 5 || READ32LE(pCursor + 1) != READ32LE("alse"))
        return false;
      pCursor += 5;
      node.type = TYPE_BOOL;
      node.boolValue = false;
      break;
    case 't':
      if (pEnd - pCursor < 4 || READ32LE(pCursor) != READ32LE("true"))
        return false;
      pCursor += 4;
      node.type = TYPE_BOOL;
      node.boolValue = true;
      break;
    case '"': {
      const char* pStringStart = pCursor + 1;
      const char* pStringEnd = FindUnescapedStringEnd(pStringStart, pEnd);
      if (!pStringEnd)
        return false;
      size_t size = pStringEnd - pStringStart;
      u32 stringOffset = BackAlloc(back, used, size + 1, 1);
      if (stringOffset == InvalidOffset) {
        status = INSUFFICIENT_SPACE;
        return true;
      }
      memcpy(pBase + stringOffset, pStringStart, size);
      pBase[stringOffset + size] = '\0';
      pCursor = pStringEnd + 1;
      node.type = TYPE_PLAIN_STRING;
      node.stringOffset = stringOffset;
      node.stringSize = (u32)size;
      break;
    }
    default:
      return false;
  }
  status = StoreNode(pBase, used, back, node, subtreeEnd, &nodeOffset);
  return true;
}

[[gnu::flatten]] static Json::Status ParseJsonRecursive(u32& nodeOffset, char* pBase, size_t used, size_t& back, const char*& pCursor, const char* pEnd, int context, int depth);

static Json::Status ParseArrayNode(u32& nodeOffset, char* pBase, size_t used, size_t& back, const char*& pCursor, const char* pEnd, int depth, size_t subtreeEnd)
{
  using enum Json::Status;
  using enum Json::Type;
  if (!depth)
    return MALFORMED;
  Json node;
  u32 elementCount = 0;
  u32 lastChildOffset = 0;
  bool reversedScalarArray = true;
  int context = ARRAY;
  for (;;) {
    u32 childOffset;
    Json::Status status;
    const char* pNumber = pCursor;
    while (pNumber < pEnd && (*pNumber == ' ' || *pNumber == '\n' || *pNumber == '\r' || *pNumber == '\t'))
      ++pNumber;
    if (context & COMMA) {
      if (pNumber < pEnd && *pNumber == ',') {
        do {
          ++pNumber;
        } while (pNumber < pEnd && (*pNumber == ' ' || *pNumber == '\n' || *pNumber == '\r' || *pNumber == '\t'));
      } else {
        pNumber = pEnd;
      }
    }
    if (pNumber < pEnd && (*pNumber == '-' || ('0' <= *pNumber && *pNumber <= '9'))) {
      pCursor = pNumber;
      status = ParseNumberNode(childOffset, pBase, used, back, pCursor, pEnd);
    } else if (pNumber < pEnd) {
      const char* pValue = pNumber;
      if (TryParseSimpleNode(childOffset, status, pBase, used, back, pValue, pEnd))
        pCursor = pValue;
      else
        status = ParseJsonRecursive(childOffset, pBase, used, back, pCursor, pEnd, context, depth - 1);
    } else {
      status = ParseJsonRecursive(childOffset, pBase, used, back, pCursor, pEnd, context, depth - 1);
    }
    if (status == ABSENT_VALUE) {
      if (elementCount && reversedScalarArray) {
        node.type = TYPE_ARRAY;
        node.arrayOffset = lastChildOffset;
        node.arraySize = elementCount | Json::ReversedArrayFlag;
        return StoreNode(pBase, used, back, node, subtreeEnd, &nodeOffset);
      }
      size_t arraySize = (size_t)elementCount * sizeof(Json);
      u32 arrayOffset = BackAlloc(back, used, arraySize, alignof(Json));
      if (arrayOffset == InvalidOffset)
        return INSUFFICIENT_SPACE;
      u32 cursorOffset = lastChildOffset;
      for (u32 i = elementCount; i--;) {
        const Json* pChild = (const Json*)(pBase + cursorOffset);
        u32 childSpan = pChild->span;
        u32 childOffset = arrayOffset + i * sizeof(Json);
        JSON_ASSERT(cursorOffset >= childOffset);
        u32 delta = cursorOffset - childOffset;
        Json child = *pChild;
        JSON_ASSERT((uint64_t)child.span + delta <= UINT32_MAX);
        child.span += delta;
        switch (child.type)
        {
          case TYPE_STRING:
          case TYPE_PLAIN_STRING:
            JSON_ASSERT((uint64_t)child.stringOffset + delta <= UINT32_MAX);
            child.stringOffset += delta;
            break;
          case TYPE_ARRAY:
            JSON_ASSERT((uint64_t)child.arrayOffset + delta <= UINT32_MAX);
            child.arrayOffset += delta;
            break;
          case TYPE_OBJECT:
            JSON_ASSERT((uint64_t)child.objectOffset + delta <= UINT32_MAX);
            child.objectOffset += delta;
            break;
          default:
            break;
        }
        new (pBase + childOffset) Json(child);
        cursorOffset += childSpan;
      }
      node.type = TYPE_ARRAY;
      node.arrayOffset = arrayOffset;
      node.arraySize = elementCount;
      return StoreNode(pBase, used, back, node, subtreeEnd, &nodeOffset);
    }
    if (status != SUCCESS)
      return status;
    lastChildOffset = childOffset;
    reversedScalarArray &= ((const Json*)(pBase + childOffset))->span == sizeof(Json);
    ++elementCount;
    context = ARRAY | COMMA;
  }
}

///////////////////////////////////////////////////////
// ParseJson
//  Parses one subtree backward into immutable buffer records.
///////////////////////////////////////////////////////
[[gnu::flatten]] static Json::Status ParseJsonRecursive(u32& nodeOffset, char* pBase, size_t used, size_t& back, const char*& pCursor, const char* pEnd, int context, int depth)
{
  using enum Json::Status;
  using enum Json::Type;
  char encodedBytes[Utf8MaximumSequenceSize];
  unsigned long long integerMagnitude;
  const char* pNumberStart;
  int hexA, hexB, hexC, hexD, character, sign, byteCount, lowSurrogate, integerLastDigit;
  if (!depth)
    return MALFORMED;
  size_t subtreeEnd = back;
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
          return StoreNode(pBase, used, back, node, subtreeEnd, &nodeOffset);
        }
        return MALFORMED;

      case 'f':
        if (context & (KEY | COLON | COMMA))
          return MALFORMED;
        if (pCursor + 4 <= pEnd && READ32LE(pCursor) == READ32LE("alse")) {
          pCursor += 4;
          node.type = TYPE_BOOL;
          node.boolValue = false;
          return StoreNode(pBase, used, back, node, subtreeEnd, &nodeOffset);
        }
        return MALFORMED;

      case 't':
        if (context & (KEY | COLON | COMMA))
          return MALFORMED;
        if (pCursor + 3 <= pEnd && READ32LE(pCursor - 1) == READ32LE("true")) {
          pCursor += 3;
          node.type = TYPE_BOOL;
          node.boolValue = true;
          return StoreNode(pBase, used, back, node, subtreeEnd, &nodeOffset);
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
        return StoreNode(pBase, used, back, node, subtreeEnd, &nodeOffset);

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
        integerMagnitude = character - '0';
        integerLastDigit = sign < 0 ? 8 : 7;
        for (; pCursor < pEnd; ++pCursor) {
          character = *pCursor & 255;
          if (isdigit(character)) {
            unsigned digit = character - '0';
            if (integerMagnitude > 922337203685477580ull ||
                (integerMagnitude == 922337203685477580ull && digit > (unsigned)integerLastDigit))
              goto UseDouble;
            integerMagnitude = integerMagnitude * 10 + digit;
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
        if (sign < 0)
          node.longValue = integerMagnitude == 1ull << 63 ? LLONG_MIN : -(long long)integerMagnitude;
        else
          node.longValue = integerMagnitude;
        return StoreNode(pBase, used, back, node, subtreeEnd, &nodeOffset);

      UseDouble: {
        node.type = TYPE_DOUBLE;
        const char* pNumberEnd;
        Json::Status numberStatus = StringToDouble(pBase, used, back, pNumberStart, pEnd, &pNumberEnd, &node.doubleValue);
        if (numberStatus != SUCCESS)
          return numberStatus;
        pCursor = pNumberEnd;
        return StoreNode(pBase, used, back, node, subtreeEnd, &nodeOffset);
      }

      case '[': {
        if (context & (COLON | COMMA | KEY))
          return MALFORMED;
        return ParseArrayNode(nodeOffset, pBase, used, back, pCursor, pEnd, depth, subtreeEnd);
      }

      case ']':
        return context & ARRAY ? ABSENT_VALUE : MALFORMED;

      case '}':
        return context & OBJECT ? ABSENT_VALUE : MALFORMED;

      case '{': {
        if (context & (COLON | COMMA | KEY))
          return MALFORMED;
        size_t scratchMark = used;
        u32 memberCount = 0;
        for (context = KEY | OBJECT;;) {
          u32 keyOffset;
          Json::Status status;
          const char* pKeyStart = pCursor;
          while (pKeyStart < pEnd && (*pKeyStart == ' ' || *pKeyStart == '\n' || *pKeyStart == '\r' || *pKeyStart == '\t'))
            ++pKeyStart;
          if (context & COMMA) {
            if (pKeyStart < pEnd && *pKeyStart == ',') {
              do {
                ++pKeyStart;
              } while (pKeyStart < pEnd && (*pKeyStart == ' ' || *pKeyStart == '\n' || *pKeyStart == '\r' || *pKeyStart == '\t'));
            } else {
              pKeyStart = pEnd;
            }
          }
          if (pKeyStart < pEnd && *pKeyStart == '"') {
            const char* pStringStart = pKeyStart + 1;
            if (const char* pStringEnd = FindUnescapedStringEnd(pStringStart, pEnd)) {
              size_t keyEnd = back;
              size_t size = pStringEnd - pStringStart;
              u32 stringOffset = BackAlloc(back, used, size + 1, 1);
              if (stringOffset == InvalidOffset)
                return INSUFFICIENT_SPACE;
              memcpy(pBase + stringOffset, pStringStart, size);
              pBase[stringOffset + size] = '\0';
              Json key;
              key.type = TYPE_PLAIN_STRING;
              key.stringOffset = stringOffset;
              key.stringSize = (u32)size;
              status = StoreNode(pBase, used, back, key, keyEnd, &keyOffset);
              pCursor = pStringEnd + 1;
            } else {
              status = ParseJsonRecursive(keyOffset, pBase, used, back, pCursor, pEnd, context, depth - 1);
            }
          } else {
            status = ParseJsonRecursive(keyOffset, pBase, used, back, pCursor, pEnd, context, depth - 1);
          }
          if (status == ABSENT_VALUE) {
            size_t indexSize = (size_t)memberCount * (sizeof(u32) + sizeof(ObjectEntry));
            if (memberCount > ObjectBinarySearchThreshold)
              indexSize += (size_t)memberCount * sizeof(u32);
            u32 indexOffset = BackAlloc(back, used, indexSize, alignof(u32));
            if (indexOffset == InvalidOffset)
              return INSUFFICIENT_SPACE;
            char* pIndex = pBase + indexOffset;
            u32* pKeySizes = ObjectKeySizes(pIndex);
            ObjectEntry* pEntries = ObjectEntries(pIndex, memberCount);
            const u32* pOffsets = (const u32*)(pBase + scratchMark);
            for (u32 i = 0; i < memberCount; ++i) {
              u32 storedKeyOffset = pOffsets[2 * i];
              u32 valueOffset = pOffsets[2 * i + 1];
              const Json* pKey = (const Json*)(pBase + storedKeyOffset);
              JSON_ASSERT(pKey->IsString());
              pKeySizes[i] = pKey->stringSize;
              pEntries[i] = {
                storedKeyOffset - indexOffset,
                valueOffset - indexOffset,
              };
            }
            if (memberCount > ObjectBinarySearchThreshold) {
              u32* pOrder = ObjectSortOrder(pIndex, memberCount);
              for (u32 i = 0; i < memberCount; ++i)
                pOrder[i] = i;
              std::sort(pOrder, pOrder + memberCount, [&](u32 left, u32 right) {
                if (pKeySizes[left] != pKeySizes[right])
                  return pKeySizes[left] < pKeySizes[right];
                const Json* pLeft = (const Json*)(pIndex + pEntries[left].keyOffset);
                const Json* pRight = (const Json*)(pIndex + pEntries[right].keyOffset);
                int order = memcmp(pLeft->GetString().pData, pRight->GetString().pData, pKeySizes[left]);
                return order ? order < 0 : left < right;
              });
            }
            node.type = TYPE_OBJECT;
            node.objectOffset = indexOffset;
            node.objectSize = memberCount;
            used = scratchMark;
            return StoreNode(pBase, used, back, node, subtreeEnd, &nodeOffset);
          }
          if (status != SUCCESS)
            return status;
          const Json* pKey = (const Json*)(pBase + keyOffset);
          if (!pKey->IsString())
            return MALFORMED;
          u32 valueOffset;
          const char* pNumber = pCursor;
          while (pNumber < pEnd && (*pNumber == ' ' || *pNumber == '\n' || *pNumber == '\r' || *pNumber == '\t'))
            ++pNumber;
          if (pNumber < pEnd && *pNumber == ':') {
            do {
              ++pNumber;
            } while (pNumber < pEnd && (*pNumber == ' ' || *pNumber == '\n' || *pNumber == '\r' || *pNumber == '\t'));
          } else {
            pNumber = pEnd;
          }
          if (pNumber < pEnd && (*pNumber == '-' || ('0' <= *pNumber && *pNumber <= '9'))) {
            pCursor = pNumber;
            status = ParseNumberNode(valueOffset, pBase, used, back, pCursor, pEnd);
          } else if (pNumber < pEnd && *pNumber == '[') {
            size_t childEnd = back;
            pCursor = pNumber + 1;
            status = ParseArrayNode(valueOffset, pBase, used, back, pCursor, pEnd, depth - 1, childEnd);
          } else if (pNumber < pEnd) {
            const char* pValue = pNumber;
            if (TryParseSimpleNode(valueOffset, status, pBase, used, back, pValue, pEnd))
              pCursor = pValue;
            else
              status = ParseJsonRecursive(valueOffset, pBase, used, back, pCursor, pEnd, COLON, depth - 1);
          } else {
            status = ParseJsonRecursive(valueOffset, pBase, used, back, pCursor, pEnd, COLON, depth - 1);
          }
          if (status != SUCCESS)
            return status;
          if (back - used < 2 * sizeof(u32)) {
            JSON_WARN("JSON parse buffer has no room for object offset scratch space.\n");
            return INSUFFICIENT_SPACE;
          }
          u32* pOffsets = (u32*)(pBase + used);
          pOffsets[0] = keyOffset;
          pOffsets[1] = valueOffset;
          used += 2 * sizeof(u32);
          ++memberCount;
          context = KEY | COMMA | OBJECT;
        }
      }

      case '"': {
        if (context & (COLON | COMMA))
          return MALFORMED;
        const char* pStringStart = pCursor;
        if (const char* pStringEnd = FindUnescapedStringEnd(pStringStart, pEnd)) {
          size_t size = pStringEnd - pStringStart;
          u32 stringOffset = BackAlloc(back, used, size + 1, 1);
          if (stringOffset == InvalidOffset)
            return INSUFFICIENT_SPACE;
          memcpy(pBase + stringOffset, pStringStart, size);
          pBase[stringOffset + size] = '\0';
          pCursor = pStringEnd + 1;
          node.type = TYPE_PLAIN_STRING;
          node.stringOffset = stringOffset;
          node.stringSize = (u32)size;
          return StoreNode(pBase, used, back, node, subtreeEnd, &nodeOffset);
        }
        u32 nullOffset = BackAlloc(back, used, 1, 1);
        if (nullOffset == InvalidOffset)
          return INSUFFICIENT_SPACE;
        pBase[nullOffset] = '\0';
        bool arenaFull = false;
        auto appendBytes = [&](const char* pSource, size_t size) {
          for (size_t i = 0; i < size; ++i) {
            if (arenaFull)
              return;
            u32 offset = BackAlloc(back, used, 1, 1);
            if (offset == InvalidOffset) {
              arenaFull = true;
              return;
            }
            pBase[offset] = pSource[i];
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
              if (arenaFull)
                return INSUFFICIENT_SPACE;
              u32 dataOffset = (u32)back;
              size_t size = nullOffset - dataOffset;
              ReverseBytes(pBase + dataOffset, size);
              node.type = TYPE_STRING;
              node.stringOffset = dataOffset;
              node.stringSize = (u32)size;
              return StoreNode(pBase, used, back, node, subtreeEnd, &nodeOffset);
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
          if (arenaFull)
            return INSUFFICIENT_SPACE;
        }
      }

      default:
        return MALFORMED;
    }
  }
  return depth == DEPTH ? ABSENT_VALUE : MALFORMED;
}

///////////////////////////////////////////////////////
// Json::EstimateSize
//  Returns a deliberately loose upper bound without reading or parsing input.
///////////////////////////////////////////////////////
size_t Json::EstimateSize(const char* pData, size_t size)
{
  if (!pData && size) {
    JSON_WARN("JSON size estimate input cannot be null when its size is nonzero.\n");
    return SIZE_MAX;
  }
  if (!size)
    return 0;

  // Every value or key consumes at least one distinct input byte. In the
  // worst case a value owns one node, one copied array slot, one aggregate
  // header, and their alignment padding. A key owns one node, one cached size,
  // one object entry, one temporary key/value offset pair, at most one sorted
  // index, and node padding. Decoded strings plus terminators consume less
  // space than their source tokens. These bounds are currently 49 bytes per
  // value and 47 per key, plus one
  // string byte.
  constexpr uint64_t ValueBytes = 2 * sizeof(Json) + 2 * (alignof(Json) - 1) + alignof(u32) - 1;
  constexpr uint64_t KeyBytes = sizeof(Json) + 4 * sizeof(u32) + sizeof(ObjectEntry) + alignof(Json) - 1;
  constexpr uint64_t BytesPerInputByte = 64;
  static_assert((ValueBytes > KeyBytes ? ValueBytes : KeyBytes) + 1 <= BytesPerInputByte,
                "JSON parse-size multiplier no longer covers the immutable layout.");

  constexpr uint64_t MaximumBufferSize = UINT32_MAX - 1;
  if (size > (MaximumBufferSize - DoubleParseScratchCapacity) / BytesPerInputByte)
    return SIZE_MAX;
  return (size_t)(size * BytesPerInputByte + DoubleParseScratchCapacity);
}

///////////////////////////////////////////////////////
// ParseJson
//  Parses one bounded JSON document and rolls back on failure.
///////////////////////////////////////////////////////
static Json::Status ParseJson(const char* pData, size_t size, JsonBuffer* pBuffer)
{
  using enum Json::Status;
  if (!pBuffer || !pBuffer->pData) {
    JSON_WARN("JSON parse buffer cannot be null.\n");
    return INVALID_ARGUMENT;
  }
  pBuffer->pRoot = nullptr;
  if (!pData && size) {
    JSON_WARN("JSON input cannot be null when its size is nonzero.\n");
    return INVALID_ARGUMENT;
  }
  if (!pData)
    return ABSENT_VALUE;

  size_t backMark = pBuffer->back;
  const char* pCursor = pData;
  const char* pEnd = pData + size;
  u32 rootOffset = 0;
  Json::Status status = ParseJsonRecursive(rootOffset, pBuffer->pData, pBuffer->used, pBuffer->back, pCursor, pEnd, 0, DEPTH);
  while (status == SUCCESS && pCursor < pEnd && (*pCursor == ' ' || *pCursor == '\n' || *pCursor == '\r' || *pCursor == '\t'))
    ++pCursor;
  if (status != SUCCESS || pCursor != pEnd) {
    pBuffer->back = backMark;
    return status == INSUFFICIENT_SPACE ? status : MALFORMED;
  }
  pBuffer->pRoot = (const Json*)(pBuffer->pData + rootOffset);
  return SUCCESS;
}

Json::Status Json::Parse(const char* pData, size_t size, JsonBuffer* pBuffer) { return ParseJson(pData, size, pBuffer); }


const char* Json::StatusToString(Status status)
{
  switch (status)
  {
    case SUCCESS:
      return "success";
    case MALFORMED:
      return "JSON is malformed.";
    case ABSENT_VALUE:
      return "JSON input contains no value.";
    case INVALID_ARGUMENT:
      return "Invalid JSON API argument.";
    case INSUFFICIENT_SPACE:
      return "JSON buffer has insufficient space.";
    case IO_ERROR:
      return "JSON file mapping failed.";
    default:
      JSON_PANIC("Unhandled JSON status.");
  }
}

////////////////////////////////////////////////////////////////////////////////
}  // namespace flat
////////////////////////////////////////////////////////////////////////////////
