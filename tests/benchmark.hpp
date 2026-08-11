#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace benchmark {

inline constexpr char JsonText[] = R"json({
  "array": [
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
    48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63
  ],
  "object": {
    "key00": 0, "key01": 1, "key02": 2, "key03": 3,
    "key04": 4, "key05": 5, "key06": 6, "key07": 7,
    "key08": 8, "key09": 9, "key10": 10, "key11": 11,
    "key12": 12, "key13": 13, "key14": 14, "key15": 15,
    "key16": 16, "key17": 17, "key18": 18, "key19": 19,
    "key20": 20, "key21": 21, "key22": 22, "key23": 23,
    "key24": 24, "key25": 25, "key26": 26, "key27": 27,
    "key28": 28, "key29": 29, "key30": 30, "key31": 31,
    "target": 31337
  },
  "integer": 123456789,
  "floating": 3.1415927,
  "string": "flat-json-benchmark-string",
  "nested": [
    {"id": 1, "name": "alpha", "samples": [1.25, 2.5, 5.0]},
    {"id": 2, "name": "beta", "samples": [10.0, 20.0, 40.0]},
    {"id": 3, "name": "gamma", "samples": [0.125, 0.25, 0.5]},
    {"id": 4, "name": "delta", "samples": [100, 200, 400]}
  ],
  "metadata": {
    "active": true,
    "missing": null,
    "description": "One shared document for parse, serialize, and lookup benchmarks",
    "tags": ["json", "flat", "arena", "immutable", "benchmark"]
  }
})json";

inline constexpr size_t JsonSize = sizeof(JsonText) - 1;
inline constexpr size_t ArrayLookupIndex = 63;
inline constexpr long long ArrayLookupValue = 63;
inline constexpr long long ObjectLookupValue = 31337;
inline constexpr long long IntegerValue = 123456789;
inline constexpr double FloatingValue = 3.1415927;
inline constexpr char StringValue[] = "flat-json-benchmark-string";
inline constexpr size_t StringSize = sizeof(StringValue) - 1;

// The 32-bit workload covers every JSON kind, nested arrays and objects,
// escaped and Unicode strings, signed-int32 boundaries, and decimal values
// spanning the finite binary32 range. JSON has no float32 type, so validation
// compares each parsed decimal after binary32 rounding.
inline constexpr char Parse32BitJsonText[] = R"json([
  [
    -2147483648, 2147483647, -1000000000, 1000000000,
    -123456789, 123456789, -65536, 65536,
    -32768, 32767, -1024, 1024, -1, 0, 1, 42
  ],
  [
    0.0, -0.0, 1.0, -1.0,
    1.5, -2.5, 0.125, 3.1415927,
    1e-20, -1e20, 16777216.0, 0.000001,
    12345.625, 6.02214076e23, 1.17549435e-38, 3.4028235e38
  ],
  {
    "null": null,
    "booleans": [true, false],
    "strings": ["", "ASCII", "line\nquote\"slash\\", "\u03c0"],
    "nested": [{"id": 1, "values": [0, 1.25, -2.5]}, [], {}]
  }
])json";

inline constexpr int32_t Int32Values[] = {
  INT32_MIN, INT32_MAX, -1000000000, 1000000000,
  -123456789, 123456789, -65536, 65536,
  -32768, 32767, -1024, 1024, -1, 0, 1, 42,
};

inline constexpr float FloatRangeValues[] = {
  0.0f, -0.0f, 1.0f, -1.0f,
  1.5f, -2.5f, 0.125f, 3.1415927f,
  1e-20f, -1e20f, 16777216.0f, 0.000001f,
  12345.625f, 6.02214076e23f, 1.17549435e-38f, 3.4028235e38f,
};

inline constexpr size_t Int32Count = sizeof(Int32Values) / sizeof(*Int32Values);
inline constexpr size_t FloatRangeCount = sizeof(FloatRangeValues) / sizeof(*FloatRangeValues);
inline constexpr size_t Parse32BitJsonSize = sizeof(Parse32BitJsonText) - 1;

// The 64-bit workload repeats every 32-bit case and adds exact signed-int64
// and correctly rounded IEEE-754 binary64 boundaries.
inline constexpr char Parse64BitJsonText[] = R"json([
  [
    -2147483648, 2147483647, -1000000000, 1000000000,
    -123456789, 123456789, -65536, 65536,
    -32768, 32767, -1024, 1024, -1, 0, 1, 42
  ],
  [
    0.0, -0.0, 1.0, -1.0,
    1.5, -2.5, 0.125, 3.1415927,
    1e-20, -1e20, 16777216.0, 0.000001,
    12345.625, 6.02214076e23, 1.17549435e-38, 3.4028235e38
  ],
  [
    -9223372036854775808,
    9223372036854775807,
    -9007199254740993,
    9007199254740993,
    -4294967297,
    4294967297
  ],
  [
    0.1000000000000000055511151231257827021181583404541015625,
    2.2250738585072014e-308,
    1.7976931348623157e308,
    4.9406564584124654e-324,
    1.00000000000000011102230246251565404236316680908203125,
    9007199254740991.0
  ],
  {
    "null": null,
    "booleans": [true, false],
    "strings": ["", "ASCII", "line\nquote\"slash\\", "\u03c0"],
    "nested": [{"id": 1, "values": [0, 1.25, -2.5]}, [], {}]
  }
])json";

inline constexpr size_t Parse64BitJsonSize = sizeof(Parse64BitJsonText) - 1;
inline constexpr int64_t ExactIntegerValues[] = {
  INT64_MIN,
  INT64_MAX,
  -INT64_C(9007199254740993),
  INT64_C(9007199254740993),
  -INT64_C(4294967297),
  INT64_C(4294967297),
};
inline constexpr double ExactDoubleValues[] = {
  0.1,
  0x1p-1022,
  0x1.fffffffffffffp+1023,
  0x1p-1074,
  1.0,
  9007199254740991.0,
};
inline constexpr size_t ExactIntegerCount = sizeof(ExactIntegerValues) / sizeof(*ExactIntegerValues);
inline constexpr size_t ExactDoubleCount = sizeof(ExactDoubleValues) / sizeof(*ExactDoubleValues);

inline uint64_t DoubleBits(double value);
inline uint32_t FloatBits(float value);

template<typename GetInteger, typename GetDouble> bool ValidateExactNumbers(GetInteger getInteger, GetDouble getDouble)
{
  for (size_t i = 0; i < ExactIntegerCount; ++i) {
    if (getInteger(i) != ExactIntegerValues[i])
      return false;
  }
  for (size_t i = 0; i < ExactDoubleCount; ++i) {
    if (DoubleBits(getDouble(i)) != DoubleBits(ExactDoubleValues[i]))
      return false;
  }
  return true;
}

template<typename GetInteger> bool ValidateInt32Numbers(GetInteger getInteger)
{
  for (size_t i = 0; i < Int32Count; ++i) {
    if (getInteger(i) != Int32Values[i])
      return false;
  }
  return true;
}

template<typename GetDouble> bool ValidateFloatRangeNumbers(GetDouble getDouble)
{
  for (size_t i = 0; i < FloatRangeCount; ++i) {
    if (FloatBits((float)getDouble(i)) != FloatBits(FloatRangeValues[i]))
      return false;
  }
  return true;
}

inline volatile uint64_t ResultSink = 0;

template<typename Value> inline void DoNotOptimize(const Value& value)
{
#if defined(__GNUC__) || defined(__clang__)
  __asm__ volatile("" : : "g"(&value) : "memory");
#else
  ResultSink = ResultSink ^ (uintptr_t)&value;
#endif
}

inline uint64_t DoubleBits(double value)
{
  uint64_t bits;
  memcpy(&bits, &value, sizeof(bits));
  return bits;
}

inline uint32_t FloatBits(float value)
{
  uint32_t bits;
  memcpy(&bits, &value, sizeof(bits));
  return bits;
}

inline uint64_t StringChecksum(const char* pData, size_t size)
{
  if (!pData || !size)
    return 0;
  return size + (unsigned char)pData[0] * 257u + (unsigned char)pData[size - 1] * 65537u;
}

struct Measurement
{
  bool supported;
  double nanoseconds;
};

template<typename Function> Measurement Measure(bool supported, size_t initialIterations, Function&& function)
{
  if (!supported)
    return {false, 0};

  using Clock = std::chrono::steady_clock;
  constexpr long long TargetNanoseconds = 25'000'000;
  size_t iterations = initialIterations;
  long long elapsed = 0;
  for (;;) {
    auto start = Clock::now();
    uint64_t result = function(iterations);
    auto end = Clock::now();
    ResultSink = ResultSink ^ result;
    elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    if (elapsed >= TargetNanoseconds || iterations > SIZE_MAX / 16)
      break;
    size_t factor = (size_t)((TargetNanoseconds + elapsed - 1) / std::max<long long>(elapsed, 1));
    factor = std::clamp<size_t>(factor, 2, 16);
    iterations *= factor;
  }

  std::array<double, 7> samples;
  for (double& sample : samples) {
    auto start = Clock::now();
    uint64_t result = function(iterations);
    auto end = Clock::now();
    ResultSink = ResultSink ^ result;
    elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    sample = (double)elapsed / iterations;
  }
  std::sort(samples.begin(), samples.end());
  return {true, samples[samples.size() / 2]};
}

inline void PrintMeasurement(Measurement measurement, const char* pUnavailableSuffix = "")
{
  if (measurement.supported)
    printf("%.1f ns", measurement.nanoseconds);
  else
    printf("N/A%s", pUnavailableSuffix);
}

template<typename Adapter> int Run()
{
  Adapter adapter;
  if (!adapter.Prepare()) {
    fprintf(stderr, "%s benchmark setup failed.\n", Adapter::Name);
    return 1;
  }
  if (Adapter::SupportsCompactSerialize && !adapter.SerializeCompact(1)) {
    fprintf(stderr, "%s compact serialization validation failed.\n", Adapter::Name);
    return 1;
  }
  if (Adapter::SupportsPrettySerialize && !adapter.SerializePretty(1)) {
    fprintf(stderr, "%s pretty serialization validation failed.\n", Adapter::Name);
    return 1;
  }
  if constexpr (Adapter::SupportsParse32Bit) {
    if (!adapter.ValidateParse32Bit()) {
      fprintf(stderr, "%s 32-bit parsing validation failed.\n", Adapter::Name);
      return 1;
    }
  }
  if constexpr (Adapter::SupportsParse64Bit) {
    if (!adapter.ValidateParse64Bit()) {
      fprintf(stderr, "%s 64-bit parsing validation failed.\n", Adapter::Name);
      return 1;
    }
  }

  Measurement parse32Bit{false, 0};
  if constexpr (Adapter::SupportsParse32Bit)
    parse32Bit = Measure(true, 16, [&](size_t iterations) { return adapter.Parse32Bit(iterations); });
  Measurement parse64Bit{false, 0};
  if constexpr (Adapter::SupportsParse64Bit)
    parse64Bit = Measure(true, 16, [&](size_t iterations) { return adapter.Parse64Bit(iterations); });
  Measurement compactSerialize = Measure(Adapter::SupportsCompactSerialize, 16, [&](size_t iterations) { return adapter.SerializeCompact(iterations); });
  Measurement prettySerialize = Measure(Adapter::SupportsPrettySerialize, 16, [&](size_t iterations) { return adapter.SerializePretty(iterations); });
  Measurement arrayLookup = Measure(true, 4096, [&](size_t iterations) { return adapter.LookupArray(iterations); });
  Measurement objectLookup = Measure(true, 4096, [&](size_t iterations) { return adapter.LookupObject(iterations); });
  Measurement integerAccess = Measure(true, 4096, [&](size_t iterations) { return adapter.AccessInteger(iterations); });
  Measurement floatingAccess = Measure(true, 4096, [&](size_t iterations) { return adapter.AccessFloating(iterations); });
  Measurement stringAccess = Measure(true, 4096, [&](size_t iterations) { return adapter.AccessString(iterations); });

  const char* pSerializeUnavailableSuffix = "";
  if constexpr (requires { Adapter::SerializeUnavailableSuffix; })
    pSerializeUnavailableSuffix = Adapter::SerializeUnavailableSuffix;

  printf("| %s | ", Adapter::Name);
  PrintMeasurement(parse32Bit);
  printf(" | ");
  PrintMeasurement(parse64Bit);
  printf(" | ");
  PrintMeasurement(compactSerialize, pSerializeUnavailableSuffix);
  printf(" | ");
  PrintMeasurement(prettySerialize, pSerializeUnavailableSuffix);
  printf(" | ");
  PrintMeasurement(arrayLookup);
  printf(" | ");
  PrintMeasurement(objectLookup);
  printf(" | ");
  PrintMeasurement(integerAccess);
  printf(" | ");
  PrintMeasurement(floatingAccess);
  printf(" | ");
  PrintMeasurement(stringAccess);
  printf(" |\n");
  return 0;
}

}  // namespace benchmark
