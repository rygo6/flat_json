#include "benchmark.hpp"
#include <cJSON.h>

struct CJsonBenchmark
{
  static constexpr const char* Name = "DaveGamble/cJSON";
  static constexpr bool SupportsCompactSerialize = true;
  static constexpr bool SupportsPrettySerialize = true;
  static constexpr bool SupportsParse32Bit = true;
  static constexpr bool SupportsParse64Bit = false;

  cJSON* document = nullptr;
  cJSON* array = nullptr;
  cJSON* object = nullptr;
  cJSON* integer = nullptr;
  cJSON* floating = nullptr;
  cJSON* string = nullptr;

  ~CJsonBenchmark() { cJSON_Delete(document); }

  bool Prepare()
  {
    document = cJSON_ParseWithLength(benchmark::JsonText, benchmark::JsonSize);
    if (!document)
      return false;
    array = cJSON_GetObjectItemCaseSensitive(document, "array");
    object = cJSON_GetObjectItemCaseSensitive(document, "object");
    integer = cJSON_GetObjectItemCaseSensitive(document, "integer");
    floating = cJSON_GetObjectItemCaseSensitive(document, "floating");
    string = cJSON_GetObjectItemCaseSensitive(document, "string");
    cJSON* arrayValue = cJSON_GetArrayItem(array, benchmark::ArrayLookupIndex);
    cJSON* objectValue = cJSON_GetObjectItemCaseSensitive(object, "target");
    return arrayValue && objectValue &&
           arrayValue->valuedouble == benchmark::ArrayLookupValue &&
           objectValue->valuedouble == benchmark::ObjectLookupValue &&
           integer->valuedouble == benchmark::IntegerValue &&
           floating->valuedouble == benchmark::FloatingValue &&
           !strcmp(string->valuestring, benchmark::StringValue);
  }

  bool ValidateParse32Bit()
  {
    cJSON* values = cJSON_ParseWithLength(benchmark::Parse32BitJsonText, benchmark::Parse32BitJsonSize);
    if (!values)
      return false;
    cJSON* integers = cJSON_GetArrayItem(values, 0);
    cJSON* floating = cJSON_GetArrayItem(values, 1);
    bool valid = cJSON_GetArraySize(values) == 3 &&
                 benchmark::ValidateInt32Numbers([&](size_t i) {
                   return (int32_t)cJSON_GetArrayItem(integers, i)->valuedouble;
                 }) &&
                 benchmark::ValidateFloatRangeNumbers([&](size_t i) {
                   return cJSON_GetArrayItem(floating, i)->valuedouble;
                 });
    cJSON_Delete(values);
    return valid;
  }

  uint64_t Parse32Bit(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      cJSON* parsed = cJSON_ParseWithLength(benchmark::Parse32BitJsonText, benchmark::Parse32BitJsonSize);
      if (!parsed)
        abort();
      result += cJSON_GetArraySize(parsed);
      benchmark::DoNotOptimize(parsed);
      cJSON_Delete(parsed);
    }
    return result;
  }

  uint64_t SerializeCompact(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      char pJsonText[64 * 1024];
      if (!cJSON_PrintPreallocated(document, pJsonText, sizeof(pJsonText), false))
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
      char* pText = cJSON_Print(document);
      if (!pText)
        abort();
      result += (unsigned char)pText[0] + (unsigned char)pText[1];
      benchmark::DoNotOptimize(pText);
      cJSON_free(pText);
    }
    return result;
  }

  uint64_t LookupArray(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      cJSON* value = cJSON_GetArrayItem(array, benchmark::ArrayLookupIndex);
      benchmark::DoNotOptimize(value);
      result += (long long)value->valuedouble;
    }
    return result;
  }

  uint64_t LookupObject(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      cJSON* value = cJSON_GetObjectItemCaseSensitive(object, "target");
      benchmark::DoNotOptimize(value);
      result += (long long)value->valuedouble;
    }
    return result;
  }

  uint64_t AccessInteger(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      double value = integer->valuedouble;
      benchmark::DoNotOptimize(value);
      result += (long long)value;
    }
    return result;
  }

  uint64_t AccessFloating(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      double value = floating->valuedouble;
      benchmark::DoNotOptimize(value);
      result += benchmark::DoubleBits(value);
    }
    return result;
  }

  uint64_t AccessString(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      const char* pValue = string->valuestring;
      benchmark::DoNotOptimize(pValue);
      result += benchmark::StringChecksum(pValue, benchmark::StringSize);
    }
    return result;
  }
};

int main() { return benchmark::Run<CJsonBenchmark>(); }
