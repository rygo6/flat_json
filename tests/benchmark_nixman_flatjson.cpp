#include "benchmark.hpp"
#include <flatjson/flatjson.hpp>
#include <flatjson/io.hpp>

#include <string>

struct NixmanFlatJsonBenchmark
{
  static constexpr const char* Name = "niXman/flatjson";
  static constexpr bool SupportsCompactSerialize = true;
  static constexpr bool SupportsPrettySerialize = true;
  static constexpr bool SupportsCommonNumericParse = false;
  static constexpr bool SupportsExactNumericParse = false;

  flatjson::fjson document;
  flatjson::fjson array;
  flatjson::fjson object;
  flatjson::fjson integer;
  flatjson::fjson floating;
  flatjson::fjson string;

  bool Prepare()
  {
    document = flatjson::fjson(benchmark::JsonText, benchmark::JsonText + benchmark::JsonSize);
    if (!document.is_valid())
      return false;
    array = document["array"];
    object = document["object"];
    integer = document["integer"];
    floating = document["floating"];
    string = document["string"];
    flatjson::string_view stringValue = string.to_string_view();
    return array[benchmark::ArrayLookupIndex].to_int64() == benchmark::ArrayLookupValue &&
           object["target"].to_int64() == benchmark::ObjectLookupValue &&
           integer.to_int64() == benchmark::IntegerValue &&
           floating.to_double() == benchmark::FloatingValue &&
           stringValue.size() == benchmark::StringSize &&
           !memcmp(stringValue.data(), benchmark::StringValue, benchmark::StringSize);
  }

  uint64_t Parse(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      flatjson::fjson parsed(benchmark::JsonText, benchmark::JsonText + benchmark::JsonSize);
      if (!parsed.is_valid())
        abort();
      result += parsed.size();
      benchmark::DoNotOptimize(parsed);
    }
    return result;
  }

  uint64_t SerializeCompact(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      flatjson::iterator begin = document.begin().m_it;
      flatjson::iterator end = document.end().m_it;
      char pJsonText[64 * 1024];
      int error = 0;
      size_t size = flatjson::serialize(begin, end, pJsonText, sizeof(pJsonText), 0, &error);
      if (error || !size)
        abort();
      result += (unsigned char)pJsonText[0] + (unsigned char)pJsonText[1];
      benchmark::DoNotOptimize(pJsonText);
    }
    return result;
  }

  uint64_t SerializePretty(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      flatjson::iterator begin = document.begin().m_it;
      flatjson::iterator end = document.end().m_it;
      std::string text = flatjson::to_string(begin, end, 2);
      result += (unsigned char)text[0] + (unsigned char)text[1];
      benchmark::DoNotOptimize(text);
    }
    return result;
  }

  uint64_t LookupArray(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      int64_t value = array[benchmark::ArrayLookupIndex].to_int64();
      benchmark::DoNotOptimize(value);
      result += value;
    }
    return result;
  }

  uint64_t LookupObject(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      int64_t value = object["target"].to_int64();
      benchmark::DoNotOptimize(value);
      result += value;
    }
    return result;
  }

  uint64_t AccessInteger(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      int64_t value = integer.to_int64();
      benchmark::DoNotOptimize(value);
      result += value;
    }
    return result;
  }

  uint64_t AccessFloating(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      double value = floating.to_double();
      benchmark::DoNotOptimize(value);
      result += benchmark::DoubleBits(value);
    }
    return result;
  }

  uint64_t AccessString(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      flatjson::string_view value = string.to_string_view();
      benchmark::DoNotOptimize(value);
      result += benchmark::StringChecksum(value.data(), value.size());
    }
    return result;
  }
};

int main() { return benchmark::Run<NixmanFlatJsonBenchmark>(); }
