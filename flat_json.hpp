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
#include <initializer_list>
#include <limits.h>
#include <span>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <utility>

#ifndef JSN_INFO
#define JSN_INFO(format, ...) fprintf(stderr, __FILE__ ":%-*d[JSN] "       format, (int)(20 - sizeof(__FILE__)), __LINE__ __VA_OPT__(,) __VA_ARGS__)
#endif

#ifndef JSN_WARN
#define JSN_WARN(format, ...) fprintf(stderr, __FILE__ ":%-*d[JSN] WARN: " format, (int)(20 - sizeof(__FILE__)), __LINE__ __VA_OPT__(,) __VA_ARGS__)
#endif

#ifndef JSN_ERR
#define JSN_ERR(format, ...) fprintf(stderr, __FILE__ ":%-*d[JSN] ERR: "  format, (int)(20 - sizeof(__FILE__)), __LINE__ __VA_OPT__(,) __VA_ARGS__)
#endif

#ifndef JSN_VERBOSE
#ifdef JSN_ENABLE_VERBOSE
#define JSN_VERBOSE(format, ...) JSN_INFO(format __VA_OPT__(,) __VA_ARGS__)
#else
#define JSN_VERBOSE(format, ...) ((void)0)
#endif
#endif

#ifndef JSN_PANIC
#define JSN_PANIC(format, ...) do {                                                          \
  fprintf(stderr, __FILE__ ":%-*d[JSN] PANIC: " format "\n",                              \
          (int)(20 - sizeof(__FILE__)), __LINE__ __VA_OPT__(,) __VA_ARGS__);                 \
  __builtin_trap();                                                                          \
} while (0)
#endif

#ifndef JSN_REQUIRE
#define JSN_REQUIRE(expr, ...) do {                                                          \
  if (!(expr)) [[unlikely]] {                                                                \
    fprintf(stderr, __FILE__ ":%-*d[JSN] ERR: JSN_REQUIRE: %s",                             \
            (int)(20 - sizeof(__FILE__)), __LINE__, #expr);                                  \
    __VA_OPT__(fputs(": ", stderr); fprintf(stderr, __VA_ARGS__);)                           \
    fputc('\n', stderr);                                                                     \
    __builtin_trap();                                                                        \
  }                                                                                          \
} while (0)
#endif

#ifndef JSN_ASSERT
#ifdef DEBUG
#define JSN_ASSERT(expr, ...) do {                                                           \
  if (!(expr)) [[unlikely]] {                                                                \
    fprintf(stderr, __FILE__ ":%-*d[JSN] ERR: JSN_ASSERT: %s",                              \
            (int)(20 - sizeof(__FILE__)), __LINE__, #expr);                                  \
    __VA_OPT__(fputs(": ", stderr); fprintf(stderr, __VA_ARGS__);)                           \
    fputc('\n', stderr);                                                                     \
    __builtin_trap();                                                                        \
  }                                                                                          \
} while (0)
#else
#define JSN_ASSERT(expr, ...) ((void)0)
#endif
#endif

////////////////////////////////////////////////////////////////////////////////
namespace flat {
////////////////////////////////////////////////////////////////////////////////

using u32 = uint32_t;

struct ArenaBuffer
{
  char* pBase = nullptr;

  static ArenaBuffer Attach(void* pBuffer, size_t size);
};

struct MappedBuffer
{
  void* mapped = nullptr;
  size_t size = 0;
  size_t cursor = 0;
  int _descriptor = -1;
  bool _writable = false;

  MappedBuffer() = default;
  MappedBuffer(void* pMapping, size_t byteCount, size_t byteOffset = 0);
  explicit MappedBuffer(const char* pPath);
  MappedBuffer(const char* pPath, size_t capacity);
  ~MappedBuffer();

  MappedBuffer(const MappedBuffer&) = delete;
  MappedBuffer& operator=(const MappedBuffer&) = delete;
  MappedBuffer(MappedBuffer&&) = delete;
  MappedBuffer& operator=(MappedBuffer&&) = delete;

  bool IsValid() const { return mapped != nullptr; }
};

struct HeapArena : ArenaBuffer
{
  explicit HeapArena(size_t capacity = 1ull << 30);
  ~HeapArena();

  HeapArena(const HeapArena&) = delete;
  HeapArena& operator=(const HeapArena&) = delete;
};

template<size_t Capacity> struct FixedArena : ArenaBuffer
{
  static_assert(Capacity >= 24, "FixedArena capacity is too small.");

  alignas(8) char bytes[Capacity];

  FixedArena() : ArenaBuffer(Attach(bytes, Capacity)) {}

  FixedArena(const FixedArena&) = delete;
  FixedArena& operator=(const FixedArena&) = delete;
};

////////////////////////////////////////////////////////////////////////////////
// JSON read types
////////////////////////////////////////////////////////////////////////////////

struct Json
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
  };

  enum Status
  {
    SUCCESS,
    MALFORMED,
    ABSENT_VALUE
  };

  Type type;
  u32 span;
  union {
    bool boolValue;
    float floatValue;
    double doubleValue;
    long long longValue;
    u32 stringOffset;  // Relative to this Json.
    u32 arrayOffset;   // Relative to this Json.
    u32 objectOffset;  // Relative to this Json.
  };

  Json(const decltype(nullptr) = nullptr) : type(TYPE_NULL), span(0) {}

  bool IsNull() const { return type == TYPE_NULL; }
  bool IsBool() const { return type == TYPE_BOOL; }
  bool IsNumber() const { return IsFloat() || IsDouble() || IsLong(); }
  bool IsLong() const { return type == TYPE_LONG; }
  bool IsFloat() const { return type == TYPE_FLOAT; }
  bool IsDouble() const { return type == TYPE_DOUBLE; }
  bool IsString() const { return type == TYPE_STRING; }
  bool IsArray() const { return type == TYPE_ARRAY; }
  bool IsObject() const { return type == TYPE_OBJECT; }

  bool GetBool() const;
  float GetFloat() const;
  double GetDouble() const;
  double GetNumber() const;
  long long GetLong() const;
  size_t GetSize() const;

  const char* GetString() const;
  const Json& GetArray() const { JSN_REQUIRE(IsArray(), "JSON value is not an array."); return *this; }
  const Json& GetObject() const { JSN_REQUIRE(IsObject(), "JSON value is not an object."); return *this; }

  bool Contains(std::span<const char> key) const;

  template<size_t Size> bool Contains(const char (&key)[Size]) const { return Contains(std::span<const char>(key, Size - 1)); }

  const char* ToString(ArenaBuffer output) const;
  const char* ToStringPretty(ArenaBuffer output) const;

  const Json& operator[](size_t index) const;
  const Json& operator[](std::span<const char> key) const;

  template<size_t Size> const Json& operator[](const char (&key)[Size]) const { return (*this)[std::span<const char>(key, Size - 1)]; }

  const Json& operator[](int index) const { JSN_REQUIRE(index >= 0, "JSON index is negative."); return (*this)[(size_t)index]; }

  static const char* StatusToString(Status status);
  static std::pair<Status, const Json*> Parse(ArenaBuffer arena, const char* pData, size_t size);
  static std::pair<Status, const Json*> Parse(ArenaBuffer arena, const MappedBuffer& input);

  static std::pair<Status, const Json*> Parse(ArenaBuffer arena, std::span<const char> data) { return Parse(arena, data.data(), data.size()); }

  template<size_t Size> static std::pair<Status, const Json*> Parse(ArenaBuffer arena, const char (&text)[Size]) { return Parse(arena, text, Size - 1); }

};

////////////////////////////////////////////////////////////////////////////////
// JSON write types
////////////////////////////////////////////////////////////////////////////////

struct JsonValue;
struct JsonMember;

struct JsonArrayValue
{
  std::span<const JsonValue> values;
};

struct JsonObjectValue
{
  std::span<const JsonMember> members;
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

  struct Text
  {
    const char* pData;
    size_t size;
  };

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
    Text stringValue;
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
  JsonValue(std::span<const char> value) : type(TYPE_STRING), stringValue{value.data(), value.size()} {}
  template<size_t Size> JsonValue(const char (&value)[Size]) : type(TYPE_STRING), stringValue{value, Size - 1} {}
  JsonValue(JsonArrayValue value) : type(TYPE_ARRAY), listValue{value.values.data(), value.values.size()} {}
  JsonValue(JsonObjectValue value);
};

struct JsonMember
{
  std::span<const char> key;
  JsonValue value;

  JsonMember(std::span<const char> inputKey, JsonValue inputValue) : key(inputKey), value(inputValue) {}
  template<size_t Size> JsonMember(const char (&inputKey)[Size], JsonValue inputValue) : key(inputKey, Size - 1), value(inputValue) {}
};

inline JsonValue::JsonValue(JsonObjectValue value) : type(TYPE_OBJECT), listValue{value.members.data(), value.members.size()} {}

inline JsonArrayValue JsonArray(std::initializer_list<JsonValue>&& values) { return {std::span<const JsonValue>(values.begin(), values.size())}; }
inline JsonObjectValue JsonObject(std::initializer_list<JsonMember>&& members) { return {std::span<const JsonMember>(members.begin(), members.size())}; }

const char* WriteJson(const JsonValue& value, ArenaBuffer output);
const char* WriteJsonPretty(const JsonValue& value, ArenaBuffer output);
const char* WriteJson(const JsonValue& value, std::span<char> output, size_t* pByteCount);
const char* WriteJsonPretty(const JsonValue& value, std::span<char> output, size_t* pByteCount);
const char* WriteJson(const JsonValue& value, MappedBuffer& output);
const char* WriteJsonPretty(const JsonValue& value, MappedBuffer& output);

const char* WriteJson(const Json& value, ArenaBuffer output);
const char* WriteJsonPretty(const Json& value, ArenaBuffer output);
const char* WriteJson(const Json& value, std::span<char> output, size_t* pByteCount);
const char* WriteJsonPretty(const Json& value, std::span<char> output, size_t* pByteCount);
const char* WriteJson(const Json& value, MappedBuffer& output);
const char* WriteJsonPretty(const Json& value, MappedBuffer& output);

////////////////////////////////////////////////////////////////////////////////
}  // namespace flat
////////////////////////////////////////////////////////////////////////////////
