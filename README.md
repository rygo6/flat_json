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
FixedJsonBuffer<4096> buffer;

if (Json::EstimateSize(Text) > sizeof(buffer.bytes))
    return false;

switch (Json::Parse(Text, &buffer))
{
    case Json::SUCCESS: break;
    case Json::MALFORMED:
    case Json::ABSENT_VALUE:
    case Json::INVALID_ARGUMENT:
    case Json::INSUFFICIENT_SPACE:
    case Json::IO_ERROR: return false;
}

const Json* pDocument = buffer.Root();
long long second = (*pDocument)["values"][1].GetLong();
```

`Root()` is null until parsing succeeds and is cleared after a failed parse.
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

## Write JSON

Fixed `char` arrays and compatible `std::span` values convert to
`JsonSpan<char>` automatically. Other memory uses
`JsonSpan<char>(capacity, pointer)`. Successful memory output is
NUL-terminated.

Non-integral floating-point output uses roughly 2 KiB of the uncommitted span
tail as conversion scratch. It can therefore return
`INSUFFICIENT_SPACE` even when the final JSON text alone would fit.

```cpp
#include "flat_json.hpp"

using namespace flat;

char pOutput[4096];
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
    pOutput);

if (result == Json::SUCCESS)
    puts(pOutput);
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

char pOutput[4096];
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
    pOutput);

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
switch (Json::Parse(pOutput, strlen(pOutput), &parseBuffer))
{
    case Json::SUCCESS: {
        const Json* pJson = parseBuffer.Root();
        JsonString model = (*pJson)["model"].GetString();
        JsonString content = (*pJson)["messages"][0]["content"].GetString();
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

`WriteJson()` streams directly through `WritableFile` and flushes before
returning. The temporary closes at the end of the call. A short write or flush
failure returns `IO_ERROR`.

```cpp
#include "flat_file.hpp"
#include "flat_json.hpp"

using namespace flat;

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
    WritableFile("request.json")))
{
    case Json::SUCCESS: break;
    case Json::MALFORMED:
    case Json::ABSENT_VALUE:
    case Json::INVALID_ARGUMENT:
    case Json::INSUFFICIENT_SPACE:
    case Json::IO_ERROR: return false;
}

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

const Json& root = parseBuffer.Root()->GetObject();
JsonString model = root["model"].GetString();
bool stream = root["stream"].GetBool();
JsonString content = root["messages"][0]["content"].GetString();
```

The embedded floating-point code emits the shortest round-trippable finite
`float` or `double`. A `float` initializer retains its type instead of first
widening to `double`. NaN writes as `null`; positive and negative infinity
write as `1e5000` and `-1e5000`.

## Benchmarks

Measured 2026-08-11 on macOS 26.5.1 ARM64 with Apple Clang 17.0.0, C++23,
`-O3`, and `-DNDEBUG`. Values are the median of seven samples lasting at least
25 ms. Lower is better.

| Library | Parse 32-bit only | Parse with 64-bit | Serialize binary to string | Serialize binary to string pretty | Array lookup | Object lookup | Integer access | Floating access | String access |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Flat C++ JSON | 517.9 ns | 1,055.9 ns | 1,192.7 ns | 1,265.7 ns | 0.5 ns | 6.9 ns | 0.5 ns | 0.4 ns | 0.6 ns |
| jart/json.cpp | 2,143.0 ns | 3,149.4 ns | 2,947.7 ns | 3,978.4 ns | 2.0 ns | 38.5 ns | 0.9 ns | 1.0 ns | 1.0 ns |
| Mozilla-Ocho/llamafile json.cpp | 1,743.2 ns | 2,551.6 ns | 2,890.1 ns | 3,951.2 ns | 1.8 ns | 36.9 ns | 0.8 ns | 0.8 ns | 0.8 ns |
| nlohmann::ordered_json | 3,353.9 ns | 5,171.2 ns | 2,971.5 ns | 3,966.7 ns | 1.3 ns | 14.6 ns | 0.4 ns | 0.5 ns | 0.7 ns |
| niXman/flatjson | N/A | N/A | N/A* | N/A* | 4.2 ns | 24.0 ns | 3.5 ns | 12.3 ns | 0.5 ns |
| chadaustin/sajson | 502.0 ns | N/A | N/A | N/A | 0.5 ns | 9.7 ns | 0.6 ns | 0.5 ns | 0.7 ns |
| DaveGamble/cJSON | 2,888.4 ns | N/A | 5,990.5 ns | 6,241.7 ns | 25.7 ns | 50.1 ns | 0.5 ns | 0.4 ns | 0.5 ns |
| zserge/jsmn | N/A | N/A | N/A | N/A | 34.8 ns | 27.9 ns | 3.6 ns | 11.0 ns | 0.6 ns |

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

Native tests, benchmarks, ASan, and UBSan were rerun 2026-08-11 on macOS ARM64
with Apple Clang 17.0.0. x86-64 checks were last run 2026-08-09.

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
