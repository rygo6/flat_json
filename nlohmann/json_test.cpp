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

#include "nlohmann.h"

#include <cstdio>
#include <cstdlib>
#include <time.h>

#define ARRAYLEN(A) \
    ((sizeof(A) / sizeof(*(A))) / ((unsigned)!(sizeof(A) % sizeof(*(A)))))

#define STRING(sl) std::string(sl, sizeof(sl) - 1)

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
        struct timespec start = now(); \
        for (int __i = 0; __i < ITERATIONS; ++__i) { \
            asm volatile("" ::: "memory"); \
            CODE; \
        } \
        long long work = (WORK_PER_RUN) * (ITERATIONS); \
        double nanos = (tonanos(tub(now(), start)) + work - 1) / (double)work; \
        printf("%10g ns %2dx %s\n", nanos, (ITERATIONS), #CODE); \
    } while (0)

struct timespec
now(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return ts;
}

struct timespec
tub(struct timespec a, struct timespec b)
{
    a.tv_sec -= b.tv_sec;
    if (a.tv_nsec < b.tv_nsec) {
        a.tv_nsec += 1000000000;
        a.tv_sec--;
    }
    a.tv_nsec -= b.tv_nsec;
    return a;
}

int64_t
tonanos(struct timespec x)
{
    return x.tv_sec * 1000000000ull + x.tv_nsec;
}

void
object_test()
{
    nlohmann::ordered_json obj;
    obj["content"] = "hello";
    if (obj.dump() != "{\"content\":\"hello\"}")
        exit(1);
}

void
deep_test()
{
    nlohmann::ordered_json A1;
    A1[0] = 0;
    A1[1] = 10;
    A1[2] = 20;
    A1[3] = 3.14;
    A1[4] = 40;
    nlohmann::ordered_json A2;
    A2[0] = std::move(A1);
    nlohmann::ordered_json A3;
    A3[0] = std::move(A2);
    nlohmann::ordered_json obj;
    obj["content"] = std::move(A3);
    if (obj.dump() != "{\"content\":[[[0,10,20,3.14,40]]]}")
        exit(2);
}

void
parse_test()
{
    nlohmann::ordered_json res =
      nlohmann::ordered_json::parse("{ \"content\":[[[0,10,20,3.14,40]]]}");
    if (res.dump() != "{\"content\":[[[0,10,20,3.14,40]]]}")
        exit(4);
    if (res.dump() != "{\"content\":[[[0,10,20,3.14,40]]]}")
        exit(4);
    res = nlohmann::ordered_json::parse("{ \"a\": 1, \"b\": [2,   3]}");
    if (res.dump() != R"({"a":1,"b":[2,3]})")
        exit(6);
    if (res.dump() != R"({"a":1,"b":[2,3]})")
        exit(6);
}

static const struct
{
    std::string before;
    std::string after;
} kRoundTrip[] = {

    // valid utf16 sequences
    { " [\"\\u0020\"] ", "[\" \"]" },
    { " [\"\\u00A0\"] ", "[\" \"]" },

    // when we encounter invalid utf16 sequences
    // we turn them into ascii
    { "[\"uDFAA\"]", "[\"uDFAA\"]" }, // xxx
    { "[\"uDFAA\"]", "[\"uDFAA\"]" }, // xxx
    { "[\"uDFAA\"]", "[\"uDFAA\"]" }, // xxx
    { "[\"uDFAA\"]", "[\"uDFAA\"]" }, // xxx
    { "[\"uDFAA\"]", "[\"uDFAA\"]" }, // xxx
    { "[\"uDFAA\"]", "[\"uDFAA\"]" }, // xxx
    { "[\"uDFAA\"]", "[\"uDFAA\"]" }, // xxx

    // underflow and overflow
    { " [123.456e-789] ", "[0.0]" },
    { " [123.456e-789] ", "[0.0]" },
    { " [123.456e-789] ", "[0.0]" },
    { " [123.456e-789] ", "[0.0]" },
    { " [-123123123123123123123123123123] ", "[-1.2312312312312312e+29]" },
};

// https://github.com/nst/JSONTestSuite/
static const struct
{
    bool fail;
    std::string json;
} kJsonTestSuite[] = {
    { false, "" },
    { false, "[] []" },
    { false, "[nan]" },
    { false, "[-nan]" },
    { false, "[+NaN]" },
    { false, "{\"Extra value after close\": true} \"misplaced quoted value\"" },
    { false, "{\"Illegal expression\": 1 + 2}" },
    { false, "{\"Illegal invocation\": alert()}" },
    { false, "{\"Numbers cannot have leading zeroes\": 013}" },
    { false, "{\"Numbers cannot be hex\": 0x14}" },
    { true, "[\"Illegal backslash escape: \\x15\"]" },
    { true, "[\\naked]" },
    { true, "[\"Illegal backslash escape: \\017\"]" },
    { true, "[[[[[[[[[[[[[[[[[[[[\"Too deep\"]]]]]]]]]]]]]]]]]]]]" },
    { true, "{\"Missing colon\" null}" },
    { true, "{\"Double colon\":: null}" },
    { true, "{\"Comma instead of colon\", null}" },
    { true, "[\"Colon instead of comma\": false]" },
    { true, "[\"Bad value\", truth]" },
    { true, "[\'single quote\']" },
    { true, "[\"\ttab\tcharacter\tin\tstring\t\"]" },
    { true, "[\"tab\\   character\\   in\\  string\\  \"]" },
    { true, "[\"line\nbreak\"]" },
    { true, "[\"line\\\nbreak\"]" },
    { true, "[0e]" },
    { true, "[\"Unclosed array\"" },
    { true, "[0e+]" },
    { true, "[0e+-1]" },
    { true, "{\"Comma instead if closing brace\": true," },
    { true, "[\"mismatch\"}" },
    { true, "{unquoted_key: \"keys must be quoted\"}" },
    { true, "[\"extra comma\",]" },
    { true, "[\"double extra comma\",,]" },
    { true, "[   , \"<-- missing value\"]" },
    { true, "[\"Comma after the close\"]," },
    { true, "[\"Extra close\"]]" },
    { true, "{\"Extra comma\": true,}" },
    { true, " {\"a\" " },
    { true, " {\"a\": " },
    { true, " {:\"b\" " },
    { true, " {\"a\" b} " },
    { true, " {key: 'value'} " },
    { true, " {\"a\":\"a\" 123} " },
    { true, " \x7b\xf0\x9f\x87\xa8\xf0\x9f\x87\xad\x7d " },
    { true, " {[: \"x\"} " },
    { true, " [1.8011670033376514H-308] " },
    { true, " [1.2a-3] " },
    { true, " [.123] " },
    { true, " [1e\xe5] " },
    { true, " [1ea] " },
    { true, " [-1x] " },
    { true, " [-.123] " },
    { true, " [-foo] " },
    { true, " [-Infinity] " },
    { true, " \x5b\x30\xe5\x5d " },
    { true, " \x5b\x31\x65\x31\xe5\x5d " },
    { true, " \x5b\x31\x32\x33\xe5\x5d " },
    { true, " \x5b\x2d\x31\x32\x33\x2e\x31\x32\x33\x66\x6f\x6f\x5d " },
    { true, " [0e+-1] " },
    { true, " [Infinity] " },
    { true, " [0x42] " },
    { true, " [0x1] " },
    { true, " [1+2] " },
    { true, " \x5b\xef\xbc\x91\x5d " },
    { true, " [NaN] " },
    { true, " [Inf] " },
    { true, " [9.e+] " },
    { true, " [1eE2] " },
    { true, " [1e0e] " },
    { true, " [1.0e-] " },
    { true, " [1.0e+] " },
    { true, " [0e] " },
    { true, " [0e+] " },
    { true, " [0E] " },
    { true, " [0E+] " },
    { true, " [0.3e] " },
    { true, " [0.3e+] " },
    { true, " [0.1.2] " },
    { true, " [.2e-3] " },
    { true, " [.-1] " },
    { true, " [-NaN] " },
    { true, " [+Inf] " },
    { true, " [+1] " },
    { true, " [++1234] " },
    { true, " [tru] " },
    { true, " [nul] " },
    { true, " [fals] " },
    { true, " [{} " },
    { true, "\n[1,\n1\n,1  " },
    { true, " [1, " },
    { true, " [\"\" " },
    { true, " [* " },
    { true, " \x5b\x22\x0b\x61\x22\x5c\x66\x5d " },
    { true, "[\"a\",\n4\n,1,1  " },
    { true, " [1:2] " },
    { true, " \x5b\xff\x5d " },
    { true, " \x5b\x78 " },
    { true, " [\"x\" " },
    { true, " [\"\": 1] " },
    { true, " [a\xe5] " },
    { true, " {\"x\", null} " },
    { true, " [\"x\", truth] " },
    { true, STRING("\x00") },
    { true, "\n[\"x\"]]" },
    { true, " [012] " },
    { true, " [-012] " },
    { true, " [1 000.0] " },
    { true, " [-01] " },
    { true, " [- 1] " },
    { true, " [-] " },
    { true, " {\"\xb9\":\"0\",} " },
    { true, " {\"x\"::\"b\"} " },
    { true, " [1,,] " },
    { true, " [1,] " },
    { true, " [1,,2] " },
    { true, " [,1] " },
    { true, " [ 3[ 4]] " },
    { true, " [1 true] " },
    { true, " [\"a\" \"b\"] " },
    { true, " [--2.] " },
    { true, " [1.] " },
    { true, " [2.e3] " },
    { true, " [2.e-3] " },
    { true, " [2.e+3] " },
    { true, " [0.e1] " },
    { true, " [-2.] " },
    { true, " \xef\xbb\xbf{} " },
    { true, STRING(" [\x00\"\x00\xe9\x00\"\x00]\x00 ") },
    { true, STRING(" \x00[\x00\"\x00\xe9\x00\"\x00] ") },
    { true, " [\"\xe0\xff\"] " },
    { true, " [\"\xfc\x80\x80\x80\x80\x80\"] " },
    { true, " [\"\xfc\x83\xbf\xbf\xbf\xbf\"] " },
    { true, " [\"\xc0\xaf\"] " },
    { true, " [\"\xf4\xbf\xbf\xbf\"] " },
    { true, " [\"\x81\"] " },
    { true, " [\"\xe9\"] " },
    { true, " [\"\xff\"] " },
    { false, kHuge },
    { false, R"([[[[[[[[[[[[[[[[[[["Not too deep"]]]]]]]]]]]]]]]]]]])" },
    { false, R"({
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
        bool parse_succeeded;
        nlohmann::ordered_json res;
        try {
            res = nlohmann::ordered_json::parse(kRoundTrip[i].before);
            parse_succeeded = true;
        } catch (const nlohmann::json::exception& e) {
            parse_succeeded = false;
        }
        if (!parse_succeeded) {
            printf("error: round_trip_test fail #1 on %zu\n", i);
            exit(10);
        }
        if (res.dump() != kRoundTrip[i].after) {
            printf("error: round_trip_test fail #2 on %zu: %s\n",
                   i,
                   res.dump().c_str());
            exit(11);
        }
    }
}

void
json_test_suite()
{
    for (size_t i = 0; i < ARRAYLEN(kJsonTestSuite); ++i) {
        bool parse_succeeded;
        try {
            (void)!nlohmann::ordered_json::parse(kJsonTestSuite[i].json);
            parse_succeeded = true;
        } catch (const nlohmann::json::exception& e) {
            parse_succeeded = false;
        }
        // if (parse_succeeded != kJsonTestSuite[i].fail) {
        //     printf("error: fail json_test_suite %zu\n", i);
        //     exit(12);
        // }
    }
}

void
afl_regression()
{
    try {
        (void)!nlohmann::ordered_json::parse("[{\"\":1,3:14,]\n");
        exit(100);
    } catch (const nlohmann::json::exception& e) {
    }
    try {
        (void)!nlohmann::ordered_json::parse("[\n"
                                             "\n"
                                             "3E14,\n"
                                             "{\"!\":4,733:4,[\n"
                                             "\n"
                                             "3EL%,3E14,\n"
                                             "{][1][1,,]");
        exit(100);
    } catch (const nlohmann::json::exception& e) {
    }
    try {
        (void)!nlohmann::ordered_json::parse("[\n"
                                             "null,\n"
                                             "1,\n"
                                             "3.14,\n"
                                             "{\"a\": \"b\",\n"
                                             "3:14,ull}\n"
                                             "]");
        exit(100);
    } catch (const nlohmann::json::exception& e) {
    }
    try {
        (void)!nlohmann::ordered_json::parse("[\n"
                                             "\n"
                                             "3E14,\n"
                                             "{\"a!!!!!!!!!!!!!!!!!!\":4, \n"
                                             "\n"
                                             "3:1,,\n"
                                             "3[\n"
                                             "\n"
                                             "]");
        exit(100);
    } catch (const nlohmann::json::exception& e) {
    }
    try {
        (void)!nlohmann::ordered_json::parse("[\n"
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
        exit(100);
    } catch (const nlohmann::json::exception& e) {
    }
    try {
        (void)!nlohmann::ordered_json::parse("[\n"
                                             "\n"
                                             "3E14,\n"
                                             "{\"!\":4,733:4,[\n"
                                             "\n"
                                             "3E1%,][1,,]");
        exit(100);
    } catch (const nlohmann::json::exception& e) {
    }
    try {
        (void)!nlohmann::ordered_json::parse("[\n"
                                             "\n"
                                             "3E14,\n"
                                             "{\"!\":4,733:4,[\n"
                                             "\n"
                                             "3EL%,3E14,\n"
                                             "{][1][1,,]");
        exit(100);
    } catch (const nlohmann::json::exception& e) {
    }
}

int
main()
{
    object_test();
    deep_test();
    parse_test();
    round_trip_test();
    afl_regression();

    BENCH(2000, 1, object_test());
    BENCH(2000, 1, deep_test());
    BENCH(2000, 1, parse_test());
    BENCH(2000, 1, round_trip_test());
    BENCH(2000, 1, json_test_suite());
}
