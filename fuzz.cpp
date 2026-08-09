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

int
main(int argc, char* argv[])
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
    flat::HeapArena a;
    const flat::Json* pJson;
    flat::Json::Status status = flat::Json::Parse(s, n, a, &pJson);
    free(s);
    if (status != flat::Json::SUCCESS) {
        puts(flat::Json::StatusToString(status));
        return 1;
    }
    const char* pText;
    status = pJson->ToStringPretty(a, &pText);
    if (status != flat::Json::SUCCESS) {
        puts(flat::Json::StatusToString(status));
        return 1;
    }
    puts(pText);
}
