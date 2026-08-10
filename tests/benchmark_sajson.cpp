#include "benchmark.hpp"
#include <sajson.h>

#include <memory>

struct SajsonBenchmark
{
  static constexpr const char* Name = "chadaustin/sajson";
  static constexpr bool SupportsCompactSerialize = false;
  static constexpr bool SupportsPrettySerialize = false;
  static constexpr bool SupportsCommonNumericParse = true;
  static constexpr bool SupportsExactNumericParse = false;

  std::unique_ptr<sajson::document> document;
  std::unique_ptr<sajson::value> array;
  std::unique_ptr<sajson::value> object;
  std::unique_ptr<sajson::value> integer;
  std::unique_ptr<sajson::value> floating;
  std::unique_ptr<sajson::value> string;

  static sajson::value Find(const sajson::value& value, const char* pKey, size_t size)
  {
    return value.get_value_of_key(sajson::string(pKey, size));
  }

  bool Prepare()
  {
    document = std::make_unique<sajson::document>(sajson::parse(
      sajson::single_allocation(),
      sajson::string(benchmark::JsonText, benchmark::JsonSize)));
    if (!document->is_valid())
      return false;
    sajson::value root = document->get_root();
    array = std::make_unique<sajson::value>(Find(root, "array", 5));
    object = std::make_unique<sajson::value>(Find(root, "object", 6));
    integer = std::make_unique<sajson::value>(Find(root, "integer", 7));
    floating = std::make_unique<sajson::value>(Find(root, "floating", 8));
    string = std::make_unique<sajson::value>(Find(root, "string", 6));
    return array->get_array_element(benchmark::ArrayLookupIndex).get_integer_value() == benchmark::ArrayLookupValue &&
           Find(*object, "target", 6).get_integer_value() == benchmark::ObjectLookupValue &&
           integer->get_integer_value() == benchmark::IntegerValue &&
           floating->get_double_value() == benchmark::FloatingValue &&
           string->get_string_length() == benchmark::StringSize &&
           !memcmp(string->as_cstring(), benchmark::StringValue, benchmark::StringSize);
  }

  uint64_t Parse(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      sajson::document parsed = sajson::parse(
        sajson::single_allocation(),
        sajson::string(benchmark::JsonText, benchmark::JsonSize));
      if (!parsed.is_valid())
        abort();
      result += parsed.get_root().get_length();
      benchmark::DoNotOptimize(parsed);
    }
    return result;
  }

  bool ValidateInt32Parse()
  {
    sajson::document parsed = sajson::parse(
      sajson::single_allocation(), sajson::string(benchmark::Int32JsonText, benchmark::Int32JsonSize));
    if (!parsed.is_valid())
      return false;
    sajson::value values = parsed.get_root();
    return benchmark::ValidateInt32Numbers([&](size_t i) { return values.get_array_element(i).get_integer_value(); });
  }

  uint64_t ParseInt32Numbers(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      sajson::document parsed = sajson::parse(
        sajson::single_allocation(), sajson::string(benchmark::Int32JsonText, benchmark::Int32JsonSize));
      if (!parsed.is_valid())
        abort();
      result += parsed.get_root().get_length();
      benchmark::DoNotOptimize(parsed);
    }
    return result;
  }

  bool ValidateFloatRangeParse()
  {
    sajson::document parsed = sajson::parse(
      sajson::single_allocation(), sajson::string(benchmark::FloatRangeJsonText, benchmark::FloatRangeJsonSize));
    if (!parsed.is_valid())
      return false;
    sajson::value values = parsed.get_root();
    return benchmark::ValidateFloatRangeNumbers([&](size_t i) { return values.get_array_element(i).get_double_value(); });
  }

  uint64_t ParseFloatRangeNumbers(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      sajson::document parsed = sajson::parse(
        sajson::single_allocation(), sajson::string(benchmark::FloatRangeJsonText, benchmark::FloatRangeJsonSize));
      if (!parsed.is_valid())
        abort();
      result += parsed.get_root().get_length();
      benchmark::DoNotOptimize(parsed);
    }
    return result;
  }

  uint64_t SerializeCompact(size_t) { return 0; }
  uint64_t SerializePretty(size_t) { return 0; }

  uint64_t LookupArray(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      int value = array->get_array_element(benchmark::ArrayLookupIndex).get_integer_value();
      benchmark::DoNotOptimize(value);
      result += value;
    }
    return result;
  }

  uint64_t LookupObject(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      int value = Find(*object, "target", 6).get_integer_value();
      benchmark::DoNotOptimize(value);
      result += value;
    }
    return result;
  }

  uint64_t AccessInteger(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      int value = integer->get_integer_value();
      benchmark::DoNotOptimize(value);
      result += value;
    }
    return result;
  }

  uint64_t AccessFloating(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      double value = floating->get_double_value();
      benchmark::DoNotOptimize(value);
      result += benchmark::DoubleBits(value);
    }
    return result;
  }

  uint64_t AccessString(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      const char* pValue = string->as_cstring();
      benchmark::DoNotOptimize(pValue);
      result += benchmark::StringChecksum(pValue, string->get_string_length());
    }
    return result;
  }
};

int main() { return benchmark::Run<SajsonBenchmark>(); }
