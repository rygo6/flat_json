////////////////////////////////////////////////////////////////////////////////
// @author: rygo6
// Document.hpp - Flat, caller-owned arena JSON parsing and serialization.
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

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#if defined(_MSC_VER)
#error "MSVC not supported. Please convert your MSVC dependent code to clang or GCC. A modern LLM will be able to do this automatically."
#elif !defined(__clang__) && !defined(__GNUC__)
#error "Flat C++ JSON requires Clang or GCC."
#endif

#include <stdio.h>

#include "Container.hpp"

#define DOC_INLINE [[gnu::always_inline]]

////////////////////////////////////////////////////////////////////////////////
// Logging
////////////////////////////////////////////////////////////////////////////////

#define DOC_INFO(format, ...) INFO(DOC, (220,220,120), format, ##__VA_ARGS__)
#define DOC_WARN(format, ...) WARN(DOC, format, ##__VA_ARGS__)
#define DOC_ERR(format, ...)  ERR(DOC, format, ##__VA_ARGS__)
#define DOC_VERBOSE(format, ...) VERBOSE(DOC, (220,220,120), format, ##__VA_ARGS__)

#define DOC_PANIC(format, ...)  PANIC(DOC, format, ##__VA_ARGS__)
#define DOC_REQUIRE(expr, ...)  REQUIRE(DOC, expr, "" __VA_OPT__(__VA_ARGS__))
#define DOC_ASSERT(expr, ...)   ASSERT(DOC, expr, "" __VA_OPT__(__VA_ARGS__))

////////////////////////////////////////////////////////////////////////////////
namespace Flat::Document {
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// Read types
////////////////////////////////////////////////////////////////////////////////

enum Type : u32
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

struct Node
{
  static constexpr u32 ReversedArrayFlag = 1u << 31;
  static constexpr u32 ArraySizeMask = ReversedArrayFlag - 1;

  Type type;
  u32 span;
  union {
    bool boolValue;
    float floatValue;
    double doubleValue;
    long long longValue;
    struct {
      u32 stringOffset;  // Relative to this Node; points directly to UTF-8 bytes.
      u32 stringSize;
    };
    struct {
      u32 arrayOffset;  // Relative to this Node.
      u32 arraySize;
    };
    struct {
      u32 objectOffset;  // Relative to this Node.
      u32 objectSize;
    };
  };

  Node(const decltype(nullptr) = nullptr) : type(TYPE_NULL), span(0) {}

  DOC_INLINE bool IsNull() const { return type == TYPE_NULL; }
  DOC_INLINE bool IsBool() const { return type == TYPE_BOOL; }
  DOC_INLINE bool IsNumber() const { return IsFloat() || IsDouble() || IsLong(); }
  DOC_INLINE bool IsFloatingPoint() const { return IsFloat() || IsDouble(); }
  DOC_INLINE bool IsLong() const { return type == TYPE_LONG; }
  DOC_INLINE bool IsFloat() const { return type == TYPE_FLOAT; }
  DOC_INLINE bool IsDouble() const { return type == TYPE_DOUBLE; }
  DOC_INLINE bool IsString() const { return type == TYPE_STRING || type == TYPE_PLAIN_STRING; }
  DOC_INLINE bool IsArray() const { return type == TYPE_ARRAY; }
  DOC_INLINE bool IsObject() const { return type == TYPE_OBJECT; }

  DOC_INLINE bool GetBool() const { DOC_ASSERT(IsBool(), "Node value is not a bool."); return boolValue; }
  DOC_INLINE float GetFloat() const { DOC_ASSERT(IsFloatingPoint(), "Node value is not a floating-point number."); return IsFloat() ? floatValue : (float)doubleValue; }
  DOC_INLINE double GetDouble() const { DOC_ASSERT(IsDouble(), "Node value is not a double."); return doubleValue; }
  DOC_INLINE double GetNumber() const { DOC_ASSERT(IsNumber(), "Node value is not a number."); return IsLong() ? (double)longValue : IsFloat() ? (double)floatValue : doubleValue; }
  DOC_INLINE long long GetLong() const { DOC_ASSERT(IsLong(), "Node value is not a long."); return longValue; }
  DOC_INLINE size_t GetSize() const { DOC_ASSERT(HasSize(), "Node value has no size."); return IsString() ? stringSize : IsArray() ? arraySize & ArraySizeMask : objectSize; }

  DOC_INLINE String GetString() const { DOC_ASSERT(IsString(), "Node value is not a string."); return String(stringSize, (const char*)this + stringOffset); }
  DOC_INLINE const Node& GetArray() const { DOC_ASSERT(IsArray(), "Node value is not an array."); return *this; }
  DOC_INLINE const Node& GetObject() const { DOC_ASSERT(IsObject(), "Node value is not an object."); return *this; }

  bool Contains(String key) const;
  DOC_INLINE bool HasIndex(size_t index) const { return IsArray() && index < (arraySize & ArraySizeMask); }
  DOC_INLINE bool HasIndex(int index) const { return index >= 0 && HasIndex((size_t)index); }
  DOC_INLINE bool HasKey(String key) const { return Contains(key); }
  DOC_INLINE bool HasSize() const { return IsString() || IsArray() || IsObject(); }

  template<size_t Size>
  DOC_INLINE bool Contains(const char (&key)[Size]) const { return Contains(String(key)); }
  template<size_t Size>
  DOC_INLINE bool HasKey(const char (&key)[Size]) const { return HasKey(String(key)); }

  Result ToString(Span<char> output) const;
  Result ToStringPretty(Span<char> output) const;

  DOC_INLINE const Node& operator[](size_t index) const
  {
    DOC_ASSERT(IsArray(), "Node value is not an array.");
    u32 size = arraySize & ArraySizeMask;
    DOC_ASSERT(index < size, "Node index %zu is outside array of size %u.", index, size);
    size_t physicalIndex = arraySize & ReversedArrayFlag ? size - index - 1 : index;
    return *(const Node*)((const char*)this + arrayOffset + physicalIndex * sizeof(Node));
  }
  const Node& operator[](String key) const;

  template<size_t Size>
  DOC_INLINE const Node& operator[](const char (&key)[Size]) const { return (*this)[String(key)]; }

  DOC_INLINE const Node& operator[](int index) const { DOC_ASSERT(index >= 0, "Node index is negative."); return (*this)[(size_t)index]; }
  DOC_INLINE const Node& operator[](u32 index) const { return (*this)[(size_t)index]; }

  DOC_INLINE bool TryCopyString(String key, Span<char> output) const
  {
    if (!output.data || !output.size)
      return false;

    if (!Contains(key))
      return false;

    const Node& value = (*this)[key];
    if (!value.IsString())
      return false;

    String text = value.GetString();
    int written = snprintf(output.data, output.size, "%.*s", (int)text.size, text.data);
    return written >= 0 && (size_t)written == text.size && text.size < output.size;
  }

  template<size_t Size>
  DOC_INLINE bool TryCopyString(const char (&key)[Size], Span<char> output) const { return TryCopyString(String(key), output); }

  DOC_INLINE bool TryGetLong(String key, long long* pOut) const
  {
    if (!pOut || !Contains(key))
      return false;

    const Node& value = (*this)[key];
    if (!value.IsLong())
      return false;

    *pOut = value.GetLong();
    return true;
  }

  DOC_INLINE bool TryGetU32(String key, u32* pOut) const
  {
    long long value;
    if (!pOut || !TryGetLong(key, &value) || value < 0 || value > (long long)UINT32_MAX)
      return false;

    *pOut = (u32)value;
    return true;
  }

  DOC_INLINE bool TryGetFloat(String key, float* pOut) const
  {
    if (!pOut || !Contains(key))
      return false;

    const Node& value = (*this)[key];
    if (!value.IsNumber())
      return false;

    *pOut = (float)value.GetNumber();
    return true;
  }

  DOC_INLINE bool TryGetDouble(String key, double* pOut) const
  {
    if (!pOut || !Contains(key))
      return false;

    const Node& value = (*this)[key];
    if (!value.IsNumber())
      return false;

    *pOut = value.GetNumber();
    return true;
  }

  DOC_INLINE bool TryGetBool(String key, bool* pOut) const
  {
    if (!pOut || !Contains(key))
      return false;

    const Node& value = (*this)[key];
    if (!value.IsBool())
      return false;

    *pOut = value.GetBool();
    return true;
  }

  DOC_INLINE bool TryGetString(String key, String* pOut) const
  {
    if (!pOut || !Contains(key))
      return false;

    const Node& value = (*this)[key];
    if (!value.IsString())
      return false;

    *pOut = value.GetString();
    return true;
  }

  DOC_INLINE bool TryGetArray(String key, const Node** ppOut) const
  {
    if (!ppOut || !Contains(key))
      return false;

    const Node& value = (*this)[key];
    if (!value.IsArray())
      return false;

    *ppOut = &value;
    return true;
  }

  DOC_INLINE bool TryGetObject(String key, const Node** ppOut) const
  {
    if (!ppOut || !Contains(key))
      return false;

    const Node& value = (*this)[key];
    if (!value.IsObject())
      return false;

    *ppOut = &value;
    return true;
  }

  template<size_t Size>
  DOC_INLINE bool TryGetLong(const char (&key)[Size], long long* pOut) const { return TryGetLong(String(key), pOut); }

  template<size_t Size>
  DOC_INLINE bool TryGetU32(const char (&key)[Size], u32* pOut) const { return TryGetU32(String(key), pOut); }

  template<size_t Size>
  DOC_INLINE bool TryGetFloat(const char (&key)[Size], float* pOut) const { return TryGetFloat(String(key), pOut); }

  template<size_t Size>
  DOC_INLINE bool TryGetDouble(const char (&key)[Size], double* pOut) const { return TryGetDouble(String(key), pOut); }

  template<size_t Size>
  DOC_INLINE bool TryGetBool(const char (&key)[Size], bool* pOut) const { return TryGetBool(String(key), pOut); }

  template<size_t Size>
  DOC_INLINE bool TryGetString(const char (&key)[Size], String* pOut) const { return TryGetString(String(key), pOut); }

  template<size_t Size>
  DOC_INLINE bool TryGetArray(const char (&key)[Size], const Node** ppOut) const { return TryGetArray(String(key), ppOut); }

  template<size_t Size>
  DOC_INLINE bool TryGetObject(const char (&key)[Size], const Node** ppOut) const { return TryGetObject(String(key), ppOut); }

  DOC_INLINE bool TryCopyFloatArray(String key, Span<float> output) const
  {
    const Node* pArray;
    if ((output.size && !output.data) || !TryGetArray(key, &pArray) || pArray->GetSize() != output.size)
      return false;

    for (size_t index = 0; index < output.size; ++index) {
      if (!(*pArray)[index].IsNumber())
        return false;
    }

    for (size_t index = 0; index < output.size; ++index)
      output.data[index] = (float)(*pArray)[index].GetNumber();

    return true;
  }

  DOC_INLINE bool TryCopyDoubleArray(String key, Span<double> output) const
  {
    const Node* pArray;
    if ((output.size && !output.data) || !TryGetArray(key, &pArray) || pArray->GetSize() != output.size)
      return false;

    for (size_t index = 0; index < output.size; ++index) {
      if (!(*pArray)[index].IsNumber())
        return false;
    }

    for (size_t index = 0; index < output.size; ++index)
      output.data[index] = (*pArray)[index].GetNumber();

    return true;
  }

  template<size_t Size>
  DOC_INLINE bool TryCopyFloatArray(const char (&key)[Size], Span<float> output) const { return TryCopyFloatArray(String(key), output); }

  template<size_t Size>
  DOC_INLINE bool TryCopyDoubleArray(const char (&key)[Size], Span<double> output) const { return TryCopyDoubleArray(String(key), output); }

  DOC_INLINE bool TryParseHexString(String key, u32* pOut) const
  {
    String text;
    if (!pOut || !TryGetString(key, &text))
      return false;

    if (!text.size || text.size > 15)
      return false;

    char bounded[16] = {};
    __builtin_memcpy(bounded, text.data, text.size);

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
  DOC_INLINE bool TryParseHexString(const char (&key)[Size], u32* pOut) const { return TryParseHexString(String(key), pOut); }

  struct Member
  {
    String key;
    const Node& value;
  };

  const Node* MemberAt(size_t index, String* pKey) const;

  struct ArrayIterator
  {
    const Node* pDocument;
    size_t index;

    DOC_INLINE const Node& operator*() const { return (*pDocument)[index]; }
    DOC_INLINE ArrayIterator& operator++() { ++index; return *this; }
    DOC_INLINE bool operator!=(const ArrayIterator& other) const { return index != other.index; }
  };

  struct MemberIterator
  {
    const Node* pDocument;
    size_t index;

    DOC_INLINE Member operator*() const { String key; const Node* pValue = pDocument->MemberAt(index, &key); return {key, *pValue}; }
    DOC_INLINE MemberIterator& operator++() { ++index; return *this; }
    DOC_INLINE bool operator!=(const MemberIterator& other) const { return index != other.index; }
  };

  struct ElementsView
  {
    const Node* pDocument;

    DOC_INLINE ArrayIterator begin() const { return {pDocument, 0}; }
    DOC_INLINE ArrayIterator end() const { return {pDocument, pDocument && pDocument->IsArray() ? pDocument->GetSize() : 0}; }
  };

  struct MembersView
  {
    const Node* pDocument;

    DOC_INLINE MemberIterator begin() const { return {pDocument, 0}; }
    DOC_INLINE MemberIterator end() const { return {pDocument, pDocument && pDocument->IsObject() ? (size_t)pDocument->objectSize : 0}; }
  };

  DOC_INLINE ElementsView Elements() const { DOC_ASSERT(IsArray(), "Node value is not an array."); return {this}; }
  DOC_INLINE MembersView Members() const { DOC_ASSERT(IsObject(), "Node value is not an object."); return {this}; }

  DOC_INLINE ElementsView TryElements() const { return {IsArray() ? this : nullptr}; }
  DOC_INLINE MembersView TryMembers() const { return {IsObject() ? this : nullptr}; }

  DOC_INLINE ElementsView TryElements(String key) const
  {
    const Node* pArray = nullptr;
    TryGetArray(key, &pArray);
    return {pArray};
  }

  DOC_INLINE MembersView TryMembers(String key) const
  {
    const Node* pObject = nullptr;
    TryGetObject(key, &pObject);
    return {pObject};
  }

  template<size_t Size>
  DOC_INLINE ElementsView TryElements(const char (&key)[Size]) const { return TryElements(String(key)); }

  template<size_t Size>
  DOC_INLINE MembersView TryMembers(const char (&key)[Size]) const { return TryMembers(String(key)); }
};

static_assert(sizeof(Node) == 16, "Node records must remain 16 bytes for direct array indexing.");

size_t EstimateSize(const char* pData, size_t size);
inline size_t EstimateSize(Span<const char> data) { return EstimateSize(data.data, data.size); }

template <size_t Size>
size_t EstimateSize(const char (&text)[Size]) { return EstimateSize(text, Size - 1); }

Result ParseJSON(const char* pData, size_t size, Arena* pArena, const Node** ppRoot);
inline Result ParseJSON(Span<const char> data, Arena* pArena, const Node** ppRoot) { return ParseJSON(data.data, data.size, pArena, ppRoot); }

template <u32 N>
Result ParseJSON(const char* pData, size_t size, FixedArena<N>* pFixedArena, const Node** ppRoot)
{
  if (!pFixedArena)
    return ParseJSON(pData, size, (Arena*)nullptr, ppRoot);
  Arena arena = *pFixedArena;
  Result status = ParseJSON(pData, size, &arena, ppRoot);
  pFixedArena->offset = arena.offset;
  return status;
}

template <u32 N>
Result ParseJSON(Span<const char> data, FixedArena<N>* pArena, const Node** ppRoot) { return ParseJSON(data.data, data.size, pArena, ppRoot); }

template <size_t Size>
Result ParseJSON(const char (&text)[Size], Arena* pArena, const Node** ppRoot) { return ParseJSON(text, Size - 1, pArena, ppRoot); }

template <size_t Size, u32 N>
Result ParseJSON(const char (&text)[Size], FixedArena<N>* pArena, const Node** ppRoot) { return ParseJSON(text, Size - 1, pArena, ppRoot); }

////////////////////////////////////////////////////////////////////////////////
// Write types
////////////////////////////////////////////////////////////////////////////////

struct Value;
struct Member;

struct ArrayValue : Span<const Value>
{
  ArrayValue(size_t count, const Value* pValues) : Span<const Value>(count, pValues) {}
  ArrayValue(InitList<Value>&& values [[clang::lifetimebound]]) : Span<const Value>(values.size, values.data) {}
  ArrayValue(const ArrayValue&) = delete;
};

struct ObjectValue : Span<const Member>
{
  ObjectValue(size_t count, const Member* pMembers) : Span<const Member>(count, pMembers) {}
  ObjectValue(InitList<Member>&& members [[clang::lifetimebound]]) : Span<const Member>(members.size, members.data) {}
  ObjectValue(const ObjectValue&) = delete;
};

struct Value
{
  Type type;

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
    String stringValue;
    List listValue;
  };

  Value(const decltype(nullptr) = nullptr) : type(TYPE_NULL) {}
  Value(bool value) : type(TYPE_BOOL), boolValue(value) {}
  Value(int value) : type(TYPE_LONG), longValue(value) {}
  Value(unsigned value) : type(TYPE_LONG), longValue(value) {}
  Value(long value) : type(TYPE_LONG), longValue(value) {}
  Value(long long value) : type(TYPE_LONG), longValue(value) {}
  Value(unsigned long value)
  {
    if (value <= LLONG_MAX) {
      type = TYPE_LONG;
      longValue = (long long)value;
    } else {
      type = TYPE_DOUBLE;
      doubleValue = value;
    }
  }

  Value(unsigned long long value)
  {
    if (value <= LLONG_MAX) {
      type = TYPE_LONG;
      longValue = (long long)value;
    } else {
      type = TYPE_DOUBLE;
      doubleValue = value;
    }
  }
  Value(float value) : type(TYPE_FLOAT), floatValue(value) {}
  Value(double value) : type(TYPE_DOUBLE), doubleValue(value) {}
  Value(String value) : type(TYPE_STRING), stringValue(value) {}
  template<size_t Size>
  Value(const char (&value)[Size]) : type(TYPE_STRING), stringValue(value) {}
  Value(const ArrayValue& value [[clang::lifetimebound]]) : type(TYPE_ARRAY), listValue{value.data, value.size} {}
  Value(const ObjectValue& value [[clang::lifetimebound]]) : type(TYPE_OBJECT), listValue{value.data, value.size} {}
};

struct Member
{
  String key;
  Value value;

  Member(String inputKey, Value inputValue) : key(inputKey), value(inputValue) {}
  template<size_t Size>
  Member(const char (&inputKey)[Size], Value inputValue) : key(inputKey), value(inputValue) {}
};

Result WriteJSON(Value&& value, Span<char> output);
Result WriteJSONPretty(Value&& value, Span<char> output);

Result WriteJSON(const Node& value, Span<char> output);
Result WriteJSONPretty(const Node& value, Span<char> output);

////////////////////////////////////////////////////////////////////////////////
}  // namespace Flat::Document
////////////////////////////////////////////////////////////////////////////////
