#!/bin/sh
set -eu

dependencyRoot=${FLAT_JSON_BENCH_DEPS:-${TMPDIR:-/tmp}/flat-json-benchmark-deps}
mkdir -p "$dependencyRoot"

clone_at()
{
  name=$1
  repository=$2
  revision=$3
  destination=$dependencyRoot/$name
  if [ ! -d "$destination/.git" ]; then
    git clone --filter=blob:none "$repository" "$destination"
  fi
  if ! git -C "$destination" cat-file -e "$revision^{commit}" 2>/dev/null; then
    git -C "$destination" fetch --depth 1 origin "$revision"
  fi
  currentRevision=$(git -C "$destination" rev-parse HEAD 2>/dev/null || true)
  if [ "$currentRevision" != "$revision" ]; then
    git -C "$destination" checkout --detach "$revision"
  fi
}

clone_at jart-json https://github.com/jart/json.cpp.git ccb9195e6abe9eb060e338a04f43e8d2e67b45ea
clone_at nlohmann-json https://github.com/nlohmann/json.git 21af527e756435701f23e01aa8ea8dab6e050c90
clone_at flatjson https://github.com/niXman/flatjson.git b46e20da85c9ed550e55c55d6d06fb5f1edf5572
clone_at sajson https://github.com/chadaustin/sajson.git 68fe32ed6bcb5ac026671d6eadac9024a21c8b05
clone_at cjson https://github.com/DaveGamble/cJSON.git fb16e5cf358798aabb049655975cde8427101056
clone_at jsmn https://github.com/zserge/jsmn.git 25647e692c7906b96ffd2b05ca54c097948e879c

llamafileRevision=880894d5c2d639a439db9f7fcf49960b5f0b1eda
llamafileDirectory=$dependencyRoot/llamafile
if [ ! -d "$llamafileDirectory/.git" ]; then
  git clone --filter=blob:none --no-checkout https://github.com/Mozilla-Ocho/llamafile.git "$llamafileDirectory"
fi
if ! git -C "$llamafileDirectory" cat-file -e "$llamafileRevision^{commit}" 2>/dev/null; then
  git -C "$llamafileDirectory" fetch --depth 1 origin "$llamafileRevision"
fi
git -C "$llamafileDirectory" sparse-checkout set --no-cone \
  /llamafile/server/json.cpp \
  /llamafile/server/json.h \
  /llamafile/server/utils.h \
  /double-conversion/
currentRevision=$(git -C "$llamafileDirectory" rev-parse HEAD 2>/dev/null || true)
if [ "$currentRevision" != "$llamafileRevision" ]; then
  git -C "$llamafileDirectory" checkout --detach "$llamafileRevision"
fi

printf '%s\n' "$dependencyRoot"
