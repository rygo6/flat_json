////////////////////////////////////////////////////////////////////////////////
// @author rygo6
// Container.hpp — Flat containers, strings, locks, scope guards, pools, and atomic rings.
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Terminal.hpp"
#include "Types.hpp"
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <initializer_list>

#include "Error.hpp"
#include <stdlib.h>

#include <string.h>

////////////////////////////////////////////////////////////////////////////////
// Logging
////////////////////////////////////////////////////////////////////////////////

#define CTR_INFO(format, ...) INFO(CTR, (150,200,180), format, ##__VA_ARGS__)
#define CTR_WARN(format, ...) WARN(CTR, format, ##__VA_ARGS__)
#define CTR_ERR(format, ...)  ERR(CTR, format, ##__VA_ARGS__)

#define CTR_PANIC(format, ...)   PANIC(CTR, format, ##__VA_ARGS__)
#define CTR_REQUIRE(expr, ...)   REQUIRE(CTR, expr, "" __VA_OPT__(__VA_ARGS__))
#define CTR_ASSERT(expr, ...)    ASSERT(CTR, expr, "" __VA_OPT__(__VA_ARGS__))

///////////////////////////////////////////////////////
// RAII_OWNER
//  Declares a type the sole owner of its allocation: copy, move, and both
//  assignments are deleted, so the resource is allocated in one place and
//  never leaves the scope that declared it.
///////////////////////////////////////////////////////
#define RAII_OWNER(Type)               \
  Type(const Type&)            = delete; \
  Type(Type&&)                 = delete; \
  Type& operator=(const Type&) = delete; \
  Type& operator=(Type&&)      = delete

////////////////////////////////////////////////////////////////////////////////
namespace Flat {
////////////////////////////////////////////////////////////////////////////////

template<typename S, typename T>
concept SpanOf = requires(size_t count, T* data) { S(count, data); };

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
    CTR_ASSERT(Fits(n), "Span exceeds its 32-bit size; check Fits() first");
  }

  template<typename U, size_t Size>
    requires __is_convertible(U(*)[], T(*)[])
  constexpr Span(U (&value)[Size]) : size((u32)Size), data(value) { static_assert(Size <= UINT32_MAX, "Span array exceeds its 32-bit size."); }

  static constexpr bool Fits(size_t n) { return n <= UINT32_MAX; }
  constexpr bool IsEmpty() const { return size == 0; }
  constexpr bool HasIndex(u32 i) const { return i < size; }
  constexpr T& operator[](u32 i) const { CTR_ASSERT(HasIndex(i), "Span index out of range; check HasIndex() first"); return data[i]; }
  constexpr T* begin() const { return data; }
  constexpr T* end() const { return data + size; }
};

////////////////////////////////////////////////////////////////////////////////
// Fundamentals
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////
// Min
///////////////////////////////////////////////////////
template <typename T>
constexpr T Min(const T a, const T b) { return a < b ? a : b; }

///////////////////////////////////////////////////////
// Max
///////////////////////////////////////////////////////
template <typename T>
constexpr T Max(const T a, const T b) { return a > b ? a : b; }

///////////////////////////////////////////////////////
// BitCeil
//  Smallest power of two >= n, for capacities that must wrap by mask (AtomicRingQueue).
///////////////////////////////////////////////////////
constexpr u32 BitCeil(u32 n) { return n <= 1 ? 1 : 1u << (32 - __builtin_clz(n - 1)); }

////////////////////////////////////////////////////////////////////////////////
// Compile-Time Literals
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////
// Literal
//  A compile-time string with its length. The consteval constructor is the gate.
///////////////////////////////////////////////////////
struct Literal {
  const char* pText;
  u32         size;

  consteval Literal(const char* pLiteral)
    : pText(pLiteral), size((u32)__builtin_strlen(pLiteral) + 1) {}
};

consteval Literal operator""_str(const char* pText, size_t) { return Literal(pText); }

///////////////////////////////////////////////////////
// LiteralCompare
//  strcmp bounded by the shorter operand; neither side can be a runtime string.
///////////////////////////////////////////////////////
inline int LiteralCompare(Literal a, Literal b) { return __builtin_strncmp(a.pText, b.pText, Min(a.size, b.size)); }

///////////////////////////////////////////////////////
// LiteralEqual
//  strcmp == 0 bounded by the shorter operand; neither side can be a runtime string.
///////////////////////////////////////////////////////
inline bool LiteralEqual(Literal a, Literal b) { return LiteralCompare(a, b) == 0; }

///////////////////////////////////////////////////////
// LiteralStartsWith
//  Compares b without its terminator, so it tests a prefix of a.
///////////////////////////////////////////////////////
inline bool LiteralStartsWith(Literal a, Literal b) { return __builtin_strncmp(a.pText, b.pText, Min(a.size, b.size - 1)) == 0; }

///////////////////////////////////////////////////////
// LiteralFind
//  First occurrence of c in the literal; null if absent. The length comes from
//  the Literal, so the scan stops at its terminator.
///////////////////////////////////////////////////////
inline const char* LiteralFind(Literal literal, char c) { return (const char*)memchr(literal.pText, c, literal.size - 1); }

////////////////////////////////////////////////////////////////////////////////
// Bounded Strings
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////
// StrCompare
//  Runtime strncmp: reads at most n bytes of either side, so an unterminated
//  buffer cannot run away. Pass the CAPACITY of the shorter operand.
///////////////////////////////////////////////////////
inline int StrCompare(const char* pText, const char* pOther, u32 n) { return __builtin_strncmp(pText, pOther, n); }

///////////////////////////////////////////////////////
// StrEqual
//  Runtime strncmp == 0: reads at most n bytes of either side, so an unterminated
//  buffer cannot run away. Pass the CAPACITY of the shorter operand.
///////////////////////////////////////////////////////
inline bool StrEqual(const char* pText, const char* pOther, u32 n) { return __builtin_strncmp(pText, pOther, n) == 0; }

///////////////////////////////////////////////////////
// StrEqual
//  Whole-string equality against a compile-time Literal; the bound comes from the Literal.
///////////////////////////////////////////////////////
inline bool StrEqual(const char* pText, Literal other) { return __builtin_strncmp(pText, other.pText, other.size) == 0; }

///////////////////////////////////////////////////////
// StrStartsWith
//  Prefix test: n is the length of the prefix, terminator excluded.
///////////////////////////////////////////////////////
inline bool StrStartsWith(const char* pText, const char* pPrefix, u32 n) { return __builtin_strncmp(pText, pPrefix, n) == 0; }

///////////////////////////////////////////////////////
// StrStartsWith
//  Prefix test against a compile-time Literal; the bound comes from the Literal.
///////////////////////////////////////////////////////
inline bool StrStartsWith(const char* pText, Literal prefix) { return __builtin_strncmp(pText, prefix.pText, prefix.size - 1) == 0; }

///////////////////////////////////////////////////////
// StrLength
//  Bounded strlen: returns n when no terminator is found within n bytes.
///////////////////////////////////////////////////////
inline u32 StrLength(const char* pText, u32 n) { return (u32)strnlen(pText, n); }

///////////////////////////////////////////////////////
// StrHash
//  FNV-1a 64 over the terminated string; constexpr, so hashes of known strings
//  are compile-time constants usable as switch case labels.
///////////////////////////////////////////////////////
constexpr u64 StrHash(const char* pText)
{
  u64 hash = 14695981039346656037ull;
  while (*pText) {
    hash ^= (u8)*pText++;
    hash *= 1099511628211ull;
  }

  return hash;
}

///////////////////////////////////////////////////////
// operator""_h64
//  FNV-1a 64 hash literal: "config"_h64 == StrHash("config"); compile-time only.
///////////////////////////////////////////////////////
consteval u64 operator""_h64(const char* pText, size_t) { return StrHash(pText); }

///////////////////////////////////////////////////////
// StrFind
//  First occurrence of c within n bytes; null if absent. Scans all n bytes, so
//  pass the string length, not a buffer capacity, to stop at the terminator.
///////////////////////////////////////////////////////
inline const char* StrFind(const char* pText, char c, u32 n) { return (const char*)memchr(pText, c, n); }

///////////////////////////////////////////////////////
// StrFindSubstring
//  First occurrence of the literal within textLength bytes; null if absent.
///////////////////////////////////////////////////////
const char* StrFindSubstring(const char* pText, u32 textLength, Literal needle);

///////////////////////////////////////////////////////
// FoldASCIICase
//  Folds an ASCII uppercase character for protocol and extension matching.
///////////////////////////////////////////////////////
inline char FoldASCIICase(char value) { return value >= 'A' && value <= 'Z' ? (char)(value - 'A' + 'a') : value; }

///////////////////////////////////////////////////////
// StrCaseEqual
//  Compares exactly the proven byte count using ASCII case folding.
///////////////////////////////////////////////////////
bool StrCaseEqual(const char* pText, const char* pOther, u32 length);

///////////////////////////////////////////////////////
// StrCaseFind
//  Finds a bounded ASCII case-insensitive substring in text of known length.
///////////////////////////////////////////////////////
const char* StrCaseFind(const char* pText, u32 textLength, const char* pNeedle, u32 needleLength);

////////////////////////////////////////////////////////////////////////////////
// Memory
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////
// MemCompare
//  Runtime memcmp over n bytes of either side. Padding bytes compare too, so both
//  operands must come from the same fully-initialized type.
///////////////////////////////////////////////////////
inline int MemCompare(const void* pData, const void* pOther, size_t n) { return __builtin_memcmp(pData, pOther, n); }

///////////////////////////////////////////////////////
// MemEqual
//  Runtime memcmp == 0 over n bytes of either side. Padding bytes compare too, so
//  both operands must come from the same fully-initialized type.
///////////////////////////////////////////////////////
inline bool MemEqual(const void* pData, const void* pOther, size_t n) { return __builtin_memcmp(pData, pOther, n) == 0; }

///////////////////////////////////////////////////////
// MemCompare
//  Whole-object memcmp, size deduced. Rejects pointers, which would compare the
//  addresses rather than what they point at.
///////////////////////////////////////////////////////
template <typename T>
inline int MemCompare(const T& data, const T& other)
{
  static_assert(!__is_pointer(T), "MemCompare on a pointer compares the address; pass the object or a byte count");
  return __builtin_memcmp(&data, &other, sizeof(T));
}

///////////////////////////////////////////////////////
// MemEqual
//  Whole-object memcmp == 0, size deduced. Rejects pointers, which would compare
//  the addresses rather than what they point at.
///////////////////////////////////////////////////////
template <typename T>
inline bool MemEqual(const T& data, const T& other)
{
  static_assert(!__is_pointer(T), "MemEqual on a pointer compares the address; pass the object or a byte count");
  return __builtin_memcmp(&data, &other, sizeof(T)) == 0;
}

///////////////////////////////////////////////////////
// MemZero
///////////////////////////////////////////////////////
template <typename T>
inline void MemZero(T* p) { __builtin_memset(p, 0, sizeof(T)); }

///////////////////////////////////////////////////////
// MemCopy
///////////////////////////////////////////////////////
template <typename T, typename U>
inline void MemCopy(T* dst, const U* src, size_t count = 1)
{
  static_assert(sizeof(T) == sizeof(U), "Copy: source and target sizes differ");
  __builtin_memcpy((void*)dst, (const void*)src, sizeof(T) * count);
}

///////////////////////////////////////////////////////
// MemMove
///////////////////////////////////////////////////////
template <typename T, typename U>
inline void MemMove(T* dst, const U* src, size_t count = 1)
{
  static_assert(sizeof(T) == sizeof(U), "Move: source and target sizes differ");
  __builtin_memmove((void*)dst, (const void*)src, sizeof(T) * count);
}

///////////////////////////////////////////////////////
// CopyString
//  Copies a compile-time string; its own length is the bound, so no capacity is passed.
///////////////////////////////////////////////////////
template <u32 N>
inline bool CopyString(Literal input, char (*pOutput)[N])
{
  if (input.size > N)
    return false;

  MemCopy(*pOutput, input.pText, input.size);
  return true;
}

///////////////////////////////////////////////////////
// CopyString
//  Copies a capacity-bounded C string without silent truncation.
///////////////////////////////////////////////////////
template <u32 N>
inline bool CopyString(const char* pInput, u32 inputCapacity, char (*pOutput)[N])
{
  u32 length = StrLength(pInput, inputCapacity);
  if (length == inputCapacity || length >= N)
    return false;

  MemCopy(*pOutput, pInput, (size_t)length + 1);
  return true;
}

///////////////////////////////////////////////////////
// CopySegment
//  Copies one bounded span into caller storage and terminates it.
///////////////////////////////////////////////////////
template <u32 N>
inline bool CopySegment(const char* pInput, size_t length, char (*pOutput)[N])
{
  if (!length || length >= N)
    return false;

  MemCopy(*pOutput, pInput, length);
  (*pOutput)[length] = 0;
  return true;
}

////////////////////////////////////////////////////////////////////////////////
// Locks
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////
// LockScope
//  Holds a pthread mutex for the enclosing scope; unlocks on every exit path.
///////////////////////////////////////////////////////
struct LockScope {
  pthread_mutex_t* pMutex;

  explicit LockScope(pthread_mutex_t* pMutex) : pMutex(pMutex) { pthread_mutex_lock(pMutex); }
  ~LockScope() { pthread_mutex_unlock(pMutex); }

  LockScope(const LockScope&) = delete;
  void operator=(const LockScope&) = delete;
};

///////////////////////////////////////////////////////
// LOCK_SCOPE
//  Creates a dedicated braced mutex scope without changing break/continue targets.
///////////////////////////////////////////////////////
#define LOCK_SCOPE(mutex) if (LockScope lockScopeGuard(&(mutex)); false) {} else

///////////////////////////////////////////////////////
// EnsureThread
//  Captures one POSIX thread and tests whether later calls remain on that thread.
///////////////////////////////////////////////////////
struct EnsureThread {
  pthread_t thread = {};
  bool captured    = false;

  void Capture() { thread = pthread_self(); captured = true; }
  bool IsCurrentThread() const { return captured && pthread_equal(thread, pthread_self()); }
};

///////////////////////////////////////////////////////
// Atomic
//  Lock-free atomic over the compiler builtins, with explicit ordering in each
//  method name. Rejects at compile time any type wider than the hardware
//  lock-free width, which a library atomic would silently back with a lock.
///////////////////////////////////////////////////////
template <typename T>
struct Atomic {
  static_assert(__atomic_always_lock_free(sizeof(T), 0),
                "Atomic<T> exceeds the hardware lock-free width; wider state needs a cold mutex and LockScope");

  T value = {};

  Atomic() = default;
  constexpr Atomic(T initial) : value(initial) {}

  T    Load() const         { T loaded; __atomic_load(&value, &loaded, __ATOMIC_RELAXED); return loaded; }
  T    LoadAcquire() const  { T loaded; __atomic_load(&value, &loaded, __ATOMIC_ACQUIRE); return loaded; }
  void Store(T next)        { __atomic_store(&value, &next, __ATOMIC_RELAXED); }
  void StoreRelease(T next) { __atomic_store(&value, &next, __ATOMIC_RELEASE); }

  T Exchange(T next)               { T previous; __atomic_exchange(&value, &next, &previous, __ATOMIC_RELAXED); return previous; }
  T ExchangeAcquireRelease(T next) { T previous; __atomic_exchange(&value, &next, &previous, __ATOMIC_ACQ_REL); return previous; }

  bool CompareExchangeAcquireRelease(T* pExpected, T desired) {
    return __atomic_compare_exchange(&value, pExpected, &desired, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
  }

  T FetchAdd(T addend)               { return __atomic_fetch_add(&value, addend, __ATOMIC_RELAXED); }
  T FetchAddRelease(T addend)        { return __atomic_fetch_add(&value, addend, __ATOMIC_RELEASE); }
  T FetchAddAcquireRelease(T addend) { return __atomic_fetch_add(&value, addend, __ATOMIC_ACQ_REL); }
};

inline void AtomicFenceAcquire() { __atomic_thread_fence(__ATOMIC_ACQUIRE); }
inline void AtomicFenceRelease() { __atomic_thread_fence(__ATOMIC_RELEASE); }

///////////////////////////////////////////////////////
// defer
//  Always runs `fn` at scope exit.
///////////////////////////////////////////////////////
template <typename F>
struct Defer {
  F fn;
  ~Defer() { fn(); }
};
template <typename F>
Defer(F) -> Defer<F>;

///////////////////////////////////////////////////////
// errdefer
//  Runs `fn` at scope exit when the referenced result int is non-zero.
///////////////////////////////////////////////////////
template <typename F>
struct ErrDefer {
  const int& rc;
  F          fn;
  ~ErrDefer() { if (rc != 0) fn(); }
};
template <typename F>
ErrDefer(const int&, F) -> ErrDefer<F>;

///////////////////////////////////////////////////////
// Arena
//  Non-owning handle to a bump allocator whose direction is fixed by first use.
//  offset carries both the amount used and the direction: positive grows up
//  from the base, negative grows down from the end, zero is empty and not yet
//  committed. One arena is only ever one direction; claiming the other way
//  asserts. Nothing is freed individually; Reset reclaims everything at once.
///////////////////////////////////////////////////////
struct Arena {
  u32 capacity = 0;
  i32 offset   = 0;
  u8* data     = nullptr;

  Arena() = default;
  Arena(u32 bufferCapacity, void* pBuffer) : capacity(bufferCapacity), data((u8*)pBuffer) {}

  u32  Used() const         { return offset < 0 ? 0u - (u32)offset : (u32)offset; }
  u32  Remaining() const    { return Used() <= capacity ? capacity - Used() : 0; }
  bool CanTake(u32 n) const { return Used() <= capacity && n <= capacity - Used(); }
  template <typename T>
  bool CanPush() const
  {
    static_assert(sizeof(T) <= UINT32_MAX, "Arena Push type exceeds the u32 capacity");
    static_assert(alignof(T) <= 8, "Arena Push type exceeds the arena base alignment");
    static_assert(__is_trivially_destructible(T), "Arena Push holds only trivially-destructible T; free element resources yourself");
    if (offset < 0) return false;
    u32 alignment = (u32)alignof(T);
    u32 start = ((u32)offset + alignment - 1) & ~(alignment - 1);
    return start <= capacity && (u32)sizeof(T) <= capacity - start;
  }
  void Reset()              { offset = 0; }
  bool IsForward() const    { return offset > 0; }
  bool IsReverse() const    { return offset < 0; }

  template <typename T>
  T* Push()
  {
    CTR_ASSERT(offset >= 0, "Arena Push on a reverse arena; an arena is only one direction");
    CTR_ASSERT(CanPush<T>(), "Arena Push exceeds the block; check CanPush<T>() first");
    u32 alignment = (u32)alignof(T);
    u32 start = ((u32)offset + alignment - 1) & ~(alignment - 1);
    offset = (i32)(start + (u32)sizeof(T));
    T* pValue = (T*)(data + start);
    MemZero(pValue);
    return pValue;
  }

  template <typename T>
  Result TryPush(T** ppValue)
  {
    if (!ppValue) return ERROR_INVALID_ARGUMENT;
    *ppValue = nullptr;
    if (!CanPush<T>()) return ERROR_INSUFFICIENT_SPACE;
    *ppValue = Push<T>();
    return SUCCESS;
  }

  void* TakeForward(u32 n, u32 alignment = 8) {
    CTR_ASSERT(offset >= 0, "Arena TakeForward on a reverse arena; an arena is only one direction");
    u32 start = ((u32)offset + alignment - 1) & ~(alignment - 1);
    CTR_ASSERT(start + n <= capacity, "Arena TakeForward exceeds the block; check CanTake() first");
    if (offset < 0 || start + n > capacity) return nullptr;
    offset = (i32)(start + n);
    return data + start;
  }

  void* TakeReverse(u32 n, u32 alignment = 8) {
    CTR_ASSERT(offset <= 0, "Arena TakeReverse on a forward arena; an arena is only one direction");
    u32 end = capacity - Used();
    CTR_ASSERT(n <= end, "Arena TakeReverse exceeds the block; check CanTake() first");
    if (offset > 0 || n > end) return nullptr;
    u32 start = (end - n) & ~(alignment - 1);
    offset = -(i32)(capacity - start);
    return data + start;
  }
};

///////////////////////////////////////////////////////
// ArenaBuffer
//  RAII heap allocation backing an Arena. Frees on scope exit; copy and move
//  are deleted. Bump state and every operation are inherited from Arena.
///////////////////////////////////////////////////////
struct ArenaBuffer : Arena {
  RAII_OWNER(ArenaBuffer);

  ArenaBuffer() = default;
  explicit ArenaBuffer(u32 n) { data = (u8*)malloc(n); capacity = n; }
  ~ArenaBuffer() { free(data); }

  void Resize(u32 n) {
    u32 used = Used();
    CTR_ASSERT(n >= used, "ArenaBuffer Resize below the bytes already claimed");
    if (n < used || n == capacity) return;
    if (offset < 0) {
      if (n > capacity) { data = (u8*)realloc(data, n); MemMove(data + n - used, data + capacity - used, used); }
      else              { MemMove(data + n - used, data + capacity - used, used); data = (u8*)realloc(data, n); }
    } else {
      data = (u8*)realloc(data, n);
    }
    capacity = n;
  }
};

///////////////////////////////////////////////////////
// Array
//  Non-owning handle to a sized-once run of T, where every element is live and
//  the size never changes. Carries the array operations so they instantiate per
//  element type rather than per capacity. Converts to Span for code that only
//  needs a generic window.
///////////////////////////////////////////////////////
template <typename T>
struct Array {
  static_assert(__is_trivially_destructible(T), "Array holds only trivially-destructible T; free element resources yourself");

  u32 size = 0;
  T*  data = nullptr;

  Array() = default;
  Array(u32 elementCount, T* pBuffer) : size(elementCount), data(pBuffer) {}

  static Array Emplace(u32 elementCount, Arena* pArena) {
    Array array;
    array.data = (T*)pArena->TakeForward(elementCount * (u32)sizeof(T), (u32)alignof(T));
    array.size = array.data ? elementCount : 0;
    return array;
  }

  bool IsEmpty() const          { return size == 0; }
  bool HasIndex(u32 index) const { return index < size; }
  i32  Find(const T& value) const { for (u32 i = 0; i < size; ++i) if (data[i] == value) return (i32)i; return -1; }

  T*       front()       { CTR_ASSERT(size > 0, "Array front on empty (check IsEmpty() first)"); return &data[0]; }
  const T& front() const { CTR_ASSERT(size > 0, "Array front on empty (check IsEmpty() first)"); return data[0]; }
  T*       back()        { CTR_ASSERT(size > 0, "Array back on empty (check IsEmpty() first)"); return &data[size - 1]; }
  const T& back()  const { CTR_ASSERT(size > 0, "Array back on empty (check IsEmpty() first)"); return data[size - 1]; }

  T*       operator[](u32 index)       { CTR_ASSERT(HasIndex(index), "Array index out of range; check HasIndex() first"); return &data[index]; }
  const T& operator[](u32 index) const { CTR_ASSERT(HasIndex(index), "Array index out of range; check HasIndex() first"); return data[index]; }
  auto* begin(this auto&& self) { return self.data; }
  auto* end(this auto&& self)   { return self.data + self.size; }

  operator Span<T>() const { return Span<T>(size, data); }
};

///////////////////////////////////////////////////////
// ArrayBuffer
//  RAII heap allocation sized once (no growth); the heap sibling of FixedArray.
//  Frees on scope exit; copy and move are deleted. Every operation is inherited
//  from Array, so nothing is duplicated per allocation site.
///////////////////////////////////////////////////////
template <typename T>
struct ArrayBuffer : Array<T> {
  RAII_OWNER(ArrayBuffer);

  using Array<T>::size;
  using Array<T>::data;

  ArrayBuffer() = default;
  explicit ArrayBuffer(u32 n) { data = (T*)malloc((size_t)n * sizeof(T)); size = data ? n : 0; }
  ~ArrayBuffer() { free(data); }

  void Resize(u32 n) { data = (T*)realloc(data, (size_t)n * sizeof(T)); size = n; }
};

///////////////////////////////////////////////////////
// Vector
//  Non-owning handle to a counted, capacity-bounded run of T, carrying every
//  vector operation. Instantiated per element type only, so the code is not
//  duplicated per capacity. Push never grows: check CanPush, and only a
//  VectorBuffer can Reserve. Requires a trivially destructible T.
///////////////////////////////////////////////////////
template <typename T>
struct Vector {
  static_assert(__is_trivially_destructible(T), "Vector holds only trivially-destructible T; free element resources yourself");

  u32 size     = 0;
  u32 capacity = 0;
  T*  data     = nullptr;

  Vector() = default;
  Vector(u32 initialSize, u32 bufferCapacity, T* pBuffer) : size(initialSize), capacity(bufferCapacity), data(pBuffer) {}

  static Vector Emplace(u32 elementCapacity, Arena* pArena) {
    Vector vector;
    vector.data     = (T*)pArena->TakeForward(elementCapacity * (u32)sizeof(T), (u32)alignof(T));
    vector.capacity = vector.data ? elementCapacity : 0;
    return vector;
  }

  bool IsEmpty() const          { return size == 0; }
  bool IsFull() const           { return size == capacity; }
  void Clear()                  { size = 0; }
  void Resize(u32 n)            { CTR_ASSERT(n <= capacity, "Vector Resize beyond capacity (Reserve a VectorBuffer first)"); size = n < capacity ? n : capacity; }
  bool CanPush() const          { return size < capacity; }
  bool CanPush(u32 count) const { return size + count <= capacity; }

  void Push(const T& value) {
    CTR_ASSERT(CanPush(), "Vector Push overflow (check CanPush(), or use a VectorBuffer)");
    if (size < capacity) data[size++] = value;
  }
  void PushRange(const T* pSource, u32 count) {
    CTR_ASSERT(CanPush(count), "Vector PushRange overflow (check CanPush(count), or use a VectorBuffer)");
    u32 take = size + count <= capacity ? count : capacity - size;
    MemCopy(data + size, pSource, take);
    size += take;
  }
  T* PushSlot() {
    CTR_ASSERT(CanPush(), "Vector PushSlot overflow (check CanPush(), or use a VectorBuffer)");
    if (!CanPush()) return nullptr;
    T* pSlot = &data[size++];
    MemZero(pSlot);
    return pSlot;
  }
  void Pop()                 { CTR_ASSERT(size > 0, "Vector Pop underflow (check IsEmpty() first)"); if (size) --size; }
  void RemoveSwapBack(u32 index) { CTR_ASSERT(index < size, "Vector RemoveSwapBack index out of range"); data[index] = data[--size]; }

  i32  Find(const T& value) const { for (u32 i = 0; i < size; ++i) if (data[i] == value) return (i32)i; return -1; }
  bool TryPush(const T& value)    { if (Find(value) >= 0) return false; Push(value); return true; }

  T*       front()       { CTR_ASSERT(size > 0, "Vector front on empty (check IsEmpty() first)"); return &data[0]; }
  const T& front() const { CTR_ASSERT(size > 0, "Vector front on empty (check IsEmpty() first)"); return data[0]; }
  T*       back()        { CTR_ASSERT(size > 0, "Vector back on empty (check IsEmpty() first)"); return &data[size - 1]; }
  const T& back()  const { CTR_ASSERT(size > 0, "Vector back on empty (check IsEmpty() first)"); return data[size - 1]; }

  T*       operator[](u32 index)       { CTR_ASSERT(index < size, "Vector index out of range"); return &data[index]; }
  const T& operator[](u32 index) const { CTR_ASSERT(index < size, "Vector index out of range"); return data[index]; }
  auto* begin(this auto&& self) { return self.data; }
  auto* end(this auto&& self)   { return self.data + self.size; }
};

///////////////////////////////////////////////////////
// VectorBuffer
//  RAII heap allocation backing a Vector: it owns the malloc, frees on scope
//  exit, and is the only place that may grow. Growth is never implicit: check
//  CanPush, call Reserve, then push. Every other operation is inherited from
//  Vector, which asserts rather than growing.
///////////////////////////////////////////////////////
template <typename T>
struct VectorBuffer : Vector<T> {
  RAII_OWNER(VectorBuffer);

  using Vector<T>::size;
  using Vector<T>::capacity;
  using Vector<T>::data;

  VectorBuffer() = default;
  explicit VectorBuffer(u32 n) { data = (T*)malloc((size_t)n * sizeof(T)); capacity = n; }
  ~VectorBuffer() { free(data); }

  void Reserve(u32 n) {
    if (n <= capacity) return;
    u32 next = capacity ? capacity : 8;
    while (next < n) next *= 2;
    data     = (T*)realloc(data, (size_t)next * sizeof(T));
    capacity = next;
  }
};

///////////////////////////////////////////////////////
// Queue
//  Growable FIFO ring buffer for trivially relocatable T. Ownership transfers
//  by relocation (byte copy of the source, then zero it), so it holds move-only
//  elements without any copy or move constructor.
///////////////////////////////////////////////////////
template <typename T>
struct Queue {
  RAII_OWNER(Queue);

  static_assert(__is_trivially_destructible(T), "Queue holds only trivially-destructible T (plain data or pointers); free element resources yourself");

  u32 size     = 0;
  u32 capacity = 0;
  u32 head     = 0;
  T*  data     = nullptr;

  Queue() = default;
  ~Queue() { free(data); }
  bool IsEmpty() const { return size == 0; }

  void Reserve(u32 n) {
    if (n <= capacity) return;
    u32 next = capacity ? capacity : 8;
    while (next < n) next *= 2;
    T* pNew = (T*)malloc((size_t)next * sizeof(T));
    for (u32 i = 0; i < size; ++i) MemCopy(&pNew[i], &data[(head + i) % capacity]);
    free(data);
    data = pNew;
    capacity  = next;
    head = 0;
  }

  void Enqueue(T* pItem) {
    if (size == capacity) Reserve(capacity ? capacity * 2 : 8);
    u32 slot = (head + size) % capacity;
    MemCopy(&data[slot], pItem);
    MemZero(pItem);
    ++size;
  }

  bool Dequeue(T* pOut) {
    if (size == 0) return false;
    MemCopy(pOut, &data[head]);
    head = (head + 1) % capacity;
    --size;
    return true;
  }
};

///////////////////////////////////////////////////////
// HashMap
//  Open-addressing map (linear probe, backward-shift delete) for integer or
//  pointer keys; key 0 is the reserved empty sentinel. Fibonacci-mixed hash,
//  power-of-two capacity, grows at 0.75 load. malloc-backed; values by value.
///////////////////////////////////////////////////////
template <typename TKey, typename TValue>
struct HashMap {
  RAII_OWNER(HashMap);

  u32     capacity = 0;
  u32     count    = 0;
  u32     shift    = 0;
  TKey*   keys     = nullptr;
  TValue* values   = nullptr;

  HashMap() = default;
  ~HashMap() { free(keys); free(values); }

  static u64 Hash(TKey key)     { return (u64)key * 0x9E3779B97F4A7C15ull; }
  u32 SlotOf(TKey key) const    { return (u32)(Hash(key) >> shift); }
  bool IsEmpty() const          { return count == 0; }

  void InsertRaw(TKey key, const TValue& value) {
    u32 mask = capacity - 1;
    u32 i    = SlotOf(key);
    while (keys[i] != 0) {
      if (keys[i] == key) { values[i] = value; return; }
      i = (i + 1) & mask;
    }
    keys[i]   = key;
    values[i] = value;
    count++;
  }

  void Rehash(u32 newCapacity) {
    TKey*   oldKeys   = keys;
    TValue* oldValues = values;
    u32     oldCapacity    = capacity;
    keys   = (TKey*)malloc((size_t)newCapacity * sizeof(TKey));
    values = (TValue*)malloc((size_t)newCapacity * sizeof(TValue));
    __builtin_memset(keys, 0, (size_t)newCapacity * sizeof(TKey));
    capacity   = newCapacity;
    count = 0;
    shift = (u32)(1 + __builtin_clzll((u64)newCapacity));
    for (u32 i = 0; i < oldCapacity; ++i) if (oldKeys[i] != 0) InsertRaw(oldKeys[i], oldValues[i]);
    free(oldKeys);
    free(oldValues);
  }

  void Insert(TKey key, const TValue& value) {
    CTR_ASSERT(key != 0, "HashMap key 0 is the reserved empty sentinel");
    if (capacity == 0) Rehash(16);
    else if (count + 1 > capacity - (capacity >> 2)) Rehash(capacity * 2);
    InsertRaw(key, value);
  }

  TValue* Find(TKey key) {
    if (!capacity) return nullptr;
    u32 mask = capacity - 1;
    u32 i    = SlotOf(key);
    while (keys[i] != 0) {
      if (keys[i] == key) return &values[i];
      i = (i + 1) & mask;
    }
    return nullptr;
  }

  bool Remove(TKey key) {
    if (!capacity) return false;
    u32 mask = capacity - 1;
    u32 i    = SlotOf(key);
    while (keys[i] != 0 && keys[i] != key) i = (i + 1) & mask;
    if (keys[i] == 0) return false;
    for (u32 j = i;;) {
      j = (j + 1) & mask;
      if (keys[j] == 0) break;
      u32  k       = SlotOf(keys[j]);
      bool canMove = (i <= j) ? (k <= i || k > j) : (k <= i && k > j);
      if (canMove) { keys[i] = keys[j]; values[i] = values[j]; i = j; }
    }
    keys[i] = 0;
    count--;
    return true;
  }
};

///////////////////////////////////////////////////////
// InitList
//  Minimal non-owning view over brace-initialized values.
///////////////////////////////////////////////////////
template <typename T>
struct InitList {
  const T* data = nullptr;
  size_t size   = 0;

  // Rvalue-only requires a call-local brace-list so InitList's pointer/count wrapper compiles out.
  constexpr InitList(std::initializer_list<T>&& values [[clang::lifetimebound]])
    : data(values.begin()), size(values.size())
  {}

  constexpr size_t Size() const { return size; }
  constexpr const T* begin() const { return data; }
  constexpr const T* end() const { return data + size; }
};

///////////////////////////////////////////////////////
// Tuple
//  Minimal necessity for tuple return syntax. auto [var0, var1, var2] =
///////////////////////////////////////////////////////
template <typename...>
struct Tuple;
template <>
struct Tuple<> {};
template <typename T0, typename... Ts>
struct Tuple<T0, Ts...> {
  T0           head;
  Tuple<Ts...> tail;
  constexpr Tuple() = default;
  constexpr Tuple(const T0& h, const Ts&... t) : head(h), tail(t...) {}

  template <size_t I>
  constexpr auto get() const {
    if constexpr (I == 0) return head;
    else                  return tail.template get<I - 1>();
  }
};

////////////////////////////////////////////////////////////////////////////////
// Fixed Containers
//  Compile-time inline storage; prefer over a heap Buffer when bounded, measured 2.1x faster at -O2.
////////////////////////////////////////////////////////////////////////////////

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
  constexpr T& operator[](size_t index) { CTR_ASSERT(HasIndex(index), "FixedArray index out of range; check HasIndex() first"); return data[index]; }
  constexpr const T& operator[](size_t index) const { CTR_ASSERT(HasIndex(index), "FixedArray index out of range; check HasIndex() first"); return data[index]; }
  constexpr auto* begin(this auto&& self) { return self.data; }
  constexpr auto* end(this auto&& self)   { return self.data + Size; }

  template<SpanOf<T> S>
  constexpr operator S() { return S(Size, data); }
  template<SpanOf<const T> S>
  constexpr operator S() const { return S(Size, data); }
};

template<typename T, typename... Ts>
FixedArray(T, Ts...) -> FixedArray<T, sizeof...(Ts) + 1>;

///////////////////////////////////////////////////////
// FixedString
//  Fixed-capacity owning string, always terminated; converts to a flat String view for reads.
///////////////////////////////////////////////////////
template <size_t N>
struct FixedString {
  static constexpr size_t capacity = N;

  char data[N] = {};

  u32  Length()  const { return StrLength(data, N); }
  bool IsEmpty() const { return data[0] == 0; }
  void Clear()         { data[0] = 0; }

  // Overwrites with a bounded copy; false when the source did not fit.
  bool Set(const char* pText)
  {
    int written = snprintf(data, N, "%s", pText);
    return written >= 0 && (u32)written < N;
  }

  bool Equals(const char* pText, u32 n) const { return StrEqual(data, pText, n); }

  constexpr char operator[](size_t index) const { return data[index]; }
};

///////////////////////////////////////////////////////
// FixedArena
//  Inline storage for a bump allocator plus its cursor. Operations are defined
//  here directly; they are small enough that routing them through the handle
//  would emit more code, not less. Converts to an Arena so a helper can take
//  any backing without being templated on the capacity.
///////////////////////////////////////////////////////
template <u32 N>
struct FixedArena {
  i32 offset = 0;
  alignas(8) u8 bytes[N];

  operator Arena() { Arena arena(N, bytes); arena.offset = offset; return arena; }

  u32  Used() const         { return offset < 0 ? 0u - (u32)offset : (u32)offset; }
  u32  Remaining() const    { return Used() <= N ? N - Used() : 0; }
  bool CanTake(u32 n) const { return Used() <= N && n <= N - Used(); }
  template <typename T>
  bool CanPush() const
  {
    static_assert(sizeof(T) <= UINT32_MAX, "FixedArena Push type exceeds the u32 capacity");
    static_assert(alignof(T) <= 8, "FixedArena Push type exceeds the arena base alignment");
    static_assert(__is_trivially_destructible(T), "FixedArena Push holds only trivially-destructible T; free element resources yourself");
    if (offset < 0) return false;
    u32 alignment = (u32)alignof(T);
    u32 start = ((u32)offset + alignment - 1) & ~(alignment - 1);
    return start <= N && (u32)sizeof(T) <= N - start;
  }
  void Reset()              { offset = 0; }
  bool IsForward() const    { return offset > 0; }
  bool IsReverse() const    { return offset < 0; }

  template <typename T>
  T* Push()
  {
    CTR_ASSERT(offset >= 0, "FixedArena Push on a reverse arena; an arena is only one direction");
    CTR_ASSERT(CanPush<T>(), "FixedArena Push exceeds the block; check CanPush<T>() first");
    u32 alignment = (u32)alignof(T);
    u32 start = ((u32)offset + alignment - 1) & ~(alignment - 1);
    offset = (i32)(start + (u32)sizeof(T));
    T* pValue = (T*)(bytes + start);
    MemZero(pValue);
    return pValue;
  }

  template <typename T>
  Result TryPush(T** ppValue)
  {
    if (!ppValue) return ERROR_INVALID_ARGUMENT;
    *ppValue = nullptr;
    if (!CanPush<T>()) return ERROR_INSUFFICIENT_SPACE;
    *ppValue = Push<T>();
    return SUCCESS;
  }

  void* TakeForward(u32 n, u32 alignment = 8) {
    CTR_ASSERT(offset >= 0, "FixedArena TakeForward on a reverse arena; an arena is only one direction");
    u32 start = ((u32)offset + alignment - 1) & ~(alignment - 1);
    CTR_ASSERT(start + n <= N, "FixedArena TakeForward exceeds the block; check CanTake() first");
    if (offset < 0 || start + n > N) return nullptr;
    offset = (i32)(start + n);
    return bytes + start;
  }

  void* TakeReverse(u32 n, u32 alignment = 8) {
    CTR_ASSERT(offset <= 0, "FixedArena TakeReverse on a forward arena; an arena is only one direction");
    u32 end = N - Used();
    CTR_ASSERT(n <= end, "FixedArena TakeReverse exceeds the block; check CanTake() first");
    if (offset > 0 || n > end) return nullptr;
    u32 start = (end - n) & ~(alignment - 1);
    offset = -(i32)(N - start);
    return bytes + start;
  }
};

///////////////////////////////////////////////////////
// FixedVector
//  Inline storage for a counted run of T plus its count. Operations are defined
//  here directly; they are small enough that routing them through the handle
//  would emit more code, not less. Converts to a Vector so a helper can take any
//  backing without being templated on the capacity.
///////////////////////////////////////////////////////
template <typename T, size_t N>
struct FixedVector {
  static_assert(N <= 0xFFFFFFFFu, "FixedVector capacity N exceeds the u32 size counter");

  u32 size = 0;
  T   storage[N];

  operator Vector<T>() { return Vector<T>(size, (u32)N, storage); }

  bool IsEmpty() const          { return size == 0; }
  bool IsFull() const           { return size == N; }
  void Clear()                  { size = 0; }
  bool CanPush() const          { return size < N; }
  bool CanPush(u32 count) const { return size + count <= N; }

  void Push(const T& value) {
    CTR_ASSERT(size < N, "FixedVector Push overflow (check CanPush() first)");
    if (size < N) storage[size++] = value;
  }
  void PushRange(const T* pSource, u32 count) {
    CTR_ASSERT(size + count <= N, "FixedVector PushRange overflow (check CanPush(count) first)");
    u32 take = size + count <= N ? count : (u32)N - size;
    MemCopy(storage + size, pSource, take);
    size += take;
  }
  T* PushSlot() {
    CTR_ASSERT(size < N, "FixedVector PushSlot overflow (check CanPush() first)");
    if (size >= N) return nullptr;
    T* pSlot = &storage[size++];
    MemZero(pSlot);
    return pSlot;
  }
  void Pop()                     { CTR_ASSERT(size > 0, "FixedVector Pop underflow (check IsEmpty() first)"); if (size) --size; }
  void RemoveSwapBack(u32 index) { CTR_ASSERT(index < size, "FixedVector RemoveSwapBack index out of range"); storage[index] = storage[--size]; }
  i32  Find(const T& value) const { for (u32 i = 0; i < size; ++i) if (storage[i] == value) return (i32)i; return -1; }
  bool TryPush(const T& value)    { if (Find(value) >= 0) return false; Push(value); return true; }

  T*       front()       { CTR_ASSERT(size > 0, "FixedVector front on empty (check IsEmpty() first)"); return &storage[0]; }
  const T& front() const { CTR_ASSERT(size > 0, "FixedVector front on empty (check IsEmpty() first)"); return storage[0]; }
  T*       back()        { CTR_ASSERT(size > 0, "FixedVector back on empty (check IsEmpty() first)"); return &storage[size - 1]; }
  const T& back()  const { CTR_ASSERT(size > 0, "FixedVector back on empty (check IsEmpty() first)"); return storage[size - 1]; }

  T*       operator[](u32 index)       { CTR_ASSERT(index < size, "FixedVector index out of range"); return &storage[index]; }
  const T& operator[](u32 index) const { CTR_ASSERT(index < size, "FixedVector index out of range"); return storage[index]; }
  auto* begin(this auto&& self) { return self.storage; }
  auto* end(this auto&& self)   { return self.storage + self.size; }
};

///////////////////////////////////////////////////////
// FixedKeyedVector
//  FixedVector's two-column twin. Parallel keys[] / values[] arrays kept in lockstep.
///////////////////////////////////////////////////////
template <typename TKey, typename TValue, size_t N>
struct FixedKeyedVector {
  static_assert(N <= 0xFFFFFFFFu, "FixedKeyedVector capacity N exceeds the u32 size counter");
  u32    size = 0;
  TKey   keys  [N];
  TValue values[N];

  struct Entry {
    TKey* pKey;
    TValue* pValue;
  };
  struct EmplaceResult {
    TValue* pValue;
    bool inserted;
  };
  struct Iterator {
    FixedKeyedVector* pVector;
    u32 index;

    constexpr Entry operator*() const { return { &pVector->keys[index], &pVector->values[index] }; }
    constexpr void operator++() { ++index; }
    constexpr bool operator!=(Iterator other) const { return index != other.index; }
  };

  constexpr void Push(const TKey& k, const TValue& v) {
    CTR_ASSERT(size < N, "FixedKeyedVector Push overflow (check CanPush() first)");
    keys[size] = k; values[size] = v; ++size;
  }
  constexpr bool TryPush(const TKey& k, const TValue& v) {   // push only if key absent; true if added
    for (u32 i = 0; i < size; ++i) if (keys[i] == k) return false;
    Push(k, v);
    return true;
  }
  constexpr void PushRange(const TKey* ks, const TValue* vs, u32 n) {
    CTR_ASSERT(size + n <= N, "FixedKeyedVector PushRange overflow (check CanPush(n) first)");
    __builtin_memcpy(keys   + size, ks, (size_t)n * sizeof(TKey));
    __builtin_memcpy(values + size, vs, (size_t)n * sizeof(TValue));
    size += n;
  }
  constexpr void Pop  ()               { CTR_ASSERT(size > 0, "FixedKeyedVector Pop underflow (check IsEmpty() first)"); --size; }
  constexpr void Clear()               { size = 0; }
  constexpr void RemoveSwapBack(u32 i) { CTR_ASSERT(i < size, "FixedKeyedVector RemoveSwapBack index out of range"); --size; keys[i] = keys[size]; values[i] = values[size]; }
  constexpr bool IsEmpty() const       { return size == 0; }
  constexpr bool IsFull () const       { return size == N; }

  constexpr TValue* Find(const TKey& k) {
    for (u32 i = 0; i < size; ++i)
      if (keys[i] == k) return &values[i];
    return nullptr;
  }
  constexpr const TValue* Find(const TKey& k) const {
    for (u32 i = 0; i < size; ++i)
      if (keys[i] == k) return &values[i];
    return nullptr;
  }

  constexpr EmplaceResult TryEmplace(const TKey& k) {
    for (u32 i = 0; i < size; ++i)
      if (keys[i] == k) return { &values[i], false };
    CTR_ASSERT(size < N, "FixedKeyedVector TryEmplace overflow (check CanPush() first)");
    keys[size]   = k;
    values[size] = TValue{};
    EmplaceResult result{ &values[size], true };
    ++size;
    return result;
  }

  constexpr TValue* front() { CTR_ASSERT(size > 0, "FixedKeyedVector front on empty (check IsEmpty() first)"); return &values[0]; }
  constexpr const TValue& front() const { CTR_ASSERT(size > 0, "FixedKeyedVector front on empty (check IsEmpty() first)"); return values[0]; }
  constexpr TValue* back() { CTR_ASSERT(size > 0, "FixedKeyedVector back on empty (check IsEmpty() first)"); return &values[size - 1]; }
  constexpr const TValue& back() const { CTR_ASSERT(size > 0, "FixedKeyedVector back on empty (check IsEmpty() first)"); return values[size - 1]; }
  constexpr TValue* operator[](u32 index) { CTR_ASSERT(index < size, "FixedKeyedVector index out of range"); return &values[index]; }
  constexpr const TValue& operator[](u32 index) const { CTR_ASSERT(index < size, "FixedKeyedVector index out of range"); return values[index]; }
  constexpr Iterator begin() { return { this, 0 }; }
  constexpr Iterator end() { return { this, size }; }
};

///////////////////////////////////////////////////////
// FixedBitSet
//  N bits packed into u64 words: Test/Set/Clear/Reset, plus FirstUnset (lowest 0-bit, or N if full)
//  and Claim (FirstUnset + Set) for free-slot loops; phantom high bits in the last word are skipped.
///////////////////////////////////////////////////////
template <size_t N>
struct FixedBitSet {
  u64 words[(N + 63) / 64] = {};

  constexpr bool Test (u32 i) const { return (words[i >> 6] >> (i & 63)) & 1; }
  constexpr void Set  (u32 i)       { words[i >> 6] |=  (u64)1 << (i & 63); }
  constexpr void Clear(u32 i)       { words[i >> 6] &= ~((u64)1 << (i & 63)); }
  constexpr void Reset() {
    for (u32 index = 0; index < (N + 63) / 64; ++index) words[index] = 0;
  }

  // Index of the lowest unset bit, or N if all N bits are set.
  constexpr u32 FirstUnset() const {
    for (u32 w = 0; w < (N + 63) / 64; ++w) {
      u64 freeBits = ~words[w];
      if (!freeBits) continue;                          // word fully set
      u32 i = w * 64 + (u32)__builtin_ctzll(freeBits);  // lowest unset bit
      if (i >= N) break;                                // only phantom high bits left → full
      return i;
    }
    return N;
  }

  constexpr bool IsFull() const   { return FirstUnset() == N; }
  constexpr bool CanClaim() const { return FirstUnset() != N; }

  constexpr bool IsEmpty() const {
    for (u32 index = 0; index < (N + 63) / 64; ++index)
      if (words[index]) return false;
    return true;
  }

  // FirstUnset() then Set() it; returns the claimed index, or N if full.
  constexpr u32 Claim() {
    u32 i = FirstUnset();
    if (i < N) Set(i);
    return i;
  }
};

///////////////////////////////////////////////////////
// FixedMultiVector
//  Struct-of-arrays fixed-capacity vector. Act on every column in lockstep.
///////////////////////////////////////////////////////
template <typename...>
struct VectorTypes {};
template <typename Arrays, size_t N>
struct FixedMultiVector;
template <typename... Ts,  size_t N>
struct FixedMultiVector<VectorTypes<Ts...>, N> : FixedArray<Ts, N>... {
  static_assert(N <= 0xFFFFFFFFu, "FixedMultiVector capacity N exceeds the u32 size counter");
  using Elements = Tuple<Ts*...>;

  u32 size = 0;

  template <typename T>
  constexpr T* Column() { return FixedArray<T, N>::data; }

  // Append a row; return pointers to its columns. Debug-asserts on overflow; check IsFull() first.
  constexpr Elements Push(const Ts&... vals) {
    CTR_ASSERT(size < N, "FixedMultiVector Push overflow (check IsFull() first)");
    ((FixedArray<Ts, N>::data[size] = vals), ...);
    Elements row{ &FixedArray<Ts, N>::data[size]... };
    ++size;
    return row;
  }
  constexpr void Pop  () { CTR_ASSERT(size > 0, "FixedMultiVector Pop underflow (check IsEmpty() first)"); --size; }
  constexpr void Clear() { size = 0; }

  constexpr void RemoveSwapBack(u32 i) {
    CTR_ASSERT(i < size, "FixedMultiVector RemoveSwapBack index out of range");
    --size;
    ((FixedArray<Ts, N>::data[i] = FixedArray<Ts, N>::data[size]), ...);
  }

  constexpr bool IsEmpty() const { return size == 0; }
  constexpr bool IsFull () const { return size == N; }
  constexpr bool CanPush() const { return size < N; }
  constexpr bool CanPush(u32 count) const { return size + count <= N; }

  // Search the Key column for `key`; return pointers to Key + the listed Sel columns (Key first),
  // else all-null. You always list the columns to return: the Key (also the search column, first in
  // the result) plus any extras. Columns not listed are never addressed.
  template <typename Key, typename... Sel>
  constexpr auto Find(const Key& key) {
    const Key* keys = Column<Key>();
    for (u32 i = 0; i < size; ++i)
      if (keys[i] == key) return Tuple<Key*, Sel*...>{ &Column<Key>()[i], &Column<Sel>()[i]... };
    return Tuple<Key*, Sel*...>{ (Key*)nullptr, (Sel*)nullptr... };
  }

  // Get-or-append: the row whose Key column == key, else a new default row. Returns Key + the listed
  // Sel columns (Key first). All-null if a push was needed and the container is full.
  template <typename Key, typename... Sel>
  constexpr auto TryEmplace(const Key& key) {
    auto row = Find<Key, Sel...>(key);
    if (row.head) return row;
    if (IsFull()) return Tuple<Key*, Sel*...>{ (Key*)nullptr, (Sel*)nullptr... };
    Push(Ts{}...);
    return Tuple<Key*, Sel*...>{ &Column<Key>()[size - 1], &Column<Sel>()[size - 1]... };
  }
};

///////////////////////////////////////////////////////
// String
//  Non-owning bounded string view; size excludes the terminating NUL when one is present.
///////////////////////////////////////////////////////
struct String
{
  u32   size     = 0;
  u32   capacity = 0;
  char* data     = nullptr;

  String() = default;
  String(size_t n, const char* p) : size((u32)n), data((char*)p) {}

  template<size_t Size>
  String(const char (&value)[Size]) : size(StrLength(value, (u32)Size)), data((char*)value) {}

  String(const char* pText) : size(pText ? (u32)__builtin_strlen(pText) : 0), data((char*)(pText ? pText : "")) {}

  String(Literal literal) : size(literal.size - 1), data((char*)literal.pText) {}

  template <size_t N>
  String(FixedString<N>& fixed) : size(fixed.Length()), capacity((u32)N), data(fixed.data) {}

  static String Emplace(u32 n, Arena* pArena) {
    String text;
    text.data     = (char*)pArena->TakeForward(n, 1);
    text.capacity = text.data ? n : 0;
    return text;
  }

  bool IsEmpty() const { return size == 0; }
  void Clear()         { size = 0; }
  void Resize(u32 n)   { u32 limit = capacity > size ? capacity : size; CTR_ASSERT(n <= limit, "String Resize beyond capacity (Reserve first)"); size = n < limit ? n : limit; }

  bool CanAppend(u32 n) const { return size + n + 1 <= capacity; }

  // Bounded: writes at most the remaining capacity, so a short buffer truncates instead of overflowing.
  void Append(const char* p, u32 n) {
    if (!n || !data) return;
    CTR_ASSERT(CanAppend(n), "String Append truncated (size the buffer, or use a StringBuffer)");
    u32 room = capacity > size + 1 ? capacity - size - 1 : 0;
    u32 take = n < room ? n : room;
    if (!take) return;
    __builtin_memcpy(data + size, p, take);
    size += take;
  }
  void Append(String text)   { Append(text.data, text.size); }
  void Append(const char* p) { if (p) Append(p, (u32)__builtin_strlen(p)); }
  void Append(char c)        { Append(&c, 1); }

  void AppendU64(u64 v) { char t[20]; u32 i = 20; do { t[--i] = (char)('0' + (v % 10)); v /= 10; } while (v); Append(t + i, 20 - i); }
  void AppendI64(i64 v) { if (v < 0) { Append('-'); AppendU64((u64)(-(v + 1)) + 1); } else AppendU64((u64)v); }

  const char* CStr() { if (!data) return ""; if (size < capacity) data[size] = '\0'; return data; }

  bool operator==(String other) const  { if (size != other.size) return false; return size == 0 || __builtin_memcmp(data, other.data, size) == 0; }
  bool operator==(const char* p) const { u32 n = p ? (u32)__builtin_strlen(p) : 0; if (size != n) return false; return n == 0 || __builtin_memcmp(data, p, n) == 0; }
  bool operator!=(String other) const  { return !(*this == other); }
  bool operator!=(const char* p) const { return !(*this == p); }

  bool StartsWith(const char* p) const { u32 n = p ? (u32)__builtin_strlen(p) : 0; return n <= size && (n == 0 || __builtin_memcmp(data, p, n) == 0); }
  i64  Find(char c, u32 from = 0) const { for (u32 i = from; i < size; ++i) if (data[i] == c) return (i64)i; return -1; }
  i64  RFind(char c) const { for (u32 i = size; i > 0; --i) if (data[i - 1] == c) return (i64)(i - 1); return -1; }
  i64  Find(const char* pNeedle, u32 from = 0) const { u32 n = pNeedle ? (u32)__builtin_strlen(pNeedle) : 0; if (n == 0) return (i64)from; if (n > size) return -1; for (u32 i = from; i + n <= size; ++i) if (__builtin_memcmp(data + i, pNeedle, n) == 0) return (i64)i; return -1; }
  String Substr(u32 pos, u32 length = 0xFFFFFFFFu) const { if (pos >= size) return String((size_t)0, ""); u32 n = length > size - pos ? size - pos : length; return String((size_t)n, data + pos); }

  char operator[](size_t index) const { return data[index]; }
};

///////////////////////////////////////////////////////
// StringBuffer
//  RAII heap allocation backing a String: it owns the malloc, frees on scope
//  exit, and is the only place that may grow, since it is the only one that
//  knows the bytes came from malloc. Copy and move are deleted. Every read and
//  query is inherited from String, so the methods are not duplicated; only the
//  appends are overridden to reserve first.
///////////////////////////////////////////////////////
struct StringBuffer : String {
  RAII_OWNER(StringBuffer);

  StringBuffer() = default;
  explicit StringBuffer(u32 n) { data = (char*)malloc(n); capacity = n; }
  ~StringBuffer() { free(data); }

  void Reserve(u32 n) {
    if (n <= capacity) return;
    u32 next = capacity ? capacity : 16;
    while (next < n) next *= 2;
    data     = (char*)realloc(data, next);
    capacity = next;
  }
};

////////////////////////////////////////////////////////////////////////////////
// Pools
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////
// FixedPool
//  Instance-backed fixed-capacity pool of T, claimed and released by u16 index.
///////////////////////////////////////////////////////
template <typename T, size_t N>
struct FixedPool {
  static_assert(N <= 65535, "FixedPool exceeds the u16 index");

  T              data[N];
  FixedBitSet<N> used = {};

  static constexpr u16 capacity     = N;
  static constexpr u16 InvalidIndex = (u16)N;

  constexpr bool IsUsed(u16 index) const  { return used.Test(index); }
  constexpr bool IsValid(u16 index) const { return index < N && used.Test(index); }
  constexpr bool IsFull() const           { return used.IsFull(); }
  constexpr bool IsEmpty() const          { return used.IsEmpty(); }
  constexpr bool CanAcquire() const       { return used.CanClaim(); }
  constexpr void Clear ()                 { used.Reset(); }

  // Claim the lowest free slot. Debug-asserts a slot is free; check CanAcquire() first.
  u16 Acquire() {
    u32 index = used.Claim();
    CTR_ASSERT(index < N, "FixedPool Acquire on a full pool (check CanAcquire() first)");
    return (u16)index;
  }

  void Release(u16 index) {
    CTR_ASSERT(IsValid(index), "FixedPool Release of a free or out-of-range index (check IsValid() first)");
    used.Clear(index);
  }

  T* operator[](u16 index) { CTR_ASSERT(IsValid(index), "FixedPool operator[] on a free or out-of-range index (check IsValid() first)"); return &data[index]; }

  const T& operator[](u16 index) const { CTR_ASSERT(IsValid(index), "FixedPool operator[] on a free or out-of-range index (check IsValid() first)"); return data[index]; }
};

///////////////////////////////////////////////////////
// AtomicFixedPool
//  FixedPool for concurrent claimers: Acquire and Release are lock-free, so competing threads never
//  claim one slot. A full pool returns InvalidIndex rather than asserting, because losing the race
//  is ordinary. The claiming thread owns its slot until Release, so the payload needs no atomics.
///////////////////////////////////////////////////////
template <typename T, size_t N>
struct AtomicFixedPool {
  static_assert(N <= 65535, "AtomicFixedPool exceeds the u16 index");

  T            data[N];
  Atomic<bool> used[N] = {};

  static constexpr u16 capacity     = N;
  static constexpr u16 InvalidIndex = (u16)N;

  bool IsUsed(u16 index) const  { return used[index].LoadAcquire(); }
  bool IsValid(u16 index) const { return index < N && IsUsed(index); }

  // Claim the lowest free slot; returns InvalidIndex when the pool is full.
  u16 Acquire() {
    for (u16 index = 0; index < N; ++index) {
      bool expected = false;
      if (used[index].CompareExchangeAcquireRelease(&expected, true))
        return index;
    }

    return InvalidIndex;
  }

  void Release(u16 index) {
    CTR_ASSERT(IsValid(index), "AtomicFixedPool Release of a free or out-of-range index (check IsValid() first)");
    used[index].StoreRelease(false);
  }

  T* operator[](u16 index) { CTR_ASSERT(IsValid(index), "AtomicFixedPool operator[] on a free or out-of-range index (check IsValid() first)"); return &data[index]; }
  const T& operator[](u16 index) const { CTR_ASSERT(IsValid(index), "AtomicFixedPool operator[] on a free or out-of-range index (check IsValid() first)"); return data[index]; }
};

///////////////////////////////////////////////////////
// StaticPool
//  Process-global fixed-capacity pool whose 2-byte handles resolve through their specialization.
//  Tag creates independent storage for otherwise identical pool types.
///////////////////////////////////////////////////////
template <typename T, size_t N, typename Tag = T>
struct StaticPool {
  static_assert(N <= 4096, "StaticPool exceeds the 12-bit index; >4096 needs a non-bitset Acquire");

  struct Handle {
    u16 index      : 12;
    u16 generation : 4;

    constexpr Handle() = default;
    constexpr Handle(u16 index, u16 generation) : index(index), generation(generation) {}

    static Handle Acquire() { return StaticPool::Acquire(); }

    bool IsValid() const { return StaticPool::IsValid(*this); }
    void Release() {
      StaticPool::Release(*this);
      *this = {};
    }

    T* operator->() const { return get(); }

    constexpr bool operator==(Handle otherHandle) const { return index == otherHandle.index && generation == otherHandle.generation; }

    T* get() const { CTR_ASSERT(IsValid(), "StaticPool get on a null/stale handle (check IsValid() first)"); return &StaticPool::data[index]; }
  };
  static_assert(sizeof(Handle) == 2);

  struct HandleRange {
    struct Iterator {
      u16 index;

      void operator++() { index++; }
      Handle operator*() const { return Handle(index, (u16)generation[index]); }
      bool operator!=(Iterator other) const { return index != other.index; }
    };

    Iterator begin() const { return { 0 }; }
    Iterator end() const   { return { (u16)N }; }
  };

  struct ValidHandleRange {
    struct Iterator {
      u16 index;

      void operator++() {
        index++;
        while (index < N && !IsUsed(index)) index++;
      }
      Handle operator*() const { return Handle(index, (u16)generation[index]); }
      bool operator!=(Iterator other) const { return index != other.index; }
    };

    Iterator begin() const {
      u16 index = 0;
      while (index < N && !IsUsed(index)) index++;
      return { index };
    }
    Iterator end() const { return { (u16)N }; }
  };

  inline static T              data[N]       = {};
  inline static u8             generation[N] = {};
  inline static FixedBitSet<N> used          = {};

  static constexpr u16 capacity = N;

  static bool IsUsed(u32 i) { return used.Test(i); }
  static bool IsFull()      { return used.IsFull(); }
  static bool IsEmpty()     { return used.IsEmpty(); }
  static bool CanAcquire()  { return used.CanClaim(); }
  static void Clear()       { used.Reset(); }

  static Handle Acquire() {
    u32 i = used.Claim();
    CTR_ASSERT(i < N, "StaticPool Acquire on a full pool (check CanAcquire() first)");
    u8 g = (generation[i] + 1) & 0xF;
    g = Max(g, (u8)1); // Generation 0 is the null-handle sentinel.
    generation[i] = g;
    return Handle((u16)i, g);
  }

  static bool IsValid(Handle handle) {
    return handle.generation != 0 && handle.index < N && used.Test(handle.index) &&
           generation[handle.index] == handle.generation;
  }

  static constexpr HandleRange Handles() { return {}; }
  static constexpr ValidHandleRange ValidHandles() { return {}; }

  static void Release(Handle handle) {
    CTR_ASSERT(IsValid(handle), "StaticPool Release of a null/stale handle (check IsValid() first)");
    used.Clear(handle.index);
  }

  T* operator[](Handle handle) { CTR_ASSERT(IsValid(handle), "StaticPool operator[] on a null/stale handle (check IsValid() first)"); return &data[handle.index]; }
  
  const T& operator[](Handle handle) const { CTR_ASSERT(IsValid(handle), "StaticPool operator[] on a null/stale handle (check IsValid() first)"); return data[handle.index]; }
};

////////////////////////////////////////////////////////////////////////////////
// Atomic Rings
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////
// AtomicRingBuffer
//  SPMC latest-value ring; N must prevent producer wrap onto a slot during any consumer read.
///////////////////////////////////////////////////////
template <typename T, u32 N>
struct AtomicRingBuffer {
  Atomic<u64> count = {1};
  T slots[N]        = {};

  void Push(const T& v) {
    u64 c        = count.Load();
    slots[c % N] = v;
    count.StoreRelease(c + 1);
  }

  // The pointer remains stable until the producer wraps through N newer values.
  const T* PeekLatest() const {
    u64 c = count.LoadAcquire();
    return &slots[(c - 1) % N];
  }

  bool PollLatest(T* out) const {
    *out = *PeekLatest();
    return true;
  }
};

///////////////////////////////////////////////////////
// AtomicRingQueue
//  Single-producer single-consumer ring. Push() returns false if the queue is
//  full (no overwrite); Pop() returns false if empty.
//  N must be a power of 2 so the wrap is a mask instead of a divide.
//  head / tail are cache-line aligned to prevent false sharing between the
//  producer and consumer cores.
///////////////////////////////////////////////////////
template <typename T, size_t N>
struct AtomicRingQueue {
  static_assert((N & (N - 1)) == 0, "N must be a power of 2");
  static constexpr u64 MODULO = N - 1; // Power of 2 modulo bit shift.

  Atomic<u64> head{0};
  Atomic<u64> tail{0};

  // Test 2x speed on heavy polling if we cache the head/tail and only update from atomic load when needed.
  u64 cachedTail = 0;
  u64 cachedHead = 0;

  T buffer[N] = {};

  // Returns BUFFER_WRAPPED if the queue is full (would overwrite an unread entry), else SUCCESS.
  Result Push(const T& val) {
    u64 h = head.Load();
    if (h - cachedTail == N) {                           
      cachedTail = tail.LoadAcquire();
      if (h - cachedTail == N) return BUFFER_WRAPPED;
    }
    buffer[h & MODULO] = val;
    head.StoreRelease(h + 1);
    return SUCCESS;
  }

  // True when no unread entry is visible; consumer-side check (same thread as Pop).
  bool IsEmpty() { return tail.Load() == head.LoadAcquire(); }

  // Returns BUFFER_EMPTY if the queue has no unread entry, else SUCCESS.
  Result Pop(T* pOut) {
    u64 t = tail.Load();
    if (t == cachedHead) {
      cachedHead = head.LoadAcquire();
      if (t == cachedHead) return BUFFER_EMPTY;
    }
    *pOut = buffer[t & MODULO];
    tail.StoreRelease(t + 1);
    return SUCCESS;
  }

  // Quiescent reset only (no concurrent Push/Pop).
  void Clear() {
    cachedTail = 0;
    cachedHead = 0;
    head.Store(0);
    tail.StoreRelease(0);
  }
};

////////////////////////////////////////////////////////////////////////////////
}  // namespace Flat
////////////////////////////////////////////////////////////////////////////////
