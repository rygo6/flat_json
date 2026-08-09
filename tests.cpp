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
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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
    flat::FixedArena<1024> a;
    const char* text =
      flat::WriteJson(flat::JsonObject({ { "content", "hello" } }), a);
    if (strcmp(text, "{\"content\":\"hello\"}"))
        exit(1);
}

void
direct_serialization_test()
{
    flat::FixedArena<1024> a;
    const char* output =
      flat::WriteJson(flat::JsonObject({ { "answer", 42 } }), a);
    if (output < a.bytes || output >= a.bytes + sizeof(a.bytes))
        exit(17);
    if (strcmp(output, "{\"answer\":42}"))
        exit(18);

    unsigned char mapped_bytes[1024];
    flat::MappedBuffer mapped_buffer(mapped_bytes, sizeof(mapped_bytes), 13);
    uint64_t mapped_offset = mapped_buffer.cursor;
    const char* mapped_output = flat::WriteJson(
      flat::JsonObject({ { "model", "gpt-5" }, { "stream", true } }),
      mapped_buffer);
    if (mapped_output != (char*)mapped_bytes + mapped_offset)
        exit(40);
    if (strcmp(mapped_output, "{\"model\":\"gpt-5\",\"stream\":true}"))
        exit(41);
    if (mapped_buffer.cursor != mapped_offset + strlen(mapped_output) + 1)
        exit(42);

    flat::FixedArena<512> parse_arena;
    size_t mapped_size = mapped_buffer.cursor - mapped_offset - 1;
    auto parsed = Json::Parse(parse_arena, mapped_output, mapped_size);
    if (parsed.first != Json::SUCCESS ||
        strcmp((*parsed.second)["model"].GetString(), "gpt-5") ||
        !(*parsed.second)["stream"].GetBool())
        exit(44);
    uint64_t parsed_offset = mapped_buffer.cursor;
    const char* parsed_output = flat::WriteJson(*parsed.second, mapped_buffer);
    if (parsed_output != (char*)mapped_bytes + parsed_offset ||
        strcmp(parsed_output, mapped_output))
        exit(45);
}

void
mapped_file_test()
{
    char path[] = "/tmp/json-cpp-XXXXXX";
    int descriptor = mkstemp(path);
    if (descriptor < 0)
        exit(46);
    close(descriptor);

    {
        flat::MappedBuffer output(path, 1024);
        if (!output.IsValid())
            exit(47);
        flat::WriteJson(flat::JsonObject({ { "mapped", true } }), output);
    }

    {
        static constexpr char Expected[] = "{\"mapped\":true}";
        flat::MappedBuffer input(path);
        if (!input.IsValid() || input.size != sizeof(Expected) - 1 ||
            memcmp(input.mapped, Expected, sizeof(Expected) - 1))
            exit(48);
    }

    flat::FixedArena<512> arena;
    auto parsed = Json::Parse(arena, flat::MappedBuffer(path));
    if (parsed.first != Json::SUCCESS ||
        !parsed.second->GetObject()["mapped"].GetBool())
        exit(49);

    unlink(path);
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
        flat::FixedArena<8192> output_arena;
        const char* text = flat::WriteJson(values[i], output_arena);
        if (text < output_arena.bytes ||
            text >= output_arena.bytes + sizeof(output_arena.bytes))
            exit(27);

        flat::FixedArena<8192> parse_arena;
        auto parsed = Json::Parse(parse_arena, text, strlen(text));
        if (parsed.first != Json::SUCCESS)
            exit(28);
        double expected = values[i] == 0.0 ? 0.0 : values[i];
        if (DoubleBits(parsed.second->GetNumber()) != DoubleBits(expected))
            exit(29);
    }

    flat::FixedArena<4096> special_arena;
    const char* special = flat::WriteJson(
      flat::JsonArray({ NAN, INFINITY, -INFINITY, 1.25f }),
      special_arena);
    if (strcmp(special, "[null,1e5000,-1e5000,1.25]"))
        exit(30);
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
        flat::FixedArena<1024> arena;
        if (Json::Parse(arena, invalid[i].data, invalid[i].size).first !=
            Json::MALFORMED)
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
    for (size_t i = 0; i < ARRAYLEN(valid); ++i) {
        flat::FixedArena<1024> arena;
        if (Json::Parse(arena, valid[i].data, valid[i].size).first !=
            Json::SUCCESS)
            exit(32);
    }
}

void
immutable_layout_test()
{
    flat::FixedArena<2048> a;
    auto parsed = Json::Parse(a, R"([1,[2,3],{"x":4,"s":"ok"}])");
    if (parsed.first != Json::SUCCESS)
        exit(20);
    flat::u32 root_offset = (flat::u32)((const char*)parsed.second - a.pBase);
    if ((const char*)parsed.second < a.bytes ||
        (const char*)parsed.second >= a.bytes + sizeof(a.bytes))
        exit(21);
    if ((*parsed.second)[0].GetLong() != 1 ||
        (*parsed.second)[1][1].GetLong() != 3 ||
        (*parsed.second)[2]["x"].GetLong() != 4)
        exit(22);
    const Json& array = parsed.second->GetArray();
    const Json& object = array[2].GetObject();
    if (&array != parsed.second || array.GetSize() != 3 ||
        &object != &array[2] || object.GetSize() != 2)
        exit(50);
    if (parsed.second->span != sizeof(a.bytes) - root_offset)
        exit(23);
    alignas(8) char relocated_storage[2048];
    memcpy(relocated_storage, parsed.second, parsed.second->span);
    const Json* relocated = (const Json*)relocated_storage;
    if ((*relocated)[0].GetLong() != 1 ||
        (*relocated)[1][1].GetLong() != 3 ||
        (*relocated)[2]["x"].GetLong() != 4 ||
        strcmp((*relocated)[2]["s"].GetString(), "ok"))
        exit(43);
    const char* output = parsed.second->ToString(a);
    if (strcmp(output, R"([1,[2,3],{"x":4,"s":"ok"}])"))
        exit(24);
    if (output < a.bytes || output >= (const char*)parsed.second)
        exit(25);
    if (Json::Parse(a, "[1,").first != Json::MALFORMED ||
        (*parsed.second)[2]["x"].GetLong() != 4)
        exit(26);
}

void
deep_test()
{
    flat::FixedArena<8192> a;
    const char* text = flat::WriteJson(
      flat::JsonObject({ { "content",
                           flat::JsonArray({ flat::JsonArray({ flat::JsonArray(
                         { 0, 10, 20, 3.14, 40 }) }) }) } }),
      a);
    if (strcmp(text, "{\"content\":[[[0,10,20,3.14,40]]]}"))
        exit(2);
}

static flat::FixedArena<65536> g_static_arena;

void
static_arena_test()
{
    flat::ArenaBuffer a = g_static_arena;
    const char* text = flat::WriteJson(
      flat::JsonObject({ { "name", "static" },
                         { "values", flat::JsonArray({ 1, 2 }) } }),
      a);
    if (strcmp(text, "{\"name\":\"static\",\"values\":[1,2]}"))
        exit(8);
    std::pair<Json::Status, const Json*> res =
      Json::Parse(a, "{\"k\": [true, null, 3.5]}");
    if (res.first != Json::SUCCESS)
        exit(9);
    if (strcmp(res.second->ToString(a), "{\"k\":[true,null,3.5]}"))
        exit(13);
}

void
stack_arena_test()
{
    flat::FixedArena<16384> a;
    std::pair<Json::Status, const Json*> res =
      Json::Parse(a, "[1, \"two\", {\"three\": 3}]");
    if (res.first != Json::SUCCESS)
        exit(14);
    if (strcmp(res.second->ToString(a), "[1,\"two\",{\"three\":3}]"))
        exit(15);
    if (strcmp((*res.second)[1].GetString(), "two"))
        exit(16);
}

void
parse_test()
{
    flat::FixedArena<65536> a;
    std::pair<Json::Status, const Json*> res =
      Json::Parse(a, "{ \"content\":[[[0,10,20,3.14,40]]]}");
    if (res.first != Json::SUCCESS)
        exit(3);
    if (strcmp(res.second->ToString(a), "{\"content\":[[[0,10,20,3.14,40]]]}"))
        exit(4);
    if (strcmp(res.second->ToStringPretty(a),
               R"({"content": [[[0, 10, 20, 3.14, 40]]]})"))
        exit(5);
    res = Json::Parse(a, "{ \"a\": 1, \"b\": [2,   3]}");
    if (strcmp(res.second->ToString(a), R"({"a":1,"b":[2,3]})"))
        exit(6);
    if (strcmp(res.second->ToStringPretty(a),
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
round_trip_test()
{
    for (size_t i = 0; i < ARRAYLEN(kRoundTrip); ++i) {
        flat::FixedArena<65536> a;
        std::pair<Json::Status, const Json*> res =
          Json::Parse(a, kRoundTrip[i].before, strlen(kRoundTrip[i].before));
        if (res.first != Json::SUCCESS) {
            printf(
              "error: Json::Parse returned Json::%s but wanted Json::%s: %s\n",
              Json::StatusToString(res.first),
              Json::StatusToString(Json::SUCCESS),
              kRoundTrip[i].before);
            exit(10);
        }
        const char* got = res.second->ToString(a);
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
        flat::FixedArena<65536> a;
        std::pair<Json::Status, const Json*> res =
          Json::Parse(a,
                      kJsonTestSuite[i].json,
                      kJsonTestSuite[i].size ? kJsonTestSuite[i].size
                                             : strlen(kJsonTestSuite[i].json));
        if (res.first != kJsonTestSuite[i].error) {
            printf(
              "error: Json::Parse returned Json::%s but wanted Json::%s: %s\n",
              Json::StatusToString(res.first),
              Json::StatusToString(kJsonTestSuite[i].error),
              kJsonTestSuite[i].json);
            exit(12);
        }
    }
}

void
afl_regression()
{
    flat::FixedArena<65536> a;
    Json::Parse(a, "[{\"\":1,3:14,]\n");
    Json::Parse(a,
                "[\n"
                "\n"
                "3E14,\n"
                "{\"!\":4,733:4,[\n"
                "\n"
                "3EL%,3E14,\n"
                "{][1][1,,]");
    Json::Parse(a,
                "[\n"
                "null,\n"
                "1,\n"
                "3.14,\n"
                "{\"a\": \"b\",\n"
                "3:14,ull}\n"
                "]");
    Json::Parse(a,
                "[\n"
                "\n"
                "3E14,\n"
                "{\"a!!!!!!!!!!!!!!!!!!\":4, \n"
                "\n"
                "3:1,,\n"
                "3[\n"
                "\n"
                "]");
    Json::Parse(a,
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
    Json::Parse(a,
                "[\n"
                "\n"
                "3E14,\n"
                "{\"!\":4,733:4,[\n"
                "\n"
                "3E1%,][1,,]");
    Json::Parse(a,
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
    JSN_PANIC("Could not find JSONTestSuite directory.");
}

static void
json_test_suite_files()
{
    int failures = 0;
    flat::FixedArena<1024 * 1024> arena;
    const char* base_path = get_json_test_suite_path();
    for (size_t i = 0; i < ARRAYLEN(kParsingTests); ++i) {
        char path[512];
        snprintf(path, sizeof(path), "%s%s", base_path, kParsingTests[i]);
        flat::MappedBuffer input(path);
        std::pair<Json::Status, const Json*> result = Json::Parse(arena, input);
        const char* color = "";
        const char* reason = "";
        switch (kParsingTests[i][0]) {
            case 'y':
                if (result.first == Json::SUCCESS) {
                    color = HI_GOOD;
                    reason = "PASSED";
                } else {
                    color = HI_BAD;
                    reason = "SHOULD_HAVE_PASSED";
                    ++failures;
                }
                break;
            case 'n':
                if (result.first != Json::SUCCESS) {
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
                reason = result.first == Json::SUCCESS ? "IMPLEMENTATION_PASS"
                                                       : "IMPLEMENTATION_FAIL";
                break;
            default:
                JSN_PANIC("Unknown JSONTestSuite test class.");
        }
        printf("%-70s %s%s%s", kParsingTests[i], color, reason, HI_RESET);
        if (result.first != Json::SUCCESS)
            printf(" (%s)", Json::StatusToString(result.first));
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
    mapped_file_test();
    numeric_arena_test();
    strict_string_test();
    immutable_layout_test();
    deep_test();
    static_arena_test();
    stack_arena_test();
    parse_test();
    round_trip_test();
    afl_regression();
    json_test_suite();
    json_test_suite_files();

    BENCH(2000, 1, object_test());
    BENCH(2000, 1, deep_test());
    BENCH(2000, 1, parse_test());
    BENCH(2000, 1, round_trip_test());
    BENCH(2000, 1, json_test_suite());
}
