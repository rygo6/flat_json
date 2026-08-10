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
  template<size_t Size> constexpr JsonString(const char (&value)[Size]) : pData(value), size(Size - 1) {}

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
template<size_t Capacity> struct FixedJsonBuffer : JsonBuffer
{
  static_assert(Capacity >= 16, "FixedJsonBuffer capacity is too small.");
  static_assert(Capacity < UINT32_MAX, "FixedJsonBuffer capacity exceeds its 32-bit offsets.");
  alignas(8) char bytes[Capacity];

  FixedJsonBuffer() : JsonBuffer(Capacity, bytes) {}

  FixedJsonBuffer(const FixedJsonBuffer&) = delete;
  FixedJsonBuffer& operator=(const FixedJsonBuffer&) = delete;
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
  JSON_INLINE bool IsString() const { return type == TYPE_STRING; }
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

  template<size_t Size> JSON_INLINE bool Contains(const char (&key)[Size]) const { return Contains(JsonString(key)); }
  template<size_t Size> JSON_INLINE bool HasKey(const char (&key)[Size]) const { return HasKey(JsonString(key)); }

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

  template<size_t Size> JSON_INLINE const Json& operator[](const char (&key)[Size]) const { return (*this)[JsonString(key)]; }

  JSON_INLINE const Json& operator[](int index) const { JSON_ASSERT(index >= 0, "JSON index is negative."); return (*this)[(size_t)index]; }

  static const char* StatusToString(Status status);
  static size_t EstimateSize(const char* pData, size_t size);
  static size_t EstimateSize(JsonSpan<const char> data) { return EstimateSize(data.data(), data.size()); }
  static size_t EstimateSize(const FileMap& input);

  template<size_t Size> static size_t EstimateSize(const char (&text)[Size]) { return EstimateSize(text, Size - 1); }

  static Status Parse(const char* pData, size_t size, JsonBuffer* pBuffer);
  static Status Parse(JsonSpan<const char> data, JsonBuffer* pBuffer) { return Parse(data.data(), data.size(), pBuffer); }
  static Status Parse(const FileMap& input, JsonBuffer* pBuffer);

  template<size_t Size> static Status Parse(const char (&text)[Size], JsonBuffer* pBuffer) { return Parse(text, Size - 1, pBuffer); }
};

static_assert(sizeof(Json) == 16, "Json records must remain 16 bytes for direct array indexing.");

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
  template<size_t Size> JsonValue(const char (&value)[Size]) : type(TYPE_STRING), stringValue(value) {}
  JsonValue(JsonArrayValue value) : type(TYPE_ARRAY), listValue{value.values.data(), value.values.size()} {}
  JsonValue(JsonObjectValue value);
};

struct JsonMember
{
  JsonString key;
  JsonValue value;

  JsonMember(JsonString inputKey, JsonValue inputValue) : key(inputKey), value(inputValue) {}
  template<size_t Size> JsonMember(const char (&inputKey)[Size], JsonValue inputValue) : key(inputKey), value(inputValue) {}
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
