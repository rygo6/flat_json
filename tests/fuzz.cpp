// -*- mode:c++;indent-tabs-mode:nil;c-basic-offset:4;coding:utf-8 -*-
// vi: set et ft=cpp ts=4 sts=4 sw=4 fenc=utf-8 :vi
//
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

#include "flat_json.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void*
xmalloc(size_t size)
{
    void* result = malloc(size);
    if (!result)
        abort();
    return result;
}

static size_t
output_capacity(size_t size)
{
    if (size > (SIZE_MAX - 4096) / 128)
        abort();
    return size * 128 + 4096;
}

static bool
is_parse_status(flat::Json::Status status)
{
    return status == flat::Json::SUCCESS ||
           status == flat::Json::MALFORMED ||
           status == flat::Json::ABSENT_VALUE ||
           status == flat::Json::INSUFFICIENT_SPACE;
}

static flat::Json::Status
probe_parse(const char* pData, size_t size)
{
    flat::FixedJsonBuffer<1024 * 1024> buffer;
    flat::Json::Status status = flat::Json::Parse(pData, size, &buffer);
    if (!is_parse_status(status) || (status == flat::Json::SUCCESS) != (buffer.pRoot != nullptr))
        abort();
    return status;
}

static void
probe_slices(const char* pData, size_t size)
{
    size_t cuts[] = { 0, size / 4, size / 2, size * 3 / 4, size ? size - 1 : 0, size };
    for (size_t i = 0; i < sizeof(cuts) / sizeof(cuts[0]); ++i) {
        if (i && cuts[i] == cuts[i - 1])
            continue;
        probe_parse(pData, cuts[i]);
        probe_parse(pData + size - cuts[i], cuts[i]);
    }
}

static void
probe_mutations(const char* pData, size_t size)
{
    static const unsigned char replacements[] = { 0, 1, '"', '\\', '[', '}', 0x80, 0xff };
    if (!size)
        return;
    char* pMutation = (char*)xmalloc(size + 1);
    size_t positions[] = { 0, size / 3, size / 2, size - 1 };
    for (size_t i = 0; i < sizeof(positions) / sizeof(positions[0]); ++i) {
        size_t position = positions[i];
        if (i && position == positions[i - 1])
            continue;
        for (size_t j = 0; j < sizeof(replacements); ++j) {
            memcpy(pMutation, pData, size);
            pMutation[position] = (char)replacements[j];
            probe_parse(pMutation, size);
        }

        memcpy(pMutation, pData, position);
        memcpy(pMutation + position, pData + position + 1, size - position - 1);
        probe_parse(pMutation, size - 1);

        memcpy(pMutation, pData, position);
        pMutation[position] = (char)0xff;
        memcpy(pMutation + position + 1, pData + position, size - position);
        probe_parse(pMutation, size + 1);
    }
    free(pMutation);
}

static char*
serialize_checked(const flat::Json& json, bool pretty, size_t capacity, size_t* pSize)
{
    static constexpr size_t GuardSize = 32;
    if (capacity > SIZE_MAX - GuardSize * 2)
        abort();
    unsigned char* pStorage = (unsigned char*)xmalloc(GuardSize + capacity + GuardSize);
    memset(pStorage, 0xa5, GuardSize);
    memset(pStorage + GuardSize + capacity, 0xa5, GuardSize);
    char* pOutput = (char*)pStorage + GuardSize;
    flat::JsonSpan<char> output(capacity, pOutput);
    flat::Json::Status status = pretty ? json.ToStringPretty(output) : json.ToString(output);
    if (status != flat::Json::SUCCESS)
        abort();
    for (size_t i = 0; i < GuardSize; ++i) {
        if (pStorage[i] != 0xa5 || pStorage[GuardSize + capacity + i] != 0xa5)
            abort();
    }
    const char* pEnd = (const char*)memchr(pOutput, '\0', capacity);
    if (!pEnd)
        abort();
    *pSize = (size_t)(pEnd - pOutput);
    char* pResult = (char*)xmalloc(*pSize + 1);
    memcpy(pResult, pOutput, *pSize + 1);
    free(pStorage);
    return pResult;
}

static void
verify_round_trip(const char* pText, size_t size, const char* pCanonical, const char* pPretty)
{
    flat::FixedJsonBuffer<1024 * 1024> buffer;
    if (flat::Json::Parse(pText, size, &buffer) != flat::Json::SUCCESS)
        abort();

    size_t compactSize;
    char* pCompact = serialize_checked(*buffer.pRoot, false, output_capacity(size), &compactSize);
    if (strcmp(pCompact, pCanonical))
        abort();
    free(pCompact);

    size_t prettySize;
    char* pSecondPretty = serialize_checked(*buffer.pRoot, true, output_capacity(size), &prettySize);
    if (strcmp(pSecondPretty, pPretty))
        abort();
    free(pSecondPretty);
}

static void
verify_relocation(const flat::FixedJsonBuffer<1024 * 1024>& source, const char* pCanonical)
{
    if (source.back > sizeof(source.bytes) || !source.pRoot)
        abort();
    ptrdiff_t rootOffset = (const char*)source.pRoot - source.bytes;
    if (rootOffset < 0 || (size_t)rootOffset >= sizeof(source.bytes))
        abort();

    flat::FixedJsonBuffer<1024 * 1024> relocated;
    relocated.used = source.used;
    relocated.back = source.back;
    memcpy(relocated.bytes + source.back, source.bytes + source.back, sizeof(source.bytes) - source.back);
    relocated.pRoot = (const flat::Json*)(relocated.bytes + rootOffset);

    size_t size;
    char* pRelocated = serialize_checked(*relocated.pRoot, false,
                                         output_capacity(strlen(pCanonical)), &size);
    if (strcmp(pRelocated, pCanonical))
        abort();
    free(pRelocated);
}

int
main()
{
    size_t n = 0;
    size_t c = 4096;
    char* s = (char*)xmalloc(c);
    size_t got;
    while ((got = fread(s + n, 1, c - n, stdin)) > 0) {
        n += got;
        if (n == c) {
            c *= 2;
            char* s2 = (char*)xmalloc(c);
            memcpy(s2, s, n);
            free(s);
            s = s2;
        }
    }

    probe_slices(s, n);
    probe_mutations(s, n);

    flat::FixedJsonBuffer<1024 * 1024> a;
    size_t estimate = flat::Json::EstimateSize(s, n);
    if (estimate != SIZE_MAX && estimate <= sizeof(a.bytes))
        a.back = estimate;
    flat::Json::Status status = flat::Json::Parse(s, n, &a);
    if (status == flat::Json::INSUFFICIENT_SPACE && estimate != SIZE_MAX && estimate <= sizeof(a.bytes)) {
        a.back = sizeof(a.bytes);
        if (flat::Json::Parse(s, n, &a) == flat::Json::SUCCESS)
            abort();
    }
    if (status != flat::Json::SUCCESS) {
        free(s);
        puts(flat::Json::StatusToString(status));
        return 1;
    }

    if (estimate == SIZE_MAX)
        abort();

    const flat::Json* pJson = a.pRoot;
    size_t capacity = output_capacity(n);
    size_t compactSize;
    char* pCompact = serialize_checked(*pJson, false, capacity, &compactSize);
    size_t prettySize;
    char* pPretty = serialize_checked(*pJson, true, capacity, &prettySize);

    verify_round_trip(pCompact, compactSize, pCompact, pPretty);
    verify_round_trip(pPretty, prettySize, pCompact, pPretty);
    verify_relocation(a, pCompact);

    char* pWhitespace = (char*)xmalloc(compactSize + 3);
    pWhitespace[0] = '\n';
    memcpy(pWhitespace + 1, pCompact, compactSize);
    pWhitespace[compactSize + 1] = '\t';
    pWhitespace[compactSize + 2] = '\0';
    verify_round_trip(pWhitespace, compactSize + 2, pCompact, pPretty);

    puts(pPretty);
    free(pWhitespace);
    free(pPretty);
    free(pCompact);
    free(s);
}
