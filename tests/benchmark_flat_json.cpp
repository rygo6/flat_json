#include "benchmark.hpp"
#include "../FlatJson.hpp"

using namespace Flat;
using namespace Flat::Document;

struct FlatJsonBenchmark {
  static constexpr const char* Name              = "flat_json";
  static constexpr bool SupportsCompactSerialize = true;
  static constexpr bool SupportsPrettySerialize  = true;
  static constexpr bool SupportsParse32Bit       = true;
  static constexpr bool SupportsParse64Bit       = true;

  FixedArena<64 * 1024> arenaStorage;
  Arena arena           = arenaStorage;
  const Node* arenaRoot = nullptr;
  const Node* pRoot     = nullptr;
  const Node* pArray    = nullptr;
  const Node* pObject   = nullptr;
  const Node* pInteger  = nullptr;
  const Node* pFloating = nullptr;
  const Node* pString   = nullptr;

  bool Prepare()
  {
    if (ParseJSON(benchmark::JsonText, benchmark::JsonSize, &arena, &arenaRoot) != SUCCESS)
      return false;
    pRoot               = arenaRoot;
    pArray              = &(*pRoot)["array"];
    pObject             = &(*pRoot)["object"];
    pInteger            = &(*pRoot)["integer"];
    pFloating           = &(*pRoot)["floating"];
    pString             = &(*pRoot)["string"];
    String string = pString->GetString();
    return (*pArray)[benchmark::ArrayLookupIndex].GetLong() == benchmark::ArrayLookupValue && (*pObject)["target"].GetLong() == benchmark::ObjectLookupValue &&
           pInteger->GetLong() == benchmark::IntegerValue && pFloating->GetDouble() == benchmark::FloatingValue && string.size == benchmark::StringSize &&
           !memcmp(string.data, benchmark::StringValue, string.size);
  }

  bool ValidateParse32Bit()
  {
    FixedArena<16 * 1024> parseArenaStorage;
    Arena parseArena           = parseArenaStorage;
    const Node* parseArenaRoot = nullptr;
    if (ParseJSON(benchmark::Parse32BitJsonText, benchmark::Parse32BitJsonSize, &parseArena, &parseArenaRoot) != SUCCESS)
      return false;
    const Node* pJson = parseArenaRoot;
    return pJson->GetSize() == 3 && benchmark::ValidateInt32Numbers([&](size_t i) { return (*pJson)[0][i].GetLong(); }) &&
           benchmark::ValidateFloatRangeNumbers([&](size_t i) { return (*pJson)[1][i].GetDouble(); });
  }

  uint64_t Parse32Bit(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      FixedArena<16 * 1024> parseArenaStorage;
      Arena parseArena           = parseArenaStorage;
      const Node* parseArenaRoot = nullptr;
      if (ParseJSON(benchmark::Parse32BitJsonText, benchmark::Parse32BitJsonSize, &parseArena, &parseArenaRoot) != SUCCESS)
        abort();
      const Node* pJson = parseArenaRoot;
      result += pJson->GetSize();
      benchmark::DoNotOptimize(pJson);
    }
    return result;
  }

  bool ValidateParse64Bit()
  {
    FixedArena<16 * 1024> parseArenaStorage;
    Arena parseArena           = parseArenaStorage;
    const Node* parseArenaRoot = nullptr;
    if (ParseJSON(benchmark::Parse64BitJsonText, benchmark::Parse64BitJsonSize, &parseArena, &parseArenaRoot) != SUCCESS)
      return false;
    const Node* pJson = parseArenaRoot;
    return pJson->GetSize() == 5 && benchmark::ValidateInt32Numbers([&](size_t i) { return (*pJson)[0][i].GetLong(); }) &&
           benchmark::ValidateFloatRangeNumbers([&](size_t i) { return (*pJson)[1][i].GetDouble(); }) &&
           benchmark::ValidateExactNumbers([&](size_t i) { return (*pJson)[2][i].GetLong(); }, [&](size_t i) { return (*pJson)[3][i].GetDouble(); });
  }

  uint64_t Parse64Bit(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      FixedArena<16 * 1024> parseArenaStorage;
      Arena parseArena           = parseArenaStorage;
      const Node* parseArenaRoot = nullptr;
      if (ParseJSON(benchmark::Parse64BitJsonText, benchmark::Parse64BitJsonSize, &parseArena, &parseArenaRoot) != SUCCESS)
        abort();
      const Node* pJson = parseArenaRoot;
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
      if (WriteJSON(*pRoot, pJsonText) != SUCCESS)
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
      if (WriteJSONPretty(*pRoot, output) != SUCCESS)
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
      String value = pString->GetString();
      benchmark::DoNotOptimize(value.data);
      benchmark::DoNotOptimize(value.size);
      result += benchmark::StringChecksum(value.data, value.size);
    }
    return result;
  }
};

int main() { return benchmark::Run<FlatJsonBenchmark>(); }
