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

#include "flat_file.hpp"
#include "flat_json.hpp"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ARRAYLEN(A) \
    ((sizeof(A) / sizeof(*(A))) / ((unsigned)!(sizeof(A) % sizeof(*(A)))))

#define STRING(sl) sl, sizeof(sl) - 1

using flat::Json;

static const char kHuge[] = R"([
    "JSON Test Pattern pass1",
    {"object with 1 member":["array with 1 element"]},
    {},
    [],
    -42,
    true,
    false,
    null,
    {
        "integer": 1234567890,
        "real": -9876.543210,
        "e": 0.123456789e-12,
        "E": 1.234567890E+34,
        "":  23456789012E66,
        "zero": 0,
        "one": 1,
        "space": " ",
        "quote": "\"",
        "backslash": "\\",
        "controls": "\b\f\n\r\t",
        "slash": "/ & \/",
        "alpha": "abcdefghijklmnopqrstuvwyz",
        "ALPHA": "ABCDEFGHIJKLMNOPQRSTUVWYZ",
        "digit": "0123456789",
        "0123456789": "digit",
        "special": "`1~!@#$%^&*()_+-={':[,]}|;.</>?",
        "hex": "\u0123\u4567\u89AB\uCDEF\uabcd\uef4A",
        "true": true,
        "false": false,
        "null": null,
        "array":[  ],
        "object":{  },
        "address": "50 St. James Street",
        "url": "http://www.JSON.org/",
        "comment": "// /* <!-- --",
        "# -- --> */": " ",
        " s p a c e d " :[1,2 , 3

,

4 , 5        ,          6           ,7        ],"compact":[1,2,3,4,5,6,7],
        "jsontext": "{\"object with 1 member\":[\"array with 1 element\"]}",
        "quotes": "&#34; \u0022 %22 0x22 034 &#x22;",
        "\/\\\"\uCAFE\uBABE\uAB98\uFCDE\ubcda\uef4A\b\f\n\r\t`1~!@#$%^&*()_+-=[]{}|;:',./<>?"
: "A key can be any string"
    },
    0.5 ,98.6
,
99.44
,

1066,
1e1,
0.1e1,
1e-1,
1e00,2e+00,2e-00
,"rosebud"])";

#define BENCH(ITERATIONS, WORK_PER_RUN, CODE) \
    do { \
        struct timespec start, end; \
        clock_gettime(CLOCK_MONOTONIC, &start); \
        for (int __i = 0; __i < ITERATIONS; ++__i) { \
            __asm__ volatile("" ::: "memory"); \
            CODE; \
        } \
        clock_gettime(CLOCK_MONOTONIC, &end); \
        long long duration = (end.tv_sec - start.tv_sec) * 1000000000LL + \
                             (end.tv_nsec - start.tv_nsec); \
        long long work = (WORK_PER_RUN) * (ITERATIONS); \
        double nanos = (duration + work - 1) / (double)work; \
        printf("%10g ns %2dx %s\n", nanos, (ITERATIONS), #CODE); \
    } while (0)

void
object_test()
{
    char output[1024];
    if (flat::WriteJson(flat::JsonObject({ { "content", "hello" } }), output) != Json::SUCCESS ||
        strcmp(output, "{\"content\":\"hello\"}"))
        exit(1);
}

void
direct_serialization_test()
{
    char output[1024];
    if (flat::WriteJson(flat::JsonObject({ { "answer", 42 } }), output) != Json::SUCCESS)
        exit(17);
    if (strcmp(output, "{\"answer\":42}"))
        exit(18);

    char round_trip_output[1024];
    if (flat::WriteJson(
          flat::JsonObject({ { "model", "gpt-5" }, { "stream", true } }),
          round_trip_output) != Json::SUCCESS)
        exit(40);
    if (strcmp(round_trip_output, "{\"model\":\"gpt-5\",\"stream\":true}"))
        exit(41);

    flat::FixedJsonBuffer<512> parse_arena;
    Json::Status status = Json::Parse(round_trip_output, strlen(round_trip_output), &parse_arena);
    if (status != Json::SUCCESS)
        exit(44);
    const Json* pJson = parse_arena.Root();
    flat::JsonString model = (*pJson)["model"].GetString();
    if (model.size != 5 || strcmp(model.pData, "gpt-5") || !(*pJson)["stream"].GetBool())
        exit(44);
    char parsed_output[1024];
    if (flat::WriteJson(*pJson, parsed_output) != Json::SUCCESS)
        exit(45);
    if (strcmp(parsed_output, round_trip_output))
        exit(45);

    char small_output[5];
    if (flat::WriteJson(flat::JsonObject({ { "too", "large" } }), small_output) != Json::INSUFFICIENT_SPACE)
        exit(53);
    if (flat::WriteJson(flat::JsonValue(nullptr), small_output) != Json::SUCCESS ||
        strcmp(small_output, "null"))
        exit(54);
}

void
public_soft_failure_test()
{
    flat::FixedJsonBuffer<64> arena;

    if (Json::Parse("null", (flat::FixedJsonBuffer<64>*)nullptr) != Json::INVALID_ARGUMENT)
        exit(200);
    if (Json::Parse((const char*)nullptr, 1, &arena) != Json::INVALID_ARGUMENT || arena.Root())
        exit(202);
    if (Json::Parse((const char*)nullptr, 0, &arena) != Json::ABSENT_VALUE || arena.Root())
        exit(203);
    Json::Status smallArenaStatus = Json::Parse("1.00000000000000011102230246251565404236316680908203125", &arena);
    if (smallArenaStatus != Json::INSUFFICIENT_SPACE || arena.Root()) {
        fprintf(stderr, "small numeric arena returned %s\n", Json::StatusToString(smallArenaStatus));
        exit(210);
    }
    if (Json::Parse("null", &arena) != Json::SUCCESS || !arena.Root() || !arena.Root()->IsNull())
        exit(211);

    flat::FixedJsonBuffer<24> insufficientStringArena;
    if (Json::Parse(R"("123456789")", &insufficientStringArena) != Json::INSUFFICIENT_SPACE || insufficientStringArena.Root())
        exit(212);
    flat::FixedJsonBuffer<40> insufficientEscapeArena;
    if (Json::Parse(R"("\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061")",
                    &insufficientEscapeArena) != Json::INSUFFICIENT_SPACE || insufficientEscapeArena.Root())
        exit(213);
    flat::FixedJsonBuffer<31> insufficientArrayArena;
    if (Json::Parse("[0]", &insufficientArrayArena) != Json::INSUFFICIENT_SPACE || insufficientArrayArena.Root())
        exit(214);
    flat::FixedJsonBuffer<64> insufficientObjectArena;
    if (Json::Parse(R"({"a":0})", &insufficientObjectArena) != Json::INSUFFICIENT_SPACE || insufficientObjectArena.Root())
        exit(215);

    if (Json::Parse(flat::FileMap("bin/file-does-not-exist.json"), &arena) != Json::IO_ERROR || arena.Root())
        exit(207);
    flat::FileMap invalidInput(nullptr);
    if (invalidInput.IsValid())
        exit(209);
    if (flat::WriteJson(flat::JsonValue(nullptr), flat::WritableFile("bin/missing/file.json")) != Json::IO_ERROR)
        exit(208);
    flat::WritableFile invalidFile("bin/missing/file.json");
    if (invalidFile.IsValid())
        exit(217);
    flat::WritableFileMap invalidOutput(4, "bin/missing/file-not-created.json");
    if (invalidOutput.IsValid())
        exit(216);

}

void
file_map_round_trip_test()
{
    static constexpr char Path[] = "file_map_round_trip_test.json";
    static constexpr char Expected[] = "{\"model\":\"gpt-5\",\"stream\":true,\"number\":3.14,\"escaped\":\"line\\n\"}";

    {
        flat::WritableFileMap output(4, Path);
        if (!output.IsValid())
            exit(83);
        memcpy(output.data, "null", 4);
    }
    {
        flat::FileMap input(Path);
        if (!input.IsValid() || input.size != 4 || memcmp(input.data, "null", 4))
            exit(84);
    }

    if (flat::WriteJson(
          flat::JsonObject({ { "model", "gpt-5" }, { "stream", true }, { "number", 3.14 }, { "escaped", "line\n" } }),
          flat::WritableFile(Path)) != Json::SUCCESS)
        exit(80);

    {
        flat::FileMap input(Path);
        if (!input.IsValid() || input.size != sizeof(Expected) - 1 ||
            memcmp(input.data, Expected, input.size))
            exit(81);
    }

    flat::FixedJsonBuffer<512> arena;
    if (Json::Parse(flat::FileMap(Path), &arena) != Json::SUCCESS)
        exit(82);
    const Json* pJson = arena.Root();
    flat::JsonString model = (*pJson)["model"].GetString();
    if (model.size != 5 || strcmp(model.pData, "gpt-5") || !(*pJson)["stream"].GetBool() ||
        (*pJson)["number"].GetDouble() != 3.14 || strcmp((*pJson)["escaped"].GetString().pData, "line\n"))
        exit(82);
    unlink(Path);
}

void
writable_file_round_trip_test()
{
    static constexpr char Path[] = "writable_file_round_trip_test.json";
    static constexpr char Text[] = R"({"name":"flat-json","enabled":true,"values":[-1,0,42,3.5],"nested":{"escaped":"line\n","none":null}})";

    flat::FixedJsonBuffer<2048> sourceArena;
    if (Json::Parse(Text, &sourceArena) != Json::SUCCESS)
        exit(219);

    {
        flat::WritableFile output(Path);
        if (!output.IsValid() || flat::WriteJson(*sourceArena.Root(), output) != Json::SUCCESS)
            exit(220);
    }

    {
        flat::FileMap input(Path);
        if (!input.IsValid() || input.size != sizeof(Text) - 1 || memcmp(input.data, Text, input.size))
            exit(221);
    }

    flat::FixedJsonBuffer<2048> destinationArena;
    if (Json::Parse(flat::FileMap(Path), &destinationArena) != Json::SUCCESS)
        exit(222);
    const Json* pJson = destinationArena.Root();
    if (strcmp((*pJson)["name"].GetString().pData, "flat-json") ||
        !(*pJson)["enabled"].GetBool() ||
        (*pJson)["values"][0].GetLong() != -1 ||
        (*pJson)["values"][2].GetLong() != 42 ||
        (*pJson)["values"][3].GetDouble() != 3.5 ||
        strcmp((*pJson)["nested"]["escaped"].GetString().pData, "line\n") ||
        !(*pJson)["nested"]["none"].IsNull())
        exit(223);

    char pJsonText[4096];
    if (flat::WriteJson(*pJson, pJsonText) != Json::SUCCESS || strcmp(pJsonText, Text))
        exit(224);
    unlink(Path);
}

void
large_object_index_test()
{
    char text[8192];
    char* pCursor = text;
    *pCursor++ = '{';
    for (int key = 100; key >= 0; --key) {
        size_t remaining = sizeof(text) - (size_t)(pCursor - text);
        int count = snprintf(pCursor, remaining, "%s\"key%03d\":%d", key == 100 ? "" : ",", key, key);
        if (count < 0 || (size_t)count >= remaining)
            exit(55);
        pCursor += count;
    }
    *pCursor++ = '}';
    *pCursor = '\0';

    flat::FixedJsonBuffer<32768> arena;
    if (Json::Parse(text, strlen(text), &arena) != Json::SUCCESS)
        exit(55);
    const Json* pJson = arena.Root();
    if (pJson->GetSize() != 101 ||
        !pJson->Contains("key100") ||
        !pJson->Contains("key000") ||
        pJson->Contains("missing") ||
        !pJson->HasKey("key100") ||
        !pJson->HasKey("key000") ||
        pJson->HasKey("missing") ||
        (*pJson)["key100"].GetLong() != 100 ||
        (*pJson)["key050"].GetLong() != 50 ||
        (*pJson)["key000"].GetLong() != 0)
        exit(55);

    char output[8192];
    if (pJson->ToString(output) != Json::SUCCESS || strcmp(output, text))
        exit(56);
}

void
medium_object_lookup_test()
{
    char text[2048];
    char* pCursor = text;
    *pCursor++ = '{';
    for (int key = 0; key < 32; ++key) {
        size_t remaining = sizeof(text) - (size_t)(pCursor - text);
        int count = snprintf(pCursor, remaining, "%s\"key%02d\":%d", key ? "," : "", key, key);
        if (count < 0 || (size_t)count >= remaining)
            exit(223);
        pCursor += count;
    }
    memcpy(pCursor, ",\"target\":31337}", sizeof(",\"target\":31337}"));

    flat::FixedJsonBuffer<8192> arena;
    if (Json::Parse(text, strlen(text), &arena) != Json::SUCCESS)
        exit(224);
    const Json* pJson = arena.Root();
    if ((*pJson)["key00"].GetLong() != 0 ||
        (*pJson)["key15"].GetLong() != 15 ||
        (*pJson)["key31"].GetLong() != 31 ||
        (*pJson)["target"].GetLong() != 31337 ||
        pJson->Contains("missing"))
        exit(225);
}

static uint64_t
DoubleBits(double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

void
numeric_arena_test()
{
    static constexpr char FilePath[] = "numeric_file_output_test.json";
    static const double values[] = {
        0.0,
        -0.0,
        0.1,
        1e-7,
        1e-6,
        1e20,
        1e21,
        DBL_MIN,
        DBL_MAX,
        4.9406564584124654e-324,
        2.2250738585072014e-308,
        9007199254740991.0,
        3.5844466002796428e+298,
        1.7039356390957979e-287,
        INFINITY,
        -INFINITY,
    };
    for (size_t i = 0; i < ARRAYLEN(values); ++i) {
        char text[8192];
        if (flat::WriteJson(values[i], text) != Json::SUCCESS)
            exit(27);

        flat::FixedJsonBuffer<8192> parse_arena;
        Json::Status status = Json::Parse(text, strlen(text), &parse_arena);
        if (status != Json::SUCCESS)
            exit(28);
        const Json* pJson = parse_arena.Root();
        double expected = values[i] == 0.0 ? 0.0 : values[i];
        if (DoubleBits(pJson->GetNumber()) != DoubleBits(expected))
            exit(29);

        if (flat::WriteJson(values[i], flat::WritableFile(FilePath)) != Json::SUCCESS)
            exit(31);
        flat::FileMap file(FilePath);
        if (!file.IsValid() || file.size != strlen(text) || memcmp(file.data, text, file.size))
            exit(32);
    }

    char special[4096];
    if (flat::WriteJson(
          flat::JsonArray({ NAN, INFINITY, -INFINITY, 1.25f }),
          special) != Json::SUCCESS)
        exit(30);
    if (strcmp(special, "[null,1e5000,-1e5000,1.25]"))
        exit(30);

    if (flat::WriteJson(LLONG_MIN, flat::WritableFile(FilePath)) != Json::SUCCESS)
        exit(33);
    {
        flat::FileMap file(FilePath);
        if (!file.IsValid() || file.size != 20 || memcmp(file.data, "-9223372036854775808", 20))
            exit(34);
    }
    unlink(FilePath);
}

void
fast_decimal_differential_test()
{
    static const uint64_t boundaries[] = {
        1,
        5,
        9,
        123456789,
        (1ull << 53) - 1,
        1ull << 53,
        9999999999999999999ull,
    };
    uint64_t random = 0x9e3779b97f4a7c15ull;
    for (int exponent = -64; exponent <= 38; ++exponent) {
        for (size_t index = 0; index < ARRAYLEN(boundaries) + 16; ++index) {
            uint64_t significand;
            if (index < ARRAYLEN(boundaries)) {
                significand = boundaries[index];
            } else {
                random = random * 6364136223846793005ull + 1442695040888963407ull;
                significand = random % 9999999999999999999ull + 1;
            }
            char text[64];
            int size = snprintf(text, sizeof(text), "%llue%d",
                                (unsigned long long)significand, exponent);
            char* pConvertedEnd;
            double expected = strtod(text, &pConvertedEnd);
            if (pConvertedEnd != text + size)
                exit(31);

            flat::FixedJsonBuffer<512> arena;
            if (Json::Parse(text, size, &arena) != Json::SUCCESS ||
                DoubleBits(arena.Root()->GetDouble()) != DoubleBits(expected))
                exit(32);
        }
    }
}

void
strict_string_test()
{
    struct Input {
        const char* data;
        size_t size;
    };
    static const Input invalid[] = {
        { STRING("[\"\\x00\"]") },
        { STRING("[\"a\0a\"]") },
        { STRING("[\"new\nline\"]") },
        { STRING("[\"\t\"]") },
        { STRING("[\"\x80\"]") },
        { STRING("[\"\xc0\x80\"]") },
        { STRING("[\"\xed\xa0\x80\"]") },
        { STRING("[\"\xf4\x90\x80\x80\"]") },
        { STRING("[\"\xe2\x82\"]") },
    };
    for (size_t i = 0; i < ARRAYLEN(invalid); ++i) {
        flat::FixedJsonBuffer<1024> arena;
        if (Json::Parse(invalid[i].data, invalid[i].size, &arena) != Json::MALFORMED || arena.Root())
            exit(31);
    }

    static const Input valid[] = {
        { STRING("[\"\\u0000\"]") },
        { STRING("[\"\\b\\f\\n\\r\\t\"]") },
        { STRING("[\"\xc2\x80\xe0\xa0\x80\xf0\x90\x80\x80\xf4\x8f\xbf\xbf\"]") },
        // Preserve the implementation-defined unmatched-surrogate behavior
        // recorded in README.md.
        { STRING("[\"\\uD800\"]") },
    };
    static const size_t valid_sizes[] = { 1, 5, 13, 6 };
    for (size_t i = 0; i < ARRAYLEN(valid); ++i) {
        flat::FixedJsonBuffer<1024> arena;
        if (Json::Parse(valid[i].data, valid[i].size, &arena) != Json::SUCCESS)
            exit(32);
        const Json* pJson = arena.Root();
        flat::JsonString string = (*pJson)[0].GetString();
        if (string.size != valid_sizes[i] || string[string.size] != '\0')
            exit(60);
    }
}

void
immutable_layout_test()
{
    flat::FixedJsonBuffer<2048> a;
    Json::Status status = Json::Parse(R"([1,[2,3],{"x":4,"s":"ok"}])", &a);
    if (status != Json::SUCCESS)
        exit(20);
    const Json* pJson = a.Root();
    flat::u32 root_offset = (flat::u32)((const char*)pJson - a.bytes);
    if ((const char*)pJson < a.bytes ||
        (const char*)pJson >= a.bytes + sizeof(a.bytes))
        exit(21);
    if ((*pJson)[0].GetLong() != 1 ||
        (*pJson)[1][1].GetLong() != 3 ||
        (*pJson)[2]["x"].GetLong() != 4)
        exit(22);
    if (!pJson->HasSize() || !pJson->HasIndex(0) || !pJson->HasIndex(2) ||
        pJson->HasIndex(3) || pJson->HasIndex(-1) || (*pJson)[0].HasSize() ||
        (*pJson)[0].HasIndex(0) || !(*pJson)[2].HasKey("x") || (*pJson)[2].HasKey("missing"))
        exit(59);
    if ((const char*)&(*pJson)[1] - (const char*)&(*pJson)[0] != sizeof(Json) ||
        (const char*)&(*pJson)[2] - (const char*)&(*pJson)[1] != sizeof(Json) ||
        (const char*)&(*pJson)[1][1] - (const char*)&(*pJson)[1][0] != -(ptrdiff_t)sizeof(Json))
        exit(57);
    const Json& array = pJson->GetArray();
    const Json& object = array[2].GetObject();
    if (&array != pJson || array.GetSize() != 3 ||
        &object != &array[2] || object.GetSize() != 2)
        exit(50);
    if (pJson->span != sizeof(a.bytes) - root_offset)
        exit(23);
    alignas(8) char relocated_storage[2048];
    memcpy(relocated_storage, pJson, pJson->span);
    const Json* relocated = (const Json*)relocated_storage;
    if ((*relocated)[0].GetLong() != 1 ||
        (*relocated)[1][1].GetLong() != 3 ||
        (*relocated)[2]["x"].GetLong() != 4 ||
        strcmp((*relocated)[2]["s"].GetString().pData, "ok"))
        exit(43);
    alignas(8) char relocated_array_storage[2048];
    memcpy(relocated_array_storage, &array[1], array[1].span);
    const Json* relocated_array = (const Json*)relocated_array_storage;
    if ((*relocated_array)[0].GetLong() != 2 || (*relocated_array)[1].GetLong() != 3)
        exit(58);
    char output[2048];
    if (pJson->ToString(output) != Json::SUCCESS)
        exit(24);
    if (strcmp(output, R"([1,[2,3],{"x":4,"s":"ok"}])"))
        exit(24);
    if (Json::Parse("[1,", &a) != Json::MALFORMED || a.Root() ||
        (*pJson)[2]["x"].GetLong() != 4)
        exit(26);
}

void
deep_test()
{
    char text[8192];
    if (flat::WriteJson(
          flat::JsonObject({ { "content",
                               flat::JsonArray({ flat::JsonArray({ flat::JsonArray(
                             { 0, 10, 20, 3.14, 40 }) }) }) } }),
          text) != Json::SUCCESS)
        exit(2);
    if (strcmp(text, "{\"content\":[[[0,10,20,3.14,40]]]}"))
        exit(2);
}

static flat::FixedJsonBuffer<65536> g_static_arena;

void
static_arena_test()
{
    char text[4096];
    if (flat::WriteJson(
          flat::JsonObject({ { "name", "static" },
                             { "values", flat::JsonArray({ 1, 2 }) } }),
          text) != Json::SUCCESS)
        exit(8);
    if (strcmp(text, "{\"name\":\"static\",\"values\":[1,2]}"))
        exit(8);
    Json::Status status = Json::Parse("{\"k\": [true, null, 3.5]}", &g_static_arena);
    if (status != Json::SUCCESS)
        exit(9);
    const Json* pJson = g_static_arena.Root();
    if (pJson->ToString(text) != Json::SUCCESS ||
        strcmp(text, "{\"k\":[true,null,3.5]}"))
        exit(13);
}

void
stack_arena_test()
{
    flat::FixedJsonBuffer<16384> a;
    Json::Status status = Json::Parse("[1, \"two\", {\"three\": 3}]", &a);
    if (status != Json::SUCCESS)
        exit(14);
    const Json* pJson = a.Root();
    char text[16384];
    if (pJson->ToString(text) != Json::SUCCESS ||
        strcmp(text, "[1,\"two\",{\"three\":3}]"))
        exit(15);
    if (strcmp((*pJson)[1].GetString().pData, "two"))
        exit(16);
}

void
parse_test()
{
    flat::FixedJsonBuffer<65536> a;
    Json::Status status = Json::Parse("{ \"content\":[[[0,10,20,3.14,40]]]}", &a);
    if (status != Json::SUCCESS)
        exit(3);
    const Json* pJson = a.Root();
    char text[65536];
    if (pJson->ToString(text) != Json::SUCCESS ||
        strcmp(text, "{\"content\":[[[0,10,20,3.14,40]]]}"))
        exit(4);
    if (pJson->ToStringPretty(text) != Json::SUCCESS ||
        strcmp(text,
               R"({"content": [[[0, 10, 20, 3.14, 40]]]})"))
        exit(5);
    status = Json::Parse("{ \"a\": 1, \"b\": [2,   3]}", &a);
    if (status != Json::SUCCESS)
        exit(6);
    pJson = a.Root();
    if (pJson->ToString(text) != Json::SUCCESS ||
        strcmp(text, R"({"a":1,"b":[2,3]})"))
        exit(6);
    if (pJson->ToStringPretty(text) != Json::SUCCESS ||
        strcmp(text,
               R"({
  "a": 1,
  "b": [2, 3]
})"))
        exit(7);
}

static const struct
{
    const char* before;
    const char* after;
} kRoundTrip[] = {

    // types
    { "0", "0" },
    { "[]", "[]" },
    { "{}", "{}" },
    { "0.1", "0.1" },
    { "\"\"", "\"\"" },
    { "[\"/\"]", "[\"/\"]" },
    { "[\"cafÃ©\"]", "[\"cafÃ©\"]" },
    { "null", "null" },
    { "true", "true" },
    { "false", "false" },

    // valid utf16 sequences
    { " [\"\\u0020\"] ", "[\" \"]" },
    { " [\"\\u00A0\"] ", "[\"\\u00a0\"]" },

    // when we encounter invalid utf16 sequences
    // we turn them into ascii
    { "[\"\\uDFAA\"]", "[\"\\\\uDFAA\"]" },
    { " [\"\\uDd1e\\uD834\"] ", "[\"\\\\uDd1e\\\\uD834\"]" },
    { " [\"\\ud800abc\"] ", "[\"\\\\ud800abc\"]" },
    { " [\"\\ud800\"] ", "[\"\\\\ud800\"]" },
    { " [\"\\uD800\\uD800\\n\"] ", "[\"\\\\uD800\\\\uD800\\n\"]" },
    { " [\"\\uDd1ea\"] ", "[\"\\\\uDd1ea\"]" },
    { " [\"\\uD800\\n\"] ", "[\"\\\\uD800\\n\"]" },

    // underflow and overflow
    { " [123.456e-789] ", "[0]" },
    { " [0."
      "4e0066999999999999999999999999999999999999999999999999999999999999999999"
      "9999999999999999999999999999999999999999999999999969999999006] ",
      "[1e5000]" },
    { " [1.5e+9999] ", "[1e5000]" },
    { " [-1.5e+9999] ", "[-1e5000]" },
    { " [-123123123123123123123123123123] ", "[-1.2312312312312312e+29]" },
};

// https://github.com/nst/JSONTestSuite/
static const struct
{
    Json::Status error;
    const char* json;
    size_t size;
} kJsonTestSuite[] = {
    { Json::MALFORMED, "" },
    { Json::MALFORMED, "[] []" },
    { Json::MALFORMED, "[nan]" },
    { Json::MALFORMED, "[-nan]" },
    { Json::MALFORMED, "[+NaN]" },
    { Json::MALFORMED,
      "{\"Extra value after close\": true} \"misplaced quoted value\"" },
    { Json::MALFORMED, "{\"Illegal expression\": 1 + 2}" },
    { Json::MALFORMED, "{\"Illegal invocation\": alert()}" },
    { Json::MALFORMED, "{\"Numbers cannot have leading zeroes\": 013}" },
    { Json::MALFORMED, "{\"Numbers cannot be hex\": 0x14}" },
    { Json::MALFORMED, "[\\naked]" },
    { Json::MALFORMED, "[\"Illegal backslash escape: \\017\"]" },
    { Json::MALFORMED,
      "[[[[[[[[[[[[[[[[[[[[\"Too deep\"]]]]]]]]]]]]]]]]]]]]" },
    { Json::MALFORMED, "{\"Missing colon\" null}" },
    { Json::MALFORMED, "{\"Double colon\":: null}" },
    { Json::MALFORMED, "{\"Comma instead of colon\", null}" },
    { Json::MALFORMED, "[\"Colon instead of comma\": false]" },
    { Json::MALFORMED, "[\"Bad value\", truth]" },
    { Json::MALFORMED, "[\'single quote\']" },
    { Json::MALFORMED,
      "[\"tab\\   character\\   in\\  string\\  \"]" },
    { Json::MALFORMED, "[\"line\\\nbreak\"]" },
    { Json::MALFORMED, "[0e]" },
    { Json::MALFORMED, "[\"Unclosed array\"" },
    { Json::MALFORMED, "[0e+]" },
    { Json::MALFORMED, "[0e+-1]" },
    { Json::MALFORMED, "{\"Comma instead if closing brace\": true," },
    { Json::MALFORMED, "[\"mismatch\"}" },
    { Json::MALFORMED, "{unquoted_key: \"keys must be quoted\"}" },
    { Json::MALFORMED, "[\"extra comma\",]" },
    { Json::MALFORMED, "[\"double extra comma\",,]" },
    { Json::MALFORMED, "[   , \"<-- missing value\"]" },
    { Json::MALFORMED, "[\"Comma after the close\"]," },
    { Json::MALFORMED, "[\"Extra close\"]]" },
    { Json::MALFORMED, "{\"Extra comma\": true,}" },
    { Json::MALFORMED, " {\"a\" " },
    { Json::MALFORMED, " {\"a\": " },
    { Json::MALFORMED, " {:\"b\" " },
    { Json::MALFORMED, " {\"a\" b} " },
    { Json::MALFORMED, " {key: 'value'} " },
    { Json::MALFORMED, " {\"a\":\"a\" 123} " },
    { Json::MALFORMED, " \x7b\xf0\x9f\x87\xa8\xf0\x9f\x87\xad\x7d " },
    { Json::MALFORMED, " {[: \"x\"} " },
    { Json::MALFORMED, " [1.8011670033376514H-308] " },
    { Json::MALFORMED, " [1.2a-3] " },
    { Json::MALFORMED, " [.123] " },
    { Json::MALFORMED, " [1e\xe5] " },
    { Json::MALFORMED, " [1ea] " },
    { Json::MALFORMED, " [-1x] " },
    { Json::MALFORMED, " [-.123] " },
    { Json::MALFORMED, " [-foo] " },
    { Json::MALFORMED, " [-Infinity] " },
    { Json::MALFORMED, " \x5b\x30\xe5\x5d " },
    { Json::MALFORMED, " \x5b\x31\x65\x31\xe5\x5d " },
    { Json::MALFORMED, " \x5b\x31\x32\x33\xe5\x5d " },
    { Json::MALFORMED,
      " \x5b\x2d\x31\x32\x33\x2e\x31\x32\x33\x66\x6f\x6f\x5d " },
    { Json::MALFORMED, " [0e+-1] " },
    { Json::MALFORMED, " [Infinity] " },
    { Json::MALFORMED, " [0x42] " },
    { Json::MALFORMED, " [0x1] " },
    { Json::MALFORMED, " [1+2] " },
    { Json::MALFORMED, " \x5b\xef\xbc\x91\x5d " },
    { Json::MALFORMED, " [NaN] " },
    { Json::MALFORMED, " [Inf] " },
    { Json::MALFORMED, " [9.e+] " },
    { Json::MALFORMED, " [1eE2] " },
    { Json::MALFORMED, " [1e0e] " },
    { Json::MALFORMED, " [1.0e-] " },
    { Json::MALFORMED, " [1.0e+] " },
    { Json::MALFORMED, " [0e] " },
    { Json::MALFORMED, " [0e+] " },
    { Json::MALFORMED, " [0E] " },
    { Json::MALFORMED, " [0E+] " },
    { Json::MALFORMED, " [0.3e] " },
    { Json::MALFORMED, " [0.3e+] " },
    { Json::MALFORMED, " [0.1.2] " },
    { Json::MALFORMED, " [.2e-3] " },
    { Json::MALFORMED, " [.-1] " },
    { Json::MALFORMED, " [-NaN] " },
    { Json::MALFORMED, " [+Inf] " },
    { Json::MALFORMED, " [+1] " },
    { Json::MALFORMED, " [++1234] " },
    { Json::MALFORMED, " [tru] " },
    { Json::MALFORMED, " [nul] " },
    { Json::MALFORMED, " [fals] " },
    { Json::MALFORMED, " [{} " },
    { Json::MALFORMED, "\n[1,\n1\n,1  " },
    { Json::MALFORMED, " [1, " },
    { Json::MALFORMED, " [\"\" " },
    { Json::MALFORMED, " [* " },
    { Json::MALFORMED,
      " \x5b\x22\x0b\x61\x22\x5c\x66\x5d " },
    { Json::MALFORMED, "[\"a\",\n4\n,1,1  " },
    { Json::MALFORMED, " [1:2] " },
    { Json::MALFORMED, " \x5b\xff\x5d " },
    { Json::MALFORMED, " \x5b\x78 " },
    { Json::MALFORMED, " [\"x\" " },
    { Json::MALFORMED, " [\"\": 1] " },
    { Json::MALFORMED, " [a\xe5] " },
    { Json::MALFORMED, " {\"x\", null} " },
    { Json::MALFORMED, " [\"x\", truth] " },
    { Json::MALFORMED, STRING("\x00") },
    { Json::MALFORMED, "\n[\"x\"]]" },
    { Json::MALFORMED, " [012] " },
    { Json::MALFORMED, " [-012] " },
    { Json::MALFORMED, " [1 000.0] " },
    { Json::MALFORMED, " [-01] " },
    { Json::MALFORMED, " [- 1] " },
    { Json::MALFORMED, " [-] " },
    { Json::MALFORMED, " {\"\xb9\":\"0\",} " },
    { Json::MALFORMED, " {\"x\"::\"b\"} " },
    { Json::MALFORMED, " [1,,] " },
    { Json::MALFORMED, " [1,] " },
    { Json::MALFORMED, " [1,,2] " },
    { Json::MALFORMED, " [,1] " },
    { Json::MALFORMED, " [ 3[ 4]] " },
    { Json::MALFORMED, " [1 true] " },
    { Json::MALFORMED, " [\"a\" \"b\"] " },
    { Json::MALFORMED, " [--2.] " },
    { Json::MALFORMED, " [1.] " },
    { Json::MALFORMED, " [2.e3] " },
    { Json::MALFORMED, " [2.e-3] " },
    { Json::MALFORMED, " [2.e+3] " },
    { Json::MALFORMED, " [0.e1] " },
    { Json::MALFORMED, " [-2.] " },
    { Json::MALFORMED, " \xef\xbb\xbf{} " },
    { Json::MALFORMED, STRING(" [\x00\"\x00\xe9\x00\"\x00]\x00 ") },
    { Json::MALFORMED, STRING(" \x00[\x00\"\x00\xe9\x00\"\x00] ") },
    { Json::SUCCESS, kHuge },
    { Json::SUCCESS,
      R"([[[[[[[[[[[[[[[[[[["Not too deep"]]]]]]]]]]]]]]]]]]])" },
    { Json::SUCCESS, R"({
    "JSON Test Pattern pass3": {
        "The outermost value": "must be an object or array.",
        "In this test": "It is an object."
    }
}
)" },
};

void
estimate_size_test()
{
    static const char* inputs[] = {
        "null",
        R"({"empty":[],"scalars":[null,true,false,0,-1],"nested":[["text"],{"key":"value"}]})",
        R"({"key00":0,"key01":1,"key02":2,"key03":3,"key04":4,"key05":5,"key06":6,"key07":7,"key08":8,"key09":9,"key10":10,"key11":11,"key12":12,"key13":13,"key14":14,"key15":15,"key16":16})",
        R"("escaped\nstring\uD834\uDD1E")",
        "1.00000000000000011102230246251565404236316680908203125",
        R"([[[[[[[[[[[[[[[[[[[0]]]]]]]]]]]]]]]]]]])",
    };
    if (Json::EstimateSize((const char*)nullptr, 0) || Json::EstimateSize((const char*)nullptr, 1) != SIZE_MAX ||
        Json::EstimateSize("null") != Json::EstimateSize("null", 4))
        exit(218);
    for (size_t i = 0; i < ARRAYLEN(inputs); ++i) {
        size_t size = strlen(inputs[i]);
        size_t estimate = Json::EstimateSize(inputs[i], size);
        flat::FixedJsonBuffer<64 * 1024> buffer;
        if (estimate == SIZE_MAX || estimate > sizeof(buffer.bytes))
            exit(219);
        buffer.back = estimate;
        if (Json::Parse(inputs[i], size, &buffer) != Json::SUCCESS)
            exit(220);
    }
}

void
round_trip_test()
{
    for (size_t i = 0; i < ARRAYLEN(kRoundTrip); ++i) {
        flat::FixedJsonBuffer<65536> a;
        size_t inputSize = strlen(kRoundTrip[i].before);
        size_t estimate = Json::EstimateSize(kRoundTrip[i].before, inputSize);
        if (estimate == SIZE_MAX || estimate > sizeof(a.bytes))
            exit(221);
        a.back = estimate;
        Json::Status status = Json::Parse(kRoundTrip[i].before, inputSize, &a);
        if (status != Json::SUCCESS) {
            printf(
              "error: Json::Parse returned Json::%s but wanted Json::%s: %s\n",
              Json::StatusToString(status),
              Json::StatusToString(Json::SUCCESS),
              kRoundTrip[i].before);
            exit(10);
        }
        const Json* pJson = a.Root();
        char got[65536];
        if (pJson->ToString(got) != Json::SUCCESS)
            exit(11);
        if (strcmp(got, kRoundTrip[i].after)) {
            printf("error: Json::Parse(%s).ToString() was %s but should have "
                   "been %s\n",
                   kRoundTrip[i].before,
                   got,
                   kRoundTrip[i].after);
            exit(11);
        }
    }
}

void
json_test_suite()
{
    for (size_t i = 0; i < ARRAYLEN(kJsonTestSuite); ++i) {
        flat::FixedJsonBuffer<256 * 1024> a;
        size_t inputSize = kJsonTestSuite[i].size ? kJsonTestSuite[i].size : strlen(kJsonTestSuite[i].json);
        if (kJsonTestSuite[i].error == Json::SUCCESS) {
            size_t estimate = Json::EstimateSize(kJsonTestSuite[i].json, inputSize);
            if (estimate == SIZE_MAX || estimate > sizeof(a.bytes))
                exit(222);
            a.back = estimate;
        }
        Json::Status status = Json::Parse(
          kJsonTestSuite[i].json,
          inputSize,
          &a);
        if (status != kJsonTestSuite[i].error) {
            printf(
              "error: Json::Parse returned Json::%s but wanted Json::%s: %s\n",
              Json::StatusToString(status),
              Json::StatusToString(kJsonTestSuite[i].error),
              kJsonTestSuite[i].json);
            exit(12);
        }
    }
}

void
afl_regression()
{
    flat::FixedJsonBuffer<65536> a;
    auto parse = [&](const char* pText) {
        Json::Parse(pText, strlen(pText), &a);
    };
    parse("[{\"\":1,3:14,]\n");
    parse(
                "[\n"
                "\n"
                "3E14,\n"
                "{\"!\":4,733:4,[\n"
                "\n"
                "3EL%,3E14,\n"
                "{][1][1,,]");
    parse(
                "[\n"
                "null,\n"
                "1,\n"
                "3.14,\n"
                "{\"a\": \"b\",\n"
                "3:14,ull}\n"
                "]");
    parse(
                "[\n"
                "\n"
                "3E14,\n"
                "{\"a!!!!!!!!!!!!!!!!!!\":4, \n"
                "\n"
                "3:1,,\n"
                "3[\n"
                "\n"
                "]");
    parse(
                "[\n"
                "\n"
                "3E14,\n"
                "{\"a!!:!!!!!!!!!!!!!!!\":4, \n"
                "\n"
                "3E1:4, \n"
                "\n"
                "3E1,,\n"
                ",,\n"
                "3[\n"
                "\n"
                "]");
    parse(
                "[\n"
                "\n"
                "3E14,\n"
                "{\"!\":4,733:4,[\n"
                "\n"
                "3E1%,][1,,]");
    parse(
                "[\n"
                "\n"
                "3E14,\n"
                "{\"!\":4,733:4,[\n"
                "\n"
                "3EL%,3E14,\n"
                "{][1][1,,]");
}

#define HI_RESET "\033[0m" // green
#define HI_GOOD "\033[32m" // green
#define HI_BAD "\033[31m" // red
#define HI_OK "\033[33m" // yellow
static const char* const kParsingTests[] = {
    "i_number_double_huge_neg_exp.json",
    "i_number_huge_exp.json",
    "i_number_neg_int_huge_exp.json",
    "i_number_pos_double_huge_exp.json",
    "i_number_real_neg_overflow.json",
    "i_number_real_pos_overflow.json",
    "i_number_real_underflow.json",
    "i_number_too_big_neg_int.json",
    "i_number_too_big_pos_int.json",
    "i_number_very_big_negative_int.json",
    "i_object_key_lone_2nd_surrogate.json",
    "i_string_1st_surrogate_but_2nd_missing.json",
    "i_string_1st_valid_surrogate_2nd_invalid.json",
    "i_string_incomplete_surrogate_and_escape_valid.json",
    "i_string_incomplete_surrogate_pair.json",
    "i_string_incomplete_surrogates_escape_valid.json",
    "i_string_invalid_lonely_surrogate.json",
    "i_string_invalid_surrogate.json",
    "i_string_invalid_utf-8.json",
    "i_string_inverted_surrogates_U+1D11E.json",
    "i_string_iso_latin_1.json",
    "i_string_lone_second_surrogate.json",
    "i_string_lone_utf8_continuation_byte.json",
    "i_string_not_in_unicode_range.json",
    "i_string_overlong_sequence_2_bytes.json",
    "i_string_overlong_sequence_6_bytes.json",
    "i_string_overlong_sequence_6_bytes_null.json",
    "i_string_truncated-utf-8.json",
    "i_string_utf16BE_no_BOM.json",
    "i_string_utf16LE_no_BOM.json",
    "i_string_UTF-16LE_with_BOM.json",
    "i_string_UTF-8_invalid_sequence.json",
    "i_string_UTF8_surrogate_U+D800.json",
    "i_structure_500_nested_arrays.json",
    "i_structure_UTF-8_BOM_empty_object.json",
    "n_array_1_true_without_comma.json",
    "n_array_a_invalid_utf8.json",
    "n_array_colon_instead_of_comma.json",
    "n_array_comma_after_close.json",
    "n_array_comma_and_number.json",
    "n_array_double_comma.json",
    "n_array_double_extra_comma.json",
    "n_array_extra_close.json",
    "n_array_extra_comma.json",
    "n_array_incomplete_invalid_value.json",
    "n_array_incomplete.json",
    "n_array_inner_array_no_comma.json",
    "n_array_invalid_utf8.json",
    "n_array_items_separated_by_semicolon.json",
    "n_array_just_comma.json",
    "n_array_just_minus.json",
    "n_array_missing_value.json",
    "n_array_newlines_unclosed.json",
    "n_array_number_and_comma.json",
    "n_array_number_and_several_commas.json",
    "n_array_spaces_vertical_tab_formfeed.json",
    "n_array_star_inside.json",
    "n_array_unclosed.json",
    "n_array_unclosed_trailing_comma.json",
    "n_array_unclosed_with_new_lines.json",
    "n_array_unclosed_with_object_inside.json",
    "n_incomplete_false.json",
    "n_incomplete_null.json",
    "n_incomplete_true.json",
    "n_multidigit_number_then_00.json",
    "n_number_0.1.2.json",
    "n_number_-01.json",
    "n_number_0.3e+.json",
    "n_number_0.3e.json",
    "n_number_0_capital_E+.json",
    "n_number_0_capital_E.json",
    "n_number_0.e1.json",
    "n_number_0e+.json",
    "n_number_0e.json",
    "n_number_1_000.json",
    "n_number_1.0e+.json",
    "n_number_1.0e-.json",
    "n_number_1.0e.json",
    "n_number_-1.0..json",
    "n_number_1eE2.json",
    "n_number_+1.json",
    "n_number_.-1.json",
    "n_number_2.e+3.json",
    "n_number_2.e-3.json",
    "n_number_2.e3.json",
    "n_number_.2e-3.json",
    "n_number_-2..json",
    "n_number_9.e+.json",
    "n_number_expression.json",
    "n_number_hex_1_digit.json",
    "n_number_hex_2_digits.json",
    "n_number_infinity.json",
    "n_number_+Inf.json",
    "n_number_Inf.json",
    "n_number_invalid+-.json",
    "n_number_invalid-negative-real.json",
    "n_number_invalid-utf-8-in-bigger-int.json",
    "n_number_invalid-utf-8-in-exponent.json",
    "n_number_invalid-utf-8-in-int.json",
    "n_number_++.json",
    "n_number_minus_infinity.json",
    "n_number_minus_sign_with_trailing_garbage.json",
    "n_number_minus_space_1.json",
    "n_number_-NaN.json",
    "n_number_NaN.json",
    "n_number_neg_int_starting_with_zero.json",
    "n_number_neg_real_without_int_part.json",
    "n_number_neg_with_garbage_at_end.json",
    "n_number_real_garbage_after_e.json",
    "n_number_real_with_invalid_utf8_after_e.json",
    "n_number_real_without_fractional_part.json",
    "n_number_starting_with_dot.json",
    "n_number_U+FF11_fullwidth_digit_one.json",
    "n_number_with_alpha_char.json",
    "n_number_with_alpha.json",
    "n_number_with_leading_zero.json",
    "n_object_bad_value.json",
    "n_object_bracket_key.json",
    "n_object_comma_instead_of_colon.json",
    "n_object_double_colon.json",
    "n_object_emoji.json",
    "n_object_garbage_at_end.json",
    "n_object_key_with_single_quotes.json",
    "n_object_lone_continuation_byte_in_key_and_trailing_comma.json",
    "n_object_missing_colon.json",
    "n_object_missing_key.json",
    "n_object_missing_semicolon.json",
    "n_object_missing_value.json",
    "n_object_no-colon.json",
    "n_object_non_string_key_but_huge_number_instead.json",
    "n_object_non_string_key.json",
    "n_object_repeated_null_null.json",
    "n_object_several_trailing_commas.json",
    "n_object_single_quote.json",
    "n_object_trailing_comma.json",
    "n_object_trailing_comment.json",
    "n_object_trailing_comment_open.json",
    "n_object_trailing_comment_slash_open_incomplete.json",
    "n_object_trailing_comment_slash_open.json",
    "n_object_two_commas_in_a_row.json",
    "n_object_unquoted_key.json",
    "n_object_unterminated-value.json",
    "n_object_with_single_string.json",
    "n_object_with_trailing_garbage.json",
    "n_single_space.json",
    "n_string_1_surrogate_then_escape.json",
    "n_string_1_surrogate_then_escape_u1.json",
    "n_string_1_surrogate_then_escape_u1x.json",
    "n_string_1_surrogate_then_escape_u.json",
    "n_string_accentuated_char_no_quotes.json",
    "n_string_backslash_00.json",
    "n_string_escaped_backslash_bad.json",
    "n_string_escaped_ctrl_char_tab.json",
    "n_string_escaped_emoji.json",
    "n_string_escape_x.json",
    "n_string_incomplete_escaped_character.json",
    "n_string_incomplete_escape.json",
    "n_string_incomplete_surrogate_escape_invalid.json",
    "n_string_incomplete_surrogate.json",
    "n_string_invalid_backslash_esc.json",
    "n_string_invalid_unicode_escape.json",
    "n_string_invalid_utf8_after_escape.json",
    "n_string_invalid-utf-8-in-escape.json",
    "n_string_leading_uescaped_thinspace.json",
    "n_string_no_quotes_with_bad_escape.json",
    "n_string_single_doublequote.json",
    "n_string_single_quote.json",
    "n_string_single_string_no_double_quotes.json",
    "n_string_start_escape_unclosed.json",
    "n_string_unescaped_ctrl_char.json",
    "n_string_unescaped_newline.json",
    "n_string_unescaped_tab.json",
    "n_string_unicode_CapitalU.json",
    "n_string_with_trailing_garbage.json",
    "n_structure_100000_opening_arrays.json",
    "n_structure_angle_bracket_..json",
    "n_structure_angle_bracket_null.json",
    "n_structure_array_trailing_garbage.json",
    "n_structure_array_with_extra_array_close.json",
    "n_structure_array_with_unclosed_string.json",
    "n_structure_ascii-unicode-identifier.json",
    "n_structure_capitalized_True.json",
    "n_structure_close_unopened_array.json",
    "n_structure_comma_instead_of_closing_brace.json",
    "n_structure_double_array.json",
    "n_structure_end_array.json",
    "n_structure_incomplete_UTF8_BOM.json",
    "n_structure_lone-invalid-utf-8.json",
    "n_structure_lone-open-bracket.json",
    "n_structure_no_data.json",
    "n_structure_null-byte-outside-string.json",
    "n_structure_number_with_trailing_garbage.json",
    "n_structure_object_followed_by_closing_object.json",
    "n_structure_object_unclosed_no_value.json",
    "n_structure_object_with_comment.json",
    "n_structure_object_with_trailing_garbage.json",
    "n_structure_open_array_apostrophe.json",
    "n_structure_open_array_comma.json",
    "n_structure_open_array_object.json",
    "n_structure_open_array_open_object.json",
    "n_structure_open_array_open_string.json",
    "n_structure_open_array_string.json",
    "n_structure_open_object_close_array.json",
    "n_structure_open_object_comma.json",
    "n_structure_open_object.json",
    "n_structure_open_object_open_array.json",
    "n_structure_open_object_open_string.json",
    "n_structure_open_object_string_with_apostrophes.json",
    "n_structure_open_open.json",
    "n_structure_single_eacute.json",
    "n_structure_single_star.json",
    "n_structure_trailing_#.json",
    "n_structure_U+2060_word_joined.json",
    "n_structure_uescaped_LF_before_string.json",
    "n_structure_unclosed_array.json",
    "n_structure_unclosed_array_partial_null.json",
    "n_structure_unclosed_array_unfinished_false.json",
    "n_structure_unclosed_array_unfinished_true.json",
    "n_structure_unclosed_object.json",
    "n_structure_unicode-identifier.json",
    "n_structure_UTF8_BOM_no_data.json",
    "n_structure_whitespace_formfeed.json",
    "n_structure_whitespace_U+2060_word_joiner.json",
    "y_array_arraysWithSpaces.json",
    "y_array_empty.json",
    "y_array_empty-string.json",
    "y_array_ending_with_newline.json",
    "y_array_false.json",
    "y_array_heterogeneous.json",
    "y_array_null.json",
    "y_array_with_1_and_newline.json",
    "y_array_with_leading_space.json",
    "y_array_with_several_null.json",
    "y_array_with_trailing_space.json",
    "y_number_0e+1.json",
    "y_number_0e1.json",
    "y_number_after_space.json",
    "y_number_double_close_to_zero.json",
    "y_number_int_with_exp.json",
    "y_number.json",
    "y_number_minus_zero.json",
    "y_number_negative_int.json",
    "y_number_negative_one.json",
    "y_number_negative_zero.json",
    "y_number_real_capital_e.json",
    "y_number_real_capital_e_neg_exp.json",
    "y_number_real_capital_e_pos_exp.json",
    "y_number_real_exponent.json",
    "y_number_real_fraction_exponent.json",
    "y_number_real_neg_exp.json",
    "y_number_real_pos_exponent.json",
    "y_number_simple_int.json",
    "y_number_simple_real.json",
    "y_object_basic.json",
    "y_object_duplicated_key_and_value.json",
    "y_object_duplicated_key.json",
    "y_object_empty.json",
    "y_object_empty_key.json",
    "y_object_escaped_null_in_key.json",
    "y_object_extreme_numbers.json",
    "y_object.json",
    "y_object_long_strings.json",
    "y_object_simple.json",
    "y_object_string_unicode.json",
    "y_object_with_newlines.json",
    "y_string_1_2_3_bytes_UTF-8_sequences.json",
    "y_string_accepted_surrogate_pair.json",
    "y_string_accepted_surrogate_pairs.json",
    "y_string_allowed_escapes.json",
    "y_string_backslash_and_u_escaped_zero.json",
    "y_string_backslash_doublequotes.json",
    "y_string_comments.json",
    "y_string_double_escape_a.json",
    "y_string_double_escape_n.json",
    "y_string_escaped_control_character.json",
    "y_string_escaped_noncharacter.json",
    "y_string_in_array.json",
    "y_string_in_array_with_leading_space.json",
    "y_string_last_surrogates_1_and_2.json",
    "y_string_nbsp_uescaped.json",
    "y_string_nonCharacterInUTF-8_U+10FFFF.json",
    "y_string_nonCharacterInUTF-8_U+FFFF.json",
    "y_string_null_escape.json",
    "y_string_one-byte-utf-8.json",
    "y_string_pi.json",
    "y_string_reservedCharacterInUTF-8_U+1BFFF.json",
    "y_string_simple_ascii.json",
    "y_string_space.json",
    "y_string_surrogates_U+1D11E_MUSICAL_SYMBOL_G_CLEF.json",
    "y_string_three-byte-utf-8.json",
    "y_string_two-byte-utf-8.json",
    "y_string_u+2028_line_sep.json",
    "y_string_u+2029_par_sep.json",
    "y_string_uescaped_newline.json",
    "y_string_uEscape.json",
    "y_string_unescaped_char_delete.json",
    "y_string_unicode_2.json",
    "y_string_unicodeEscapedBackslash.json",
    "y_string_unicode_escaped_double_quote.json",
    "y_string_unicode.json",
    "y_string_unicode_U+10FFFE_nonchar.json",
    "y_string_unicode_U+1FFFE_nonchar.json",
    "y_string_unicode_U+200B_ZERO_WIDTH_SPACE.json",
    "y_string_unicode_U+2064_invisible_plus.json",
    "y_string_unicode_U+FDD0_nonchar.json",
    "y_string_unicode_U+FFFE_nonchar.json",
    "y_string_utf8.json",
    "y_string_with_del_character.json",
    "y_structure_lonely_false.json",
    "y_structure_lonely_int.json",
    "y_structure_lonely_negative_real.json",
    "y_structure_lonely_null.json",
    "y_structure_lonely_string.json",
    "y_structure_lonely_true.json",
    "y_structure_string_empty.json",
    "y_structure_trailing_newline.json",
    "y_structure_true_in_array.json",
    "y_structure_whitespace_array.json",
};

static const char*
get_json_test_suite_path()
{
    FILE* file = fopen("JSONTestSuite/test_parsing/y_array_empty.json", "rb");
    if (file) {
        fclose(file);
        return "JSONTestSuite/test_parsing/";
    }
    file = fopen("../JSONTestSuite/test_parsing/y_array_empty.json", "rb");
    if (file) {
        fclose(file);
        return "../JSONTestSuite/test_parsing/";
    }
    JSON_PANIC("Could not find JSONTestSuite directory.");
}

static void
json_test_suite_files()
{
    int failures = 0;
    flat::FixedJsonBuffer<1024 * 1024> arena;
    const char* base_path = get_json_test_suite_path();
    for (size_t i = 0; i < ARRAYLEN(kParsingTests); ++i) {
        char path[512];
        snprintf(path, sizeof(path), "%s%s", base_path, kParsingTests[i]);
        FILE* input = fopen(path, "rb");
        JSON_REQUIRE(input, "Could not open JSONTestSuite input '%s'.", path);
        JSON_REQUIRE(!fseek(input, 0, SEEK_END), "Could not seek JSONTestSuite input '%s'.", path);
        long input_size = ftell(input);
        JSON_REQUIRE(input_size >= 0, "Could not size JSONTestSuite input '%s'.", path);
        rewind(input);
        char* input_data = (char*)malloc((size_t)input_size + 1);
        JSON_REQUIRE(input_data, "Could not allocate JSONTestSuite input '%s'.", path);
        JSON_REQUIRE(fread(input_data, 1, (size_t)input_size, input) == (size_t)input_size,
                    "Could not read JSONTestSuite input '%s'.", path);
        fclose(input);
        arena.back = sizeof(arena.bytes);
        if (kParsingTests[i][0] == 'y') {
            size_t estimate = Json::EstimateSize(input_data, (size_t)input_size);
            JSON_REQUIRE(estimate != SIZE_MAX && estimate <= sizeof(arena.bytes),
                         "JSON size estimate failed for '%s'.", path);
            arena.back = estimate;
        }
        Json::Status status = Json::Parse(input_data, (size_t)input_size, &arena);
        free(input_data);
        const char* color = "";
        const char* reason = "";
        switch (kParsingTests[i][0]) {
            case 'y':
                if (status == Json::SUCCESS) {
                    color = HI_GOOD;
                    reason = "PASSED";
                } else {
                    color = HI_BAD;
                    reason = "SHOULD_HAVE_PASSED";
                    ++failures;
                }
                break;
            case 'n':
                if (status != Json::SUCCESS) {
                    color = HI_GOOD;
                    reason = "REJECTED";
                } else {
                    color = HI_BAD;
                    reason = "SHOULD_HAVE_FAILED";
                    ++failures;
                }
                break;
            case 'i':
                color = HI_OK;
                reason = status == Json::SUCCESS ? "IMPLEMENTATION_PASS"
                                                 : "IMPLEMENTATION_FAIL";
                break;
            default:
                JSON_PANIC("Unknown JSONTestSuite test class.");
        }
        printf("%-70s %s%s%s", kParsingTests[i], color, reason, HI_RESET);
        if (status != Json::SUCCESS)
            printf(" (%s)", Json::StatusToString(status));
        printf("\n");
    }
    if (failures)
        exit(failures);
}

int
main()
{
    object_test();
    direct_serialization_test();
    public_soft_failure_test();
    file_map_round_trip_test();
    writable_file_round_trip_test();
    large_object_index_test();
    medium_object_lookup_test();
    numeric_arena_test();
    fast_decimal_differential_test();
    strict_string_test();
    immutable_layout_test();
    deep_test();
    static_arena_test();
    stack_arena_test();
    parse_test();
    estimate_size_test();
    round_trip_test();
    afl_regression();
    json_test_suite();
    json_test_suite_files();

    if (!getenv("FLAT_JSON_SKIP_BENCHMARKS")) {
        BENCH(2000, 1, object_test());
        BENCH(2000, 1, deep_test());
        BENCH(2000, 1, parse_test());
        BENCH(2000, 1, round_trip_test());
        BENCH(2000, 1, json_test_suite());
    }
}
