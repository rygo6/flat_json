# JSON library benchmarks

Run the complete comparison from the repository root:

```sh
make benchmark
```

The setup clones pinned upstream revisions beneath
`${FLAT_JSON_BENCH_DEPS:-${TMPDIR:-/tmp}/flat-json-benchmark-deps}`. No
third-party source is copied into this repository. Set `FLAT_JSON_BENCH_DEPS`
to reuse another temporary clone directory.

## Compared revisions

| Library | Revision |
| --- | --- |
| Flat C++ JSON | Current worktree |
| Mozilla-Ocho/llamafile `json.cpp` | `880894d5c2d639a439db9f7fcf49960b5f0b1eda` |
| jart/json.cpp | `ccb9195e6abe9eb060e338a04f43e8d2e67b45ea` |
| nlohmann/json | `21af527e756435701f23e01aa8ea8dab6e050c90` |
| niXman/flatjson | `b46e20da85c9ed550e55c55d6d06fb5f1edf5572` |
| chadaustin/sajson | `68fe32ed6bcb5ac026671d6eadac9024a21c8b05` |
| DaveGamble/cJSON | `fb16e5cf358798aabb049655975cde8427101056` |
| zserge/jsmn | `25647e692c7906b96ffd2b05ca54c097948e879c` |

Jart's initial import says it was exported from Mozilla-Ocho/llamafile. The
lineage row benchmarks the last llamafile `json.cpp` revision from earlier on
that export date. Its source is compiled unchanged. The files under
`llamafile_compat/` only provide Cosmopolitan primitives missing from the host
toolchain: checked integer arithmetic, UTF helpers, integer formatting, and
the hex lookup table.

## Method

Every adapter parses the exact document in `benchmark.hpp`, validates the same
values, and then measures:

- parsing and destroying the whole document;
- parsing a focused signed-int32 corpus with eager conversion;
- parsing a focused decimal corpus spanning the finite binary32 range and
  validating the final binary32-rounded values;
- parsing a numeric corpus into exact signed 64-bit integers and correctly
  rounded IEEE-754 binary64 values, where the library supports both eagerly;
- serializing the prepared whole document as compact JSON to memory;
- serializing the prepared whole document with the library's native pretty
  formatting;
- array element and object member lookup on prepared containers;
- integer, floating-point, and string access on prepared values.

Each reported value is the median of seven samples. Calibration makes every
sample run for at least 25 ms. The executables are built with C++23, `-O3`, and
`-DNDEBUG` by default. Set `BENCHMARK_CXXFLAGS` to override the C++ flags and
`BENCHMARK_CFLAGS` to pass matching architecture or optimization flags to the
cJSON C adapter.

The benchmark uses each library's normal ownership model. Flat C++ JSON uses a
caller-owned fixed arena for parsing. Flat C++ JSON, niXman/flatjson, and cJSON
serialize compact JSON into caller-owned 64 KiB `char` arrays; the other
serializers use their native returned strings. The in-memory serialization
loops perform no file operation. sajson uses its documented
`single_allocation` mode, which copies immutable input to mutable storage. jsmn
uses a caller-owned token array.

Pretty output uses two-space indentation where the library exposes an indent
width: Flat C++ JSON, jart/json.cpp, llamafile json.cpp, nlohmann/json, and
niXman/flatjson. cJSON's native pretty writer uses tabs. The pretty column is
therefore a comparison of each library's native formatted writer, while the
compact column compares whitespace-free output.

sajson and jsmn are parser-only libraries, so serialization is reported as
`N/A`; the harness does not pretend an adapter-written serializer belongs to
them. jsmn also exposes token spans rather than typed values, so its integer and
floating access rows include the adapter converting those spans to numbers.

The exact-number corpus includes `INT64_MIN`, `INT64_MAX`, integers immediately
beyond the exact binary64 integer range, minimum subnormal, minimum normal, and
maximum finite binary64 values, plus a halfway-rounding case. sajson is excluded
because its typed integer storage is 32-bit and larger integers become doubles.
cJSON is excluded because it stores every number as a double. niXman/flatjson
and jsmn retain numeric text and defer typed conversion until access, so timing
their tokenization as if it had already produced exact 64-bit values would not
measure the same work. Their exact-number cells are therefore `N/A`.

JSON syntax does not distinguish float32 from binary64. The float32-range
column therefore does not claim that each library stores a `float`; it measures
ordinary decimal parsing over that range and validates the value after a
binary32 cast. Libraries such as niXman/flatjson and jsmn that retain number
text and defer conversion are `N/A` in both focused eager-conversion columns.
