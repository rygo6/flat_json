#include "benchmark.hpp"
#include "json.h"

#include <string>
#include <utility>

struct JartBenchmark
{
  static constexpr const char* Name = "jart/json.cpp";
  static constexpr bool SupportsCompactSerialize = true;
  static constexpr bool SupportsPrettySerialize = true;
  static constexpr bool SupportsCommonNumericParse = true;
  static constexpr bool SupportsExactNumericParse = true;

  inline static const std::string Input{benchmark::JsonText, benchmark::JsonSize};
  inline static const std::string Int32Input{benchmark::Int32JsonText, benchmark::Int32JsonSize};
  inline static const std::string FloatRangeInput{benchmark::FloatRangeJsonText, benchmark::FloatRangeJsonSize};
  inline static const std::string ExactNumericInput{benchmark::ExactNumericJsonText, benchmark::ExactNumericJsonSize};
  inline static const std::string ArrayKey{"array"};
  inline static const std::string ObjectKey{"object"};
  inline static const std::string IntegerKey{"integer"};
  inline static const std::string FloatingKey{"floating"};
  inline static const std::string StringKey{"string"};
  inline static const std::string TargetKey{"target"};

  jt::Json document;
  jt::Json* pArray = nullptr;
  jt::Json* pObject = nullptr;
  jt::Json* pInteger = nullptr;
  jt::Json* pFloating = nullptr;
  jt::Json* pString = nullptr;

  bool Prepare()
  {
    auto parsed = jt::Json::parse(Input);
    if (parsed.first != jt::Json::success)
      return false;
    document = std::move(parsed.second);
    pArray = &document[ArrayKey];
    pObject = &document[ObjectKey];
    pInteger = &document[IntegerKey];
    pFloating = &document[FloatingKey];
    pString = &document[StringKey];
    return (*pArray)[benchmark::ArrayLookupIndex].getLong() == benchmark::ArrayLookupValue &&
           (*pObject)[TargetKey].getLong() == benchmark::ObjectLookupValue &&
           pInteger->getLong() == benchmark::IntegerValue &&
           pFloating->getDouble() == benchmark::FloatingValue &&
           pString->getString() == benchmark::StringValue;
  }

  uint64_t Parse(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      auto parsed = jt::Json::parse(Input);
      if (parsed.first != jt::Json::success)
        abort();
      result += parsed.second.getObject().size();
      benchmark::DoNotOptimize(parsed.second);
    }
    return result;
  }

  bool ValidateInt32Parse()
  {
    auto parsed = jt::Json::parse(Int32Input);
    if (parsed.first != jt::Json::success)
      return false;
    return benchmark::ValidateInt32Numbers([&](size_t i) { return parsed.second[i].getLong(); });
  }

  uint64_t ParseInt32Numbers(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      auto parsed = jt::Json::parse(Int32Input);
      if (parsed.first != jt::Json::success)
        abort();
      result += parsed.second.getArray().size();
      benchmark::DoNotOptimize(parsed.second);
    }
    return result;
  }

  bool ValidateFloatRangeParse()
  {
    auto parsed = jt::Json::parse(FloatRangeInput);
    if (parsed.first != jt::Json::success)
      return false;
    return benchmark::ValidateFloatRangeNumbers([&](size_t i) { return parsed.second[i].getDouble(); });
  }

  uint64_t ParseFloatRangeNumbers(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      auto parsed = jt::Json::parse(FloatRangeInput);
      if (parsed.first != jt::Json::success)
        abort();
      result += parsed.second.getArray().size();
      benchmark::DoNotOptimize(parsed.second);
    }
    return result;
  }

  bool ValidateExactNumericParse()
  {
    auto parsed = jt::Json::parse(ExactNumericInput);
    if (parsed.first != jt::Json::success)
      return false;
    jt::Json& values = parsed.second;
    return benchmark::ValidateExactNumbers(
      [&](size_t i) { return values[i].getLong(); },
      [&](size_t i) { return values[benchmark::ExactIntegerCount + i].getDouble(); });
  }

  uint64_t ParseExactNumbers(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      auto parsed = jt::Json::parse(ExactNumericInput);
      if (parsed.first != jt::Json::success)
        abort();
      result += parsed.second.getArray().size();
      benchmark::DoNotOptimize(parsed.second);
    }
    return result;
  }

  uint64_t SerializeCompact(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      std::string text = document.toString();
      result += (unsigned char)text[0] + (unsigned char)text[1];
      benchmark::DoNotOptimize(text);
    }
    return result;
  }

  uint64_t SerializePretty(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      std::string text = document.toStringPretty();
      result += (unsigned char)text[0] + (unsigned char)text[1];
      benchmark::DoNotOptimize(text);
    }
    return result;
  }

  uint64_t LookupArray(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      long long value = (*pArray)[benchmark::ArrayLookupIndex].getLong();
      benchmark::DoNotOptimize(value);
      result += value;
    }
    return result;
  }

  uint64_t LookupObject(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      long long value = (*pObject)[TargetKey].getLong();
      benchmark::DoNotOptimize(value);
      result += value;
    }
    return result;
  }

  uint64_t AccessInteger(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      long long value = pInteger->getLong();
      benchmark::DoNotOptimize(value);
      result += value;
    }
    return result;
  }

  uint64_t AccessFloating(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      double value = pFloating->getDouble();
      benchmark::DoNotOptimize(value);
      result += benchmark::DoubleBits(value);
    }
    return result;
  }

  uint64_t AccessString(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      const std::string& value = pString->getString();
      benchmark::DoNotOptimize(value);
      result += benchmark::StringChecksum(value.data(), value.size());
    }
    return result;
  }
};

int main() { return benchmark::Run<JartBenchmark>(); }
