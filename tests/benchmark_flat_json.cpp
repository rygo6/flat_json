#include "benchmark.hpp"
#include "../flat_json.hpp"

struct FlatJsonBenchmark
{
  static constexpr const char* Name = "flat_json";
  static constexpr bool SupportsCompactSerialize = true;
  static constexpr bool SupportsPrettySerialize = true;
  static constexpr bool SupportsCommonNumericParse = true;
  static constexpr bool SupportsExactNumericParse = true;

  flat::FixedJsonBuffer<64 * 1024> arena;
  const flat::Json* pRoot = nullptr;
  const flat::Json* pArray = nullptr;
  const flat::Json* pObject = nullptr;
  const flat::Json* pInteger = nullptr;
  const flat::Json* pFloating = nullptr;
  const flat::Json* pString = nullptr;

  bool Prepare()
  {
    if (flat::Json::Parse(benchmark::JsonText, benchmark::JsonSize, &arena) != flat::Json::SUCCESS)
      return false;
    pRoot = arena.Root();
    pArray = &(*pRoot)["array"];
    pObject = &(*pRoot)["object"];
    pInteger = &(*pRoot)["integer"];
    pFloating = &(*pRoot)["floating"];
    pString = &(*pRoot)["string"];
    flat::JsonString string = pString->GetString();
    return (*pArray)[benchmark::ArrayLookupIndex].GetLong() == benchmark::ArrayLookupValue &&
           (*pObject)["target"].GetLong() == benchmark::ObjectLookupValue &&
           pInteger->GetLong() == benchmark::IntegerValue &&
           pFloating->GetDouble() == benchmark::FloatingValue &&
           string.size == benchmark::StringSize &&
           !memcmp(string.pData, benchmark::StringValue, string.size);
  }

  uint64_t Parse(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      flat::FixedJsonBuffer<64 * 1024> parseArena;
      if (flat::Json::Parse(benchmark::JsonText, benchmark::JsonSize, &parseArena) != flat::Json::SUCCESS)
        abort();
      const flat::Json* pJson = parseArena.Root();
      result += pJson->GetSize();
      benchmark::DoNotOptimize(pJson);
    }
    return result;
  }

  bool ValidateInt32Parse()
  {
    flat::FixedJsonBuffer<4096> parseArena;
    if (flat::Json::Parse(benchmark::Int32JsonText, benchmark::Int32JsonSize, &parseArena) != flat::Json::SUCCESS)
      return false;
    const flat::Json* pJson = parseArena.Root();
    return benchmark::ValidateInt32Numbers([&](size_t i) { return (*pJson)[i].GetLong(); });
  }

  uint64_t ParseInt32Numbers(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      flat::FixedJsonBuffer<4096> parseArena;
      if (flat::Json::Parse(benchmark::Int32JsonText, benchmark::Int32JsonSize, &parseArena) != flat::Json::SUCCESS)
        abort();
      const flat::Json* pJson = parseArena.Root();
      result += pJson->GetSize();
      benchmark::DoNotOptimize(pJson);
    }
    return result;
  }

  bool ValidateFloatRangeParse()
  {
    flat::FixedJsonBuffer<4096> parseArena;
    if (flat::Json::Parse(benchmark::FloatRangeJsonText, benchmark::FloatRangeJsonSize, &parseArena) != flat::Json::SUCCESS)
      return false;
    const flat::Json* pJson = parseArena.Root();
    return benchmark::ValidateFloatRangeNumbers([&](size_t i) { return (*pJson)[i].GetDouble(); });
  }

  uint64_t ParseFloatRangeNumbers(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      flat::FixedJsonBuffer<4096> parseArena;
      if (flat::Json::Parse(benchmark::FloatRangeJsonText, benchmark::FloatRangeJsonSize, &parseArena) != flat::Json::SUCCESS)
        abort();
      const flat::Json* pJson = parseArena.Root();
      result += pJson->GetSize();
      benchmark::DoNotOptimize(pJson);
    }
    return result;
  }

  bool ValidateExactNumericParse()
  {
    flat::FixedJsonBuffer<4096> parseArena;
    if (flat::Json::Parse(benchmark::ExactNumericJsonText, benchmark::ExactNumericJsonSize, &parseArena) != flat::Json::SUCCESS)
      return false;
    const flat::Json* pJson = parseArena.Root();
    return benchmark::ValidateExactNumbers(
      [&](size_t i) { return (*pJson)[i].GetLong(); },
      [&](size_t i) { return (*pJson)[benchmark::ExactIntegerCount + i].GetDouble(); });
  }

  uint64_t ParseExactNumbers(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      flat::FixedJsonBuffer<4096> parseArena;
      if (flat::Json::Parse(benchmark::ExactNumericJsonText, benchmark::ExactNumericJsonSize, &parseArena) != flat::Json::SUCCESS)
        abort();
      const flat::Json* pJson = parseArena.Root();
      result += pJson->GetSize();
      benchmark::DoNotOptimize(pJson);
    }
    return result;
  }

  uint64_t SerializeCompact(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      char pJsonText[64 * 1024];
      if (flat::WriteJson(*pRoot, pJsonText) != flat::Json::SUCCESS)
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
      char output[64 * 1024];
      if (flat::WriteJsonPretty(*pRoot, output) != flat::Json::SUCCESS)
        abort();
      result += (unsigned char)output[0] + (unsigned char)output[1];
      benchmark::DoNotOptimize(output);
    }
    return result;
  }

  uint64_t LookupArray(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      long long value = (*pArray)[benchmark::ArrayLookupIndex].GetLong();
      benchmark::DoNotOptimize(value);
      result += value;
    }
    return result;
  }

  uint64_t LookupObject(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      long long value = (*pObject)["target"].GetLong();
      benchmark::DoNotOptimize(value);
      result += value;
    }
    return result;
  }

  uint64_t AccessInteger(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      long long value = pInteger->GetLong();
      benchmark::DoNotOptimize(value);
      result += value;
    }
    return result;
  }

  uint64_t AccessFloating(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      double value = pFloating->GetDouble();
      benchmark::DoNotOptimize(value);
      result += benchmark::DoubleBits(value);
    }
    return result;
  }

  uint64_t AccessString(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      flat::JsonString value = pString->GetString();
      benchmark::DoNotOptimize(value.pData);
      benchmark::DoNotOptimize(value.size);
      result += benchmark::StringChecksum(value.pData, value.size);
    }
    return result;
  }
};

int main() { return benchmark::Run<FlatJsonBenchmark>(); }
