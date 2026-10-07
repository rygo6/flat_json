# Flat C++ JSON

`flat_json` parses JSON into one caller-owned, immutable arena —
`FixedArena<Capacity>` inline, or `ArenaBuffer` on the heap. The root, values,
indexes, keys, and strings all live in that arena; no heap-allocated tree is
built.

It writes compact or pretty JSON to caller-owned memory, which can then be
written to a file. Nested `ObjectValue`, `ArrayValue`, and value initializers are
consumed immediately without building an intermediate tree.

`flat_json` is written in the [Flat C++ dialect](https://github.com/rygo6/cb).

Requirements: C++23 with Clang and 64-bit ARM64 or x86-64. The build uses GNU
syntax extensions; GCC compatibility is not currently validated.

## Credits

- [jart/json.cpp](https://github.com/jart/json.cpp): original C++ implementation and tests.
- [jart/cosmopolitan](https://github.com/jart/cosmopolitan/blob/master/tool/net/ljson.c): original C parser ported to C++.
- [google/double-conversion](https://github.com/google/double-conversion): amalgamated floating-point conversion subset.
- [fastfloat/fast_float](https://github.com/fastfloat/fast_float): amalgamated Eisel-Lemire decimal parser.
- [chadaustin/sajson](https://github.com/chadaustin/sajson): object-lookup strategy and benchmark comparison.
- [nlohmann/json](https://github.com/nlohmann/json): vendored test and benchmark comparison.
- [nst/JSONTestSuite](https://github.com/nst/JSONTestSuite): vendored conformance corpus.

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for versions, provenance,
copyright notices, and licenses.

## Build

The library is two amalgamated files: `FlatJson.hpp` and `FlatJson.cpp`.
On macOS, compile and link `FlatJson.cpp` with your application:

```sh
clang++ -std=c++23 -O2 -fno-exceptions -fno-rtti -nostdlib++ -I. \
  app.cpp FlatJson.cpp -o app
```

Use `#include "FlatJson.hpp"` and the `Flat::Document` namespace.
The repository's Makefile supplies the supported GNU-extension warning flags.

## Parse and read

`EstimateSize()` returns a constant-time conservative upper bound: 64
bytes per input byte. It does not parse or validate the input. Numeric
conversion uses a fixed stack buffer, so the arena pays nothing for it.

```cpp
#include "FlatJson.hpp"

using namespace Flat;
using namespace Flat::Document;

constexpr char Text[] = R"({"values":[1,2,3]})";
FixedArena<4096> storage;
const Node* pDocument = nullptr;

if (EstimateSize(Text) > sizeof(storage.bytes))
  return false;

switch (ParseJSON(Text, &storage, &pDocument))
{
  case SUCCESS: break;
  case ABSENT_VALUE:
  case ERROR_MALFORMED:
  case ERROR_INVALID_ARGUMENT:
  case ERROR_INSUFFICIENT_SPACE:
  default: return false;
}

long long second = (*pDocument)["values"][1].GetLong();
```

With a valid arena, the output root is cleared before parsing and published
only on success. The input text is not retained. The returned pointer remains
valid until the arena is reset, resized, or destroyed. Null input with zero
length returns `ABSENT_VALUE`; a non-null empty or whitespace-only input
returns `ERROR_MALFORMED`. Normal parse failures restore the previous cursor.
The small-arena overlap regression is covered by `make check`.

Pass `FixedArena*` directly to `ParseJSON()`; it supplies aligned storage and
its cursor is updated automatically. `Arena*` is also supported for externally
backed storage. Converting a `FixedArena` to an `Arena` copies its cursor, so
subsequent changes to that handle do not update the fixed arena.

Caller-supplied storage must span its declared capacity. The parser rejects
null or misaligned storage, capacities above `INT32_MAX`, forward cursors,
and cursors outside the buffer.

`ArenaBuffer` backs an `Arena` with a heap allocation that is freed when it
leaves scope. It passes to `ParseJSON()` as an `Arena*`:

```cpp
ArenaBuffer buffer((u32)EstimateSize(Text));
const Node* pDocument = nullptr;
if (ParseJSON(Text, &buffer, &pDocument) != SUCCESS)
  return false;
```

`ArenaBuffer::Resize()` returns `void`. It preserves the used bytes when moving
reverse data, but does not provide recoverable allocation-failure handling.
For a parsed document, keep the new capacity at least `Used()` and preserve its
remainder modulo 8. After resizing, rederive pointers from their distance to the
arena end. Prefer allocating the required capacity before parsing.

### Arrays and objects

Arrays and objects are represented by their `Node` nodes. `GetArray()` and
`GetObject()` assert the type in `DEBUG` builds and return that same node.
`GetSize()` returns the number of elements or members.

```cpp
const Node& root = pDocument->GetObject();
const Node& values = root["values"].GetArray();

for (size_t i = 0; i < values.GetSize(); ++i) {
  double value = values[i].GetNumber();
}

if (root.HasKey("settings")) {
  const Node& settings = root["settings"].GetObject();
  if (settings.HasKey("enabled") && settings["enabled"].IsBool())
    enabled = settings["enabled"].GetBool();
}
```

Read accessors use `DOC_ASSERT` for type and bounds contracts. These checks
run in `DEBUG` builds and compile out otherwise. Validate uncertain data with
`Is*()`, `HasIndex()`, and `HasKey()` before access.

`GetDouble()` requires `TYPE_DOUBLE`. `GetNumber()` accepts any numeric type.
`GetString()` returns a `String` with `size` and `data` fields. `data[size]`
is always NUL, but `size`
is authoritative because decoded strings may contain embedded NUL bytes.
The parser accepts up to 19 nested arrays or objects; deeper input returns
`ERROR_MALFORMED`.

`String` is a non-owning view; every operation is bounded by `size` and never
reads past it. It constructs from a string literal, a `const char*`, or a
`(size, pointer)` pair. `Find()` and `RFind()` return the index of the match,
or -1 when there is none:

```cpp
String model = root["model"].GetString();
if (model == "gpt-5" || model.StartsWith("gpt-")) {
  i64 dash = model.Find('-');
  String family = model.Substr(0, (u32)dash);
}
```

| Method | Result |
| --- | --- |
| `IsEmpty()` | `size == 0` |
| `==`, `!=` | Same size and bytes |
| `StartsWith(prefix)` | Prefix test |
| `Find(c, from)`, `Find(needle, from)` | First match at or after `from`, or -1 |
| `RFind(c)` | Last match, or -1 |
| `Substr(pos, length)` | View clamped to the string's bounds |
| `operator[]` | Byte at an index; unchecked |

### Try accessors

Ordinary `Get*()` methods validate their preconditions only with assertions.
When `DEBUG` is not defined, those assertions compile out completely and add no
runtime validation cost. This is a Flat C++ semantic: callers choose which
checks remain in release builds by using `Try*()` methods, or by explicitly
testing `Is*()` and `Has*()` predicates before accessing a value. These explicit
checks remain active independently of `DEBUG`.

Every typed getter has a `Try` form taking a member key and an output pointer.
It returns false when the key is absent or the value is the wrong type, and the
output is left untouched — so pre-loaded defaults survive an absent member:

```cpp
u32 retries = 3;
float timeout = 30.0f;
root.TryGetU32("retries", &retries);
root.TryGetFloat("timeout", &timeout);
```

| Method | Output | Accepts |
| --- | --- | --- |
| `TryGetBool` | `bool` | `TYPE_BOOL` |
| `TryGetLong` | `long long` | `TYPE_LONG` |
| `TryGetU32` | `u32` | `TYPE_LONG` within `[0, UINT32_MAX]` |
| `TryGetFloat` | `float` | any numeric type, converted |
| `TryGetDouble` | `double` | any numeric type, converted |
| `TryGetString` | `String` | string |
| `TryGetArray` | `const Node*` | array |
| `TryGetObject` | `const Node*` | object |

`TryGetArray` and `TryGetObject` pair with a C++17 if-init declaration, so the
node pointer is scoped to exactly the block that checked it:

```cpp
if (const Node* pItems; root.TryGetArray("items", &pItems) && pItems->GetSize() <= ItemCapacity) {
  u32 count = (u32)pItems->GetSize();
  // ...
}
```

### Copy and parse helpers

`TryCopyString` copies a string member into caller memory, NUL-terminated.
Fixed `char` arrays convert to the `Span<char>` output automatically. It
returns false when the member is absent, not a string, larger than the output,
or contains an embedded NUL — a truncated copy never reports success:

```cpp
char name[32];
if (!root.TryCopyString("name", name))
  return false;
```

`TryCopyFloatArray` and `TryCopyDoubleArray` copy a fixed-length numeric array
member; the output span's size is the required element count, and every element
must be numeric:

```cpp
float color[4];
if (!root.TryCopyFloatArray("color", color))
  return false;
```

`TryParseHexString` parses a string member as a `u32` using explicit decimal or hexadecimal conversion,
accepting `"0x1a2b"` hex or decimal — for values conventionally written in hex
such as hardware identifiers. A `0x`/`0X` prefix selects hexadecimal; leading
zeros remain decimal. Signs, whitespace, embedded NULs, trailing characters,
strings longer than 15 characters, and values beyond `UINT32_MAX` are rejected:

```cpp
u32 deviceId = 0;
root.TryParseHexString("deviceId", &deviceId);
```

### Iteration

`Elements()` and `Members()` support range-for over arrays and objects. Like
`GetArray()` and `GetObject()`, they assert the type in `DEBUG` builds. The
`Try` forms never assert and iterate zero times instead: on the value itself
when it is the wrong type, or with a key when the member is absent or the wrong
type.

```cpp
for (const Node& entry : root["values"].Elements())
  total += entry.GetNumber();

for (const Node& entry : root.TryElements("tags")) {
  if (!entry.IsString())
    continue;
  // ...
}

for (Node::Member member : root.TryMembers("attributes")) {
  String key = member.key;
  const Node& value = member.value;
}
```

`MemberAt(index, &key)` gives indexed access to object members in source order.

### Caller-filled text

Text and records both stay in caller-owned storage. A bounded body (a socket
read, a request payload) parses from a caller array into a caller arena. This
example has a fixed memory budget and handles exhaustion explicitly; use
`EstimateSize()` when a conservative bound is required:

```cpp
char text[16 * 1024];
FixedArena<64 * 1024> storage;
const Node* pJson = nullptr;

size_t length = ReadBody(text, sizeof(text));
if (ParseJSON(text, length, &storage, &pJson) != SUCCESS)
  return false;
```

A JSON file parses the same way through a temporary read-only mapping, released
at the end of the statement:

```cpp
FixedArena<16 * 1024> storage;
const Node* pJson = nullptr;
if (ParseJSON(FileMap("settings.json"), &storage, &pJson) != SUCCESS)
  return false;

String theme;
if (!pJson->TryGetString("theme", &theme))
  return false;
```

`FileMap` converts implicitly to any span constructible from
`(size_t, const char*)`, so `ParseJSON` takes it through its `Span<const char>`
overload — the JSON API itself has no file types. An invalid mapping converts
to an empty span and parses as `ABSENT_VALUE`; when a missing file is an
ordinary case, check `FileMap::IsValid()` first and skip the parse.

## Write JSON

`FixedArray` and fixed C arrays convert to `Span` automatically. Other memory uses `Span<char>(capacity, pointer)`.
Successful memory output is NUL-terminated.

Non-integral floating-point output uses roughly 2 KiB of the uncommitted span
tail as conversion scratch. It can therefore return
`ERROR_INSUFFICIENT_SPACE` even when the final JSON text alone would fit.

```cpp
#include "FlatJson.hpp"

using namespace Flat;
using namespace Flat::Document;

FixedArray<char, 4096> output;
Result result = WriteJSON(
  ObjectValue({
    {"model", "gpt-5"},
    {"stream", true},
    {"messages", ArrayValue({
      ObjectValue({
        {"role", "user"},
        {"content", "Hello"},
      }),
    })},
  }),
  output);

if (result == SUCCESS)
  puts(output.data);
```

Initializer values are non-owning and must be consumed in the same full
expression. Brace lists pass through `InitList`; `WriteJSON(Value&&, ...)`
and `WriteJSONPretty(Value&&, ...)` accept temporary initializer trees.
Named `Value` trees are rejected; do not cast a retained tree to an rvalue
because its borrowed lists may already have expired. Parsed `Node` values
remain reusable through the separate `const Node&` overloads.
Initializer serialization is recursive and has no explicit depth or cycle
check. Supply an acyclic tree with valid storage for every string and
container span; nonzero lengths require non-null pointers.

`WriteJSON()` and `ParseJSON()` return statuses for recoverable failures.
The checked argument and output-capacity failures emit warnings;
malformed JSON returns `ERROR_MALFORMED`. File I/O status is handled by the
separate file wrappers. `DOC_REQUIRE` and `DOC_PANIC` are
reserved for internal invariants that indicate a library bug.

## Write and parse immediately

Memory output contains no unescaped NUL bytes. After a successful write, a
bounded `strnlen(output.data, output.size)` gives its text size.

```cpp
using namespace Flat;
using namespace Flat::Document;

FixedArray<char, 4096> output;
Result result = WriteJSON(
  ObjectValue({
    {"model", "gpt-5"},
    {"messages", ArrayValue({
      ObjectValue({
        {"role", "user"},
        {"content", "Hello"},
      }),
    })},
  }),
  output);

if (result != SUCCESS)
  return false;

FixedArena<4096> parseStorage;
const Node* pJson = nullptr;
switch (ParseJSON(output.data, strnlen(output.data, output.size), &parseStorage, &pJson))
{
  case SUCCESS: {
    String model = (*pJson)["model"].GetString();
    String content = (*pJson)["messages"][0]["content"].GetString();
    break;
  }
  default: return false;
}
```

## Packed binary layout

Parsing decodes JSON text into native binary records. Records grow backward
from the end of the buffer while the front holds transient key/value offset
pairs for the object currently being assembled. Numeric conversion uses a
fixed stack buffer in the parse entry frame.

```text
low address                                                   high address
0 / used                back                                      capacity
v                         v                                              v
+-------------------------+------+---------------------------------------+
| member-offset scratch / | root | descendants, indexes, and string data |
| unused capacity         | Node |                                       |
+-------------------------+------+---------------------------------------+
                          <---------- immutable packed tree ------------->
                          <---- allocations grow toward lower addresses
```

Current 64-bit records:

| Record | Size | Contents |
| --- | ---: | --- |
| `Node` | 16 bytes | Type, subtree span, and an 8-byte scalar or relative-offset payload. |
| Array children | `16N` bytes | Contiguous `Node` records, allowing O(1) indexed access without an offset-table load. |
| Object index | `12N` or `16N` bytes | Key sizes and source-ordered `{keyOffset, valueOffset}` entries. Objects above 100 members add sorted entry indexes. |

Relative pointer paths:

```text
string -> Node + stringOffset -> NUL-terminated UTF-8
array  -> Node + arrayOffset  -> contiguous Node[index]
object -> Node + objectOffset -> keySizes[] + entries[]
                                  index + keyOffset   -> key Node
                                  index + valueOffset -> value Node
```

Scalars live inside their `Node` records. Arrays use fixed index arithmetic; scalar-only arrays may store their
records in reverse order, marked by an internal bit in `arraySize`. Always use
`GetSize()`, indexing, and iteration rather than interpreting that raw field.
Objects through 100 members scan contiguous key sizes and compare bytes only
after a size match. Larger objects binary-search indexes sorted by key size and
bytes. Source-order entries remain unchanged for serialization.

Each `Node` record has an internal `span` field covering that node and every
descendant, including padding. Copying those bytes to another suitably aligned
address preserves all relative offsets. The layout uses the native ABI and
endianness; it is not a stable cross-platform file format. The signed arena cursor
limits supported capacity to `INT32_MAX` bytes (just under 2 GiB).
`EstimateSize()` returns `SIZE_MAX` when its conservative bound exceeds
that capacity. Check for `SIZE_MAX` before narrowing or allocating an arena.

## Amalgamated sources

`FlatJson.hpp` and `FlatJson.cpp` amalgamate FlatLib's `Types`, `Terminal`,
`Error`, `Container`, and `File` sources with the JSON `Document` sources.
`FlatJson.hpp` holds every header in dependency order; `FlatJson.cpp` holds the
implementations. Each section keeps its original file banner, so it can be
compared against FlatLib.

The container section retains only `Span`, `String`, `InitList`, `FixedArray`,
`Arena`, `FixedArena`, and `ArenaBuffer`, plus `MemCopy`, `MemMove`, and the
`Min`/`Str*` helpers behind `String`. `String` matches FlatLib's string view
except for its `Literal` and `FixedString` constructors, whose types are not
included. `ArenaBuffer` matches FlatLib. The other types are trimmed to the
members the JSON API, tests, and README examples use. The file section retains
`FileMap` and `WritableFile` for JSON file input and output.

## Logging and terminal

Library warnings (`[DOC] WARN`, `[FILE] WARN`) go to stderr through
`Flat::Terminal::Log`, prefixed with the source location and ANSI colors.
`DEBUG` enables the `*_ASSERT` checks.

`FlatJson.cpp` also carries FlatLib's terminal setup, which runs before
`main()`. When stdin is a TTY it switches stdin to non-canonical, no-echo
input; when stderr is a TTY it reserves the bottom row as a status line. It
installs handlers for fatal signals that restore the terminal and re-raise,
and restores the terminal at exit. Compile `FlatJson.cpp` with
`-DFLAT_SHARED_LIB` to skip this setup, for example when loading it into a
host process.

## Files

`FlatJson.hpp` provides two file RAII wrappers:

| Type | Purpose |
| --- | --- |
| `WritableFile` | Growing sequential output; truncates or creates the file. |
| `FileMap` | Read-only mapping of an existing file. |

`WritableFile::Flush()` reports buffered write errors, including previous
write failures. Call `Flush()` explicitly to check success before the
destructor closes the stream; destructors do not report `fclose()` failures.

JSON serialization and file output are separate operations: serialize into a
`Span<char>`, then pass the resulting bytes to `WritableFile::Write()`.

```cpp
#include "FlatJson.hpp"

#include <string.h>

using namespace Flat;
using namespace Flat::Document;

FixedArray<char, 4096> output;
switch (WriteJSON(
  ObjectValue({
    {"model", "gpt-5"},
    {"stream", true},
    {"messages", ArrayValue({
      ObjectValue({
        {"role", "user"},
        {"content", "Hello"},
      }),
    })},
  }),
  output))
{
  case SUCCESS: break;
  default: return false;
}

WritableFile file("request.json");
if (!file.IsValid() || !file.Write(output.data, strnlen(output.data, output.size)) ||
  !file.Flush())
  return false;

FixedArena<64 * 1024> parseStorage;
const Node* pJson = nullptr;
switch (ParseJSON(FileMap("request.json"), &parseStorage, &pJson))
{
  case SUCCESS: break;
  default: return false;
}

const Node& root = pJson->GetObject();
String model = root["model"].GetString();
bool stream = root["stream"].GetBool();
String content = root["messages"][0]["content"].GetString();
```

The embedded floating-point code emits the shortest round-trippable finite
`float` or `double`. A `float` initializer retains its type instead of first
widening to `double`. NaN writes as `null`; positive and negative infinity
write as `1e5000` and `-1e5000`.

## Benchmarks

Measured 2026-10-07 on macOS 26.5.1 ARM64 with Apple Clang 17.0.0, C++23,
`-O3`, and `-DNDEBUG`. Values are the median of seven samples lasting at least
25 ms. Lower is better.

| Library | Parse 32-bit only | Parse with 64-bit | Serialize binary to string | Serialize binary to string pretty | Array lookup | Object lookup | Integer access | Floating access | String access |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Flat C++ JSON | 477.4 ns | 1041.1 ns | 1131.0 ns | 1221.6 ns | 0.5 ns | 6.8 ns | 0.5 ns | 0.4 ns | 0.7 ns |
| jart/json.cpp | 1902.3 ns | 2771.4 ns | 2916.5 ns | 3952.5 ns | 2.0 ns | 37.4 ns | 0.9 ns | 1.0 ns | 1.0 ns |
| llamafile json.cpp | 1539.1 ns | 2262.7 ns | 2843.5 ns | 3862.4 ns | 1.8 ns | 32.7 ns | 0.7 ns | 0.7 ns | 0.8 ns |
| nlohmann::ordered_json | 2938.6 ns | 4666.7 ns | 2855.0 ns | 3941.8 ns | 1.3 ns | 14.2 ns | 0.4 ns | 0.4 ns | 0.7 ns |
| niXman/flatjson | N/A | N/A | N/A* | N/A* | 4.2 ns | 23.5 ns | 3.5 ns | 12.9 ns | 0.5 ns |
| chadaustin/sajson | 475.3 ns | N/A | N/A | N/A | 0.5 ns | 9.6 ns | 0.6 ns | 0.5 ns | 0.7 ns |
| DaveGamble/cJSON | 2228.3 ns | N/A | 5937.3 ns | 6185.5 ns | 23.3 ns | 48.0 ns | 0.5 ns | 0.4 ns | 0.5 ns |
| zserge/jsmn | N/A | N/A | N/A | N/A | 34.5 ns | 28.4 ns | 3.5 ns | 10.9 ns | 0.6 ns |

The parse columns include only libraries that eagerly produce and validate the
required numeric values:

- `Parse 32-bit only` covers every JSON value kind, nesting, escapes, signed-int32 boundaries, and decimals spanning the finite binary32 range.
- `Parse with 64-bit` repeats every 32-bit case, then adds exact signed-int64 and correctly rounded binary64 boundaries.
- `Serialize binary to string` writes typed values as compact JSON; the pretty column uses native formatting.

\* niXman/flatjson retains parsed scalar values as source text and copies that
text during serialization. It does not convert typed binary values to strings,
so these columns are `N/A`.

`sajson` cannot preserve all tested 64-bit integers, and `cJSON` stores every
number as `double`. niXman/flatjson and jsmn defer numeric conversion, so both
typed parse columns are `N/A`. `sajson` and jsmn are parser-only. See
[tests/README.md](tests/README.md) for pinned revisions and reproduction details.

Run the comparison with:

```sh
make benchmark
```

## Verification

Validation on 2026-10-07 rebuilt the amalgamated `FlatJson.hpp`/`FlatJson.cpp`
from scratch on macOS 26.5.1 with Apple Clang 17.0.0 (`clang-1700.6.3.2`).
Native, UBSan, and x86-64 suites pass, including the small-arena regression.

| Check | Result |
| --- | --- |
| Native unit, generated property, parse/write, and file tests | Passed on ARM64 |
| JSONTestSuite required cases | Accepted 95/95 `y_`; rejected 188/188 `n_` |
| JSONTestSuite implementation-defined cases | Accepted 20 and rejected 15 |
| Native fuzz regression corpus | 2,304/2,304 seeds passed with slice, mutation, round-trip, relocation, and canary checks |
| UBSan with debug assertions | Existing unit tests and all 2,304 fuzz seeds passed |
| Arena capacity sweep | Passed native, UBSan, and x86-64: two nested documents across capacities 0–16,384, including the 106-byte regression |
| ASan | Runtime initialization deadlock reproduced in a minimal C program, inside and outside the sandbox; see [diagnosis](tests/README.md#asan-startup-deadlock) |
| x86-64 under Rosetta | Build, unit tests, and all 2,304 fuzz seeds passed |
| Warning-clean build | `FlatJson.cpp` and owned test executables pass `-Wall -Wextra -Werror` with the documented GNU-extension flags |
| README examples | All 15 C++ examples compiled and linked against the amalgamated `FlatJson.cpp` |
| Benchmark adapters | All eight built, validated their supported workloads, and completed |

Backward allocation checks both remaining capacity and live object scratch.
The [capacity regression](tests/arena_capacity_regression.cpp) runs under
`make check`; insufficient space must clear the root and preserve the arena cursor.
The public parse boundary also rejects misaligned storage and invalid cursors.

The existing suites cover generated documents, numeric bit patterns, output
canaries, lookup thresholds, embedded NULs, relocation, nesting, and rollback.
They do not establish that every undersized arena fails safely.

JSONTestSuite prefixes mean:

| Prefix | Required result |
| --- | --- |
| `y_` | Accept |
| `n_` | Reject |
| `i_` | Implementation-defined; accept or reject |

The fuzz result is a replay of `fuzzies/`, not a claim of exhaustive
coverage-guided fuzzing. Exit 0 means accepted and exit 1 means rejected; both
are normal. Signals, sanitizer reports, or exit codes above 1 are failures.

Build and run the native suite with:

```sh
make check
make fuzz-check
```

`make check` rebuilds the test and fuzz executables as needed and always runs
the unit suite and arena capacity regression. See [tests/README.md](tests/README.md) for sanitizer commands.

## Code style and compatibility

The API lives in `Flat::Document`: `Node`, `Value`, `ArrayValue`, `ObjectValue`,
`ParseJSON`, `EstimateSize`, `WriteJSON`, and `WriteJSONPretty`.
Storage and file types live in `Flat`: `Span`, `String`, `FixedArray`, `Arena`,
`FixedArena`, `ArenaBuffer`, `FileMap`, and `WritableFile`.
Results use `Flat::Result` and `Flat::string_Result`. The former
`flat::Json` names and signatures are not provided.

The implementation uses vendored FlatLib sources and standalone configuration.
`.clang-format` captures the mechanical formatting rules.
Embedded numeric conversion regions retain upstream conventions and attribution.
The 16-byte relocatable `Node` layout remains specific to the native ABI.

`FlatJson.cpp` compiles the Document, File, Error, and Terminal
implementations together; the container subset is header-only.
It has no dependency on application code and links without the C++ runtime
library. The terminal section uses the Apple backend; validation covers
macOS ARM64 and x86-64 under Rosetta. Other platform backends are not validated.
Third-party benchmark adapters use their libraries' normal runtime requirements.
