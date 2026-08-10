# Flat C++ JSON

`flat_json` is a JSON library written in Flat C++ style. It parses JSON into one
caller-owned, immutable flattened `FixedJsonBuffer<Capacity>` containing the root,
values, indexes, keys, and strings. There is no heap-allocated object tree: on
success, `buffer.Root()` returns the `const Json*` stored directly in that buffer.

It also writes JSON directly to a caller-provided span or growing C file stream
from nested `JsonObject`, `JsonArray`, and `JsonValue` initializer expressions.
The writer consumes those temporary values immediately and does not build an
intermediate JSON tree or whole-document output buffer.

## Credits

- [jart/json.cpp](https://github.com/jart/json.cpp): original C++ JSON implementation used as the basis for this library and its tests.
- [jart/cosmopolitan](https://github.com/jart/cosmopolitan/blob/master/tool/net/ljson.c): original `ljson.c` parser from which the C++ implementation was ported.
- [google/double-conversion](https://github.com/google/double-conversion): amalgamated floating-point parsing and formatting subset.
- [fastfloat/fast_float](https://github.com/fastfloat/fast_float): amalgamated Eisel-Lemire decimal parsing fast path.
- [chadaustin/sajson](https://github.com/chadaustin/sajson): inspired the small-object scan and large-object sorted lookup strategy and provides a benchmark comparison.
- [nlohmann/json](https://github.com/nlohmann/json): vendored comparison implementation used by tests and benchmarks.
- [nst/JSONTestSuite](https://github.com/nst/JSONTestSuite): vendored JSON conformance tests and fixtures.

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the comprehensive provenance, details of how each repository was used, pinned versions, copyright notices, and licenses.

## Buffer parsing and direct output

Parsed documents are immutable. Parsing packs the tree backward from the end
of one caller-owned buffer and records a `const Json*` pointing directly at the
root record. Every child, index, key, and string offset is relative to the
record that contains it, so recursive access needs no buffer pointer and the
packed subtree can be relocated as one byte range. Arrays contain contiguous
fixed-size `Json` records, so indexed access uses fixed index arithmetic and
one scaled pointer addition with no offset-table lookup.

Memory serialization writes directly into a caller-provided `JsonSpan<char>`,
which is only a pointer and maximum byte count. The output is null-terminated
on success, so the same pointer can be passed directly to `puts()` or another
C-string API. File serialization writes incrementally through `FILE*` and lets
the file grow to the exact JSON text length. Neither path builds an
intermediate JSON tree or whole-document output buffer.

Fixed `char` arrays convert to `JsonSpan<char>` automatically. Compatible
`std::span` values also convert to `JsonSpan` implicitly. Other writable memory
is passed explicitly as `JsonSpan<char>(capacity, pointer)`.

`Json::GetString()` returns a `JsonString` containing `pData` and `size`.
`pData[size]` is always the terminating NUL; `size` remains authoritative when
the decoded JSON string contains embedded NUL characters.

`JsonBuffer` is the non-owning base accepted by `Json::Parse()`.
`FixedJsonBuffer<Capacity>` derives from it, owns fixed storage inline, and
exposes the parsed root through `Root()`.
`Json::EstimateSize()` returns a constant-time conservative upper bound of 64
bytes per input byte plus fixed exact-number scratch space. The packed layout's
proven worst case is below 55 bytes per input byte, so the estimate deliberately
trades space for a simple guarantee that it cannot be too small. It does not
read or validate the JSON; `Json::Parse()` remains authoritative. `SIZE_MAX`
means the pointer/size pair is invalid or even the conservative bound cannot
fit the 32-bit offset format.

```cpp
using namespace flat;

FixedJsonBuffer<4096> buffer;
char pOutput[4096];

const char* text = R"({"values":[1,2,3]})";
size_t required = Json::EstimateSize(text);
if (required <= sizeof(buffer.bytes) && Json::Parse(text, &buffer) == Json::SUCCESS) {
    const Json* pDocument = buffer.Root();
    long long second = (*pDocument)["values"][1].GetLong();
    if (WriteJson(*pDocument, pOutput) == Json::SUCCESS)
        puts(pOutput);
}
```

### Packed binary read layout

`Json::Parse` decodes the input text into native binary records in the buffer;
on success, `buffer.Root()` points at the first byte of the packed root
subtree. `Root()` is null before a successful parse and is cleared when parsing
fails. The input text is not retained. This is separate from `WriteJson`, which
emits UTF-8 JSON text rather than dumping the packed representation.

The buffer object holds `used`, `back`, and the root pointer outside the byte
payload. Parsed records grow backward from `back`; the low end
beginning at `used` is temporary numeric-conversion workspace. A successful
parse leaves one contiguous packed subtree beginning at `back`:

```text
low address                                                        high address

used = 0                                      back                 capacity
|             temporary scratch / free space   | root packed tree |
                                                | Json root first  |
```

On the supported 64-bit targets, the current records are:

| Record | Current size | Contents and offset base |
| --- | ---: | --- |
| `Json` | 16 bytes | Type, packed-subtree `span`, and an 8-byte scalar value or relative-offset payload. String, array, and object nodes each store a `u32` relative offset and `u32` byte/element/member count. |
| Array child table | `16N` bytes | One contiguous 16-byte `Json` record per element. Scalar-only arrays reuse the records written during parsing and mark their physically reversed table; the accessor reverses the index with fixed arithmetic. Other arrays use source order. Variable-sized descendant payloads remain elsewhere in the same packed subtree and use relative offsets. |
| Object index | `12N` or `16N` bytes | `N` contiguous `u32` key sizes followed by `N` source-ordered `{keyOffset, valueOffset}` entries. Objects with more than 100 members append `N` sorted `u32` entry indexes for binary search. Both entry offsets are relative to the start of the key sizes; `keyOffset` points to the key's `Json` record. |

Alignment can insert padding between records, so readers always add the stored
offset instead of assuming that the next record begins immediately. Conceptually,
the accessors perform these pointer additions:

```text
string node  -> Json + stringOffset -> UTF-8 bytes
array node   -> Json + arrayOffset  -> contiguous Json[physical index]
object node  -> Json + objectOffset -> keySizes[i] -> entries[i]
                                             + keyOffset   -> key Json -> UTF-8 bytes
                                             + valueOffset -> value Json
                                             sortedIndexes[i] -> entries[index]
```

Scalar booleans and numbers live directly in the `Json` payload. String nodes
store their decoded byte count and a direct relative offset to decoded UTF-8
bytes followed by a trailing null byte; the
stored size remains authoritative because a JSON string may contain `\u0000`.
Array indexing is O(1) and has no dependent offset-table load. Objects with at
most 100 members scan contiguous `u32` key sizes and compare bytes only when a
size matches. Larger objects binary-search an index sorted by key size and then
key bytes. The entry array remains in source order for serialization.

`Json::span` is the complete byte size of that node and everything beneath it,
including indexes, strings, children, and alignment padding. Consequently, the
`span` bytes beginning at a root `Json` can be copied to another suitably aligned
address and read through the same relative offsets. The representation uses
native ABI layout and native endianness and is not a versioned, cross-platform
file format. Its `u32` offsets also limit one packed subtree to less than 4 GiB.

### Read arrays and objects

An array or object is already represented by its `Json` node. `GetArray()` and
`GetObject()` validate the node type and return that same node; `GetSize()`
returns the element or member count. Array indexing and object-key lookup remain
on `Json`:

```cpp
using namespace flat;

const Json& root = pDocument->GetObject();
const Json& values = root["values"].GetArray();

for (size_t i = 0; i < values.GetSize(); ++i) {
    double value = values[i].GetNumber();
}

if (root.HasKey("settings")) {
    const Json& settings = root["settings"].GetObject();
    if (settings.HasKey("enabled")) {
        bool enabled = settings["enabled"].GetBool();
    }
}
```

Read accessors use `JSON_ASSERT` for type and bounds contracts. Those checks are
active in `DEBUG` builds and compile out completely otherwise. Release code
that consumes untrusted or uncertain structure should validate explicitly with
the `Is*()` methods, `HasIndex()`, and `HasKey()` before accessing a value:

```cpp
using namespace flat;

if (root.HasKey("values")) {
    const Json& values = root["values"].GetArray();
    if (values.HasIndex(1) && values[1].IsLong())
        second = values[1].GetLong();
}
```

Calling a read accessor with the wrong type, a missing key, or an out-of-range
index is a debug assertion failure and is undefined behavior in release builds.
`GetDouble()` is the exact `TYPE_DOUBLE` accessor; use `GetNumber()` when the
caller accepts any numeric representation.
Public parse and write failures never call `JSON_REQUIRE` or `JSON_PANIC`.
Invalid arguments, failed file mappings, and insufficient buffers emit
`JSON_WARN` and return `INVALID_ARGUMENT`, `IO_ERROR`, or
`INSUFFICIENT_SPACE`. `JSON_REQUIRE` and `JSON_PANIC` are reserved for internal
logic or memory invariants that indicate a library bug.

Internally, an array has a contiguous block of fixed-size child `Json` records,
while an object has contiguous key-size and relative-offset tables. The
parser duplicates each array child's 16-byte root record into that block; this
costs roughly 12 additional bytes per element versus a `u32` offset table but
removes its dependent load. Object lookup scans key sizes through 100 members;
larger objects append a sorted index and use binary search without changing
source-order serialization. These records are storage details, not separate
public API values.

Outgoing JSON can also be written directly from temporary initializer-list
expressions. The expression is non-owning and must be consumed in the same full
expression.

```cpp
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

Caller-owned memory is passed as `JsonSpan<char>`, a compatible `std::span`, or
a fixed `char` array. `flat_file.hpp` provides four independent file
wrappers with deliberately different jobs:

- `File` owns a read-only `FILE*` and provides `Read()`.
- `WritableFile` owns a growing sequential `FILE*`. It truncates by default or appends when constructed with `append = true`; pass it directly to `WriteJson`.
- `FileMap` maps an existing file read-only and can be passed directly to `Json::Parse`.
- `WritableFileMap` creates an exact-size writable mapping for fixed binary payloads, random-access patches, or shared memory. It is not a JSON writer; write its `data` directly and let its destructor flush, unmap, and close it.

`WriteJson` writes directly through `WritableFile`; there is no intermediary
writer object. A short write, flush failure, or invalid file emits a warning
and returns `IO_ERROR`.

If the destination cannot hold the complete text and its null terminator,
`WriteJson` emits `JSON_WARN` and returns `INSUFFICIENT_SPACE`.

### Parse generated JSON immediately

For span output, `WriteJson` always appends a trailing null byte on `SUCCESS`.
The caller's output pointer is therefore a null-terminated JSON string, and
`strlen` gives its byte length excluding that terminator. JSON string values
cannot introduce an earlier null because the writer escapes them as `\u0000`.

```cpp
using namespace flat;

char pOutput[4096];
switch (WriteJson(
    JsonObject({
        {"model", "gpt-5"},
        {"messages", JsonArray({
            JsonObject({
                {"role", "user"},
                {"content", "Hello"},
            }),
        })},
    }), pOutput))
{
    case Json::SUCCESS: break;
    case Json::INSUFFICIENT_SPACE:
    case Json::MALFORMED:
    case Json::ABSENT_VALUE:
    case Json::INVALID_ARGUMENT:
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

The parsed `Json` tree and its strings are copied into `parseBuffer`, so its
`Root()` remains valid until that storage is reused or destroyed.

### Round-trip a JSON file

Pass a temporary `WritableFile` directly to `WriteJson`. The file grows as JSON is
serialized and contains exactly the emitted JSON bytes without a trailing null.
`WriteJson` flushes before returning; the temporary closes immediately afterward.

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
    case Json::INSUFFICIENT_SPACE:
    case Json::MALFORMED:
    case Json::ABSENT_VALUE:
    case Json::INVALID_ARGUMENT:
    case Json::IO_ERROR: return false;
}
```

Pass a temporary read-only `FileMap` directly to `Parse`. Parsing copies the
immutable representation into `parseBuffer`; the temporary unmaps the input at
the end of the call.

```cpp
#include "flat_file.hpp"
#include "flat_json.hpp"

using namespace flat;

FixedJsonBuffer<64 * 1024> parseBuffer;
switch (Json::Parse(FileMap("request.json"), &parseBuffer))
{
    case Json::SUCCESS:
        break;
    case Json::MALFORMED:
    case Json::ABSENT_VALUE:
    case Json::INVALID_ARGUMENT:
    case Json::INSUFFICIENT_SPACE:
    case Json::IO_ERROR:
        return false;
}

const Json& root = parseBuffer.Root()->GetObject();
JsonString model = root["model"].GetString();
bool stream = root["stream"].GetBool();
const Json& messages = root["messages"].GetArray();
JsonString content = messages[0].GetObject()["content"].GetString();
```

The embedded double-conversion code emits the shortest round-trippable decimal
form for both `float` and `double`. `JsonValue` preserves whether an initializer
was a `float`, so `WriteJson` does not first widen it and print irrelevant
double-precision digits. This is useful for large embedding arrays.

## Benchmark Results

Measured 2026-08-09 on macOS 26.5.1 ARM64 with Apple Clang 17.0.0, C++23,
`-O3`, and `-DNDEBUG`. Each cell is the median of seven samples lasting at
least 25 ms. Values are nanoseconds per operation; lower is better.

| Library | Parse document | Parse int32 corpus | Parse float32-range corpus | Parse exact int64/binary64 | Serialize | Serialize pretty | Array lookup | Object lookup | Integer access | Floating access | String access |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Flat C++ JSON | 1,445.0 ns | 112.0 ns | 148.7 ns | 536.3 ns | 1,885.4 ns | 2,173.2 ns | 0.4 ns | 12.1 ns | 0.5 ns | 0.4 ns | 0.6 ns |
| jart/json.cpp | 6,201.2 ns | 298.2 ns | 574.1 ns | 623.4 ns | 2,969.5 ns | 4,027.4 ns | 2.0 ns | 36.9 ns | 0.9 ns | 1.0 ns | 1.0 ns |
| Mozilla-Ocho/llamafile json.cpp | 5,789.0 ns | 274.9 ns | 552.4 ns | 613.8 ns | 2,956.7 ns | 4,053.4 ns | 1.8 ns | 36.6 ns | 0.8 ns | 0.8 ns | 0.8 ns |
| nlohmann::ordered_json | 11,337.6 ns | 819.9 ns | 888.7 ns | 1,701.2 ns | 2,933.7 ns | 4,048.0 ns | 1.3 ns | 14.4 ns | 0.4 ns | 0.5 ns | 0.7 ns |
| niXman/flatjson | 2,681.6 ns | N/A | N/A | N/A | 516.4 ns | 1,074.6 ns | 4.3 ns | 23.8 ns | 3.4 ns | 16.9 ns | 0.5 ns |
| chadaustin/sajson | 1,145.8 ns | 185.0 ns | 175.0 ns | N/A | N/A | N/A | 0.5 ns | 9.6 ns | 0.6 ns | 0.6 ns | 0.7 ns |
| DaveGamble/cJSON | 8,100.3 ns | 895.1 ns | 964.9 ns | N/A | 6,250.2 ns | 6,399.4 ns | 24.8 ns | 49.2 ns | 0.6 ns | 0.4 ns | 0.4 ns |
| zserge/jsmn | 1,872.4 ns | N/A | N/A | N/A | N/A | N/A | 34.3 ns | 25.3 ns | 3.5 ns | 15.4 ns | 0.6 ns |

The focused x86-64/Rosetta check on the same host also kept the target lead:
Flat C++ JSON measured 136.1 ns for int32 and 221.1 ns for float32-range
decimals, versus sajson at 252.3 ns and 239.8 ns respectively. Rosetta timings
are recorded as a cross-architecture regression check, not as native x86-64
hardware results.

All adapters use the same general document and validate the same values before
timing. Focused corpora separately measure eager int32 conversion and decimal
conversion across the finite binary32 range. Because JSON has no float32 type,
the latter validates the resulting value after correctly rounding it to
binary32; each library still uses its native eager number representation. The
exact-number column uses another corpus containing the signed
64-bit boundaries, integers beyond 2^53, binary64 boundaries, and a halfway
rounding case. It includes only parsers that eagerly materialize lossless
`int64_t` and correctly rounded binary64 values. sajson stores only 32-bit
integers (larger integers become doubles), cJSON stores all numbers as doubles,
and niXman/flatjson and jsmn defer numeric conversion from retained text, so
those rows are `N/A` rather than misleading comparisons. `Serialize` emits
compact JSON to memory. Flat C++ JSON, niXman/flatjson, and cJSON use
caller-owned 64 KiB character arrays; the other serializers use their native
returned strings. Native pretty serialization remains a separate column.
sajson and jsmn report `N/A` because they do not provide serializers. See
[tests/README.md](tests/README.md) for pinned upstream revisions,
ownership-model details, and reproduction commands.

## Current Verification Results

Last verified 2026-08-09 on macOS ARM64 with Apple Clang 17.0.0.

| Check | Current result |
| --- | --- |
| Native unit and round-trip tests | Passed on ARM64 |
| Writable-file round trips | Initializer and parsed immutable documents wrote, closed, remapped, reparsed, and matched exactly |
| Debug accessor assertions | Full conformance suite passed on ARM64 and x86-64 with `DEBUG` enabled |
| Recoverable API failures | Invalid arguments, failed mappings, and insufficient parse/write buffers warn and return a distinct status |
| `EstimateSize` upper-bound audit | Every required JSONTestSuite acceptance, round-trip case, and accepted fuzz input parsed within its reported capacity |
| JSONTestSuite | Accepted 95/95 required `y_` cases; rejected 188/188 required `n_` cases; recorded all 35 implementation-defined `i_` cases |
| README classification audit | All 318 detailed classifications below match the current runner |
| Native fuzz regression corpus | 2,304/2,304 inputs completed without a crash |
| UBSan unit tests and fuzz corpus | Unit tests passed; 2,304/2,304 fuzz inputs completed without undefined-behavior or crash failures |
| x86-64 build and unit tests | Passed under Rosetta on the ARM64 host |
| Warning-clean build | `flat_json.cpp` compiled with `-Wall -Wextra -Werror` |

The fuzz count is a replay of the repository's `fuzzies/` regression corpus,
not a claim of exhaustive coverage-guided fuzzing. For an individual fuzz
input, exit code 0 means the parser accepted it and exit code 1 means it
rejected it; either is expected. A signal, sanitizer report, or exit code above
1 is treated as a failure.

Build and run the native suites with:

```sh
make check fuzz
./bin/tests
```

## JSONTestSuite Results

Here's the JSONTestSuite portion of `tests` for flat_json.

### Undefined test cases

The parser implementation is free to choose to accept or reject.

```
i_number_double_huge_neg_exp.json                                      IMPLEMENTATION_PASS
i_number_huge_exp.json                                                 IMPLEMENTATION_PASS
i_number_neg_int_huge_exp.json                                         IMPLEMENTATION_PASS
i_number_pos_double_huge_exp.json                                      IMPLEMENTATION_PASS
i_number_real_neg_overflow.json                                        IMPLEMENTATION_PASS
i_number_real_pos_overflow.json                                        IMPLEMENTATION_PASS
i_number_real_underflow.json                                           IMPLEMENTATION_PASS
i_number_too_big_neg_int.json                                          IMPLEMENTATION_PASS
i_number_too_big_pos_int.json                                          IMPLEMENTATION_PASS
i_number_very_big_negative_int.json                                    IMPLEMENTATION_PASS
i_object_key_lone_2nd_surrogate.json                                   IMPLEMENTATION_PASS
i_string_1st_surrogate_but_2nd_missing.json                            IMPLEMENTATION_PASS
i_string_1st_valid_surrogate_2nd_invalid.json                          IMPLEMENTATION_PASS
i_string_incomplete_surrogate_and_escape_valid.json                    IMPLEMENTATION_PASS
i_string_incomplete_surrogate_pair.json                                IMPLEMENTATION_PASS
i_string_incomplete_surrogates_escape_valid.json                       IMPLEMENTATION_PASS
i_string_invalid_lonely_surrogate.json                                 IMPLEMENTATION_PASS
i_string_invalid_surrogate.json                                        IMPLEMENTATION_PASS
i_string_invalid_utf-8.json                                            IMPLEMENTATION_FAIL (illegal_utf8_character)
i_string_inverted_surrogates_U+1D11E.json                              IMPLEMENTATION_PASS
i_string_iso_latin_1.json                                              IMPLEMENTATION_FAIL (malformed_utf8)
i_string_lone_second_surrogate.json                                    IMPLEMENTATION_PASS
i_string_lone_utf8_continuation_byte.json                              IMPLEMENTATION_FAIL (c1_control_code_in_string)
i_string_not_in_unicode_range.json                                     IMPLEMENTATION_FAIL (utf8_exceeds_utf16_range)
i_string_overlong_sequence_2_bytes.json                                IMPLEMENTATION_FAIL (overlong_ascii)
i_string_overlong_sequence_6_bytes.json                                IMPLEMENTATION_FAIL (illegal_utf8_character)
i_string_overlong_sequence_6_bytes_null.json                           IMPLEMENTATION_FAIL (illegal_utf8_character)
i_string_truncated-utf-8.json                                          IMPLEMENTATION_FAIL (malformed_utf8)
i_string_utf16BE_no_BOM.json                                           IMPLEMENTATION_FAIL (illegal_character)
i_string_utf16LE_no_BOM.json                                           IMPLEMENTATION_FAIL (illegal_character)
i_string_UTF-16LE_with_BOM.json                                        IMPLEMENTATION_FAIL (illegal_character)
i_string_UTF-8_invalid_sequence.json                                   IMPLEMENTATION_FAIL (illegal_utf8_character)
i_string_UTF8_surrogate_U+D800.json                                    IMPLEMENTATION_FAIL (utf16_surrogate_in_utf8)
i_structure_500_nested_arrays.json                                     IMPLEMENTATION_FAIL (depth_exceeded)
i_structure_UTF-8_BOM_empty_object.json                                IMPLEMENTATION_FAIL (illegal_character)
```

### Invalid JSON test cases

The parser must reject this data.

```
n_array_1_true_without_comma.json                                      REJECTED (missing_comma)
n_array_a_invalid_utf8.json                                            REJECTED (illegal_character)
n_array_colon_instead_of_comma.json                                    REJECTED (unexpected_colon)
n_array_comma_after_close.json                                         REJECTED (trailing_content)
n_array_comma_and_number.json                                          REJECTED (unexpected_comma)
n_array_double_comma.json                                              REJECTED (unexpected_comma)
n_array_double_extra_comma.json                                        REJECTED (unexpected_comma)
n_array_extra_close.json                                               REJECTED (trailing_content)
n_array_extra_comma.json                                               REJECTED (unexpected_end_of_array)
n_array_incomplete_invalid_value.json                                  REJECTED (illegal_character)
n_array_incomplete.json                                                REJECTED (unexpected_eof)
n_array_inner_array_no_comma.json                                      REJECTED (missing_comma)
n_array_invalid_utf8.json                                              REJECTED (illegal_character)
n_array_items_separated_by_semicolon.json                              REJECTED (unexpected_colon)
n_array_just_comma.json                                                REJECTED (unexpected_comma)
n_array_just_minus.json                                                REJECTED (bad_negative)
n_array_missing_value.json                                             REJECTED (unexpected_comma)
n_array_newlines_unclosed.json                                         REJECTED (unexpected_eof)
n_array_number_and_comma.json                                          REJECTED (unexpected_end_of_array)
n_array_number_and_several_commas.json                                 REJECTED (unexpected_comma)
n_array_spaces_vertical_tab_formfeed.json                              REJECTED (non_del_c0_control_code_in_string)
n_array_star_inside.json                                               REJECTED (illegal_character)
n_array_unclosed.json                                                  REJECTED (unexpected_eof)
n_array_unclosed_trailing_comma.json                                   REJECTED (unexpected_eof)
n_array_unclosed_with_new_lines.json                                   REJECTED (unexpected_eof)
n_array_unclosed_with_object_inside.json                               REJECTED (unexpected_eof)
n_incomplete_false.json                                                REJECTED (illegal_character)
n_incomplete_null.json                                                 REJECTED (illegal_character)
n_incomplete_true.json                                                 REJECTED (illegal_character)
n_multidigit_number_then_00.json                                       REJECTED (trailing_content)
n_number_0.1.2.json                                                    REJECTED (illegal_character)
n_number_-01.json                                                      REJECTED (unexpected_octal)
n_number_0.3e+.json                                                    REJECTED (bad_exponent)
n_number_0.3e.json                                                     REJECTED (bad_exponent)
n_number_0_capital_E+.json                                             REJECTED (bad_exponent)
n_number_0_capital_E.json                                              REJECTED (bad_exponent)
n_number_0.e1.json                                                     REJECTED (bad_double)
n_number_0e+.json                                                      REJECTED (bad_exponent)
n_number_0e.json                                                       REJECTED (bad_exponent)
n_number_1_000.json                                                    REJECTED (missing_comma)
n_number_1.0e+.json                                                    REJECTED (bad_exponent)
n_number_1.0e-.json                                                    REJECTED (bad_exponent)
n_number_1.0e.json                                                     REJECTED (bad_exponent)
n_number_-1.0..json                                                    REJECTED (illegal_character)
n_number_1eE2.json                                                     REJECTED (bad_exponent)
n_number_+1.json                                                       REJECTED (illegal_character)
n_number_.-1.json                                                      REJECTED (illegal_character)
n_number_2.e+3.json                                                    REJECTED (bad_double)
n_number_2.e-3.json                                                    REJECTED (bad_double)
n_number_2.e3.json                                                     REJECTED (bad_double)
n_number_.2e-3.json                                                    REJECTED (illegal_character)
n_number_-2..json                                                      REJECTED (bad_double)
n_number_9.e+.json                                                     REJECTED (bad_double)
n_number_expression.json                                               REJECTED (illegal_character)
n_number_hex_1_digit.json                                              REJECTED (illegal_character)
n_number_hex_2_digits.json                                             REJECTED (illegal_character)
n_number_infinity.json                                                 REJECTED (illegal_character)
n_number_+Inf.json                                                     REJECTED (illegal_character)
n_number_Inf.json                                                      REJECTED (illegal_character)
n_number_invalid+-.json                                                REJECTED (bad_exponent)
n_number_invalid-negative-real.json                                    REJECTED (missing_comma)
n_number_invalid-utf-8-in-bigger-int.json                              REJECTED (illegal_character)
n_number_invalid-utf-8-in-exponent.json                                REJECTED (illegal_character)
n_number_invalid-utf-8-in-int.json                                     REJECTED (illegal_character)
n_number_++.json                                                       REJECTED (illegal_character)
n_number_minus_infinity.json                                           REJECTED (bad_negative)
n_number_minus_sign_with_trailing_garbage.json                         REJECTED (bad_negative)
n_number_minus_space_1.json                                            REJECTED (bad_negative)
n_number_-NaN.json                                                     REJECTED (bad_negative)
n_number_NaN.json                                                      REJECTED (illegal_character)
n_number_neg_int_starting_with_zero.json                               REJECTED (unexpected_octal)
n_number_neg_real_without_int_part.json                                REJECTED (bad_negative)
n_number_neg_with_garbage_at_end.json                                  REJECTED (illegal_character)
n_number_real_garbage_after_e.json                                     REJECTED (bad_exponent)
n_number_real_with_invalid_utf8_after_e.json                           REJECTED (bad_exponent)
n_number_real_without_fractional_part.json                             REJECTED (bad_double)
n_number_starting_with_dot.json                                        REJECTED (illegal_character)
n_number_U+FF11_fullwidth_digit_one.json                               REJECTED (illegal_character)
n_number_with_alpha_char.json                                          REJECTED (illegal_character)
n_number_with_alpha.json                                               REJECTED (illegal_character)
n_number_with_leading_zero.json                                        REJECTED (unexpected_octal)
n_object_bad_value.json                                                REJECTED (illegal_character)
n_object_bracket_key.json                                              REJECTED (object_key_must_be_string)
n_object_comma_instead_of_colon.json                                   REJECTED (unexpected_comma)
n_object_double_colon.json                                             REJECTED (unexpected_colon)
n_object_emoji.json                                                    REJECTED (illegal_character)
n_object_garbage_at_end.json                                           REJECTED (object_key_must_be_string)
n_object_key_with_single_quotes.json                                   REJECTED (illegal_character)
n_object_lone_continuation_byte_in_key_and_trailing_comma.json         REJECTED (illegal_utf8_character)
n_object_missing_colon.json                                            REJECTED (illegal_character)
n_object_missing_key.json                                              REJECTED (unexpected_colon)
n_object_missing_semicolon.json                                        REJECTED (missing_colon)
n_object_missing_value.json                                            REJECTED (unexpected_eof)
n_object_no-colon.json                                                 REJECTED (unexpected_eof)
n_object_non_string_key_but_huge_number_instead.json                   REJECTED (object_key_must_be_string)
n_object_non_string_key.json                                           REJECTED (object_key_must_be_string)
n_object_repeated_null_null.json                                       REJECTED (object_key_must_be_string)
n_object_several_trailing_commas.json                                  REJECTED (unexpected_comma)
n_object_single_quote.json                                             REJECTED (illegal_character)
n_object_trailing_comma.json                                           REJECTED (unexpected_end_of_object)
n_object_trailing_comment.json                                         REJECTED (trailing_content)
n_object_trailing_comment_open.json                                    REJECTED (trailing_content)
n_object_trailing_comment_slash_open_incomplete.json                   REJECTED (trailing_content)
n_object_trailing_comment_slash_open.json                              REJECTED (trailing_content)
n_object_two_commas_in_a_row.json                                      REJECTED (unexpected_comma)
n_object_unquoted_key.json                                             REJECTED (illegal_character)
n_object_unterminated-value.json                                       REJECTED (unexpected_end_of_string)
n_object_with_single_string.json                                       REJECTED (unexpected_end_of_object)
n_object_with_trailing_garbage.json                                    REJECTED (trailing_content)
n_single_space.json                                                    REJECTED (absent_value)
n_string_1_surrogate_then_escape.json                                  REJECTED (unexpected_end_of_string)
n_string_1_surrogate_then_escape_u1.json                               REJECTED (invalid_unicode_escape)
n_string_1_surrogate_then_escape_u1x.json                              REJECTED (invalid_unicode_escape)
n_string_1_surrogate_then_escape_u.json                                REJECTED (invalid_unicode_escape)
n_string_accentuated_char_no_quotes.json                               REJECTED (illegal_character)
n_string_backslash_00.json                                             REJECTED (invalid_escape_character)
n_string_escaped_backslash_bad.json                                    REJECTED (unexpected_end_of_string)
n_string_escaped_ctrl_char_tab.json                                    REJECTED (invalid_escape_character)
n_string_escaped_emoji.json                                            REJECTED (invalid_escape_character)
n_string_escape_x.json                                                 REJECTED (hex_escape_not_printable)
n_string_incomplete_escaped_character.json                             REJECTED (invalid_unicode_escape)
n_string_incomplete_escape.json                                        REJECTED (unexpected_end_of_string)
n_string_incomplete_surrogate_escape_invalid.json                      REJECTED (invalid_hex_escape)
n_string_incomplete_surrogate.json                                     REJECTED (invalid_unicode_escape)
n_string_invalid_backslash_esc.json                                    REJECTED (invalid_escape_character)
n_string_invalid_unicode_escape.json                                   REJECTED (invalid_unicode_escape)
n_string_invalid_utf8_after_escape.json                                REJECTED (invalid_escape_character)
n_string_invalid-utf-8-in-escape.json                                  REJECTED (invalid_unicode_escape)
n_string_leading_uescaped_thinspace.json                               REJECTED (illegal_character)
n_string_no_quotes_with_bad_escape.json                                REJECTED (illegal_character)
n_string_single_doublequote.json                                       REJECTED (unexpected_end_of_string)
n_string_single_quote.json                                             REJECTED (illegal_character)
n_string_single_string_no_double_quotes.json                           REJECTED (illegal_character)
n_string_start_escape_unclosed.json                                    REJECTED (unexpected_end_of_string)
n_string_unescaped_ctrl_char.json                                      REJECTED (non_del_c0_control_code_in_string)
n_string_unescaped_newline.json                                        REJECTED (non_del_c0_control_code_in_string)
n_string_unescaped_tab.json                                            REJECTED (non_del_c0_control_code_in_string)
n_string_unicode_CapitalU.json                                         REJECTED (invalid_escape_character)
n_string_with_trailing_garbage.json                                    REJECTED (trailing_content)
n_structure_100000_opening_arrays.json                                 REJECTED (depth_exceeded)
n_structure_angle_bracket_..json                                       REJECTED (illegal_character)
n_structure_angle_bracket_null.json                                    REJECTED (illegal_character)
n_structure_array_trailing_garbage.json                                REJECTED (trailing_content)
n_structure_array_with_extra_array_close.json                          REJECTED (trailing_content)
n_structure_array_with_unclosed_string.json                            REJECTED (unexpected_end_of_string)
n_structure_ascii-unicode-identifier.json                              REJECTED (illegal_character)
n_structure_capitalized_True.json                                      REJECTED (illegal_character)
n_structure_close_unopened_array.json                                  REJECTED (trailing_content)
n_structure_comma_instead_of_closing_brace.json                        REJECTED (unexpected_eof)
n_structure_double_array.json                                          REJECTED (trailing_content)
n_structure_end_array.json                                             REJECTED (unexpected_end_of_array)
n_structure_incomplete_UTF8_BOM.json                                   REJECTED (illegal_character)
n_structure_lone-invalid-utf-8.json                                    REJECTED (illegal_character)
n_structure_lone-open-bracket.json                                     REJECTED (unexpected_eof)
n_structure_no_data.json                                               REJECTED (absent_value)
n_structure_null-byte-outside-string.json                              REJECTED (illegal_character)
n_structure_number_with_trailing_garbage.json                          REJECTED (trailing_content)
n_structure_object_followed_by_closing_object.json                     REJECTED (trailing_content)
n_structure_object_unclosed_no_value.json                              REJECTED (unexpected_eof)
n_structure_object_with_comment.json                                   REJECTED (illegal_character)
n_structure_object_with_trailing_garbage.json                          REJECTED (trailing_content)
n_structure_open_array_apostrophe.json                                 REJECTED (illegal_character)
n_structure_open_array_comma.json                                      REJECTED (unexpected_comma)
n_structure_open_array_object.json                                     REJECTED (depth_exceeded)
n_structure_open_array_open_object.json                                REJECTED (unexpected_eof)
n_structure_open_array_open_string.json                                REJECTED (unexpected_end_of_string)
n_structure_open_array_string.json                                     REJECTED (unexpected_eof)
n_structure_open_object_close_array.json                               REJECTED (unexpected_end_of_array)
n_structure_open_object_comma.json                                     REJECTED (unexpected_comma)
n_structure_open_object.json                                           REJECTED (unexpected_eof)
n_structure_open_object_open_array.json                                REJECTED (object_key_must_be_string)
n_structure_open_object_open_string.json                               REJECTED (unexpected_end_of_string)
n_structure_open_object_string_with_apostrophes.json                   REJECTED (illegal_character)
n_structure_open_open.json                                             REJECTED (invalid_escape_character)
n_structure_single_eacute.json                                         REJECTED (illegal_character)
n_structure_single_star.json                                           REJECTED (illegal_character)
n_structure_trailing_#.json                                            REJECTED (trailing_content)
n_structure_U+2060_word_joined.json                                    REJECTED (illegal_character)
n_structure_uescaped_LF_before_string.json                             REJECTED (illegal_character)
n_structure_unclosed_array.json                                        REJECTED (unexpected_eof)
n_structure_unclosed_array_partial_null.json                           REJECTED (illegal_character)
n_structure_unclosed_array_unfinished_false.json                       REJECTED (illegal_character)
n_structure_unclosed_array_unfinished_true.json                        REJECTED (illegal_character)
n_structure_unclosed_object.json                                       REJECTED (unexpected_eof)
n_structure_unicode-identifier.json                                    REJECTED (illegal_character)
n_structure_UTF8_BOM_no_data.json                                      REJECTED (illegal_character)
n_structure_whitespace_formfeed.json                                   REJECTED (illegal_character)
n_structure_whitespace_U+2060_word_joiner.json                         REJECTED (illegal_character)
```

### Success JSON test cases

The parser must accept this JSON as valid.

```
y_array_arraysWithSpaces.json                                          PASSED
y_array_empty.json                                                     PASSED
y_array_empty-string.json                                              PASSED
y_array_ending_with_newline.json                                       PASSED
y_array_false.json                                                     PASSED
y_array_heterogeneous.json                                             PASSED
y_array_null.json                                                      PASSED
y_array_with_1_and_newline.json                                        PASSED
y_array_with_leading_space.json                                        PASSED
y_array_with_several_null.json                                         PASSED
y_array_with_trailing_space.json                                       PASSED
y_number_0e+1.json                                                     PASSED
y_number_0e1.json                                                      PASSED
y_number_after_space.json                                              PASSED
y_number_double_close_to_zero.json                                     PASSED
y_number_int_with_exp.json                                             PASSED
y_number.json                                                          PASSED
y_number_minus_zero.json                                               PASSED
y_number_negative_int.json                                             PASSED
y_number_negative_one.json                                             PASSED
y_number_negative_zero.json                                            PASSED
y_number_real_capital_e.json                                           PASSED
y_number_real_capital_e_neg_exp.json                                   PASSED
y_number_real_capital_e_pos_exp.json                                   PASSED
y_number_real_exponent.json                                            PASSED
y_number_real_fraction_exponent.json                                   PASSED
y_number_real_neg_exp.json                                             PASSED
y_number_real_pos_exponent.json                                        PASSED
y_number_simple_int.json                                               PASSED
y_number_simple_real.json                                              PASSED
y_object_basic.json                                                    PASSED
y_object_duplicated_key_and_value.json                                 PASSED
y_object_duplicated_key.json                                           PASSED
y_object_empty.json                                                    PASSED
y_object_empty_key.json                                                PASSED
y_object_escaped_null_in_key.json                                      PASSED
y_object_extreme_numbers.json                                          PASSED
y_object.json                                                          PASSED
y_object_long_strings.json                                             PASSED
y_object_simple.json                                                   PASSED
y_object_string_unicode.json                                           PASSED
y_object_with_newlines.json                                            PASSED
y_string_1_2_3_bytes_UTF-8_sequences.json                              PASSED
y_string_accepted_surrogate_pair.json                                  PASSED
y_string_accepted_surrogate_pairs.json                                 PASSED
y_string_allowed_escapes.json                                          PASSED
y_string_backslash_and_u_escaped_zero.json                             PASSED
y_string_backslash_doublequotes.json                                   PASSED
y_string_comments.json                                                 PASSED
y_string_double_escape_a.json                                          PASSED
y_string_double_escape_n.json                                          PASSED
y_string_escaped_control_character.json                                PASSED
y_string_escaped_noncharacter.json                                     PASSED
y_string_in_array.json                                                 PASSED
y_string_in_array_with_leading_space.json                              PASSED
y_string_last_surrogates_1_and_2.json                                  PASSED
y_string_nbsp_uescaped.json                                            PASSED
y_string_nonCharacterInUTF-8_U+10FFFF.json                             PASSED
y_string_nonCharacterInUTF-8_U+FFFF.json                               PASSED
y_string_null_escape.json                                              PASSED
y_string_one-byte-utf-8.json                                           PASSED
y_string_pi.json                                                       PASSED
y_string_reservedCharacterInUTF-8_U+1BFFF.json                         PASSED
y_string_simple_ascii.json                                             PASSED
y_string_space.json                                                    PASSED
y_string_surrogates_U+1D11E_MUSICAL_SYMBOL_G_CLEF.json                 PASSED
y_string_three-byte-utf-8.json                                         PASSED
y_string_two-byte-utf-8.json                                           PASSED
y_string_u+2028_line_sep.json                                          PASSED
y_string_u+2029_par_sep.json                                           PASSED
y_string_uescaped_newline.json                                         PASSED
y_string_uEscape.json                                                  PASSED
y_string_unescaped_char_delete.json                                    PASSED
y_string_unicode_2.json                                                PASSED
y_string_unicodeEscapedBackslash.json                                  PASSED
y_string_unicode_escaped_double_quote.json                             PASSED
y_string_unicode.json                                                  PASSED
y_string_unicode_U+10FFFE_nonchar.json                                 PASSED
y_string_unicode_U+1FFFE_nonchar.json                                  PASSED
y_string_unicode_U+200B_ZERO_WIDTH_SPACE.json                          PASSED
y_string_unicode_U+2064_invisible_plus.json                            PASSED
y_string_unicode_U+FDD0_nonchar.json                                   PASSED
y_string_unicode_U+FFFE_nonchar.json                                   PASSED
y_string_utf8.json                                                     PASSED
y_string_with_del_character.json                                       PASSED
y_structure_lonely_false.json                                          PASSED
y_structure_lonely_int.json                                            PASSED
y_structure_lonely_negative_real.json                                  PASSED
y_structure_lonely_null.json                                           PASSED
y_structure_lonely_string.json                                         PASSED
y_structure_lonely_true.json                                           PASSED
y_structure_string_empty.json                                          PASSED
y_structure_trailing_newline.json                                      PASSED
y_structure_true_in_array.json                                         PASSED
y_structure_whitespace_array.json                                      PASSED
```
