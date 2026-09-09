////////////////////////////////////////////////////////////////////////////////
// @author rygo6
// Error.hpp - The Result status vocabulary and the trap/soft-fail macros built over it.
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Terminal.hpp"


#include "Types.hpp"


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
