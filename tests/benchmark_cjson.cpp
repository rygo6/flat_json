#include "benchmark.hpp"
#include <cJSON.h>

struct CJsonBenchmark
{
  static constexpr const char* Name = "DaveGamble/cJSON";
  static constexpr bool SupportsCompactSerialize = true;
  static constexpr bool SupportsPrettySerialize = true;
  static constexpr bool SupportsCommonNumericParse = true;
  static constexpr bool SupportsExactNumericParse = false;

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

  uint64_t Parse(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      cJSON* parsed = cJSON_ParseWithLength(benchmark::JsonText, benchmark::JsonSize);
      if (!parsed)
        abort();
      result += cJSON_GetArraySize(parsed);
      benchmark::DoNotOptimize(parsed);
      cJSON_Delete(parsed);
    }
    return result;
  }

  bool ValidateInt32Parse()
  {
    cJSON* values = cJSON_ParseWithLength(benchmark::Int32JsonText, benchmark::Int32JsonSize);
    if (!values)
      return false;
    bool valid = benchmark::ValidateInt32Numbers([&](size_t i) {
      return (int32_t)cJSON_GetArrayItem(values, i)->valuedouble;
    });
    cJSON_Delete(values);
    return valid;
  }

  uint64_t ParseInt32Numbers(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      cJSON* parsed = cJSON_ParseWithLength(benchmark::Int32JsonText, benchmark::Int32JsonSize);
      if (!parsed)
        abort();
      result += cJSON_GetArraySize(parsed);
      benchmark::DoNotOptimize(parsed);
      cJSON_Delete(parsed);
    }
    return result;
  }

  bool ValidateFloatRangeParse()
  {
    cJSON* values = cJSON_ParseWithLength(benchmark::FloatRangeJsonText, benchmark::FloatRangeJsonSize);
    if (!values)
      return false;
    bool valid = benchmark::ValidateFloatRangeNumbers([&](size_t i) {
      return cJSON_GetArrayItem(values, i)->valuedouble;
    });
    cJSON_Delete(values);
    return valid;
  }

  uint64_t ParseFloatRangeNumbers(size_t iterations)
  {
    uint64_t result = 0;
    for (size_t i = 0; i < iterations; ++i) {
      cJSON* parsed = cJSON_ParseWithLength(benchmark::FloatRangeJsonText, benchmark::FloatRangeJsonSize);
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
