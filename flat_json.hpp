////////////////////////////////////////////////////////////////////////////////
// @author: ryan
// flat_json.hpp — Flat, caller-owned arena JSON parsing and serialization.
////////////////////////////////////////////////////////////////////////////////

// Copyright 2024 Mozilla Foundation
//
// Project lineage:
//   - Cosmopolitan tool/net/ljson.c (2022), by Justine Tunney and
//     Gautham Venkatasubramanian.
//   - The Mozilla-sponsored C++ port used by Mozilla-Ocho/llamafile and
//     published as jart/json.cpp by Justine Tunney and contributors (2024).
//   - This immutable flat-arena parse/serialization derivative.
//
// See THIRD_PARTY_NOTICES.md for complete provenance.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#if defined(_MSC_VER)
#error "Microsoft has been deprecated from the software industry. Please convert your MSVC dependent code to clang or GCC. A modern LLM will be able to do most of this automatically."
#elif !defined(__clang__) && !defined(__GNUC__)
#error "Flat C++ JSON requires Clang or GCC."
#endif

#include <initializer_list>
#include <limits.h>
#include <span>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <type_traits>

#ifndef JSON_INLINE
#define JSON_INLINE [[gnu::always_inline]] inline
#endif

#ifndef JSON_INFO
#define JSON_INFO(format, ...) fprintf(stderr, __FILE__ ":%-*d[JSON] "       format, (int)(20 - sizeof(__FILE__)), __LINE__ __VA_OPT__(,) __VA_ARGS__)
#endif

#ifndef JSON_WARN
#define JSON_WARN(format, ...) fprintf(stderr, __FILE__ ":%-*d[JSON] WARN: " format, (int)(20 - sizeof(__FILE__)), __LINE__ __VA_OPT__(,) __VA_ARGS__)
#endif

#ifndef JSON_ERR
#define JSON_ERR(format, ...) fprintf(stderr, __FILE__ ":%-*d[JSON] ERR: "  format, (int)(20 - sizeof(__FILE__)), __LINE__ __VA_OPT__(,) __VA_ARGS__)
#endif

#ifndef JSON_VERBOSE
#ifdef JSON_ENABLE_VERBOSE
#define JSON_VERBOSE(format, ...) JSON_INFO(format __VA_OPT__(,) __VA_ARGS__)
#else
#define JSON_VERBOSE(format, ...) ((void)0)
#endif
#endif

#ifndef JSON_PANIC
#define JSON_PANIC(format, ...) ({                                           \
  fprintf(stderr, __FILE__ ":%-*d[JSON] PANIC: " format "\n",                \
          (int)(20 - sizeof(__FILE__)), __LINE__ __VA_OPT__(,) __VA_ARGS__); \
  __builtin_trap();                                                          \
})
#endif

#ifndef JSON_REQUIRE
#define JSON_REQUIRE(expr, ...) ({                                 \
  if (!(expr)) [[unlikely]] {                                      \
    fprintf(stderr, __FILE__ ":%-*d[JSON] ERR: JSON_REQUIRE: %s",  \
            (int)(20 - sizeof(__FILE__)), __LINE__, #expr);        \
    __VA_OPT__(fputs(": ", stderr); fprintf(stderr, __VA_ARGS__);) \
    fputc('\n', stderr);                                           \
    __builtin_trap();                                              \
  }                                                                \
})
#endif

#ifndef JSON_ASSERT
#ifdef DEBUG
#define JSON_ASSERT(expr, ...) ({                                  \
  if (!(expr)) [[unlikely]] {                                      \
    fprintf(stderr, __FILE__ ":%-*d[JSON] ERR: JSON_ASSERT: %s",   \
            (int)(20 - sizeof(__FILE__)), __LINE__, #expr);        \
    __VA_OPT__(fputs(": ", stderr); fprintf(stderr, __VA_ARGS__);) \
    fputc('\n', stderr);                                           \
    __builtin_trap();                                              \
  }                                                                \
})
#else
#define JSON_ASSERT(expr, ...) ((void)0)
#endif
#endif

////////////////////////////////////////////////////////////////////////////////
namespace flat {
////////////////////////////////////////////////////////////////////////////////

using u32 = uint32_t;

template<typename T>
struct JsonSpan
{
  size_t count = 0;
  T* pData = nullptr;

  constexpr JsonSpan() = default;
  constexpr JsonSpan(size_t inputCount, T* inputData) : count(inputCount), pData(inputData) {}

  template<typename U, size_t Extent>
    requires std::is_convertible_v<U(*)[], T(*)[]>
  constexpr JsonSpan(std::span<U, Extent> value) : count(value.size()), pData(value.data()) {}

  template<typename U, size_t Size>
    requires std::is_convertible_v<U(*)[], T(*)[]>
  constexpr JsonSpan(U (&value)[Size]) : count(Size), pData(value) {}

  constexpr T* data() const { return pData; }
  constexpr size_t size() const { return count; }
  constexpr bool empty() const { return !count; }
  constexpr T& operator[](size_t index) const { return pData[index]; }
};

struct JsonString
{
  const char* pData = "";
  size_t size = 0;

  constexpr JsonString() = default;
  constexpr JsonString(size_t inputSize, const char* inputData) : pData(inputData), size(inputSize) {}
  template<size_t Size>
  constexpr JsonString(const char (&value)[Size]) : pData(value), size(Size - 1) {}

  JSON_INLINE char operator[](size_t index) const { return pData[index]; }
};

static_assert(std::is_convertible_v<std::span<char>, JsonSpan<char>>);
static_assert(std::is_convertible_v<std::span<char>, JsonSpan<const char>>);

struct FileMap;
struct WritableFile;
struct Json;

///////////////////////////////////////////////////////
// JsonBuffer
//  Non-owning base for JSON-specific buffers. Parsed records allocate backward
//  from back while the front is temporary number-conversion scratch.
///////////////////////////////////////////////////////
struct JsonBuffer
{
  char* pData = nullptr;
  size_t used = 0;
  size_t back = 0;
  const Json* pRoot = nullptr;

  JSON_INLINE const Json* Root() const { return pRoot; }

  JsonBuffer(const JsonBuffer&) = delete;
  JsonBuffer& operator=(const JsonBuffer&) = delete;

protected:
  JsonBuffer(size_t capacity, void* pBuffer) : pData((char*)pBuffer), back(capacity) {}
};

///////////////////////////////////////////////////////
// FixedJsonBuffer
//  Owns fixed inline storage for one immutable parsed JSON document.
///////////////////////////////////////////////////////
template<size_t Capacity>
struct FixedJsonBuffer : JsonBuffer
{
  static_assert(Capacity >= 16, "FixedJsonBuffer capacity is too small.");
  static_assert(Capacity < UINT32_MAX, "FixedJsonBuffer capacity exceeds its 32-bit offsets.");
  alignas(8) char bytes[Capacity];

  FixedJsonBuffer() : JsonBuffer(Capacity, bytes) {}

  FixedJsonBuffer(const FixedJsonBuffer&) = delete;
  FixedJsonBuffer& operator=(const FixedJsonBuffer&) = delete;

  JSON_INLINE void Reset()
  {
    used = 0;
    back = Capacity;
    pRoot = nullptr;
  }
};

////////////////////////////////////////////////////////////////////////////////
// JSON read types
////////////////////////////////////////////////////////////////////////////////

struct Json
{
  static constexpr u32 ReversedArrayFlag = 1u << 31;
  static constexpr u32 ArraySizeMask = ReversedArrayFlag - 1;

  enum Type
  {
    TYPE_NULL,
    TYPE_BOOL,
    TYPE_LONG,
    TYPE_FLOAT,
    TYPE_DOUBLE,
    TYPE_STRING,
    TYPE_ARRAY,
    TYPE_OBJECT,
    TYPE_PLAIN_STRING,  // Internal no-rescan tag; IsString() includes it.
  };

  enum Status
  {
    SUCCESS,
    MALFORMED,
    ABSENT_VALUE,
    INVALID_ARGUMENT,
    INSUFFICIENT_SPACE,
    IO_ERROR,
  };

  Type type;
  u32 span;
  union {
    bool boolValue;
    float floatValue;
    double doubleValue;
    long long longValue;
    struct {
      u32 stringOffset;  // Relative to this Json; points directly to UTF-8 bytes.
      u32 stringSize;
    };
    struct {
      u32 arrayOffset;  // Relative to this Json.
      u32 arraySize;
    };
    struct {
      u32 objectOffset;  // Relative to this Json.
      u32 objectSize;
    };
  };

  Json(const decltype(nullptr) = nullptr) : type(TYPE_NULL), span(0) {}

  JSON_INLINE bool IsNull() const { return type == TYPE_NULL; }
  JSON_INLINE bool IsBool() const { return type == TYPE_BOOL; }
  JSON_INLINE bool IsNumber() const { return IsFloat() || IsDouble() || IsLong(); }
  JSON_INLINE bool IsFloatingPoint() const { return IsFloat() || IsDouble(); }
  JSON_INLINE bool IsLong() const { return type == TYPE_LONG; }
  JSON_INLINE bool IsFloat() const { return type == TYPE_FLOAT; }
  JSON_INLINE bool IsDouble() const { return type == TYPE_DOUBLE; }
  JSON_INLINE bool IsString() const { return type == TYPE_STRING || type == TYPE_PLAIN_STRING; }
  JSON_INLINE bool IsArray() const { return type == TYPE_ARRAY; }
  JSON_INLINE bool IsObject() const { return type == TYPE_OBJECT; }

  JSON_INLINE bool GetBool() const { JSON_ASSERT(IsBool(), "JSON value is not a bool."); return boolValue; }
  JSON_INLINE float GetFloat() const { JSON_ASSERT(IsFloatingPoint(), "JSON value is not a floating-point number."); return IsFloat() ? floatValue : (float)doubleValue; }
  JSON_INLINE double GetDouble() const { JSON_ASSERT(IsDouble(), "JSON value is not a double."); return doubleValue; }
  JSON_INLINE double GetNumber() const { JSON_ASSERT(IsNumber(), "JSON value is not a number."); return IsLong() ? (double)longValue : IsFloat() ? (double)floatValue : doubleValue; }
  JSON_INLINE long long GetLong() const { JSON_ASSERT(IsLong(), "JSON value is not a long."); return longValue; }
  JSON_INLINE size_t GetSize() const { JSON_ASSERT(HasSize(), "JSON value has no size."); return IsString() ? stringSize : IsArray() ? arraySize & ArraySizeMask : objectSize; }

  JSON_INLINE JsonString GetString() const { JSON_ASSERT(IsString(), "JSON value is not a string."); return JsonString(stringSize, (const char*)this + stringOffset); }
  JSON_INLINE const Json& GetArray() const { JSON_ASSERT(IsArray(), "JSON value is not an array."); return *this; }
  JSON_INLINE const Json& GetObject() const { JSON_ASSERT(IsObject(), "JSON value is not an object."); return *this; }

  bool Contains(JsonString key) const;
  JSON_INLINE bool HasIndex(size_t index) const { return IsArray() && index < (arraySize & ArraySizeMask); }
  JSON_INLINE bool HasIndex(int index) const { return index >= 0 && HasIndex((size_t)index); }
  JSON_INLINE bool HasKey(JsonString key) const { return Contains(key); }
  JSON_INLINE bool HasSize() const { return IsString() || IsArray() || IsObject(); }

  template<size_t Size>
  JSON_INLINE bool Contains(const char (&key)[Size]) const { return Contains(JsonString(key)); }
  template<size_t Size>
  JSON_INLINE bool HasKey(const char (&key)[Size]) const { return HasKey(JsonString(key)); }

  Status ToString(JsonSpan<char> output) const;
  Status ToStringPretty(JsonSpan<char> output) const;

  JSON_INLINE const Json& operator[](size_t index) const
  {
    JSON_ASSERT(IsArray(), "JSON value is not an array.");
    u32 size = arraySize & ArraySizeMask;
    JSON_ASSERT(index < size, "JSON index %zu is outside array of size %u.", index, size);
    size_t physicalIndex = arraySize & ReversedArrayFlag ? size - index - 1 : index;
    return *(const Json*)((const char*)this + arrayOffset + physicalIndex * sizeof(Json));
  }
  const Json& operator[](JsonString key) const;

  template<size_t Size>
  JSON_INLINE const Json& operator[](const char (&key)[Size]) const { return (*this)[JsonString(key)]; }

  JSON_INLINE const Json& operator[](int index) const { JSON_ASSERT(index >= 0, "JSON index is negative."); return (*this)[(size_t)index]; }
  JSON_INLINE const Json& operator[](u32 index) const { return (*this)[(size_t)index]; }

  JSON_INLINE bool TryCopyString(JsonString key, JsonSpan<char> output) const
  {
    if (!output.data() || !output.size())
      return false;

    if (!Contains(key))
      return false;

    const Json& value = (*this)[key];
    if (!value.IsString())
      return false;

    JsonString text = value.GetString();
    int written = snprintf(output.data(), output.size(), "%.*s", (int)text.size, text.pData);
    return written >= 0 && (size_t)written == text.size && text.size < output.size();
  }

  template<size_t Size>
  JSON_INLINE bool TryCopyString(const char (&key)[Size], JsonSpan<char> output) const { return TryCopyString(JsonString(key), output); }

  JSON_INLINE bool TryGetLong(JsonString key, long long* pOut) const
  {
    if (!pOut || !Contains(key))
      return false;

    const Json& value = (*this)[key];
    if (!value.IsLong())
      return false;

    *pOut = value.GetLong();
    return true;
  }

  JSON_INLINE bool TryGetU32(JsonString key, u32* pOut) const
  {
    long long value;
    if (!pOut || !TryGetLong(key, &value) || value < 0 || value > (long long)UINT32_MAX)
      return false;

    *pOut = (u32)value;
    return true;
  }

  JSON_INLINE bool TryGetFloat(JsonString key, float* pOut) const
  {
    if (!pOut || !Contains(key))
      return false;

    const Json& value = (*this)[key];
    if (!value.IsNumber())
      return false;

    *pOut = (float)value.GetNumber();
    return true;
  }

  JSON_INLINE bool TryGetDouble(JsonString key, double* pOut) const
  {
    if (!pOut || !Contains(key))
      return false;

    const Json& value = (*this)[key];
    if (!value.IsNumber())
      return false;

    *pOut = value.GetNumber();
    return true;
  }

  JSON_INLINE bool TryGetBool(JsonString key, bool* pOut) const
  {
    if (!pOut || !Contains(key))
      return false;

    const Json& value = (*this)[key];
    if (!value.IsBool())
      return false;

    *pOut = value.GetBool();
    return true;
  }

  JSON_INLINE bool TryGetString(JsonString key, JsonString* pOut) const
  {
    if (!pOut || !Contains(key))
      return false;

    const Json& value = (*this)[key];
    if (!value.IsString())
      return false;

    *pOut = value.GetString();
    return true;
  }

  JSON_INLINE bool TryGetArray(JsonString key, const Json** ppOut) const
  {
    if (!ppOut || !Contains(key))
      return false;

    const Json& value = (*this)[key];
    if (!value.IsArray())
      return false;

    *ppOut = &value;
    return true;
  }

  JSON_INLINE bool TryGetObject(JsonString key, const Json** ppOut) const
  {
    if (!ppOut || !Contains(key))
      return false;

    const Json& value = (*this)[key];
    if (!value.IsObject())
      return false;

    *ppOut = &value;
    return true;
  }

  template<size_t Size>
  JSON_INLINE bool TryGetLong(const char (&key)[Size], long long* pOut) const { return TryGetLong(JsonString(key), pOut); }

  template<size_t Size>
  JSON_INLINE bool TryGetU32(const char (&key)[Size], u32* pOut) const { return TryGetU32(JsonString(key), pOut); }

  template<size_t Size>
  JSON_INLINE bool TryGetFloat(const char (&key)[Size], float* pOut) const { return TryGetFloat(JsonString(key), pOut); }

  template<size_t Size>
  JSON_INLINE bool TryGetDouble(const char (&key)[Size], double* pOut) const { return TryGetDouble(JsonString(key), pOut); }

  template<size_t Size>
  JSON_INLINE bool TryGetBool(const char (&key)[Size], bool* pOut) const { return TryGetBool(JsonString(key), pOut); }

  template<size_t Size>
  JSON_INLINE bool TryGetString(const char (&key)[Size], JsonString* pOut) const { return TryGetString(JsonString(key), pOut); }

  template<size_t Size>
  JSON_INLINE bool TryGetArray(const char (&key)[Size], const Json** ppOut) const { return TryGetArray(JsonString(key), ppOut); }

  template<size_t Size>
  JSON_INLINE bool TryGetObject(const char (&key)[Size], const Json** ppOut) const { return TryGetObject(JsonString(key), ppOut); }

  JSON_INLINE bool TryCopyFloatArray(JsonString key, JsonSpan<float> output) const
  {
    const Json* pArray;
    if ((output.size() && !output.data()) || !TryGetArray(key, &pArray) || pArray->GetSize() != output.size())
      return false;

    for (size_t index = 0; index < output.size(); ++index) {
      if (!(*pArray)[index].IsNumber())
        return false;
    }

    for (size_t index = 0; index < output.size(); ++index)
      output.data()[index] = (float)(*pArray)[index].GetNumber();

    return true;
  }

  JSON_INLINE bool TryCopyDoubleArray(JsonString key, JsonSpan<double> output) const
  {
    const Json* pArray;
    if ((output.size() && !output.data()) || !TryGetArray(key, &pArray) || pArray->GetSize() != output.size())
      return false;

    for (size_t index = 0; index < output.size(); ++index) {
      if (!(*pArray)[index].IsNumber())
        return false;
    }

    for (size_t index = 0; index < output.size(); ++index)
      output.data()[index] = (*pArray)[index].GetNumber();

    return true;
  }

  template<size_t Size>
  JSON_INLINE bool TryCopyFloatArray(const char (&key)[Size], JsonSpan<float> output) const { return TryCopyFloatArray(JsonString(key), output); }

  template<size_t Size>
  JSON_INLINE bool TryCopyDoubleArray(const char (&key)[Size], JsonSpan<double> output) const { return TryCopyDoubleArray(JsonString(key), output); }

  JSON_INLINE bool TryParseHexString(JsonString key, u32* pOut) const
  {
    JsonString text;
    if (!pOut || !TryGetString(key, &text))
      return false;

    if (!text.size || text.size > 15)
      return false;

    char bounded[16] = {};
    __builtin_memcpy(bounded, text.pData, text.size);

    int base = 10;
    const char* pDigits = bounded;
    if (text.size > 2 && bounded[0] == '0' && (bounded[1] == 'x' || bounded[1] == 'X')) {
      base = 16;
      pDigits += 2;
    }

    bool leadingDigit = (*pDigits >= '0' && *pDigits <= '9') ||
                        (base == 16 && ((*pDigits >= 'a' && *pDigits <= 'f') || (*pDigits >= 'A' && *pDigits <= 'F')));
    if (!leadingDigit)
      return false;

    char* pEnd = nullptr;
    unsigned long value = strtoul(pDigits, &pEnd, base);
    if (pEnd != bounded + text.size || value > UINT32_MAX)
      return false;

    *pOut = (u32)value;
    return true;
  }

  template<size_t Size>
  JSON_INLINE bool TryParseHexString(const char (&key)[Size], u32* pOut) const { return TryParseHexString(JsonString(key), pOut); }

  struct Member
  {
    JsonString key;
    const Json& value;
  };

  const Json* MemberAt(size_t index, JsonString* pKey) const;

  struct ArrayIterator
  {
    const Json* pJson;
    size_t index;

    JSON_INLINE const Json& operator*() const { return (*pJson)[index]; }
    JSON_INLINE ArrayIterator& operator++() { ++index; return *this; }
    JSON_INLINE bool operator!=(const ArrayIterator& other) const { return index != other.index; }
  };

  struct MemberIterator
  {
    const Json* pJson;
    size_t index;

    JSON_INLINE Member operator*() const { JsonString key; const Json* pValue = pJson->MemberAt(index, &key); return {key, *pValue}; }
    JSON_INLINE MemberIterator& operator++() { ++index; return *this; }
    JSON_INLINE bool operator!=(const MemberIterator& other) const { return index != other.index; }
  };

  struct ElementsView
  {
    const Json* pJson;

    JSON_INLINE ArrayIterator begin() const { return {pJson, 0}; }
    JSON_INLINE ArrayIterator end() const { return {pJson, pJson && pJson->IsArray() ? pJson->GetSize() : 0}; }
  };

  struct MembersView
  {
    const Json* pJson;

    JSON_INLINE MemberIterator begin() const { return {pJson, 0}; }
    JSON_INLINE MemberIterator end() const { return {pJson, pJson && pJson->IsObject() ? (size_t)pJson->objectSize : 0}; }
  };

  JSON_INLINE ElementsView Elements() const { JSON_ASSERT(IsArray(), "JSON value is not an array."); return {this}; }
  JSON_INLINE MembersView Members() const { JSON_ASSERT(IsObject(), "JSON value is not an object."); return {this}; }

  JSON_INLINE ElementsView TryElements() const { return {IsArray() ? this : nullptr}; }
  JSON_INLINE MembersView TryMembers() const { return {IsObject() ? this : nullptr}; }

  JSON_INLINE ElementsView TryElements(JsonString key) const
  {
    const Json* pArray = nullptr;
    TryGetArray(key, &pArray);
    return {pArray};
  }

  JSON_INLINE MembersView TryMembers(JsonString key) const
  {
    const Json* pObject = nullptr;
    TryGetObject(key, &pObject);
    return {pObject};
  }

  template<size_t Size>
  JSON_INLINE ElementsView TryElements(const char (&key)[Size]) const { return TryElements(JsonString(key)); }

  template<size_t Size>
  JSON_INLINE MembersView TryMembers(const char (&key)[Size]) const { return TryMembers(JsonString(key)); }

  static const char* StatusToString(Status status);
  static size_t EstimateSize(const char* pData, size_t size);
  static size_t EstimateSize(JsonSpan<const char> data) { return EstimateSize(data.data(), data.size()); }
  static size_t EstimateSize(const FileMap& input);

  template<size_t Size>
  static size_t EstimateSize(const char (&text)[Size]) { return EstimateSize(text, Size - 1); }

  static Status Parse(const char* pData, size_t size, JsonBuffer* pBuffer);
  static Status Parse(JsonSpan<const char> data, JsonBuffer* pBuffer) { return Parse(data.data(), data.size(), pBuffer); }
  static Status Parse(const FileMap& input, JsonBuffer* pBuffer);
  static Status ParseFile(const char* pPath, JsonBuffer* pBuffer);

  template<size_t Size>
  static Status Parse(const char (&text)[Size], JsonBuffer* pBuffer) { return Parse(text, Size - 1, pBuffer); }
};

static_assert(sizeof(Json) == 16, "Json records must remain 16 bytes for direct array indexing.");

///////////////////////////////////////////////////////
// FixedJsonDocument
//  Fixed storage a caller fills, then parses in place; the inline arena owns the
//  records and `root` points into it, so both live with the scope. On parse failure
//  (malformed text, or records outgrowing the 4x arena) `root` stays null.
///////////////////////////////////////////////////////
template<size_t Capacity>
struct FixedJsonDocument
{
  char buffer[Capacity];
  FixedJsonBuffer<Capacity * 4> arena;
  const Json* root = nullptr;

  FixedJsonDocument() = default;

  FixedJsonDocument(const FixedJsonDocument&)            = delete;
  FixedJsonDocument& operator=(const FixedJsonDocument&) = delete;
  FixedJsonDocument(FixedJsonDocument&&)                 = delete;
  FixedJsonDocument& operator=(FixedJsonDocument&&)      = delete;

  bool Parse(size_t length)
  {
    root = nullptr;
    arena.Reset();
    if (length > Capacity)
      return false;

    if (Json::Parse(buffer, length, &arena) != Json::SUCCESS)
      return false;

    root = arena.Root();
    return root != nullptr;
  }

  bool IsValid() const { return root != nullptr; }
};

///////////////////////////////////////////////////////
// JsonFileMap
//  RAII parse of a JSON file. File-maps the text, parses it into the inline arena,
//  and exposes the root value; the mapping is released before the constructor
//  returns. Panic-free: on a missing file or invalid JSON `root` is null,
//  IsValid() reports it, and `status` carries the parser verdict.
///////////////////////////////////////////////////////
template<size_t Capacity = 16 * 1024>
struct JsonFileMap
{
  FixedJsonBuffer<Capacity> arena;
  const Json*  root   = nullptr;
  Json::Status status = Json::MALFORMED;

  explicit JsonFileMap(const char* pPath)
  {
    status = Json::ParseFile(pPath, &arena);
    if (status != Json::SUCCESS)
      return;

    root = arena.Root();
  }

  JsonFileMap(const JsonFileMap&)            = delete;
  JsonFileMap& operator=(const JsonFileMap&) = delete;
  JsonFileMap(JsonFileMap&&)                 = delete;
  JsonFileMap& operator=(JsonFileMap&&)      = delete;

  bool IsValid() const { return root != nullptr; }
};

JsonFileMap(const char*) -> JsonFileMap<16 * 1024>;

////////////////////////////////////////////////////////////////////////////////
// JSON write types
////////////////////////////////////////////////////////////////////////////////

struct JsonValue;
struct JsonMember;

struct JsonArrayValue
{
  JsonSpan<const JsonValue> values;
};

struct JsonObjectValue
{
  JsonSpan<const JsonMember> members;
};

struct JsonValue
{
  enum Type
  {
    TYPE_NULL,
    TYPE_BOOL,
    TYPE_LONG,
    TYPE_FLOAT,
    TYPE_DOUBLE,
    TYPE_STRING,
    TYPE_ARRAY,
    TYPE_OBJECT,
  } type;

  struct List
  {
    const void* pData;
    size_t size;
  };

  union {
    bool boolValue;
    long long longValue;
    float floatValue;
    double doubleValue;
    JsonString stringValue;
    List listValue;
  };

  JsonValue(const decltype(nullptr) = nullptr) : type(TYPE_NULL) {}
  JsonValue(bool value) : type(TYPE_BOOL), boolValue(value) {}
  JsonValue(int value) : type(TYPE_LONG), longValue(value) {}
  JsonValue(unsigned value) : type(TYPE_LONG), longValue(value) {}
  JsonValue(long value) : type(TYPE_LONG), longValue(value) {}
  JsonValue(long long value) : type(TYPE_LONG), longValue(value) {}
  JsonValue(unsigned long value)
  {
    if (value <= LLONG_MAX) {
      type = TYPE_LONG;
      longValue = (long long)value;
    } else {
      type = TYPE_DOUBLE;
      doubleValue = value;
    }
  }

  JsonValue(unsigned long long value)
  {
    if (value <= LLONG_MAX) {
      type = TYPE_LONG;
      longValue = (long long)value;
    } else {
      type = TYPE_DOUBLE;
      doubleValue = value;
    }
  }
  JsonValue(float value) : type(TYPE_FLOAT), floatValue(value) {}
  JsonValue(double value) : type(TYPE_DOUBLE), doubleValue(value) {}
  JsonValue(JsonString value) : type(TYPE_STRING), stringValue(value) {}
  template<size_t Size>
  JsonValue(const char (&value)[Size]) : type(TYPE_STRING), stringValue(value) {}
  JsonValue(JsonArrayValue value) : type(TYPE_ARRAY), listValue{value.values.data(), value.values.size()} {}
  JsonValue(JsonObjectValue value);
};

struct JsonMember
{
  JsonString key;
  JsonValue value;

  JsonMember(JsonString inputKey, JsonValue inputValue) : key(inputKey), value(inputValue) {}
  template<size_t Size>
  JsonMember(const char (&inputKey)[Size], JsonValue inputValue) : key(inputKey), value(inputValue) {}
};

inline JsonValue::JsonValue(JsonObjectValue value) : type(TYPE_OBJECT), listValue{value.members.data(), value.members.size()} {}

inline JsonArrayValue JsonArray(std::initializer_list<JsonValue>&& values) { return {JsonSpan<const JsonValue>(values.size(), values.begin())}; }
inline JsonObjectValue JsonObject(std::initializer_list<JsonMember>&& members) { return {JsonSpan<const JsonMember>(members.size(), members.begin())}; }

Json::Status WriteJson(const JsonValue& value, JsonSpan<char> output);
Json::Status WriteJsonPretty(const JsonValue& value, JsonSpan<char> output);
Json::Status WriteJson(const JsonValue& value, WritableFile& output);
Json::Status WriteJsonPretty(const JsonValue& value, WritableFile& output);
Json::Status WriteJson(const JsonValue& value, WritableFile&& output);
Json::Status WriteJsonPretty(const JsonValue& value, WritableFile&& output);

Json::Status WriteJson(const Json& value, JsonSpan<char> output);
Json::Status WriteJsonPretty(const Json& value, JsonSpan<char> output);
Json::Status WriteJson(const Json& value, WritableFile& output);
Json::Status WriteJsonPretty(const Json& value, WritableFile& output);
Json::Status WriteJson(const Json& value, WritableFile&& output);
Json::Status WriteJsonPretty(const Json& value, WritableFile&& output);

////////////////////////////////////////////////////////////////////////////////
}  // namespace flat
////////////////////////////////////////////////////////////////////////////////
