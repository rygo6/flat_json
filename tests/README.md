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

Every supported parse adapter validates the same values before measurement.
The benchmark measures:

- parsing a 32-bit-only document containing every JSON value kind, nesting,
  escapes, signed-int32 boundaries, and finite binary32-range decimals;
- parsing a 64-bit document that repeats every 32-bit case and adds exact
  signed-int64 and correctly rounded IEEE-754 binary64 boundaries;
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
caller-owned fixed arena for parsing. Flat C++ JSON and cJSON serialize compact
JSON into caller-owned 64 KiB `char` arrays; the other serializers use their
native returned strings. The in-memory serialization loops perform no file
operation. sajson uses its documented
`single_allocation` mode, which copies immutable input to mutable storage. jsmn
uses a caller-owned token array.

Pretty output uses two-space indentation where the library exposes an indent
width: Flat C++ JSON, jart/json.cpp, llamafile json.cpp, and nlohmann/json.
cJSON's native pretty writer uses tabs. The pretty column therefore compares
each library's native formatted writer, while the compact column compares
whitespace-free output.

The serialization columns measure converting typed binary values to JSON text.
niXman/flatjson retains parsed scalar values as source text and copies that text
during serialization, so it is reported as `N/A*`.

## Serialization ablations

Each change below was measured alone against the immediately preceding build.
Retained changes passed `make check` before the next experiment.

| Retained change | Observed effect |
| --- | --- |
| Inline scalar-child dispatch | Compact about 2.6% faster |
| Direct 1–3 digit parsed integers | Compact about 10% faster cumulatively |
| Fuse compact array comma + small integer | Compact about 1.2% faster |
| Mark unescaped parsed strings as directly writable UTF-8 | Compact about 9% faster with no parse regression |
| Batch compact object headers and pretty key headers | Compact and pretty about 7% faster |
| Compile separate compact and pretty walkers | Removed runtime formatting branches |
| Batch pretty `, ` and object `,\n` | Pretty about 11% faster cumulatively |
| Put the output-capacity check on one hot success branch | Compact about 1.2% faster |

Rejected experiments were neutral or slower: generic small-integer branches,
digit-pair conversion, four-to-six-digit specialization, pointer-based output
state, word-at-a-time output string scans, direct array-pointer walking, bulk
indentation, whole object key/value fusion, and an integral-double shortcut.

sajson and jsmn are parser-only libraries, so serialization is reported as
`N/A`; niXman/flatjson is `N/A*` because it copies retained source text rather
than converting typed binary values. The harness does not pretend an
adapter-written serializer belongs to parser-only libraries. jsmn exposes token
spans, so its integer and floating access rows include converting those spans.

The 64-bit workload includes `INT64_MIN`, `INT64_MAX`, integers immediately
beyond the exact binary64 integer range, minimum subnormal, minimum normal,
maximum finite binary64, and a halfway-rounding case. sajson is `N/A` because
larger integers become potentially lossy doubles. cJSON is `N/A` because it
stores every number as a double.

JSON syntax does not distinguish float32 from binary64. The 32-bit workload
uses values requiring no more than signed-int32 and binary32 precision and
validates decimals after a binary32 cast; an implementation may use a wider
internal representation. niXman/flatjson and jsmn retain numeric source text
and defer conversion until access, so both typed parse columns are `N/A`.
