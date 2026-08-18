////////////////////////////////////////////////////////////////////////////////
// @author: rygo6
// flat_container.hpp — Minimal non-owning flat containers.
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifndef CNTR_ASSERT
#ifdef DEBUG
#define CNTR_ASSERT(expr, ...) ({                                   \
  if (!(expr)) [[unlikely]] {                                      \
    fprintf(stderr, __FILE__ ":%-*d[CNTR] ERR: CNTR_ASSERT: %s",     \
            (int)(20 - sizeof(__FILE__)), __LINE__, #expr);        \
    __VA_OPT__(fputs(": ", stderr); fprintf(stderr, __VA_ARGS__);) \
    fputc('\n', stderr);                                           \
    __builtin_trap();                                              \
  }                                                                \
})
#else
#define CNTR_ASSERT(expr, ...) ((void)0)
#endif
#endif

////////////////////////////////////////////////////////////////////////////////
namespace flat {
////////////////////////////////////////////////////////////////////////////////

using u32 = uint32_t;

template<typename S, typename T>
concept SpanOf = requires(size_t count, T* data) { S(count, data); };

///////////////////////////////////////////////////////
// String
//  Non-owning bounded string view; size excludes the terminating NUL when one is present.
///////////////////////////////////////////////////////
struct String
{
  size_t size = 0;
  const char* data = "";

  constexpr String() = default;
  constexpr String(size_t n, const char* p) : size(n), data(p) {}

  template<size_t Size>
  constexpr String(const char (&value)[Size]) : size(Size - 1), data(value) {}

  constexpr bool IsEmpty() const { return size == 0; }
  constexpr char operator[](size_t index) const { return data[index]; }
};

///////////////////////////////////////////////////////
// FixedArray
//  Fixed-size inline storage that implicitly converts to a Span.
///////////////////////////////////////////////////////
template<typename T, size_t Size>
struct FixedArray
{
  static constexpr size_t size = Size;
  T data[Size];

  static constexpr bool HasIndex(size_t index) { return index < Size; }
  constexpr T& operator[](size_t index) { CNTR_ASSERT(HasIndex(index), "FixedArray index out of range; check HasIndex() first"); return data[index]; }
  constexpr const T& operator[](size_t index) const { CNTR_ASSERT(HasIndex(index), "FixedArray index out of range; check HasIndex() first"); return data[index]; }
  constexpr T* begin() { return data; }
  constexpr const T* begin() const { return data; }
  constexpr T* end() { return data + Size; }
  constexpr const T* end() const { return data + Size; }

  template<SpanOf<T> S>
  constexpr operator S() { return S(Size, data); }
  template<SpanOf<const T> S>
  constexpr operator S() const { return S(Size, data); }
};

template<typename T, typename... Ts>
FixedArray(T, Ts...) -> FixedArray<T, sizeof...(Ts) + 1>;

///////////////////////////////////////////////////////
// Span
//  Non-owning (count, pointer) view; range-iterable so a C array + count reads as a for-each.
///////////////////////////////////////////////////////
template<typename T>
struct Span
{
  u32 size = 0;
  T* data = nullptr;

  constexpr Span() = default;
  constexpr Span(size_t n, T* p) : size((u32)n), data(p)
  {
    CNTR_ASSERT(Fits(n), "Span exceeds its 32-bit size; check Fits() first");
  }

  template<typename U, size_t Size>
    requires std::is_convertible_v<U(*)[], T(*)[]>
  constexpr Span(U (&value)[Size]) : size((u32)Size), data(value)
  {
    static_assert(Size <= UINT32_MAX, "Span array exceeds its 32-bit size.");
  }

  static constexpr bool Fits(size_t n) { return n <= UINT32_MAX; }
  constexpr bool IsEmpty() const { return size == 0; }
  constexpr bool HasIndex(u32 i) const { return i < size; }
  constexpr T& operator[](u32 i) const { CNTR_ASSERT(HasIndex(i), "Span index out of range; check HasIndex() first"); return data[i]; }
  constexpr T* begin() const { return data; }
  constexpr T* end() const { return data + size; }
};


////////////////////////////////////////////////////////////////////////////////
}  // namespace flat
////////////////////////////////////////////////////////////////////////////////
