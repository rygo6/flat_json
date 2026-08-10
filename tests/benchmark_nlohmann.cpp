#include "benchmark.hpp"
#include <nlohmann/json.hpp>

#include <string>

struct NlohmannBenchmark
{
  static constexpr const char* Name = "nlohmann::ordered_json";
  static constexpr bool SupportsCompactSerialize = true;
  static constexpr bool SupportsPrettySerialize = true;
  static constexpr bool SupportsCommonNumericParse = true;
  static constexpr bool SupportsExactNumericParse = true;

  using Json = nlohmann::ordered_json;
  Json document;
  Json* pArray = nullptr;
  Json* pObject = nullptr;
  Json* pInteger = nullptr;
  Json* pFloating = nullptr;
  Json* pString = nullptr;

  bool Prepare()
  {
    document = Json::parse(benchmark::JsonText, benchmark::JsonText + benchmark::JsonSize);
    pArray = &document["array"];
    pObject = &document["object"];
    pInteger = &document["integer"];
    pFloating = &document["floating"];
    pString = &document["string"];
    return (*pArray)[benchmark::ArrayLookupIndex].get<long long>() == benchmark::ArrayLookupValue &&
           (*pObject)["target"].get<long long>() == benchmark::ObjectLookupValue &&
           pInteger->get<long long>() == benchmark::IntegerValue &&
           pFloating->get<double>() == benchmark::FloatingValue &&
           pString->get_ref<const std::string&>() == benchmark::StringValue;
  }

  uint64_t Parse(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      Json parsed = Json::parse(benchmark::JsonText, benchmark::JsonText + benchmark::JsonSize);
      result += parsed.size();
      benchmark::DoNotOptimize(parsed);
    }
    return result;
  }

  bool ValidateInt32Parse()
  {
    Json values = Json::parse(benchmark::Int32JsonText, benchmark::Int32JsonText + benchmark::Int32JsonSize);
    return benchmark::ValidateInt32Numbers([&](size_t i) { return values[i].get<int32_t>(); });
  }

  uint64_t ParseInt32Numbers(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      Json parsed = Json::parse(benchmark::Int32JsonText, benchmark::Int32JsonText + benchmark::Int32JsonSize);
      result += parsed.size();
      benchmark::DoNotOptimize(parsed);
    }
    return result;
  }

  bool ValidateFloatRangeParse()
  {
    Json values = Json::parse(benchmark::FloatRangeJsonText, benchmark::FloatRangeJsonText + benchmark::FloatRangeJsonSize);
    return benchmark::ValidateFloatRangeNumbers([&](size_t i) { return values[i].get<double>(); });
  }

  uint64_t ParseFloatRangeNumbers(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      Json parsed = Json::parse(benchmark::FloatRangeJsonText, benchmark::FloatRangeJsonText + benchmark::FloatRangeJsonSize);
      result += parsed.size();
      benchmark::DoNotOptimize(parsed);
    }
    return result;
  }

  bool ValidateExactNumericParse()
  {
    Json values = Json::parse(benchmark::ExactNumericJsonText, benchmark::ExactNumericJsonText + benchmark::ExactNumericJsonSize);
    return benchmark::ValidateExactNumbers(
      [&](size_t i) { return values[i].get<int64_t>(); },
      [&](size_t i) { return values[benchmark::ExactIntegerCount + i].get<double>(); });
  }

  uint64_t ParseExactNumbers(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      Json parsed = Json::parse(benchmark::ExactNumericJsonText, benchmark::ExactNumericJsonText + benchmark::ExactNumericJsonSize);
      result += parsed.size();
      benchmark::DoNotOptimize(parsed);
    }
    return result;
  }

  uint64_t SerializeCompact(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      std::string text = document.dump();
      result += (unsigned char)text[0] + (unsigned char)text[1];
      benchmark::DoNotOptimize(text);
    }
    return result;
  }

  uint64_t SerializePretty(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      std::string text = document.dump(2);
      result += (unsigned char)text[0] + (unsigned char)text[1];
      benchmark::DoNotOptimize(text);
    }
    return result;
  }

  uint64_t LookupArray(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      long long value = (*pArray)[benchmark::ArrayLookupIndex].get<long long>();
      benchmark::DoNotOptimize(value);
      result += value;
    }
    return result;
  }

  uint64_t LookupObject(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      long long value = (*pObject)["target"].get<long long>();
      benchmark::DoNotOptimize(value);
      result += value;
    }
    return result;
  }

  uint64_t AccessInteger(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      long long value = pInteger->get<long long>();
      benchmark::DoNotOptimize(value);
      result += value;
    }
    return result;
  }

  uint64_t AccessFloating(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      double value = pFloating->get<double>();
      benchmark::DoNotOptimize(value);
      result += benchmark::DoubleBits(value);
    }
    return result;
  }

  uint64_t AccessString(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      const std::string& value = pString->get_ref<const std::string&>();
      benchmark::DoNotOptimize(value);
      result += benchmark::StringChecksum(value.data(), value.size());
    }
    return result;
  }
};

int main() { return benchmark::Run<NlohmannBenchmark>(); }
