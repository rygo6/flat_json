# Flat C++ JSON

`flat_json` parses JSON into one caller-owned, immutable
`FixedJsonBuffer<Capacity>`. The root, values, indexes, keys, and strings all
live in that buffer; no heap-allocated tree is built.

It also writes compact or pretty JSON directly to caller-owned memory or a
growing C file stream. Nested `JsonObject`, `JsonArray`, and `JsonValue`
initializers are consumed immediately without building an intermediate tree.

`flat_json` is written in the [Flat C++ dialect](https://github.com/rygo6/cb).

Requirements: C++23, Clang or GCC, and 64-bit ARM64 or x86-64.

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

## Parse and read

`Json::EstimateSize()` returns a constant-time conservative upper bound: 64
bytes per input byte plus fixed numeric scratch space. It does not parse or
validate the input.

```cpp
#include "flat_json.hpp"

using namespace flat;

constexpr char Text[] = R"({"values":[1,2,3]})";
FixedJsonBuffer<4096> jsonBuffer;

if (Json::EstimateSize(Text) > sizeof(jsonBuffer.bytes))
    return false;

switch (Json::Parse(Text, &jsonBuffer))
{
    case Json::SUCCESS: break;
    case Json::MALFORMED:
    case Json::ABSENT_VALUE:
    case Json::INVALID_ARGUMENT:
    case Json::INSUFFICIENT_SPACE:
    case Json::IO_ERROR: return false;
}

const Json* pDocument = jsonBuffer.pRoot;
long long second = (*pDocument)["values"][1].GetLong();
```

`pRoot` is null until parsing succeeds and is cleared after a failed parse.
The input text is not retained. The returned pointer remains valid until the
buffer is reused or destroyed.

### Arrays and objects

Arrays and objects are represented by their `Json` nodes. `GetArray()` and
`GetObject()` assert the type in `DEBUG` builds and return that same node.
`GetSize()` returns the number of elements or members.

```cpp
const Json& root = pDocument->GetObject();
const Json& values = root["values"].GetArray();

for (size_t i = 0; i < values.GetSize(); ++i) {
    double value = values[i].GetNumber();
}

if (root.HasKey("settings")) {
    const Json& settings = root["settings"].GetObject();
    if (settings.HasKey("enabled") && settings["enabled"].IsBool())
        enabled = settings["enabled"].GetBool();
}
```

Read accessors use `JSON_ASSERT` for type and bounds contracts. These checks
run in `DEBUG` builds and compile out otherwise. Validate uncertain data with
`Is*()`, `HasIndex()`, and `HasKey()` before access.

`GetDouble()` requires `TYPE_DOUBLE`. `GetNumber()` accepts any numeric type.
`GetString()` returns `{pData, size}`. `pData[size]` is always NUL, but `size`
is authoritative because decoded strings may contain embedded NUL bytes.
The parser accepts up to 19 nested arrays or objects; deeper input returns
`MALFORMED`.

### Try accessors

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
| `TryGetArray` | `const Json*` | array |
| `TryGetObject` | `const Json*` | object |

`TryGetArray` and `TryGetObject` pair with a C++17 if-init declaration, so the
node pointer is scoped to exactly the block that checked it:

```cpp
if (const Json* pItems; root.TryGetArray("items", &pItems) && pItems->GetSize() <= ItemCapacity) {
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

`TryParseHexString` parses a string member as a `u32` via `strtoul` base 0,
accepting `"0x1a2b"` hex or decimal — for values conventionally written in hex
that JSON numbers cannot express, such as hardware identifiers:

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
for (const Json& entry : root["values"].Elements())
    total += entry.GetNumber();

for (const Json& entry : root.TryElements("tags")) {
    if (!entry.IsString())
        continue;
    // ...
}

for (Json::Member member : root.TryMembers("attributes")) {
    String key = member.key;
    const Json& value = member.value;
}
```

`MemberAt(index, &key)` gives indexed access to object members in source order.

### Caller-filled text

Text and records both stay in caller-owned storage. A bounded body (a socket
read, a request payload) parses from a caller array into a caller arena — a 4x
arena covers any input that fits the text buffer:

```cpp
char text[16 * 1024];
FixedJsonBuffer<64 * 1024> jsonBuffer;

size_t length = ReadBody(text, sizeof(text));
if (Json::Parse(text, length, &jsonBuffer) != Json::SUCCESS)
    return false;

const Json* pJson = jsonBuffer;
```

A JSON file parses the same way through a temporary read-only mapping, released
at the end of the statement. `operator->` on a buffer dereferences its root,
and a buffer converts to its root `const Json*` implicitly:

```cpp
FixedJsonBuffer<16 * 1024> jsonBuffer;
if (Json::Parse(FileMap("settings.json"), &jsonBuffer) != Json::SUCCESS)
    return false;

String theme;
if (!jsonBuffer->TryGetString("theme", &theme))
    return false;
```

`FileMap` converts implicitly to any span constructible from
`(size_t, const char*)`, so `Parse` takes it through its `Span<const char>`
overload — the JSON API itself has no file types. An invalid mapping converts
to an empty span and parses as `ABSENT_VALUE`; when a missing file is an
ordinary case, check `FileMap::IsValid()` first and skip the parse.

## Write JSON

`FixedArray`, fixed C arrays, and compatible `std::span` values convert to
`Span` automatically. Other memory uses `Span<char>(capacity, pointer)`.
Successful memory output is NUL-terminated.

Non-integral floating-point output uses roughly 2 KiB of the uncommitted span
tail as conversion scratch. It can therefore return
`INSUFFICIENT_SPACE` even when the final JSON text alone would fit.

```cpp
#include "flat_json.hpp"

using namespace flat;

FixedArray<char, 4096> output;
Json::Status result = WriteJson(
    JsonObject({
        {"model", "gpt-5"},
        {"stream", true},
        {"messages", JsonArray({
            JsonObject({
                {"role", "user"},
                {"content", "Hello"},
            }),
        })},
    }),
    output);

if (result == Json::SUCCESS)
    puts(output.data);
```

Initializer values are non-owning and must be consumed in the same full
expression. `WriteJsonPretty()` provides formatted output.

`WriteJson()` and `Json::Parse()` return statuses for recoverable failures.
Invalid API arguments, I/O failures, and insufficient space also emit warnings;
malformed JSON simply returns `MALFORMED`. `JSON_REQUIRE` and `JSON_PANIC` are
reserved for internal invariants that indicate a library bug.

## Write and parse immediately

Memory output contains no unescaped NUL bytes, so `strlen()` gives the JSON
text size after a successful write.

```cpp
using namespace flat;

FixedArray<char, 4096> output;
Json::Status result = WriteJson(
    JsonObject({
        {"model", "gpt-5"},
        {"messages", JsonArray({
            JsonObject({
                {"role", "user"},
                {"content", "Hello"},
            }),
        })},
    }),
    output);

switch (result)
{
    case Json::SUCCESS: break;
    case Json::MALFORMED:
    case Json::ABSENT_VALUE:
    case Json::INVALID_ARGUMENT:
    case Json::INSUFFICIENT_SPACE:
    case Json::IO_ERROR: return false;
}

FixedJsonBuffer<4096> parseBuffer;
switch (Json::Parse(output.data, strlen(output.data), &parseBuffer))
{
    case Json::SUCCESS: {
        const Json* pJson = parseBuffer.pRoot;
        String model = (*pJson)["model"].GetString();
        String content = (*pJson)["messages"][0]["content"].GetString();
        break;
    }
    case Json::MALFORMED:
    case Json::ABSENT_VALUE:
    case Json::INVALID_ARGUMENT:
    case Json::INSUFFICIENT_SPACE:
    case Json::IO_ERROR: return false;
}
```

## Packed binary layout

Parsing decodes JSON text into native binary records. Records grow backward
from the end of the buffer while the front is temporary numeric-conversion
scratch space.

```text
low address                                                   high address
0 / used                back                                      capacity
v                         v                                              v
+-------------------------+------+---------------------------------------+
| conversion scratch /    | root | descendants, indexes, and string data |
| unused capacity         | Json |                                       |
+-------------------------+------+---------------------------------------+
                          <---------- immutable packed tree ------------->
                          <---- allocations grow toward lower addresses
```

Current 64-bit records:

| Record | Size | Contents |
| --- | ---: | --- |
| `Json` | 16 bytes | Type, subtree span, and an 8-byte scalar or relative-offset payload. |
| Array children | `16N` bytes | Contiguous `Json` records, allowing O(1) indexed access without an offset-table load. |
| Object index | `12N` or `16N` bytes | Key sizes and source-ordered `{keyOffset, valueOffset}` entries. Objects above 100 members add sorted entry indexes. |

Relative pointer paths:

```text
string -> Json + stringOffset -> NUL-terminated UTF-8
array  -> Json + arrayOffset  -> contiguous Json[index]
object -> Json + objectOffset -> keySizes[] + entries[]
                                  entry + keyOffset   -> key Json
                                  entry + valueOffset -> value Json
```

Scalars live inside their `Json` records. Arrays use fixed index arithmetic.
Objects through 100 members scan contiguous key sizes and compare bytes only
after a size match. Larger objects binary-search indexes sorted by key size and
bytes. Source-order entries remain unchanged for serialization.

Each `Json` record has an internal `span` field covering that node and every
descendant, including padding. Copying those bytes to another suitably aligned
address preserves all relative offsets. The layout uses the native ABI and
endianness; it is not a stable cross-platform file format. `u32` offsets limit
a subtree to less than 4 GiB.

## Files

`flat_file.hpp` provides four separate RAII wrappers:

| Type | Purpose |
| --- | --- |
| `File` | Read-only buffered `FILE*`. |
| `WritableFile` | Growing sequential output; truncates by default or appends when requested. |
| `FileMap` | Read-only mapping of an existing file. |
| `WritableFileMap` | Exact-size writable mapping for fixed binary data, random patches, or shared memory—not JSON streaming. |

JSON serialization and file output are separate operations: serialize into a
`Span<char>`, then pass the resulting bytes to `WritableFile::Write()`.

```cpp
#include "flat_file.hpp"
#include "flat_json.hpp"

#include <string.h>

using namespace flat;

FixedArray<char, 4096> output;
switch (WriteJson(
    JsonObject({
        {"model", "gpt-5"},
        {"stream", true},
        {"messages", JsonArray({
            JsonObject({
                {"role", "user"},
                {"content", "Hello"},
            }),
        })},
    }),
    output))
{
    case Json::SUCCESS: break;
    case Json::MALFORMED:
    case Json::ABSENT_VALUE:
    case Json::INVALID_ARGUMENT:
    case Json::INSUFFICIENT_SPACE:
    case Json::IO_ERROR: return false;
}

WritableFile file("request.json");
if (!file.IsValid() || !file.Write(output.data, strlen(output.data)) ||
    !file.Flush())
    return false;

FixedJsonBuffer<64 * 1024> parseBuffer;
switch (Json::Parse(FileMap("request.json"), &parseBuffer))
{
    case Json::SUCCESS: break;
    case Json::MALFORMED:
    case Json::ABSENT_VALUE:
    case Json::INVALID_ARGUMENT:
    case Json::INSUFFICIENT_SPACE:
    case Json::IO_ERROR: return false;
}

const Json& root = parseBuffer->GetObject();
String model = root["model"].GetString();
bool stream = root["stream"].GetBool();
String content = root["messages"][0]["content"].GetString();
```

The embedded floating-point code emits the shortest round-trippable finite
`float` or `double`. A `float` initializer retains its type instead of first
widening to `double`. NaN writes as `null`; positive and negative infinity
write as `1e5000` and `-1e5000`.

## Benchmarks

Measured 2026-08-16 on macOS 26.5.1 ARM64 with Apple Clang 17.0.0, C++23,
`-O3`, and `-DNDEBUG`. Values are the median of seven samples lasting at least
25 ms. Lower is better.

| Library | Parse 32-bit only | Parse with 64-bit | Serialize binary to string | Serialize binary to string pretty | Array lookup | Object lookup | Integer access | Floating access | String access |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Flat C++ JSON | 457.1 ns | 1,020.3 ns | 1,157.9 ns | 1,210.5 ns | 0.5 ns | 6.5 ns | 0.5 ns | 0.4 ns | 0.6 ns |
| jart/json.cpp | 2,018.0 ns | 3,024.7 ns | 2,865.9 ns | 3,843.5 ns | 1.9 ns | 32.7 ns | 0.9 ns | 1.0 ns | 1.0 ns |
| Mozilla-Ocho/llamafile json.cpp | 1,714.2 ns | 2,454.2 ns | 2,771.4 ns | 3,767.1 ns | 1.7 ns | 33.8 ns | 0.7 ns | 0.7 ns | 0.7 ns |
| nlohmann::ordered_json | 3,178.5 ns | 4,954.4 ns | 2,854.5 ns | 3,826.3 ns | 1.2 ns | 14.2 ns | 0.4 ns | 0.5 ns | 0.7 ns |
| niXman/flatjson | N/A | N/A | N/A* | N/A* | 4.1 ns | 22.5 ns | 3.4 ns | 11.6 ns | 0.6 ns |
| chadaustin/sajson | 471.7 ns | N/A | N/A | N/A | 0.5 ns | 9.4 ns | 0.5 ns | 0.6 ns | 0.7 ns |
| DaveGamble/cJSON | 2,727.0 ns | N/A | 5,777.4 ns | 6,111.9 ns | 22.0 ns | 45.4 ns | 0.5 ns | 0.4 ns | 0.5 ns |
| zserge/jsmn | N/A | N/A | N/A | N/A | 33.9 ns | 27.2 ns | 3.4 ns | 10.4 ns | 0.6 ns |

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

Native tests, benchmarks, and the fuzz corpus were rerun 2026-08-16 on macOS
ARM64 with Apple Clang 17.0.0. ASan and UBSan were last run 2026-08-11;
x86-64 checks were last run 2026-08-09.

| Check | Result |
| --- | --- |
| Unit, parse/write, and file round trips | Passed on ARM64 |
| Deterministic property tests | Generated documents, mutations, numeric bit patterns, output canaries, lookup thresholds, embedded NULs, nesting, and rollback passed |
| JSONTestSuite required cases | Accepted 95/95 `y_`; rejected 188/188 `n_` |
| JSONTestSuite implementation-defined cases | Accepted 20 and rejected 15; either result is conforming |
| `EstimateSize` bound | Every required conformance, round-trip, and accepted fuzz input fit its estimate |
| Native fuzz regression corpus | 2,304/2,304 seeds passed with slice, mutation, round-trip, relocation, and canary checks |
| UBSan | Unit tests and 2,304/2,304 expanded fuzz inputs passed |
| ASan | Unit tests and 2,304/2,304 expanded fuzz inputs passed |
| x86-64 under Rosetta | Build and unit tests passed |
| Warning-clean build | `flat_json.cpp`, `tests.cpp`, and `fuzz.cpp` passed `-Wall -Wextra -Werror` |

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
```
