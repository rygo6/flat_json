////////////////////////////////////////////////////////////////////////////////
// @author: rygo6
// fuzz.cpp - Checks bounded input, relocation, and serialization invariants.
////////////////////////////////////////////////////////////////////////////////

// Copyright 2024 Mozilla Foundation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "FlatJson.hpp"

using namespace Flat;

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void* Xmalloc(size_t size)
{
  void* result = malloc(size);
  if (!result)
    abort();
  return result;
}

static size_t OutputCapacity(size_t size)
{
  if (size > (SIZE_MAX - 4096) / 128)
    abort();
  return size * 128 + 4096;
}

static bool IsParseStatus(Result status)
{
  return status == SUCCESS || status == ERROR_MALFORMED || status == ABSENT_VALUE || status == ERROR_INSUFFICIENT_SPACE;
}

static Result ProbeParse(const char* pData, size_t size)
{
  FixedArena<1024 * 1024> bufferStorage;
  Arena buffer           = bufferStorage;
  const Node* bufferRoot = nullptr;
  Result status    = ParseJSON(pData, size, &buffer, &bufferRoot);
  if (!IsParseStatus(status) || (status == SUCCESS) != (bufferRoot != nullptr))
    abort();
  return status;
}

static void ProbeSlices(const char* pData, size_t size)
{
  size_t cuts[] = {0, size / 4, size / 2, size * 3 / 4, size ? size - 1 : 0, size};
  for (size_t i = 0; i < sizeof(cuts) / sizeof(cuts[0]); ++i) {
    if (i && cuts[i] == cuts[i - 1])
      continue;
    ProbeParse(pData, cuts[i]);
    ProbeParse(pData + size - cuts[i], cuts[i]);
  }
}

static void ProbeMutations(const char* pData, size_t size)
{
  static const unsigned char replacements[] = {0, 1, '"', '\\', '[', '}', 0x80, 0xff};
  if (!size)
    return;
  char* pMutation    = (char*)Xmalloc(size + 1);
  size_t positions[] = {0, size / 3, size / 2, size - 1};
  for (size_t i = 0; i < sizeof(positions) / sizeof(positions[0]); ++i) {
    size_t position = positions[i];
    if (i && position == positions[i - 1])
      continue;
    for (size_t j = 0; j < sizeof(replacements); ++j) {
      memcpy(pMutation, pData, size);
      pMutation[position] = (char)replacements[j];
      ProbeParse(pMutation, size);
    }

    memcpy(pMutation, pData, position);
    memcpy(pMutation + position, pData + position + 1, size - position - 1);
    ProbeParse(pMutation, size - 1);

    memcpy(pMutation, pData, position);
    pMutation[position] = (char)0xff;
    memcpy(pMutation + position + 1, pData + position, size - position);
    ProbeParse(pMutation, size + 1);
  }
  free(pMutation);
}

static char* SerializeChecked(const Node& json, bool pretty, size_t capacity, size_t* pSize)
{
  static constexpr size_t GuardSize = 32;
  if (capacity > SIZE_MAX - GuardSize * 2)
    abort();
  unsigned char* pStorage = (unsigned char*)Xmalloc(GuardSize + capacity + GuardSize);
  memset(pStorage, 0xa5, GuardSize);
  memset(pStorage + GuardSize + capacity, 0xa5, GuardSize);
  char* pOutput = (char*)pStorage + GuardSize;
  Span<char> output(capacity, pOutput);
  Result status = pretty ? json.ToStringPretty(output) : json.ToString(output);
  if (status != SUCCESS)
    abort();
  for (size_t i = 0; i < GuardSize; ++i) {
    if (pStorage[i] != 0xa5 || pStorage[GuardSize + capacity + i] != 0xa5)
      abort();
  }
  const char* pEnd = (const char*)memchr(pOutput, '\0', capacity);
  if (!pEnd)
    abort();
  *pSize        = (size_t)(pEnd - pOutput);
  char* pResult = (char*)Xmalloc(*pSize + 1);
  memcpy(pResult, pOutput, *pSize + 1);
  free(pStorage);
  return pResult;
}

static void VerifyRoundTrip(const char* pText, size_t size, String canonical, String pretty)
{
  FixedArena<1024 * 1024> bufferStorage;
  Arena buffer           = bufferStorage;
  const Node* bufferRoot = nullptr;
  if (ParseJSON(pText, size, &buffer, &bufferRoot) != SUCCESS)
    abort();

  size_t compactSize;
  char* pCompact = SerializeChecked(*bufferRoot, false, OutputCapacity(size), &compactSize);
  if (compactSize != canonical.size || memcmp(pCompact, canonical.data, compactSize))
    abort();
  free(pCompact);

  size_t prettySize;
  char* pSecondPretty = SerializeChecked(*bufferRoot, true, OutputCapacity(size), &prettySize);
  if (prettySize != pretty.size || memcmp(pSecondPretty, pretty.data, prettySize))
    abort();
  free(pSecondPretty);
}

static void VerifyRelocation(const FixedArena<1024 * 1024>& sourceStorage, const Arena& source, const Node* pSourceRoot, String canonical)
{
  if (source.Used() > sizeof(sourceStorage.bytes) || !pSourceRoot)
    abort();
  ptrdiff_t rootOffset = (const char*)pSourceRoot - (const char*)sourceStorage.bytes;
  if (rootOffset < 0 || (size_t)rootOffset >= sizeof(sourceStorage.bytes))
    abort();

  FixedArena<1024 * 1024> relocatedStorage;
  Arena relocated   = relocatedStorage;
  relocated.offset        = source.offset;
  relocated.capacity      = source.capacity;
  u32 documentStart = source.capacity - source.Used();
  memcpy(relocatedStorage.bytes + documentStart, sourceStorage.bytes + documentStart, source.Used());
  const Node* relocatedRoot = (const Node*)(relocatedStorage.bytes + rootOffset);

  size_t size;
  char* pRelocated = SerializeChecked(*relocatedRoot, false, OutputCapacity(canonical.size), &size);
  if (size != canonical.size || memcmp(pRelocated, canonical.data, size))
    abort();
  free(pRelocated);
}

int main()
{
  size_t n = 0;
  size_t c = 4096;
  char* s  = (char*)Xmalloc(c);
  size_t got;
  while ((got = fread(s + n, 1, c - n, stdin)) > 0) {
    n += got;
    if (n == c) {
      c *= 2;
      char* s2 = (char*)Xmalloc(c);
      memcpy(s2, s, n);
      free(s);
      s = s2;
    }
  }

  ProbeSlices(s, n);
  ProbeMutations(s, n);

  FixedArena<1024 * 1024> aStorage;
  Arena a           = aStorage;
  const Node* aRoot = nullptr;
  size_t estimate         = EstimateSize(s, n);
  if (estimate != SIZE_MAX && estimate <= sizeof(aStorage.bytes))
    a.capacity = estimate;
  Result status = ParseJSON(s, n, &a, &aRoot);
  if (status == ERROR_INSUFFICIENT_SPACE && estimate != SIZE_MAX && estimate <= sizeof(aStorage.bytes)) {
    a.capacity = sizeof(aStorage.bytes);
    if (ParseJSON(s, n, &a, &aRoot) == SUCCESS)
      abort();
  }
  if (status != SUCCESS) {
    free(s);
    puts(string_Result(status));
    return 1;
  }

  if (estimate == SIZE_MAX)
    abort();

  const Node* pJson = aRoot;
  size_t capacity         = OutputCapacity(n);
  size_t compactSize;
  char* pCompact = SerializeChecked(*pJson, false, capacity, &compactSize);
  size_t prettySize;
  char* pPretty = SerializeChecked(*pJson, true, capacity, &prettySize);

  VerifyRoundTrip(pCompact, compactSize, {compactSize, pCompact}, {prettySize, pPretty});
  VerifyRoundTrip(pPretty, prettySize, {compactSize, pCompact}, {prettySize, pPretty});
  VerifyRelocation(aStorage, a, aRoot, {compactSize, pCompact});

  char* pWhitespace = (char*)Xmalloc(compactSize + 3);
  pWhitespace[0]    = '\n';
  memcpy(pWhitespace + 1, pCompact, compactSize);
  pWhitespace[compactSize + 1] = '\t';
  pWhitespace[compactSize + 2] = '\0';
  VerifyRoundTrip(pWhitespace, compactSize + 2, {compactSize, pCompact}, {prettySize, pPretty});

  puts(pPretty);
  free(pWhitespace);
  free(pPretty);
  free(pCompact);
  free(s);
}
