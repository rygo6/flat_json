# Flat C++ JSON

`flat_json` is a JSON library written in Flat C++ style. It parses JSON into
one caller-owned, immutable flattened arena buffer containing the root, values,
indexes, keys, and strings. There is no heap-allocated object tree and no
document wrapper: on success, `Parse` fills a caller-provided `const Json*`
that points directly into the arena.

It also writes JSON directly to an arena, span, or mapped buffer from nested
`JsonObject`, `JsonArray`, and `JsonValue` initializer expressions. The writer
consumes those temporary values immediately and does not build an intermediate
JSON tree.

## Credits

Flat C++ JSON is derived from [jart/json.cpp](https://github.com/jart/json.cpp),
the C++ JSON library published by Justine Tunney and contributors with Mozilla
sponsorship in 2024. That implementation was itself ported from Cosmopolitan's
[`tool/net/ljson.c`](https://github.com/jart/cosmopolitan/blob/master/tool/net/ljson.c),
written by Justine Tunney and Gautham Venkatasubramanian in 2022.

The checked-in integer arithmetic helper comes from
[jart/jtckdint](https://github.com/jart/jtckdint). Complete copyright,
provenance, and license text is preserved in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Floating-point parsing and formatting use the required subset of
[google/double-conversion](https://github.com/google/double-conversion), pinned
to commit
[`75b48d66ac835da2c1678926f7d61d6cb2992922`](https://github.com/google/double-conversion/commit/75b48d66ac835da2c1678926f7d61d6cb2992922).
That subset was amalgamated into `flat_json.cpp` and adapted to operate in the
flat arena without a separate heap or intermediary conversion buffer. Its
BSD-3-Clause license is reproduced in `flat_json.cpp` and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Arena API

Parsed documents are immutable. Parsing packs the tree backward from the end
of one caller-owned arena and fills a `const Json*` output pointing directly at
the root record. There is no document wrapper. Every child, index, key, and
string offset is relative to the record that contains it, so recursive access
needs no arena pointer and the packed subtree can be relocated as one byte
range. Arrays contain exact-sized offset tables, so indexed access remains O(1).

JSON serialization grows forward from the beginning of an output arena.
Decimal digits and exact-conversion workspace live in unused output space, so
neither parsing nor serialization allocates an intermediary or heap buffer.

```cpp
flat::FixedArena<4096> arena;

const flat::Json* pDocument;
flat::Json::Status status = flat::Json::Parse(R"({"values":[1,2,3]})", arena, &pDocument);
if (status == flat::Json::SUCCESS) {
    long long second = (*pDocument)["values"][1].GetLong();
    const char* pText;
    status = flat::WriteJson(*pDocument, arena, &pText);
}
```

### Packed binary read layout

`Json::Parse` decodes the input text into native binary records in the arena;
the filled `const Json*` points at the first byte of the packed root subtree.
The input text is not retained. This is separate from `WriteJson`, which emits
UTF-8 JSON text rather than dumping the packed representation.

The arena grows in both directions. Serialized text grows forward from `used`,
while parsed records grow backward from `back`. A successful parse leaves one
contiguous packed subtree beginning at `back`:

```text
low address                                                        high address

0                 used                         back                 capacity
| ArenaHeader | JSON text |     free space     | root packed tree |
| used, back  |  (optional)                    | Json root first  |
```

On the supported 64-bit targets, the current records are:

| Record | Current size | Contents and offset base |
| --- | ---: | --- |
| `ArenaHeader` | 16 bytes | Two native `size_t` cursors: forward `used` and backward `back`. |
| `Json` | 16 bytes | Type, packed-subtree `span`, and an 8-byte scalar value or a `u32` relative offset. String, array, and object offsets are relative to this `Json`. |
| `PackedString` | 8 bytes | `u32 size` and `u32 dataOffset`; `dataOffset` is relative to the `PackedString`. |
| `ArrayIndex` | `4 + 4N` bytes | `u32 size` followed by one `u32` child offset per element. Every child offset is relative to the `ArrayIndex`. |
| `ObjectIndex` | `4 + 12N` bytes | `u32 size` followed by `{hash, keyOffset, valueOffset}` entries. Both offsets are relative to the `ObjectIndex`. |

Alignment can insert padding between records, so readers always add the stored
offset instead of assuming that the next record begins immediately. Conceptually,
the accessors perform these pointer additions:

```text
string node  -> Json + stringOffset -> PackedString + dataOffset -> UTF-8 bytes
array node   -> Json + arrayOffset  -> ArrayIndex  + offsets[i]  -> child Json
object node  -> Json + objectOffset -> ObjectIndex + keyOffset   -> PackedString
                                             same ObjectIndex + valueOffset -> value Json
```

Scalar booleans and numbers live directly in the `Json` payload. Strings store
their decoded byte count, decoded UTF-8 bytes, and a trailing null byte; the
stored size remains authoritative because a JSON string may contain `\u0000`.
Array indexing is O(1). Object lookup scans the exact-sized entry table, using
each cached hash to avoid comparing key bytes unless the hash and length match.

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
const flat::Json& root = pDocument->GetObject();
const flat::Json& values = root["values"].GetArray();

for (size_t i = 0; i < values.GetSize(); ++i) {
    double value = values[i].GetNumber();
}

if (root.Contains("settings")) {
    const flat::Json& settings = root["settings"].GetObject();
    if (settings.Contains("enabled")) {
        bool enabled = settings["enabled"].GetBool();
    }
}
```

Internally, an array has an exact-sized table of relative child offsets, and an
object has a table of hashed keys and relative value offsets. Those packed index
records provide O(1) array access and compact hash-assisted object scans; they
are storage details in `flat_json.cpp`, not values returned by the public API.

Outgoing JSON can also be written directly from temporary initializer-list
expressions. The expression is non-owning and must be consumed in the same full
expression.

```cpp
const char* pText;
flat::Json::Status status = flat::WriteJson(
    flat::JsonObject({
        {"model", "gpt-5"},
        {"stream", true},
        {"messages", flat::JsonArray({
            flat::JsonObject({
                {"role", "user"},
                {"content", "Hello"},
            }),
        })},
    }),
    arena,
    &pText);
```

`MappedBuffer` can wrap caller-owned mapped memory or own a file mapping.
`WriteJson` writes at its current cursor without an arena or intermediary
allocation, then advances the cursor by the JSON byte count including its null
terminator. Wrapping existing memory does not transfer ownership:

```cpp
char bytes[4096];
flat::MappedBuffer mappedBuffer(bytes, sizeof(bytes));

const char* pText;
flat::Json::Status status = flat::WriteJson(
    flat::JsonObject({
        {"model", "gpt-5"},
        {"stream", true},
        {"messages", flat::JsonArray({
            flat::JsonObject({
                {"role", "user"},
                {"content", "Hello"},
            }),
        })},
    }),
    mappedBuffer,
    &pText);
```

If the destination cannot hold the complete text and its null terminator,
`WriteJson` emits `JSN_WARN`, returns `INSUFFICIENT_SPACE`, and sets `pText` to
`nullptr`. It does not advance a mapped-buffer cursor or commit an arena's
forward cursor, so the caller can retry with more space. Any partially written
bytes are uncommitted and must be ignored.

### Read mapped JSON back immediately

On `SUCCESS`, `WriteJson` always appends a trailing null byte, including when
the destination is a `MappedBuffer`. The output pointer is therefore a
null-terminated JSON string, and `strlen` gives its byte length excluding that
terminator. JSON string values cannot introduce an earlier null because the
writer escapes them as `\u0000`.

```cpp
const char* pText;
flat::Json::Status status = flat::WriteJson(
    flat::JsonObject({
        {"model", "gpt-5"},
        {"messages", flat::JsonArray({
            flat::JsonObject({
                {"role", "user"},
                {"content", "Hello"},
            }),
        })},
    }),
    mappedBuffer,
    &pText);

flat::FixedArena<4096> parseArena;
const flat::Json* pJson;
if (status == flat::Json::SUCCESS)
    status = flat::Json::Parse(pText, strlen(pText), parseArena, &pJson);
if (status == flat::Json::SUCCESS) {
    const char* pModel = (*pJson)["model"].GetString();
    const char* pContent = (*pJson)["messages"][0]["content"].GetString();
}
```

`pText` remains valid only while the underlying mapping remains valid. The
parsed `Json` tree and its strings are copied into `parseArena`, so `pJson`
remains valid until that arena is reset or destroyed.

### Round-trip a file-backed mapping

The writable constructor opens, sizes, and memory-maps the file. Its destructor
synchronously flushes the written range, removes the trailing in-memory null
terminator from the file size, unmaps the memory, and closes the file.

```cpp
constexpr size_t FileCapacity = 64 * 1024;
{
    flat::MappedBuffer output("request.json", FileCapacity);
    if (!output.IsValid())
        return false;

    const char* pText;
    flat::Json::Status status = flat::WriteJson(
        flat::JsonObject({
            {"model", "gpt-5"},
            {"stream", true},
            {"messages", flat::JsonArray({
                flat::JsonObject({
                    {"role", "user"},
                    {"content", "Hello"},
                }),
            })},
        }),
        output,
        &pText);
    if (status != flat::Json::SUCCESS)
        return false;
}
```

The read-only constructor can be passed directly to `Parse()`. The temporary
mapping remains alive for the call, then immediately unmaps and closes. This is
safe because parsing copies the immutable tree and strings into `parseArena`:

```cpp
flat::FixedArena<64 * 1024> parseArena;
const flat::Json* pJson;
flat::Json::Status status = flat::Json::Parse(
    flat::MappedBuffer("request.json"), parseArena, &pJson);
if (status == flat::Json::SUCCESS) {
    const flat::Json& root = pJson->GetObject();
    const char* pModel = root["model"].GetString();
    bool stream = root["stream"].GetBool();
    const flat::Json& messages = root["messages"].GetArray();
    const char* pContent = messages[0].GetObject()["content"].GetString();
}
```

The embedded double-conversion code emits the shortest round-trippable decimal
form for both `float` and `double`. `JsonValue` preserves whether an initializer
was a `float`, so `WriteJson` does not first widen it and print irrelevant
double-precision digits. This is useful for large embedding arrays.

## Embedded third-party code

The section labeled `Embedded google/double-conversion` in `flat_json.cpp` comes
from [google/double-conversion](https://github.com/google/double-conversion),
pinned to commit `75b48d66ac835da2c1678926f7d61d6cb2992922` dated
2024-05-21. It is licensed under BSD-3-Clause. The full copyright notice,
conditions, and disclaimer appear directly above the amalgamated code in
`flat_json.cpp` and in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

The amalgamated section records the local changes retained by this repository.
The rest of this project remains under the license in the repository-level
`LICENSE` file.

## Benchmark Results

This is a historical benchmark snapshot; results depend on the compiler and
machine. Lower numbers are better. See [tests.cpp](tests.cpp) and
[nlohmann/json\_test.cpp](nlohmann/json_test.cpp). The benchmark's
`json_test_suite()` input set includes only cases both libraries accept.

```
    # flat_json
        71 ns 2000x object_test()
       226 ns 2000x deep_test()
       675 ns 2000x parse_test()
      1309 ns 2000x round_trip_test()
     10462 ns 2000x json_test_suite()

    # nlohmann::ordered_json
        202 ns 2000x object_test()
        659 ns 2000x deep_test()
       1928 ns 2000x parse_test()
       4258 ns 2000x round_trip_test()
      16617 ns 2000x json_test_suite()
```

## Current Verification Results

Last verified 2026-08-08 on macOS ARM64 with Apple Clang 17.0.0.

| Check | Current result |
| --- | --- |
| Native unit and round-trip tests | Passed on ARM64 |
| Insufficient output handling | `MappedBuffer` and `FixedArena` warn, return `INSUFFICIENT_SPACE`, preserve their cursors, and support retry |
| JSONTestSuite | Accepted 95/95 required `y_` cases; rejected 188/188 required `n_` cases; recorded all 35 implementation-defined `i_` cases |
| README classification audit | All 318 detailed classifications below match the current runner |
| Native fuzz regression corpus | 2,304/2,304 inputs completed without a crash |
| UBSan unit tests and fuzz corpus | Unit tests passed; 2,304/2,304 fuzz inputs completed without undefined-behavior or crash failures |
| x86-64 build and unit tests | Passed under Rosetta on the ARM64 host |
| CMake/CTest | 1/1 consolidated test passed |
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
