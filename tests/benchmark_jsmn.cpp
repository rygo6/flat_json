#include "benchmark.hpp"

#define JSMN_STATIC
#include <jsmn.h>

#include <array>

struct JsmnBenchmark
{
  static constexpr const char* Name = "zserge/jsmn";
  static constexpr bool SupportsCompactSerialize = false;
  static constexpr bool SupportsPrettySerialize = false;
  static constexpr bool SupportsParse32Bit = false;
  static constexpr bool SupportsParse64Bit = false;
  static constexpr size_t TokenCapacity = 512;

  std::array<jsmntok_t, TokenCapacity> tokens;
  int tokenCount = 0;
  int array = -1;
  int object = -1;
  int integer = -1;
  int floating = -1;
  int string = -1;

  int NextToken(int index) const
  {
    int next = index + 1;
    while (next < tokenCount && tokens[next].start < tokens[index].end)
      ++next;
    return next;
  }

  bool TokenEquals(int index, const char* pText, size_t size) const
  {
    const jsmntok_t& token = tokens[index];
    return token.type == JSMN_STRING && token.end - token.start == (int)size &&
           !memcmp(benchmark::JsonText + token.start, pText, size);
  }

  int FindObjectValue(int objectIndex, const char* pKey, size_t size) const
  {
    for (int key = objectIndex + 1; key < tokenCount && tokens[key].start < tokens[objectIndex].end;) {
      int value = key + 1;
      if (TokenEquals(key, pKey, size))
        return value;
      key = NextToken(value);
    }
    return -1;
  }

  int FindArrayValue(int arrayIndex, size_t target) const
  {
    int value = arrayIndex + 1;
    for (size_t i = 0; i < target && value < tokenCount; ++i)
      value = NextToken(value);
    return value < tokenCount && tokens[value].start < tokens[arrayIndex].end ? value : -1;
  }

  long long ParseInteger(int index) const
  {
    const char* pCursor = benchmark::JsonText + tokens[index].start;
    const char* pEnd = benchmark::JsonText + tokens[index].end;
    bool negative = pCursor < pEnd && *pCursor == '-';
    if (negative)
      ++pCursor;
    long long value = 0;
    while (pCursor < pEnd)
      value = value * 10 + *pCursor++ - '0';
    return negative ? -value : value;
  }

  double ParseFloating(int index) const
  {
    return strtod(benchmark::JsonText + tokens[index].start, nullptr);
  }

  bool Prepare()
  {
    jsmn_parser parser;
    jsmn_init(&parser);
    tokenCount = jsmn_parse(&parser, benchmark::JsonText, benchmark::JsonSize, tokens.data(), tokens.size());
    if (tokenCount < 1 || tokens[0].type != JSMN_OBJECT)
      return false;
    array = FindObjectValue(0, "array", 5);
    object = FindObjectValue(0, "object", 6);
    integer = FindObjectValue(0, "integer", 7);
    floating = FindObjectValue(0, "floating", 8);
    string = FindObjectValue(0, "string", 6);
    int arrayValue = FindArrayValue(array, benchmark::ArrayLookupIndex);
    int objectValue = FindObjectValue(object, "target", 6);
    return arrayValue >= 0 && objectValue >= 0 &&
           ParseInteger(arrayValue) == benchmark::ArrayLookupValue &&
           ParseInteger(objectValue) == benchmark::ObjectLookupValue &&
           ParseInteger(integer) == benchmark::IntegerValue &&
           ParseFloating(floating) == benchmark::FloatingValue &&
           tokens[string].end - tokens[string].start == (int)benchmark::StringSize &&
           !memcmp(benchmark::JsonText + tokens[string].start, benchmark::StringValue, benchmark::StringSize);
  }

  uint64_t SerializeCompact(size_t) { return 0; }
  uint64_t SerializePretty(size_t) { return 0; }

  uint64_t LookupArray(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      int token = FindArrayValue(array, benchmark::ArrayLookupIndex);
      benchmark::DoNotOptimize(token);
      result += ParseInteger(token);
    }
    return result;
  }

  uint64_t LookupObject(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      int token = FindObjectValue(object, "target", 6);
      benchmark::DoNotOptimize(token);
      result += ParseInteger(token);
    }
    return result;
  }

  uint64_t AccessInteger(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      long long value = ParseInteger(integer);
      benchmark::DoNotOptimize(value);
      result += value;
    }
    return result;
  }

  uint64_t AccessFloating(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      double value = ParseFloating(floating);
      benchmark::DoNotOptimize(value);
      result += benchmark::DoubleBits(value);
    }
    return result;
  }

  uint64_t AccessString(size_t iterations)
  {
    uint64_t result = 0;
    const jsmntok_t& value = tokens[string];
    for (size_t i = 0; i < iterations; ++i) {
      const char* pValue = benchmark::JsonText + value.start;
      size_t size = value.end - value.start;
      benchmark::DoNotOptimize(pValue);
      result += benchmark::StringChecksum(pValue, size);
    }
    return result;
  }
};

int main() { return benchmark::Run<JsmnBenchmark>(); }
