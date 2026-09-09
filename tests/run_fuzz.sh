#!/bin/sh
set -eu

executable=${1:-./bin/fuzz}
ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}halt_on_error=1:exitcode=86"
UBSAN_OPTIONS="${UBSAN_OPTIONS:+$UBSAN_OPTIONS:}halt_on_error=1:exitcode=86"
export ASAN_OPTIONS UBSAN_OPTIONS

count=0
for seed in fuzzies/*; do
  [ -f "$seed" ] || continue
  status=0
  "$executable" < "$seed" > /dev/null 2> bin/fuzz-last.log || status=$?
  if [ "$status" -gt 1 ]; then
    printf 'Fuzz regression failed (%s): %s\n' "$status" "$seed" >&2
    cat bin/fuzz-last.log >&2
    exit "$status"
  fi
  count=$((count + 1))
done

if [ "$count" -eq 0 ]; then
  printf 'No fuzz seeds found; run from the repository root.\n' >&2
  exit 2
fi

printf 'Passed %s fuzz seeds (accepted or rejected without a crash).\n' "$count"
