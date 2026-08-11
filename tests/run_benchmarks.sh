#!/bin/sh
set -eu

scriptDirectory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repositoryRoot=$(CDPATH= cd -- "$scriptDirectory/.." && pwd)
dependencyRoot=${FLAT_JSON_BENCH_DEPS:-${TMPDIR:-/tmp}/flat-json-benchmark-deps}

if [ ! -f "$dependencyRoot/jart-json/json.cpp" ] ||
   [ ! -f "$dependencyRoot/llamafile/llamafile/server/json.cpp" ] ||
   [ ! -f "$dependencyRoot/nlohmann-json/include/nlohmann/json.hpp" ] ||
   [ ! -f "$dependencyRoot/flatjson/include/flatjson/flatjson.hpp" ] ||
   [ ! -f "$dependencyRoot/sajson/include/sajson.h" ] ||
   [ ! -f "$dependencyRoot/cjson/cJSON.c" ] ||
   [ ! -f "$dependencyRoot/jsmn/jsmn.h" ]; then
  FLAT_JSON_BENCH_DEPS=$dependencyRoot sh "$scriptDirectory/setup_benchmark_deps.sh"
fi

CXX=${CXX:-c++}
CC=${CC:-cc}
benchmarkFlags=${BENCHMARK_CXXFLAGS:--std=c++23 -O3 -DNDEBUG}
benchmarkCFlags=${BENCHMARK_CFLAGS:--O3 -DNDEBUG}
upstreamWarningFlags=
case "$($CXX --version 2>/dev/null)" in
  *clang*) upstreamWarningFlags='-Wno-unknown-warning-option -Wno-tautological-constant-out-of-range-compare' ;;
esac
buildDirectory=$repositoryRoot/bin/benchmarks
mkdir -p "$buildDirectory"

# Intentional word splitting lets BENCHMARK_CXXFLAGS contain multiple flags.
# shellcheck disable=SC2086
$CXX $benchmarkFlags -I"$scriptDirectory" \
  "$scriptDirectory/benchmark_flat_json.cpp" \
  "$repositoryRoot/flat_json.cpp" \
  -o "$buildDirectory/flat_json"

# shellcheck disable=SC2086
$CXX $benchmarkFlags $upstreamWarningFlags -I"$scriptDirectory" -I"$dependencyRoot/jart-json" \
  "$scriptDirectory/benchmark_jart.cpp" \
  "$dependencyRoot/jart-json/json.cpp" \
  "$dependencyRoot/jart-json"/double-conversion/*.cc \
  -o "$buildDirectory/jart"

# The historical source is unchanged. These include paths only provide the
# small Cosmopolitan primitives that are unavailable in a normal host toolchain.
# shellcheck disable=SC2086
$CXX $benchmarkFlags $upstreamWarningFlags -I"$scriptDirectory" \
  -include "$scriptDirectory/llamafile_compat/prelude.hpp" \
  -I"$scriptDirectory/llamafile_compat" \
  -I"$dependencyRoot/llamafile/llamafile/server" \
  -I"$dependencyRoot/llamafile" \
  "$scriptDirectory/benchmark_llamafile.cpp" \
  "$scriptDirectory/llamafile_compat/compat.cpp" \
  "$dependencyRoot/llamafile/llamafile/server/json.cpp" \
  "$dependencyRoot/llamafile"/double-conversion/*.cc \
  -o "$buildDirectory/llamafile"

# shellcheck disable=SC2086
$CXX $benchmarkFlags $upstreamWarningFlags -I"$scriptDirectory" \
  -I"$dependencyRoot/nlohmann-json/include" \
  "$scriptDirectory/benchmark_nlohmann.cpp" \
  -o "$buildDirectory/nlohmann"

# shellcheck disable=SC2086
$CXX $benchmarkFlags $upstreamWarningFlags -I"$scriptDirectory" \
  -I"$dependencyRoot/flatjson/include" \
  "$scriptDirectory/benchmark_nixman_flatjson.cpp" \
  -o "$buildDirectory/nixman_flatjson"

# shellcheck disable=SC2086
$CXX $benchmarkFlags -I"$scriptDirectory" \
  -I"$dependencyRoot/sajson/include" \
  "$scriptDirectory/benchmark_sajson.cpp" \
  -o "$buildDirectory/sajson"

# Intentional word splitting lets BENCHMARK_CFLAGS contain multiple flags.
# shellcheck disable=SC2086
$CC -std=c11 $benchmarkCFlags -I"$dependencyRoot/cjson" \
  -c "$dependencyRoot/cjson/cJSON.c" \
  -o "$buildDirectory/cJSON.o"
# shellcheck disable=SC2086
$CXX $benchmarkFlags -I"$scriptDirectory" \
  -I"$dependencyRoot/cjson" \
  "$scriptDirectory/benchmark_cjson.cpp" \
  "$buildDirectory/cJSON.o" \
  -o "$buildDirectory/cjson"

# shellcheck disable=SC2086
$CXX $benchmarkFlags -I"$scriptDirectory" \
  -I"$dependencyRoot/jsmn" \
  "$scriptDirectory/benchmark_jsmn.cpp" \
  -o "$buildDirectory/jsmn"

printf '%s\n' '| Library | Parse 32-bit only | Parse with 64-bit | Serialize binary to string | Serialize binary to string pretty | Array lookup | Object lookup | Integer access | Floating access | String access |'
printf '%s\n' '| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |'
for benchmarkName in flat_json jart llamafile nlohmann nixman_flatjson sajson cjson jsmn; do
  "$buildDirectory/$benchmarkName"
done
