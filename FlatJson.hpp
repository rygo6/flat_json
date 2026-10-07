#pragma once

////////////////////////////////////////////////////////////////////////////////
// @author rygo6
// types.hpp
////////////////////////////////////////////////////////////////////////////////


#include <stdint.h>


////////////////////////////////////////////////////////////////////////////////
namespace Flat {
////////////////////////////////////////////////////////////////////////////////

using u8  = uint8_t;
using i8  = int8_t;
using u16 = uint16_t;
using i16 = int16_t;
using u32 = uint32_t;
using i32 = int32_t;
using u64 = uint64_t;
using i64 = int64_t;
using f16 = _Float16;
using f64 = double;
using f32 = float;
using ull = unsigned long long;
using ill = long long;

////////////////////////////////////////////////////////////////////////////////
}  // namespace Flat
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// Compiler attributes
////////////////////////////////////////////////////////////////////////////////

#define ALWAYS_INLINE [[gnu::always_inline]]

////////////////////////////////////////////////////////////////////////////////
// @author rygo6
// term.hpp
////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////
// ANSI escape codes
////////////////////////////////////////////////////////////////////////////////

#define ANSI_RESET            "\033[0m"

// Attributes
#define ANSI_BOLD             "\033[1m"
#define ANSI_DIM              "\033[2m"
#define ANSI_ITALIC           "\033[3m"
#define ANSI_UNDERLINE        "\033[4m"
#define ANSI_BLINK            "\033[5m"
#define ANSI_REVERSE          "\033[7m"
#define ANSI_HIDDEN           "\033[8m"
#define ANSI_STRIKETHROUGH    "\033[9m"

// Foreground — standard 8
#define ANSI_FG_BLACK         "\033[30m"
#define ANSI_FG_RED           "\033[31m"
#define ANSI_FG_GREEN         "\033[32m"
#define ANSI_FG_YELLOW        "\033[33m"
#define ANSI_FG_BLUE          "\033[34m"
#define ANSI_FG_MAGENTA       "\033[35m"
#define ANSI_FG_CYAN          "\033[36m"
#define ANSI_FG_WHITE         "\033[37m"
#define ANSI_FG_DEFAULT       "\033[39m"

// Foreground — bright 8
#define ANSI_FG_BRIGHT_BLACK   "\033[90m"
#define ANSI_FG_BRIGHT_RED     "\033[91m"
#define ANSI_FG_BRIGHT_GREEN   "\033[92m"
#define ANSI_FG_BRIGHT_YELLOW  "\033[93m"
#define ANSI_FG_BRIGHT_BLUE    "\033[94m"
#define ANSI_FG_BRIGHT_MAGENTA "\033[95m"
#define ANSI_FG_BRIGHT_CYAN    "\033[96m"
#define ANSI_FG_BRIGHT_WHITE   "\033[97m"

// Background — standard 8
#define ANSI_BG_BLACK         "\033[40m"
#define ANSI_BG_RED           "\033[41m"
#define ANSI_BG_GREEN         "\033[42m"
#define ANSI_BG_YELLOW        "\033[43m"
#define ANSI_BG_BLUE          "\033[44m"
#define ANSI_BG_MAGENTA       "\033[45m"
#define ANSI_BG_CYAN          "\033[46m"
#define ANSI_BG_WHITE         "\033[47m"
#define ANSI_BG_DEFAULT       "\033[49m"

// Background — bright 8
#define ANSI_BG_BRIGHT_BLACK   "\033[100m"
#define ANSI_BG_BRIGHT_RED     "\033[101m"
#define ANSI_BG_BRIGHT_GREEN   "\033[102m"
#define ANSI_BG_BRIGHT_YELLOW  "\033[103m"
#define ANSI_BG_BRIGHT_BLUE    "\033[104m"
#define ANSI_BG_BRIGHT_MAGENTA "\033[105m"
#define ANSI_BG_BRIGHT_CYAN    "\033[106m"
#define ANSI_BG_BRIGHT_WHITE   "\033[107m"

// 256-color (n = 0..255) and 24-bit truecolor
#define ANSI_FG_256(n)        "\033[38;5;" #n "m"
#define ANSI_BG_256(n)        "\033[48;5;" #n "m"
#define ANSI_FG_RGB(r,g,b)    "\033[38;2;" #r ";" #g ";" #b "m"
#define ANSI_BG_RGB(r,g,b)    "\033[48;2;" #r ";" #g ";" #b "m"

// Cursor + screen control
#define ANSI_SAVE_CURSOR      "\0337"        // DECSC: save cursor + attributes
#define ANSI_RESTORE_CURSOR   "\0338"        // DECRC: restore cursor + attributes
#define ANSI_CLEAR_LINE       "\033[2K"      // clear entire current line
#define ANSI_CLEAR_SCREEN     "\033[2J"      // clear entire screen
#define ANSI_SCROLL_RESET     "\033[r"       // reset DECSTBM to full screen

// Parameterized format-string fragments
#define ANSI_CUP_ROW_FMT      "\033[%d;1H"   // move cursor to (row, col=1)
#define ANSI_SCROLL_TOP_FMT   "\033[1;%dr"   // DECSTBM: scroll region = lines 1..N

////////////////////////////////////////////////////////////////////////////////
namespace Flat::Terminal {
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////
// StatusBar
//  Writes to a fixed bottom-row status area while normal stderr output scrolls above it.
///////////////////////////////////////////////////////
[[gnu::format(printf, 1, 2)]]
void StatusBar(const char* fmt, ...);

///////////////////////////////////////////////////////
// Log
//  Writes a log line to stderr (file:line + tag prefix + body) in one fwrite.
///////////////////////////////////////////////////////
[[gnu::format(printf, 5, 6)]]
void Log(const char* file, int filePad, int line, const char* tag, const char* fmt, ...);

///////////////////////////////////////////////////////
// LogOpenFile
//  Mirrors timestamped log lines with rotation in the platform log directory.
///////////////////////////////////////////////////////
void LogOpenFile(const char* appName);

///////////////////////////////////////////////////////
// LogDir
//  Directory the log file was opened in; empty string before LogOpenFile.
///////////////////////////////////////////////////////
const char* LogDir();

///////////////////////////////////////////////////////
// Terminal input capture
//  Raw stdin (no canonical line buffering, no echo) is enabled automatically at
//  startup when stdin is a TTY, and restored on exit / SIGINT / SIGTERM. Ctrl-C
//  still works (ISIG kept). Lets a headless client poll the controlling terminal
//  for keys (and, opt-in, mouse) — e.g. to drive input without a window.
///////////////////////////////////////////////////////

// Non-blocking single-byte key read. Returns the byte (0..255), or -1 if none is
// pending. Mouse escape sequences are consumed internally (not returned as keys)
// and surfaced via PollMouse instead.
int PollKey();

// One mouse report parsed from the terminal (xterm SGR 1006 + any-motion 1003).
struct MouseEvent {
  int  x       = 0;      // 1-based column (terminal cell)
  int  y       = 0;      // 1-based row
  int  button  = 0;      // 0=left, 1=middle, 2=right, 3=none (SGR code low 2 bits)
  bool pressed = false;  // 'M' = press / motion-with-button held; 'm' = release
  bool motion  = false;  // event carried the motion flag (SGR code & 32)
};

// Opt-in mouse reporting (OFF by default — any-motion tracking streams a lot of
// events to stdin, so a client asks for it explicitly). EnableMouse turns on
// xterm 1003 (any motion) + 1006 (SGR coords); PollMouse pops the next event
// (false if none). Auto-disabled on exit / SIGINT / SIGTERM. Needs raw stdin (a TTY).
void Reset();
void ResetSignalSafe();
void EnableMouse();
void DisableMouse();
bool PollMouse(MouseEvent* out);

////////////////////////////////////////////////////////////////////////////////
}  // namespace Flat::Terminal
////////////////////////////////////////////////////////////////////////////////

#define TERM_LOG(tag, format, ...) Flat::Terminal::Log(__FILE__, (int)(20 - sizeof(__FILE__)), __LINE__, tag, format, ##__VA_ARGS__)

#define INFO(name, color, format, ...) TERM_LOG(ANSI_FG_RGB color                         "[" #name "]" ANSI_RESET " ",       format, ##__VA_ARGS__)
#define WARN(name, format, ...)        TERM_LOG(ANSI_FG_RGB(0,0,0) ANSI_BG_RGB(255,255,0) "[" #name "] WARN:" ANSI_RESET " ", format, ##__VA_ARGS__)
#define ERR(name, format, ...)         TERM_LOG(ANSI_FG_RGB(0,0,0) ANSI_BG_RGB(255,0,0)   "[" #name "] ERR:" ANSI_RESET " ",  format, ##__VA_ARGS__)

#ifdef ENABLE_VERBOSE_INFO
#define VERBOSE(name, color, format, ...) INFO(name, color, format, ##__VA_ARGS__)
#else
#define VERBOSE(name, color, format, ...) ((void)0)
#endif

////////////////////////////////////////////////////////////////////////////////
// @author rygo6
// Error.hpp - The Result status vocabulary and the trap/soft-fail macros built over it.
////////////////////////////////////////////////////////////////////////////////






////////////////////////////////////////////////////////////////////////////////
namespace Flat {
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// Result
////////////////////////////////////////////////////////////////////////////////

enum Result : i32 {
  SUCCESS                     =    0,  // VK_SUCCESS / XR_SUCCESS
  ERROR_OUT_OF_MEMORY         =   -1,  // VK_ERROR_OUT_OF_HOST_MEMORY / XR_ERROR_OUT_OF_MEMORY
  ERROR_INITIALIZATION_FAILED =   -3,  // VK_ERROR_INITIALIZATION_FAILED / XR_ERROR_INITIALIZATION_FAILED
  ERROR_LAYER_NOT_PRESENT     =   -6,  // VK_ERROR_LAYER_NOT_PRESENT / XR_ERROR_API_LAYER_NOT_PRESENT
  ERROR_EXTENSION_NOT_PRESENT =   -7,  // VK_ERROR_EXTENSION_NOT_PRESENT / XR_ERROR_EXTENSION_NOT_PRESENT
  ERROR_FEATURE_NOT_PRESENT   =   -8,  // VK_ERROR_FEATURE_NOT_PRESENT / XR_ERROR_FEATURE_UNSUPPORTED
  ERROR_LIMIT_REACHED         =  -10,  // VK_ERROR_TOO_MANY_OBJECTS / XR_ERROR_LIMIT_REACHED (ring/queue full)
  ERROR_UNKNOWN               =  -13,  // VK_ERROR_UNKNOWN / XR_ERROR_RUNTIME_FAILURE

  // Library-specific results
  BUFFER_EMPTY                =  100,  // pop found no unread entry (ring/queue empty): not an error
  BUFFER_WRAPPED              = -100,  // push refused: producer would wrap onto an unread entry
  ERROR_OVERFLOW              = -101,  // push refused: fixed-capacity container is full
  REQUEST_INITIALIZE          =  101,  // signal: caller should (re)initialize, then retry
  REQUEST_EXIT                =  102,  // signal: caller should shut down

  // general recoverable conditions (positive): retry / reconnect / degrade rather than abort
  NOT_READY                   =  103,  // resource not ready yet — retry later (sensors warming, no fresh data)
  NOT_CONNECTED               =  104,  // required device/peripheral is absent — recoverable by connecting it
  CONNECTION_LOST             =  105,  // a live connection dropped — recoverable by re-acquire / replug
  DEGRADED                    =  106,  // operating but quality lost (e.g. tracking localization lost)
  CREATED                     =  107,  // success, but the resource was freshly created rather than reused
  NOT_FOUND                   =  108,  // lookup found no match: not an error
  TIMEOUT                     =  109,  // VK_TIMEOUT / XR_TIMEOUT_EXPIRED: the wait's deadline passed
  ABSENT_VALUE                =  110,  // there was nothing to read or parse: not an error

  // parsing, arguments, and host I/O
  ERROR_NOT_FOUND             = -102,  // the named file, module, or entry does not exist
  ERROR_INVALID_ARGUMENT      = -103,  // a caller-supplied argument is missing or the wrong shape
  ERROR_IO                    = -104,  // a read, write, or mapping failed
  ERROR_JS_EXCEPTION          = -105,  // script evaluation left an exception pending
  ERROR_MALFORMED             = -106,  // input did not match its grammar
  ERROR_INSUFFICIENT_SPACE    = -107,  // output did not fit the caller-provided storage
};

const char* string_Result(Result result);

////////////////////////////////////////////////////////////////////////////////
}  // namespace Flat
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////
// PANIC
//  Trap unconditionally after logging a printf-style fatal message.
///////////////////////////////////////////////////////
#define PANIC(name, format, ...) ({                                                                                  \
  TERM_LOG(ANSI_FG_RGB(0,0,0) ANSI_BG_RGB(255,0,0) "[" #name "]" ANSI_RESET " PANIC: ", format "\n", ##__VA_ARGS__); \
  __builtin_trap();                                                                                                  \
})

///////////////////////////////////////////////////////
// REQUIRE
//  Trap with a logged printf-style message if `expr` is false. `format` is a string
//  literal (a bare message works; add specifiers + args like TRY for detail).
///////////////////////////////////////////////////////
#define REQUIRE(name, expr, format, ...)                                 \
  if (!(expr)) [[unlikely]] {                                            \
    ERR(name, #name "_REQUIRE: %s: " format "\n", #expr, ##__VA_ARGS__); \
    __builtin_trap();                                                    \
  }

///////////////////////////////////////////////////////
// ASSERT
//  Debug-only REQUIRE: traps with a logged message if `expr` is false in DEBUG builds; compiled
//  out entirely (expr NOT evaluated) when DEBUG is undefined. For invariants the caller is
//  contractually responsible for (e.g. bounds the caller already checked) — a dev backstop, not
//  a release guard.
///////////////////////////////////////////////////////
#ifdef DEBUG
#define ASSERT(name, expr, format, ...)                                 \
  if (!(expr)) [[unlikely]] {                                           \
    ERR(name, #name "_ASSERT: %s: " format "\n", #expr, ##__VA_ARGS__); \
    __builtin_trap();                                                   \
  }
#else
#define ASSERT(name, expr, format, ...) ((void)0)
#endif

///////////////////////////////////////////////////////
// TRY
//  Bail to `label` with a logged warning naming `expr` if it is false.
///////////////////////////////////////////////////////
#define TRY(name, expr, label, format, ...)                      \
  if (!(expr)) [[unlikely]] {                                    \
    WARN(name, #name "_TRY: %s: " format, #expr, ##__VA_ARGS__); \
    goto label;                                                  \
  }

///////////////////////////////////////////////////////
// SUCCEED
//  Trap if `expr` (a Result) is not SUCCESS. The code name + number are logged via
//  string_Result; `format`+args add context.
///////////////////////////////////////////////////////
#define SUCCEED(name, expr, format, ...)                                                                             \
  if (const Flat::Result _r = (Flat::Result)(expr); _r != Flat::SUCCESS) [[unlikely]] {                              \
    ERR(name, #name "_SUCCEED: %s = %s (%d): " format "\n", #expr, Flat::string_Result(_r), (int)_r, ##__VA_ARGS__); \
    __builtin_trap();                                                                                                \
  }

///////////////////////////////////////////////////////
// RETURN
//  Return a Result from the enclosing function, warning with the method, expression, result name,
//  and result number on any non-success.
///////////////////////////////////////////////////////
#define RETURN(name, expr) ({                                                                             \
    const Flat::Result _r = (Flat::Result)(expr);                                                         \
    if (_r != Flat::SUCCESS) [[unlikely]]                                                                 \
      WARN(name, #name "_RETURN: %s: %s = %s (%d)\n", __func__, #expr, Flat::string_Result(_r), (int)_r); \
    return _r;                                                                                            \
  })

////////////////////////////////////////////////////////////////////////////////
// @author: rygo6
// Container.hpp - Bounded views and arena storage for JSON parsing and writing.
////////////////////////////////////////////////////////////////////////////////


#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <initializer_list>


#define CTR_ASSERT(expr, ...) ASSERT(CTR, expr, "" __VA_OPT__(__VA_ARGS__))

////////////////////////////////////////////////////////////////////////////////
namespace Flat {
////////////////////////////////////////////////////////////////////////////////

template <typename S, typename T>
concept SpanOf = requires(size_t count, T* data) { S(count, data); };

///////////////////////////////////////////////////////
// Span
//  Non-owning (count, pointer) view; range-iterable so a C array + count reads as a for-each.
///////////////////////////////////////////////////////
template <typename T>
struct Span {
  u32 size = 0;
  T* data  = nullptr;

  constexpr Span(size_t n, T* p) : size((u32)n), data(p) { CTR_ASSERT(Fits(n), "Span exceeds its 32-bit size; check Fits() first"); }

  template <typename U, size_t Size>
    requires __is_convertible(U (*)[], T (*)[])
  constexpr Span(U (&value)[Size]) : size((u32)Size), data(value)
  {
    static_assert(Size <= UINT32_MAX, "Span array exceeds its 32-bit size.");
  }

  static constexpr bool Fits(size_t n) { return n <= UINT32_MAX; }
  constexpr bool IsEmpty() const { return size == 0; }
  constexpr bool HasIndex(u32 i) const { return i < size; }
  constexpr T& operator[](u32 i) const
  {
    CTR_ASSERT(HasIndex(i), "Span index out of range; check HasIndex() first");
    return data[i];
  }
};

///////////////////////////////////////////////////////
// Min
///////////////////////////////////////////////////////
template <typename T>
constexpr T Min(const T a, const T b)
{
  return a < b ? a : b;
}

///////////////////////////////////////////////////////
// StrFind
//  First occurrence of c within n bytes; null if absent. Scans all n bytes, so
//  pass the string length, not a buffer capacity, to stop at the terminator.
///////////////////////////////////////////////////////
inline const char* StrFind(const char* pText, char c, u32 n) { return (const char*)memchr(pText, c, n); }

///////////////////////////////////////////////////////
// StrFind
//  First occurrence of the sized needle in the sized text; null if absent, pText if empty.
///////////////////////////////////////////////////////
inline const char* StrFind(const char* pText, u32 textLength, const char* pNeedle, u32 needleLength) { return (const char*)memmem(pText, textLength, pNeedle, needleLength); }

///////////////////////////////////////////////////////
// StrFindLast
//  Last occurrence of c within n bytes; null if absent.
///////////////////////////////////////////////////////
inline const char* StrFindLast(const char* pText, char c, u32 n)
{
  for (u32 index = n; index > 0; --index) {
    if (pText[index - 1] == c)
      return pText + index - 1;
  }

  return nullptr;
}

///////////////////////////////////////////////////////
// StrOffset
//  Index of a match returned by a Str find within pText, or -1 when it found nothing.
///////////////////////////////////////////////////////
inline i64 StrOffset(const char* pText, const char* pMatch) { return pMatch ? (i64)(pMatch - pText) : -1; }

///////////////////////////////////////////////////////
// StrEqual
//  Equality of two sized strings: the lengths match and so does every byte.
///////////////////////////////////////////////////////
inline bool StrEqual(const char* pText, u32 textLength, const char* pOther, u32 otherLength) { return textLength == otherLength && __builtin_memcmp(pText, pOther, textLength) == 0; }

///////////////////////////////////////////////////////
// StrStartsWith
//  Prefix test of two sized strings; no terminator is read.
///////////////////////////////////////////////////////
inline bool StrStartsWith(const char* pText, u32 textLength, const char* pPrefix, u32 prefixLength) { return prefixLength <= textLength && __builtin_memcmp(pText, pPrefix, prefixLength) == 0; }

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

  u32 Used() const { return offset < 0 ? 0u - (u32)offset : (u32)offset; }
  void Reset() { offset = 0; }
};

///////////////////////////////////////////////////////
// ArenaBuffer
//  RAII heap allocation backing an Arena. Frees on scope exit; copy and move
//  are disabled. Bump state and every operation are inherited from Arena.
///////////////////////////////////////////////////////
struct ArenaBuffer : Arena {
  ArenaBuffer(const ArenaBuffer&)            = delete;
  ArenaBuffer& operator=(const ArenaBuffer&) = delete;

  ArenaBuffer() = default;
  explicit ArenaBuffer(u32 n)
  {
    data     = (u8*)malloc(n);
    capacity = n;
  }
  ~ArenaBuffer() { free(data); }

  void Resize(u32 n)
  {
    u32 used = Used();
    CTR_ASSERT(n >= used, "ArenaBuffer Resize below the bytes already claimed");
    if (n < used || n == capacity)
      return;
    if (offset < 0) {
      if (n > capacity) {
        data = (u8*)realloc(data, n);
        MemMove(data + n - used, data + capacity - used, used);
      } else {
        MemMove(data + n - used, data + capacity - used, used);
        data = (u8*)realloc(data, n);
      }
    } else {
      data = (u8*)realloc(data, n);
    }
    capacity = n;
  }
};

///////////////////////////////////////////////////////
// InitList
//  Minimal non-owning view over brace-initialized values.
///////////////////////////////////////////////////////
template <typename T>
struct InitList {
  const T* data = nullptr;
  size_t size  = 0;

  // Rvalue-only requires a call-local brace-list so InitList's pointer/count wrapper compiles out.
  constexpr InitList(std::initializer_list<T>&& values [[clang::lifetimebound]]) : data(values.begin()), size(values.size()) {}
};

///////////////////////////////////////////////////////
// FixedArray
//  Fixed-size inline storage that implicitly converts to a Span.
///////////////////////////////////////////////////////
template <typename T, size_t Size>
struct FixedArray {
  static constexpr size_t size = Size;
  T data[Size];

  template <SpanOf<T> S>
  constexpr operator S()
  {
    return S(Size, data);
  }
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

  operator Arena()
  {
    Arena arena(N, bytes);
    arena.offset = offset;
    return arena;
  }
};

///////////////////////////////////////////////////////
// String
//  Non-owning bounded string view; size excludes the terminating NUL when one is present.
///////////////////////////////////////////////////////
struct String {
  u32 size   = 0;
  char* data = nullptr;

  String() = default;
  String(size_t n, const char* p) : size((u32)n), data((char*)p) {}

  template <size_t Size>
  String(const char (&value)[Size]) : size((u32)strnlen(value, Size)), data((char*)value) {}
  String(const char* pText) : size(pText ? (u32)__builtin_strlen(pText) : 0), data((char*)(pText ? pText : "")) {}

  bool IsEmpty() const { return size == 0; }

  bool operator==(String other) const { return StrEqual(data, size, other.data, other.size); }
  bool operator!=(String other) const { return !StrEqual(data, size, other.data, other.size); }

  bool StartsWith(String prefix) const { return StrStartsWith(data, size, prefix.data, prefix.size); }
  i64 Find(char c, u32 from = 0) const { return StrOffset(data, StrFind(data + Min(from, size), c, size - Min(from, size))); }
  i64 Find(String needle, u32 from = 0) const { return StrOffset(data, StrFind(data + Min(from, size), size - Min(from, size), needle.data, needle.size)); }
  i64 RFind(char c) const { return StrOffset(data, StrFindLast(data, c, size)); }
  String Substr(u32 pos, u32 length) const { return String(Min(length, size - Min(pos, size)), data + Min(pos, size)); }

  char operator[](size_t index) const { return data[index]; }
};

////////////////////////////////////////////////////////////////////////////////
} // namespace Flat
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// @author rygo6
// File.hpp - Read-only mappings and sequential output for JSON files.
////////////////////////////////////////////////////////////////////////////////


#include <stdio.h>
#include <sys/mman.h>


////////////////////////////////////////////////////////////////////////////////
namespace Flat {
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////
// WritableFile
//  RAII writable C file stream. Truncates/creates by default; pass append=true to open "ab" and add
//  to the end instead. Panic-free: IsValid() is false when the open failed.
///////////////////////////////////////////////////////
struct WritableFile {
  FILE* pFile = nullptr;
  bool failed = false;

  explicit WritableFile(const char* pPath);
  ~WritableFile()
  {
    if (pFile)
      fclose(pFile);
  }

  WritableFile(const WritableFile&)   = delete;
  void operator=(const WritableFile&) = delete;

  bool IsValid() const { return pFile != nullptr; }

  bool Write(const void* pData, size_t bytes); // true only when the whole payload was written
  bool Flush();
};

///////////////////////////////////////////////////////
// FileMap
//  RAII read-only memory-mapped view of a file. Panic-free: on failure
//  (missing / unreadable / empty) `data` is null and `size` is 0 — `IsValid()`
//  reports it and the caller decides whether that's fatal. Suits both required
//  assets (caller traps on !IsValid) and optional config files (caller falls back).
///////////////////////////////////////////////////////
struct FileMap {
  const u8* data = nullptr;
  size_t size    = 0;

  explicit FileMap(const char* path);
  ~FileMap()
  {
    if (data)
      munmap((void*)data, size);
  }

  FileMap(const FileMap&)        = delete;
  void operator=(const FileMap&) = delete;

  bool IsValid() const { return data != nullptr; }

  // Implicitly view the mapped bytes as a SpanOf: raw bytes, text, or u32 words for SPIR-V.
  template <SpanOf<const char> S>
  operator S() const
  {
    return S(size, (const char*)data);
  }
};

////////////////////////////////////////////////////////////////////////////////
} // namespace Flat
////////////////////////////////////////////////////////////////////////////////

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
