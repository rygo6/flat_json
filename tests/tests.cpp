////////////////////////////////////////////////////////////////////////////////
// @author: rygo6
// tests.cpp - Runs JSON, arena, file, and generated property regressions.
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

#include "File.hpp"
#include "Document.hpp"
#include <unistd.h>

using namespace Flat;
using namespace Flat::Document;
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ARRAYLEN(A) ((sizeof(A) / sizeof(*(A))) / ((unsigned)!(sizeof(A) % sizeof(*(A)))))

#define STRING(sl) sl, sizeof(sl) - 1


static bool WriteTextFile(const char* pPath, const char* pText)
{
  WritableFile output(pPath);
  return output.IsValid() && output.Write(pText, strlen(pText)) && output.Flush();
}

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

#define BENCH(ITERATIONS, WORK_PER_RUN, CODE)                                                        \
  do {                                                                                               \
    struct timespec start, end;                                                                      \
    clock_gettime(CLOCK_MONOTONIC, &start);                                                          \
    for (int __i = 0; __i < ITERATIONS; ++__i) {                                                     \
      __asm__ volatile("" ::: "memory");                                                             \
      CODE;                                                                                          \
    }                                                                                                \
    clock_gettime(CLOCK_MONOTONIC, &end);                                                            \
    long long duration = (end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec); \
    long long work     = (WORK_PER_RUN) * (ITERATIONS);                                              \
    double nanos       = (duration + work - 1) / (double)work;                                       \
    printf("%10g ns %2dx %s\n", nanos, (ITERATIONS), #CODE);                                         \
  } while (0)

void ObjectTest()
{
  FixedArray<char, 1024> output;
  if (WriteJSON(ObjectValue({{"content", "hello"}}), output) != SUCCESS || strcmp(output.data, "{\"content\":\"hello\"}"))
    exit(1);
}

void DirectSerializationTest()
{
  char output[1024];
  if (WriteJSON(ObjectValue({{"answer", 42}}), output) != SUCCESS)
    exit(17);
  if (strcmp(output, "{\"answer\":42}"))
    exit(18);

  char roundTripOutput[1024];
  if (WriteJSON(ObjectValue({{"model", "gpt-5"}, {"stream", true}}), roundTripOutput) != SUCCESS)
    exit(40);
  if (strcmp(roundTripOutput, "{\"model\":\"gpt-5\",\"stream\":true}"))
    exit(41);

  FixedArena<512> parseArenaStorage;
  Arena parseArena     = parseArenaStorage;
  const Node* parseArenaRoot = nullptr;
  Result status        = ParseJSON(roundTripOutput, strlen(roundTripOutput), &parseArena, &parseArenaRoot);
  if (status != SUCCESS)
    exit(44);
  const Node* pJson  = parseArenaRoot;
  String model = (*pJson)["model"].GetString();
  if (model.size != 5 || strcmp(model.data, "gpt-5") || !(*pJson)["stream"].GetBool())
    exit(44);
  char parsedOutput[1024];
  if (WriteJSON(*pJson, parsedOutput) != SUCCESS)
    exit(45);
  if (strcmp(parsedOutput, roundTripOutput))
    exit(45);

  char smallOutput[5];
  if (WriteJSON(ObjectValue({{"too", "large"}}), smallOutput) != ERROR_INSUFFICIENT_SPACE)
    exit(53);
  if (WriteJSON(Value(nullptr), smallOutput) != SUCCESS || strcmp(smallOutput, "null"))
    exit(54);
}

void PublicSoftFailureTest()
{
  FixedArena<64> arenaStorage;
  Arena arena     = arenaStorage;
  const Node* arenaRoot = nullptr;

  if (ParseJSON("null", (Arena*)nullptr, &arenaRoot) != ERROR_INVALID_ARGUMENT)
    exit(200);
  if (ParseJSON((const char*)nullptr, 1, &arena, &arenaRoot) != ERROR_INVALID_ARGUMENT || arenaRoot)
    exit(202);
  if (ParseJSON((const char*)nullptr, 0, &arena, &arenaRoot) != ABSENT_VALUE || arenaRoot)
    exit(203);
  Result smallArenaStatus = ParseJSON("1.00000000000000011102230246251565404236316680908203125", &arena, &arenaRoot);
  if (smallArenaStatus != SUCCESS || !arenaRoot || !arenaRoot->IsDouble()) {
    fprintf(stderr, "small numeric arena returned %s\n", string_Result(smallArenaStatus));
    exit(210);
  }
  arena     = arenaStorage;
  arenaRoot = nullptr;
  if (ParseJSON("null", &arena, &arenaRoot) != SUCCESS || !arenaRoot || !arenaRoot->IsNull())
    exit(211);

  FixedArena<24> insufficientStringArenaStorage;
  Arena insufficientStringArena     = insufficientStringArenaStorage;
  const Node* insufficientStringArenaRoot = nullptr;
  if (ParseJSON(R"("123456789")", &insufficientStringArena, &insufficientStringArenaRoot) != ERROR_INSUFFICIENT_SPACE || insufficientStringArenaRoot)
    exit(212);
  FixedArena<40> insufficientEscapeArenaStorage;
  Arena insufficientEscapeArena     = insufficientEscapeArenaStorage;
  const Node* insufficientEscapeArenaRoot = nullptr;
  if (ParseJSON(R"("\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061\u0061")",
                  &insufficientEscapeArena, &insufficientEscapeArenaRoot) != ERROR_INSUFFICIENT_SPACE ||
      insufficientEscapeArenaRoot)
    exit(213);
  FixedArena<31> insufficientArrayArenaStorage;
  Arena insufficientArrayArena     = insufficientArrayArenaStorage;
  const Node* insufficientArrayArenaRoot = nullptr;
  if (ParseJSON("[0]", &insufficientArrayArena, &insufficientArrayArenaRoot) != ERROR_INSUFFICIENT_SPACE || insufficientArrayArenaRoot)
    exit(214);
  FixedArena<64> insufficientObjectArenaStorage;
  Arena insufficientObjectArena     = insufficientObjectArenaStorage;
  const Node* insufficientObjectArenaRoot = nullptr;
  if (ParseJSON(R"({"a":0})", &insufficientObjectArena, &insufficientObjectArenaRoot) != ERROR_INSUFFICIENT_SPACE || insufficientObjectArenaRoot)
    exit(215);

  if (ParseJSON(FileMap("bin/file-does-not-exist.json"), &arena, &arenaRoot) != ABSENT_VALUE || arenaRoot)
    exit(207);
  FileMap invalidInput(nullptr);
  if (invalidInput.IsValid())
    exit(209);
  WritableFile invalidFile("bin/missing/file.json");
  if (invalidFile.IsValid())
    exit(217);
  WritableFileMap invalidOutput(4, "bin/missing/file-not-created.json");
  if (invalidOutput.IsValid())
    exit(216);
}

void FileMapRoundTripTest()
{
  static constexpr char Path[]     = "file_map_round_trip_test.json";
  static constexpr char Expected[] = "{\"model\":\"gpt-5\",\"stream\":true,\"number\":3.14,\"escaped\":\"line\\n\"}";

  {
    WritableFileMap output(4, Path);
    if (!output.IsValid())
      exit(83);
    memcpy(output.data, "null", 4);
  }
  {
    FileMap input(Path);
    if (!input.IsValid() || input.size != 4 || memcmp(input.data, "null", 4))
      exit(84);
  }

  char text[4096];
  if (WriteJSON(ObjectValue({{"model", "gpt-5"}, {"stream", true}, {"number", 3.14}, {"escaped", "line\n"}}), text) != SUCCESS || !WriteTextFile(Path, text))
    exit(80);

  {
    FileMap input(Path);
    if (!input.IsValid() || input.size != sizeof(Expected) - 1 || memcmp(input.data, Expected, input.size))
      exit(81);
  }

  FixedArena<512> arenaStorage;
  Arena arena     = arenaStorage;
  const Node* arenaRoot = nullptr;
  if (ParseJSON(FileMap(Path), &arena, &arenaRoot) != SUCCESS)
    exit(82);
  const Node* pJson  = arenaRoot;
  String model = (*pJson)["model"].GetString();
  if (model.size != 5 || strcmp(model.data, "gpt-5") || !(*pJson)["stream"].GetBool() || (*pJson)["number"].GetDouble() != 3.14 || strcmp((*pJson)["escaped"].GetString().data, "line\n"))
    exit(82);
  unlink(Path);
}

void WritableFileRoundTripTest()
{
  static constexpr char Path[] = "writable_file_round_trip_test.json";
  static constexpr char Text[] = R"({"name":"flat-json","enabled":true,"values":[-1,0,42,3.5],"nested":{"escaped":"line\n","none":null}})";

  FixedArena<2048> sourceArenaStorage;
  Arena sourceArena     = sourceArenaStorage;
  const Node* sourceArenaRoot = nullptr;
  if (ParseJSON(Text, &sourceArena, &sourceArenaRoot) != SUCCESS)
    exit(219);

  char text[4096];
  if (WriteJSON(*sourceArenaRoot, text) != SUCCESS || !WriteTextFile(Path, text))
    exit(220);

  {
    FileMap input(Path);
    if (!input.IsValid() || input.size != sizeof(Text) - 1 || memcmp(input.data, Text, input.size))
      exit(221);
  }

  FixedArena<2048> destinationArenaStorage;
  Arena destinationArena     = destinationArenaStorage;
  const Node* destinationArenaRoot = nullptr;
  if (ParseJSON(FileMap(Path), &destinationArena, &destinationArenaRoot) != SUCCESS)
    exit(222);
  const Node* pJson = destinationArenaRoot;
  if (strcmp((*pJson)["name"].GetString().data, "flat-json") || !(*pJson)["enabled"].GetBool() || (*pJson)["values"][0].GetLong() != -1 || (*pJson)["values"][2].GetLong() != 42 ||
      (*pJson)["values"][3].GetDouble() != 3.5 || strcmp((*pJson)["nested"]["escaped"].GetString().data, "line\n") || !(*pJson)["nested"]["none"].IsNull())
    exit(223);

  char pJsonText[4096];
  if (WriteJSON(*pJson, pJsonText) != SUCCESS || strcmp(pJsonText, Text))
    exit(224);
  unlink(Path);
}

void LargeObjectIndexTest()
{
  char text[8192];
  char* pCursor = text;
  *pCursor++    = '{';
  for (int key = 100; key >= 0; --key) {
    size_t remaining = sizeof(text) - (size_t)(pCursor - text);
    int count        = snprintf(pCursor, remaining, "%s\"key%03d\":%d", key == 100 ? "" : ",", key, key);
    if (count < 0 || (size_t)count >= remaining)
      exit(55);
    pCursor += count;
  }
  *pCursor++ = '}';
  *pCursor   = '\0';

  FixedArena<32768> arenaStorage;
  Arena arena     = arenaStorage;
  const Node* arenaRoot = nullptr;
  if (ParseJSON(text, strlen(text), &arena, &arenaRoot) != SUCCESS)
    exit(55);
  const Node* pJson = arenaRoot;
  if (pJson->GetSize() != 101 || !pJson->Contains("key100") || !pJson->Contains("key000") || pJson->Contains("missing") || !pJson->HasKey("key100") || !pJson->HasKey("key000") ||
      pJson->HasKey("missing") || (*pJson)["key100"].GetLong() != 100 || (*pJson)["key050"].GetLong() != 50 || (*pJson)["key000"].GetLong() != 0)
    exit(55);

  char output[8192];
  if (pJson->ToString(output) != SUCCESS || strcmp(output, text))
    exit(56);
}

void MediumObjectLookupTest()
{
  char text[2048];
  char* pCursor = text;
  *pCursor++    = '{';
  for (int key = 0; key < 32; ++key) {
    size_t remaining = sizeof(text) - (size_t)(pCursor - text);
    int count        = snprintf(pCursor, remaining, "%s\"key%02d\":%d", key ? "," : "", key, key);
    if (count < 0 || (size_t)count >= remaining)
      exit(223);
    pCursor += count;
  }
  memcpy(pCursor, ",\"target\":31337}", sizeof(",\"target\":31337}"));

  FixedArena<8192> arenaStorage;
  Arena arena     = arenaStorage;
  const Node* arenaRoot = nullptr;
  if (ParseJSON(text, strlen(text), &arena, &arenaRoot) != SUCCESS)
    exit(224);
  const Node* pJson = arenaRoot;
  if ((*pJson)["key00"].GetLong() != 0 || (*pJson)["key15"].GetLong() != 15 || (*pJson)["key31"].GetLong() != 31 || (*pJson)["target"].GetLong() != 31337 || pJson->Contains("missing"))
    exit(225);
}

static uint64_t DoubleBits(double value)
{
  uint64_t bits;
  memcpy(&bits, &value, sizeof(bits));
  return bits;
}

void NumericArenaTest()
{
  static constexpr char FilePath[] = "numeric_file_output_test.json";
  static const double values[]     = {
      0.0,      -0.0,      0.1, 1e-7, 1e-6, 1e20, 1e21, DBL_MIN, DBL_MAX, 4.9406564584124654e-324, 2.2250738585072014e-308, 9007199254740991.0, 3.5844466002796428e+298, 1.7039356390957979e-287,
      INFINITY, -INFINITY,
  };

  for (size_t i = 0; i < ARRAYLEN(values); ++i) {
    char text[8192];
    if (WriteJSON(values[i], text) != SUCCESS)
      exit(27);

    FixedArena<8192> parseArenaStorage;
    Arena parseArena     = parseArenaStorage;
    const Node* parseArenaRoot = nullptr;
    Result status        = ParseJSON(text, strlen(text), &parseArena, &parseArenaRoot);
    if (status != SUCCESS)
      exit(28);
    const Node* pJson = parseArenaRoot;
    double expected   = values[i] == 0.0 ? 0.0 : values[i];
    if (DoubleBits(pJson->GetNumber()) != DoubleBits(expected))
      exit(29);

    if (!WriteTextFile(FilePath, text))
      exit(31);
    FileMap file(FilePath);
    if (!file.IsValid() || file.size != strlen(text) || memcmp(file.data, text, file.size))
      exit(32);
  }

  char special[4096];
  if (WriteJSON(ArrayValue({NAN, INFINITY, -INFINITY, 1.25f}), special) != SUCCESS)
    exit(30);
  if (strcmp(special, "[null,1e5000,-1e5000,1.25]"))
    exit(30);

  char minimum[32];
  if (WriteJSON(LLONG_MIN, minimum) != SUCCESS || !WriteTextFile(FilePath, minimum))
    exit(33);
  {
    FileMap file(FilePath);
    if (!file.IsValid() || file.size != 20 || memcmp(file.data, "-9223372036854775808", 20))
      exit(34);
  }
  unlink(FilePath);
}

void FastDecimalDifferentialTest()
{
  static const uint64_t boundaries[] = {
      1, 5, 9, 123456789, (1ull << 53) - 1, 1ull << 53, 9999999999999999999ull,
  };

  uint64_t random = 0x9e3779b97f4a7c15ull;
  for (int exponent = -64; exponent <= 38; ++exponent) {
    for (size_t index = 0; index < ARRAYLEN(boundaries) + 16; ++index) {
      uint64_t significand;
      if (index < ARRAYLEN(boundaries)) {
        significand = boundaries[index];
      } else {
        random      = random * 6364136223846793005ull + 1442695040888963407ull;
        significand = random % 9999999999999999999ull + 1;
      }
      char text[64];
      int size = snprintf(text, sizeof(text), "%llue%d", (unsigned long long)significand, exponent);
      char* pConvertedEnd;
      double expected = strtod(text, &pConvertedEnd);
      if (pConvertedEnd != text + size)
        exit(31);

      FixedArena<512> arenaStorage;
      Arena arena     = arenaStorage;
      const Node* arenaRoot = nullptr;
      if (ParseJSON(text, size, &arena, &arenaRoot) != SUCCESS || DoubleBits(arenaRoot->GetDouble()) != DoubleBits(expected))
        exit(32);
    }
  }
}

void StrictStringTest()
{
  struct Input {
    const char* data;
    size_t size;
  };
  static const Input invalid[] = {
      {STRING("[\"\\x00\"]")},    {STRING("[\"a\0a\"]")},         {STRING("[\"new\nline\"]")},        {STRING("[\"\t\"]")},       {STRING("[\"\x80\"]")},
      {STRING("[\"\xc0\x80\"]")}, {STRING("[\"\xed\xa0\x80\"]")}, {STRING("[\"\xf4\x90\x80\x80\"]")}, {STRING("[\"\xe2\x82\"]")},
  };

  for (size_t i = 0; i < ARRAYLEN(invalid); ++i) {
    FixedArena<1024> arenaStorage;
    Arena arena     = arenaStorage;
    const Node* arenaRoot = nullptr;
    if (ParseJSON(invalid[i].data, invalid[i].size, &arena, &arenaRoot) != ERROR_MALFORMED || arenaRoot)
      exit(31);
  }

  static const Input valid[] = {
      {STRING("[\"\\u0000\"]")},
      {STRING("[\"\\b\\f\\n\\r\\t\"]")},
      {STRING("[\"\xc2\x80\xe0\xa0\x80\xf0\x90\x80\x80\xf4\x8f\xbf\xbf\"]")},
      // Preserve the implementation-defined unmatched-surrogate behavior
      // recorded in README.md.
      {STRING("[\"\\uD800\"]")},
  };
  static const size_t validSizes[] = {1, 5, 13, 6};
  for (size_t i = 0; i < ARRAYLEN(valid); ++i) {
    FixedArena<1024> arenaStorage;
    Arena arena     = arenaStorage;
    const Node* arenaRoot = nullptr;
    if (ParseJSON(valid[i].data, valid[i].size, &arena, &arenaRoot) != SUCCESS)
      exit(32);
    const Node* pJson   = arenaRoot;
    String string = (*pJson)[0].GetString();
    if (string.size != validSizes[i] || string[string.size] != '\0')
      exit(60);
  }
}

static uint64_t FuzzRandom(uint64_t& state)
{
  state ^= state >> 12;
  state ^= state << 25;
  state ^= state >> 27;
  return state * 2685821657736338717ull;
}

struct FuzzText {
  char* pData;
  size_t capacity;
  size_t size = 0;

  bool Add(char value)
  {
    if (size == capacity)
      return false;
    pData[size++] = value;
    return true;
  }

  bool Append(const char* pSource, size_t count)
  {
    if (count > capacity - size)
      return false;
    memcpy(pData + size, pSource, count);
    size += count;
    return true;
  }

  template <size_t Size>
  bool Append(const char (&text)[Size]) { return Append(text, Size - 1); }
};

static bool GenerateJsonValue(FuzzText& text, uint64_t& random, int depth)
{
  static const char* const numbers[] = {
      "0",
      "-0",
      "1",
      "-1",
      "2147483647",
      "-2147483648",
      "0.0",
      "-0.0",
      "0.1",
      "1e-20",
      "3.4028235e38",
      "9223372036854775807",
      "-9223372036854775808",
      "4.9406564584124654e-324",
      "1.7976931348623157e308",
  };
  static const char* const strings[] = {
      R"("")", R"("ASCII")", R"("quote\"slash\\line\n\t")", R"("\u0000\u001f")", R"("\u03c0\u20ac\uD834\uDD1E")", "\"caf\xc3\xa9\"",
  };
  static const char* const keys[] = {
      R"("a")", R"("")", R"("escaped\nkey")", R"("\u03c0")", R"("duplicate")",
  };

  uint64_t choice = FuzzRandom(random) % (depth ? 8 : 6);
  switch (choice)
  {
    case 0:
      return text.Append("null");
    case 1: {
      bool value = FuzzRandom(random) & 1;
      return text.Append(value ? "true" : "false", value ? 4 : 5);
    }
    case 2: {
      const char* pNumber = numbers[FuzzRandom(random) % ARRAYLEN(numbers)];
      return text.Append(pNumber, strlen(pNumber));
    }
    case 3: {
      const char* pString = strings[FuzzRandom(random) % ARRAYLEN(strings)];
      return text.Append(pString, strlen(pString));
    }
    case 4: {
      long long value = (long long)(FuzzRandom(random) % 2000000001ull) - 1000000000ll;
      char number[32];
      int size = snprintf(number, sizeof(number), "%lld", value);
      return size > 0 && (size_t)size < sizeof(number) && text.Append(number, (size_t)size);
    }
    case 5:
      return text.Append(FuzzRandom(random) & 1 ? "[]" : "{}", 2);
    case 6: {
      if (!text.Add('['))
        return false;
      size_t count = FuzzRandom(random) % 4;
      for (size_t i = 0; i < count; ++i) {
        if ((i && !text.Add(',')) || !GenerateJsonValue(text, random, depth - 1))
          return false;
      }
      return text.Add(']');
    }
    default: {
      if (!text.Add('{'))
        return false;
      size_t count = FuzzRandom(random) % 4;
      for (size_t i = 0; i < count; ++i) {
        const char* pKey = keys[FuzzRandom(random) % ARRAYLEN(keys)];
        if ((i && !text.Add(',')) || !text.Append(pKey, strlen(pKey)) || !text.Add(':') || !GenerateJsonValue(text, random, depth - 1))
          return false;
      }
      return text.Add('}');
    }
  }
}

static bool CanonicalizeIfAccepted(const char* pData, size_t size, char* pCanonical, size_t capacity)
{
  FixedArena<128 * 1024> firstStorage;
  Arena first     = firstStorage;
  const Node* firstRoot = nullptr;
  Result status   = ParseJSON(pData, size, &first, &firstRoot);
  if (status != SUCCESS) {
    if (status != ERROR_MALFORMED && status != ABSENT_VALUE)
      exit(301);
    return false;
  }
  if (firstRoot->ToString(Span<char>(capacity, pCanonical)) != SUCCESS)
    exit(302);

  size_t estimate = EstimateSize(pData, size);
  FixedArena<256 * 1024> estimatedStorage;
  Arena estimated     = estimatedStorage;
  const Node* estimatedRoot = nullptr;
  if (estimate == SIZE_MAX || estimate > sizeof(estimatedStorage.bytes))
    exit(303);
  estimated.capacity = estimate;
  if (ParseJSON(pData, size, &estimated, &estimatedRoot) != SUCCESS)
    exit(304);

  char pretty[32 * 1024];
  if (firstRoot->ToStringPretty(pretty) != SUCCESS)
    exit(305);
  FixedArena<128 * 1024> secondStorage;
  Arena second     = secondStorage;
  const Node* secondRoot = nullptr;
  if (ParseJSON(pretty, strlen(pretty), &second, &secondRoot) != SUCCESS)
    exit(306);
  char secondCanonical[32 * 1024];
  if (secondRoot->ToString(secondCanonical) != SUCCESS || strcmp(secondCanonical, pCanonical))
    exit(307);
  return true;
}

static bool AddFuzzWhitespace(const char* pCanonical, FuzzText& output, uint64_t& random)
{
  static const char whitespace[] = {' ', '\t', '\n', '\r'};
  bool inString                  = false;
  bool escaped                   = false;
  size_t size                    = strlen(pCanonical);
  if (!output.Add(whitespace[FuzzRandom(random) % ARRAYLEN(whitespace)]))
    return false;
  for (size_t i = 0; i < size; ++i) {
    char value = pCanonical[i];
    if (!inString && (value == ']' || value == '}') && (FuzzRandom(random) & 1) && !output.Add(whitespace[FuzzRandom(random) % ARRAYLEN(whitespace)]))
      return false;
    if (!output.Add(value))
      return false;
    if (inString) {
      if (escaped) {
        escaped = false;
      } else if (value == '\\') {
        escaped = true;
      } else if (value == '"') {
        inString = false;
      }
    } else if (value == '"') {
      inString = true;
    } else if ((value == '[' || value == '{' || value == ',' || value == ':') && (FuzzRandom(random) & 1) && !output.Add(whitespace[FuzzRandom(random) % ARRAYLEN(whitespace)])) {
      return false;
    }
  }
  return output.Add(whitespace[FuzzRandom(random) % ARRAYLEN(whitespace)]);
}

void GeneratedDocumentFuzzTest()
{
  uint64_t random = 0xd1b54a32d192ed03ull;
  for (int iteration = 0; iteration < 512; ++iteration) {
    char input[2048];
    FuzzText generated{input, sizeof(input)};
    if (!GenerateJsonValue(generated, random, 6))
      exit(308);

    char canonical[32 * 1024];
    if (!CanonicalizeIfAccepted(input, generated.size, canonical, sizeof(canonical)))
      exit(309);

    char spaced[32 * 1024];
    FuzzText whitespace{spaced, sizeof(spaced)};
    if (!AddFuzzWhitespace(canonical, whitespace, random))
      exit(310);
    char spacedCanonical[32 * 1024];
    if (!CanonicalizeIfAccepted(spaced, whitespace.size, spacedCanonical, sizeof(spacedCanonical)) || strcmp(spacedCanonical, canonical))
      exit(311);
  }
}

void MutationFuzzTest()
{
  static const char* const seeds[] = {
      "null",
      R"([true,false,null,0,-1,3.1415927,"text"])",
      R"({"a":1,"b":[2,3],"c":{"d":"line\n","e":"\u03c0"}})",
      R"([[[{"key":"value","empty":[],"object":{}}]]])",
  };
  static const unsigned char mutations[] = {
      0, 1, ' ', '\n', '"', '\\', ',', ':', '[', ']', '{', '}', '-', '0', 'e', 0x80, 0xff,
  };

  for (size_t seedIndex = 0; seedIndex < ARRAYLEN(seeds); ++seedIndex) {
    const char* pSeed = seeds[seedIndex];
    size_t seedSize   = strlen(pSeed);
    for (size_t position = 0; position < seedSize; ++position) {
      char mutation[1024];
      memcpy(mutation, pSeed, position);
      memcpy(mutation + position, pSeed + position + 1, seedSize - position - 1);
      char canonical[4096];
      CanonicalizeIfAccepted(mutation, seedSize - 1, canonical, sizeof(canonical));

      for (size_t value = 0; value < ARRAYLEN(mutations); ++value) {
        memcpy(mutation, pSeed, seedSize);
        mutation[position] = (char)mutations[value];
        CanonicalizeIfAccepted(mutation, seedSize, canonical, sizeof(canonical));
      }
    }
    for (size_t position = 0; position <= seedSize; ++position) {
      for (size_t value = 0; value < ARRAYLEN(mutations); ++value) {
        char mutation[1024];
        memcpy(mutation, pSeed, position);
        mutation[position] = (char)mutations[value];
        memcpy(mutation + position + 1, pSeed + position, seedSize - position);
        char canonical[4096];
        CanonicalizeIfAccepted(mutation, seedSize + 1, canonical, sizeof(canonical));
      }
    }
  }
}

void NumericBitPatternFuzzTest()
{

  uint64_t random = 0x94d049bb133111ebull;
  for (int iteration = 0; iteration < 4096; ++iteration) {
    uint64_t bits = FuzzRandom(random);
    if ((bits & 0x7ff0000000000000ull) == 0x7ff0000000000000ull)
      continue;
    double value;
    memcpy(&value, &bits, sizeof(value));
    char text[4096];
    if (WriteJSON(value, text) != SUCCESS)
      exit(312);
    FixedArena<4096> arenaStorage;
    Arena arena     = arenaStorage;
    const Node* arenaRoot = nullptr;
    Result status   = ParseJSON(text, strlen(text), &arena, &arenaRoot);
    if (status != SUCCESS) {
      fprintf(stderr, "double fuzz parse failed: bits=%016llx text=%s status=%s\n", (unsigned long long)bits, text, string_Result(status));
      exit(313);
    }
    double parsed         = arenaRoot->GetNumber();
    uint64_t expectedBits = value == 0 ? 0 : bits;
    if (DoubleBits(parsed) != expectedBits)
      exit(314);
    char canonical[4096];
    if (arenaRoot->ToString(canonical) != SUCCESS || strcmp(canonical, text))
      exit(315);
  }

  for (int iteration = 0; iteration < 4096; ++iteration) {
    uint32_t bits = (uint32_t)FuzzRandom(random);
    if ((bits & 0x7f800000u) == 0x7f800000u)
      continue;
    float value;
    memcpy(&value, &bits, sizeof(value));
    char text[4096];
    if (WriteJSON(value, text) != SUCCESS)
      exit(316);
    FixedArena<4096> arenaStorage;
    Arena arena     = arenaStorage;
    const Node* arenaRoot = nullptr;
    Result status   = ParseJSON(text, strlen(text), &arena, &arenaRoot);
    if (status != SUCCESS) {
      fprintf(stderr, "float fuzz parse failed: bits=%08x text=%s status=%s\n", bits, text, string_Result(status));
      exit(317);
    }
    float parsed = (float)arenaRoot->GetNumber();
    uint32_t parsedBits;
    memcpy(&parsedBits, &parsed, sizeof(parsedBits));
    uint32_t expectedBits = value == 0 ? 0 : bits;
    if (parsedBits != expectedBits)
      exit(318);
  }

  for (int iteration = 0; iteration < 4096; ++iteration) {
    uint64_t bits = FuzzRandom(random);
    long long value;
    memcpy(&value, &bits, sizeof(value));
    char text[128];
    if (WriteJSON(value, text) != SUCCESS)
      exit(319);
    FixedArena<512> arenaStorage;
    Arena arena     = arenaStorage;
    const Node* arenaRoot = nullptr;
    if (ParseJSON(text, strlen(text), &arena, &arenaRoot) != SUCCESS || !arenaRoot->IsLong() || arenaRoot->GetLong() != value)
      exit(320);
  }
}

template <typename Write>
static void OutputBoundaryCanaryCase(Write write, int error)
{
  static constexpr size_t GuardSize      = 32;
  static constexpr size_t OutputCapacity = 32 * 1024;
  char expected[OutputCapacity];
  if (write(Span<char>(sizeof(expected), expected)) != SUCCESS)
    exit(error);
  size_t required     = strlen(expected) + 1;
  size_t capacities[] = {0, required - 1, required, required + 7, 2048, 4096, OutputCapacity};
  bool succeeded      = false;
  for (size_t index = 0; index < ARRAYLEN(capacities); ++index) {
    size_t capacity = capacities[index];
    if (index && capacity == capacities[index - 1])
      continue;
    alignas(8) unsigned char storage[GuardSize + OutputCapacity + GuardSize];
    memset(storage, 0xa5, sizeof(storage));
    char* pOutput       = (char*)storage + GuardSize;
    Result status = write(Span<char>(capacity, pOutput));
    if (status != SUCCESS && status != ERROR_INSUFFICIENT_SPACE)
      exit(error + 1);
    if (capacity < required && status != ERROR_INSUFFICIENT_SPACE)
      exit(error + 1);
    if (succeeded && status != SUCCESS)
      exit(error + 1);
    succeeded |= status == SUCCESS;
    for (size_t i = 0; i < GuardSize; ++i) {
      if (storage[i] != 0xa5)
        exit(error + 2);
    }
    for (size_t i = GuardSize + capacity; i < sizeof(storage); ++i) {
      if (storage[i] != 0xa5)
        exit(error + 3);
    }
    if (status == SUCCESS && strcmp(pOutput, expected))
      exit(error + 4);
  }
  if (!succeeded)
    exit(error + 5);
}

void OutputBoundaryCanaryTest()
{
  static constexpr char Source[] = R"({"array":[null,true,false,-9223372036854775808,3.141592653589793],"string":"quote\"slash\\line\n\u03c0","object":{"empty":{},"items":[]}})";
  FixedArena<16 * 1024> arenaStorage;
  Arena arena     = arenaStorage;
  const Node* arenaRoot = nullptr;
  if (ParseJSON(Source, &arena, &arenaRoot) != SUCCESS)
    exit(321);
  const Node* pJson = arenaRoot;
  OutputBoundaryCanaryCase([&](Span<char> output) { return pJson->ToString(output); }, 322);
  OutputBoundaryCanaryCase([&](Span<char> output) { return pJson->ToStringPretty(output); }, 327);
  OutputBoundaryCanaryCase(
      [](Span<char> output) {
        return WriteJSON(ObjectValue({
                                   {"model", "gpt-5"},
                                   {"values", ArrayValue({0, 1, 2, 3.5, true, nullptr})},
                               }),
                               output);
      },
      332);
}

void ObjectThresholdFuzzTest()
{

  static const int counts[] = {1, 2, 31, 99, 100, 101, 127};
  for (size_t test = 0; test < ARRAYLEN(counts); ++test) {
    int count = counts[test];
    char text[32 * 1024];
    char* pCursor = text;
    *pCursor++    = '{';
    for (int key = count - 1; key >= 0; --key) {
      size_t remaining = sizeof(text) - (size_t)(pCursor - text);
      int size         = snprintf(pCursor, remaining, "%s\"key%03d\":%d", key == count - 1 ? "" : ",", key, key);
      if (size < 0 || (size_t)size >= remaining)
        exit(337);
      pCursor += size;
    }
    *pCursor++ = '}';
    *pCursor   = '\0';

    FixedArena<128 * 1024> arenaStorage;
    Arena arena     = arenaStorage;
    const Node* arenaRoot = nullptr;
    if (ParseJSON(text, (size_t)(pCursor - text), &arena, &arenaRoot) != SUCCESS || arenaRoot->GetSize() != (size_t)count)
      exit(338);
    for (int key = 0; key < count; ++key) {
      char name[16];
      int size = snprintf(name, sizeof(name), "key%03d", key);
      String keyName((size_t)size, name);
      if (!arenaRoot->HasKey(keyName) || (*arenaRoot)[keyName].GetLong() != key)
        exit(339);
    }
    if (arenaRoot->HasKey("key999") || arenaRoot->HasKey("missing"))
      exit(340);
    char output[32 * 1024];
    if (arenaRoot->ToString(output) != SUCCESS || strcmp(output, text))
      exit(341);
  }
}

void EmbeddedNulKeyTest()
{
  static constexpr char Text[] = R"({"a\u0000b":1,"a":2,"":3,"\u0000":4,"value":"x\u0000y"})";
  FixedArena<4096> arenaStorage;
  Arena arena     = arenaStorage;
  const Node* arenaRoot = nullptr;
  if (ParseJSON(Text, &arena, &arenaRoot) != SUCCESS)
    exit(342);
  const char nulKey[]  = {'a', '\0', 'b'};
  const char onlyNul[] = {'\0'};
  String value   = (*arenaRoot)["value"].GetString();
  if ((*arenaRoot)[String(sizeof(nulKey), nulKey)].GetLong() != 1 || (*arenaRoot)["a"].GetLong() != 2 || (*arenaRoot)[""].GetLong() != 3 ||
      (*arenaRoot)[String(sizeof(onlyNul), onlyNul)].GetLong() != 4 || value.size != 3 || value[0] != 'x' || value[1] != '\0' || value[2] != 'y' || value[3] != '\0')
    exit(343);
  char output[4096];
  if (arenaRoot->ToString(output) != SUCCESS || strcmp(output, Text))
    exit(344);
}

static size_t BuildNestedJson(char* pOutput, size_t capacity, int depth, bool alternating)
{
  FuzzText text{pOutput, capacity};
  for (int level = 0; level < depth; ++level) {
    if (!alternating || !(level & 1)) {
      if (!text.Add('['))
        return 0;
    } else if (!text.Append("{\"k\":")) {
      return 0;
    }
  }
  if (!text.Add('0'))
    return 0;
  for (int level = depth - 1; level >= 0; --level) {
    if (!text.Add(!alternating || !(level & 1) ? ']' : '}'))
      return 0;
  }
  return text.size;
}

void NestingAndRollbackFuzzTest()
{

  for (int alternating = 0; alternating < 2; ++alternating) {
    for (int depth = 0; depth <= 24; ++depth) {
      char text[1024];
      size_t size = BuildNestedJson(text, sizeof(text), depth, alternating);
      FixedArena<16 * 1024> arenaStorage;
      Arena arena     = arenaStorage;
      const Node* arenaRoot = nullptr;
      Result status   = ParseJSON(text, size, &arena, &arenaRoot);
      Result expected = depth <= 19 ? SUCCESS : ERROR_MALFORMED;
      if (status != expected || (status == SUCCESS) != (arenaRoot != nullptr)) {
        fprintf(stderr, "nesting boundary failed: alternating=%d depth=%d status=%s expected=%s\n", alternating, depth, string_Result(status), string_Result(expected));
        exit(345);
      }
    }
  }

  static constexpr char Stable[] = R"({"root":[1,2,{"x":"stable"}],"tail":true})";
  FixedArena<16 * 1024> arenaStorage;
  Arena arena     = arenaStorage;
  const Node* arenaRoot = nullptr;
  if (ParseJSON(Stable, &arena, &arenaRoot) != SUCCESS)
    exit(346);
  const Node* pStable = arenaRoot;
  size_t used         = arena.Used();
  char expected[1024];
  if (pStable->ToString(expected) != SUCCESS)
    exit(347);
  for (size_t size = 1; size < sizeof(Stable) - 1; ++size) {
    if (ParseJSON(Stable, size, &arena, &arenaRoot) != ERROR_MALFORMED || arenaRoot || arena.Used() != used)
      exit(348);
    char output[1024];
    if (pStable->ToString(output) != SUCCESS || strcmp(output, expected))
      exit(349);
  }
}

void ImmutableLayoutTest()
{
  FixedArena<2048> aStorage;
  Arena a       = aStorage;
  const Node* aRoot   = nullptr;
  Result status = ParseJSON(R"([1,[2,3],{"x":4,"s":"ok"}])", &a, &aRoot);
  if (status != SUCCESS)
    exit(20);
  const Node* pJson    = aRoot;
  u32 rootOffset = (u32)((const char*)pJson - (const char*)aStorage.bytes);
  if ((const char*)pJson < (const char*)aStorage.bytes || (const char*)pJson >= (const char*)aStorage.bytes + sizeof(aStorage.bytes))
    exit(21);
  if ((*pJson)[0].GetLong() != 1 || (*pJson)[1][1].GetLong() != 3 || (*pJson)[2]["x"].GetLong() != 4)
    exit(22);
  if (!pJson->HasSize() || !pJson->HasIndex(0) || !pJson->HasIndex(2) || pJson->HasIndex(3) || pJson->HasIndex(-1) || (*pJson)[0].HasSize() || (*pJson)[0].HasIndex(0) || !(*pJson)[2].HasKey("x") ||
      (*pJson)[2].HasKey("missing"))
    exit(59);
  if ((const char*)&(*pJson)[1] - (const char*)&(*pJson)[0] != sizeof(Node) || (const char*)&(*pJson)[2] - (const char*)&(*pJson)[1] != sizeof(Node) ||
      (const char*)&(*pJson)[1][1] - (const char*)&(*pJson)[1][0] != -(ptrdiff_t)sizeof(Node))
    exit(57);
  const Node& array  = pJson->GetArray();
  const Node& object = array[2].GetObject();
  if (&array != pJson || array.GetSize() != 3 || &object != &array[2] || object.GetSize() != 2)
    exit(50);
  if (pJson->span != sizeof(aStorage.bytes) - rootOffset)
    exit(23);
  alignas(8) char relocatedStorage[2048];
  memcpy(relocatedStorage, pJson, pJson->span);
  const Node* relocated = (const Node*)relocatedStorage;
  if ((*relocated)[0].GetLong() != 1 || (*relocated)[1][1].GetLong() != 3 || (*relocated)[2]["x"].GetLong() != 4 || strcmp((*relocated)[2]["s"].GetString().data, "ok"))
    exit(43);
  alignas(8) char relocatedArrayStorage[2048];
  memcpy(relocatedArrayStorage, &array[1], array[1].span);
  const Node* relocatedArray = (const Node*)relocatedArrayStorage;
  if ((*relocatedArray)[0].GetLong() != 2 || (*relocatedArray)[1].GetLong() != 3)
    exit(58);
  char output[2048];
  if (pJson->ToString(output) != SUCCESS)
    exit(24);
  if (strcmp(output, R"([1,[2,3],{"x":4,"s":"ok"}])"))
    exit(24);
  if (ParseJSON("[1,", &a, &aRoot) != ERROR_MALFORMED || aRoot || (*pJson)[2]["x"].GetLong() != 4)
    exit(26);
}

void DeepTest()
{
  char text[8192];
  if (WriteJSON(ObjectValue({{"content", ArrayValue({Value(ArrayValue({Value(ArrayValue({0, 10, 20, 3.14, 40}))}))})}}), text) != SUCCESS)
    exit(2);
  if (strcmp(text, "{\"content\":[[[0,10,20,3.14,40]]]}"))
    exit(2);
}

static struct {
  FixedArena<65536> storage;
  Arena arena = storage;
  const Node* pRoot = nullptr;
} staticArena;

void StaticArenaTest()
{
  char text[4096];
  if (WriteJSON(ObjectValue({{"name", "static"}, {"values", ArrayValue({1, 2})}}), text) != SUCCESS)
    exit(8);
  if (strcmp(text, "{\"name\":\"static\",\"values\":[1,2]}"))
    exit(8);
  Result status = ParseJSON("{\"k\": [true, null, 3.5]}", &staticArena.arena, &staticArena.pRoot);
  if (status != SUCCESS)
    exit(9);
  const Node* pJson = staticArena.pRoot;
  if (pJson->ToString(text) != SUCCESS || strcmp(text, "{\"k\":[true,null,3.5]}"))
    exit(13);
}

void StackArenaTest()
{
  FixedArena<16384> aStorage;
  Arena a       = aStorage;
  const Node* aRoot   = nullptr;
  Result status = ParseJSON("[1, \"two\", {\"three\": 3}]", &a, &aRoot);
  if (status != SUCCESS)
    exit(14);
  const Node* pJson = aRoot;
  char text[16384];
  if (pJson->ToString(text) != SUCCESS || strcmp(text, "[1,\"two\",{\"three\":3}]"))
    exit(15);
  if (strcmp((*pJson)[1].GetString().data, "two"))
    exit(16);
}

void ParseTest()
{
  FixedArena<65536> aStorage;
  Arena a       = aStorage;
  const Node* aRoot   = nullptr;
  Result status = ParseJSON("{ \"content\":[[[0,10,20,3.14,40]]]}", &a, &aRoot);
  if (status != SUCCESS)
    exit(3);
  const Node* pJson = aRoot;
  char text[65536];
  if (pJson->ToString(text) != SUCCESS || strcmp(text, "{\"content\":[[[0,10,20,3.14,40]]]}"))
    exit(4);
  if (pJson->ToStringPretty(text) != SUCCESS || strcmp(text, R"({"content": [[[0, 10, 20, 3.14, 40]]]})"))
    exit(5);
  status = ParseJSON("{ \"a\": 1, \"b\": [2,   3]}", &a, &aRoot);
  if (status != SUCCESS)
    exit(6);
  pJson = aRoot;
  if (pJson->ToString(text) != SUCCESS || strcmp(text, R"({"a":1,"b":[2,3]})"))
    exit(6);
  if (pJson->ToStringPretty(text) != SUCCESS || strcmp(text,
                                                             R"({
  "a": 1,
  "b": [2, 3]
})"))
    exit(7);
}

static const struct {
  const char* before;
  const char* after;
} kRoundTrip[] = {

    // types
    {"0", "0"},
    {"[]", "[]"},
    {"{}", "{}"},
    {"0.1", "0.1"},
    {"\"\"", "\"\""},
    {"[\"/\"]", "[\"/\"]"},
    {"[\"cafÃ©\"]", "[\"cafÃ©\"]"},
    {"null", "null"},
    {"true", "true"},
    {"false", "false"},

    // valid utf16 sequences
    {" [\"\\u0020\"] ", "[\" \"]"},
    {" [\"\\u00A0\"] ", "[\"\\u00a0\"]"},

    // when we encounter invalid utf16 sequences
    // we turn them into ascii
    {"[\"\\uDFAA\"]", "[\"\\\\uDFAA\"]"},
    {" [\"\\uDd1e\\uD834\"] ", "[\"\\\\uDd1e\\\\uD834\"]"},
    {" [\"\\ud800abc\"] ", "[\"\\\\ud800abc\"]"},
    {" [\"\\ud800\"] ", "[\"\\\\ud800\"]"},
    {" [\"\\uD800\\uD800\\n\"] ", "[\"\\\\uD800\\\\uD800\\n\"]"},
    {" [\"\\uDd1ea\"] ", "[\"\\\\uDd1ea\"]"},
    {" [\"\\uD800\\n\"] ", "[\"\\\\uD800\\n\"]"},

    // underflow and overflow
    {" [123.456e-789] ", "[0]"},
    {" [0."
     "4e0066999999999999999999999999999999999999999999999999999999999999999999"
     "9999999999999999999999999999999999999999999999999969999999006] ",
     "[1e5000]"},
    {" [1.5e+9999] ", "[1e5000]"},
    {" [-1.5e+9999] ", "[-1e5000]"},
    {" [-123123123123123123123123123123] ", "[-1.2312312312312312e+29]"},
};

// https://github.com/nst/JSONTestSuite/
static const struct {
  Result error;
  const char* json;
  size_t size;
} kJsonTestSuite[] = {
    {ERROR_MALFORMED, ""},
    {ERROR_MALFORMED, "[] []"},
    {ERROR_MALFORMED, "[nan]"},
    {ERROR_MALFORMED, "[-nan]"},
    {ERROR_MALFORMED, "[+NaN]"},
    {ERROR_MALFORMED, "{\"Extra value after close\": true} \"misplaced quoted value\""},
    {ERROR_MALFORMED, "{\"Illegal expression\": 1 + 2}"},
    {ERROR_MALFORMED, "{\"Illegal invocation\": alert()}"},
    {ERROR_MALFORMED, "{\"Numbers cannot have leading zeroes\": 013}"},
    {ERROR_MALFORMED, "{\"Numbers cannot be hex\": 0x14}"},
    {ERROR_MALFORMED, "[\\naked]"},
    {ERROR_MALFORMED, "[\"Illegal backslash escape: \\017\"]"},
    {ERROR_MALFORMED, "[[[[[[[[[[[[[[[[[[[[\"Too deep\"]]]]]]]]]]]]]]]]]]]]"},
    {ERROR_MALFORMED, "{\"Missing colon\" null}"},
    {ERROR_MALFORMED, "{\"Double colon\":: null}"},
    {ERROR_MALFORMED, "{\"Comma instead of colon\", null}"},
    {ERROR_MALFORMED, "[\"Colon instead of comma\": false]"},
    {ERROR_MALFORMED, "[\"Bad value\", truth]"},
    {ERROR_MALFORMED, "[\'single quote\']"},
    {ERROR_MALFORMED, "[\"tab\\   character\\   in\\  string\\  \"]"},
    {ERROR_MALFORMED, "[\"line\\\nbreak\"]"},
    {ERROR_MALFORMED, "[0e]"},
    {ERROR_MALFORMED, "[\"Unclosed array\""},
    {ERROR_MALFORMED, "[0e+]"},
    {ERROR_MALFORMED, "[0e+-1]"},
    {ERROR_MALFORMED, "{\"Comma instead if closing brace\": true,"},
    {ERROR_MALFORMED, "[\"mismatch\"}"},
    {ERROR_MALFORMED, "{unquoted_key: \"keys must be quoted\"}"},
    {ERROR_MALFORMED, "[\"extra comma\",]"},
    {ERROR_MALFORMED, "[\"double extra comma\",,]"},
    {ERROR_MALFORMED, "[   , \"<-- missing value\"]"},
    {ERROR_MALFORMED, "[\"Comma after the close\"],"},
    {ERROR_MALFORMED, "[\"Extra close\"]]"},
    {ERROR_MALFORMED, "{\"Extra comma\": true,}"},
    {ERROR_MALFORMED, " {\"a\" "},
    {ERROR_MALFORMED, " {\"a\": "},
    {ERROR_MALFORMED, " {:\"b\" "},
    {ERROR_MALFORMED, " {\"a\" b} "},
    {ERROR_MALFORMED, " {key: 'value'} "},
    {ERROR_MALFORMED, " {\"a\":\"a\" 123} "},
    {ERROR_MALFORMED, " \x7b\xf0\x9f\x87\xa8\xf0\x9f\x87\xad\x7d "},
    {ERROR_MALFORMED, " {[: \"x\"} "},
    {ERROR_MALFORMED, " [1.8011670033376514H-308] "},
    {ERROR_MALFORMED, " [1.2a-3] "},
    {ERROR_MALFORMED, " [.123] "},
    {ERROR_MALFORMED, " [1e\xe5] "},
    {ERROR_MALFORMED, " [1ea] "},
    {ERROR_MALFORMED, " [-1x] "},
    {ERROR_MALFORMED, " [-.123] "},
    {ERROR_MALFORMED, " [-foo] "},
    {ERROR_MALFORMED, " [-Infinity] "},
    {ERROR_MALFORMED, " \x5b\x30\xe5\x5d "},
    {ERROR_MALFORMED, " \x5b\x31\x65\x31\xe5\x5d "},
    {ERROR_MALFORMED, " \x5b\x31\x32\x33\xe5\x5d "},
    {ERROR_MALFORMED, " \x5b\x2d\x31\x32\x33\x2e\x31\x32\x33\x66\x6f\x6f\x5d "},
    {ERROR_MALFORMED, " [0e+-1] "},
    {ERROR_MALFORMED, " [Infinity] "},
    {ERROR_MALFORMED, " [0x42] "},
    {ERROR_MALFORMED, " [0x1] "},
    {ERROR_MALFORMED, " [1+2] "},
    {ERROR_MALFORMED, " \x5b\xef\xbc\x91\x5d "},
    {ERROR_MALFORMED, " [NaN] "},
    {ERROR_MALFORMED, " [Inf] "},
    {ERROR_MALFORMED, " [9.e+] "},
    {ERROR_MALFORMED, " [1eE2] "},
    {ERROR_MALFORMED, " [1e0e] "},
    {ERROR_MALFORMED, " [1.0e-] "},
    {ERROR_MALFORMED, " [1.0e+] "},
    {ERROR_MALFORMED, " [0e] "},
    {ERROR_MALFORMED, " [0e+] "},
    {ERROR_MALFORMED, " [0E] "},
    {ERROR_MALFORMED, " [0E+] "},
    {ERROR_MALFORMED, " [0.3e] "},
    {ERROR_MALFORMED, " [0.3e+] "},
    {ERROR_MALFORMED, " [0.1.2] "},
    {ERROR_MALFORMED, " [.2e-3] "},
    {ERROR_MALFORMED, " [.-1] "},
    {ERROR_MALFORMED, " [-NaN] "},
    {ERROR_MALFORMED, " [+Inf] "},
    {ERROR_MALFORMED, " [+1] "},
    {ERROR_MALFORMED, " [++1234] "},
    {ERROR_MALFORMED, " [tru] "},
    {ERROR_MALFORMED, " [nul] "},
    {ERROR_MALFORMED, " [fals] "},
    {ERROR_MALFORMED, " [{} "},
    {ERROR_MALFORMED, "\n[1,\n1\n,1  "},
    {ERROR_MALFORMED, " [1, "},
    {ERROR_MALFORMED, " [\"\" "},
    {ERROR_MALFORMED, " [* "},
    {ERROR_MALFORMED, " \x5b\x22\x0b\x61\x22\x5c\x66\x5d "},
    {ERROR_MALFORMED, "[\"a\",\n4\n,1,1  "},
    {ERROR_MALFORMED, " [1:2] "},
    {ERROR_MALFORMED, " \x5b\xff\x5d "},
    {ERROR_MALFORMED, " \x5b\x78 "},
    {ERROR_MALFORMED, " [\"x\" "},
    {ERROR_MALFORMED, " [\"\": 1] "},
    {ERROR_MALFORMED, " [a\xe5] "},
    {ERROR_MALFORMED, " {\"x\", null} "},
    {ERROR_MALFORMED, " [\"x\", truth] "},
    {ERROR_MALFORMED, STRING("\x00")},
    {ERROR_MALFORMED, "\n[\"x\"]]"},
    {ERROR_MALFORMED, " [012] "},
    {ERROR_MALFORMED, " [-012] "},
    {ERROR_MALFORMED, " [1 000.0] "},
    {ERROR_MALFORMED, " [-01] "},
    {ERROR_MALFORMED, " [- 1] "},
    {ERROR_MALFORMED, " [-] "},
    {ERROR_MALFORMED, " {\"\xb9\":\"0\",} "},
    {ERROR_MALFORMED, " {\"x\"::\"b\"} "},
    {ERROR_MALFORMED, " [1,,] "},
    {ERROR_MALFORMED, " [1,] "},
    {ERROR_MALFORMED, " [1,,2] "},
    {ERROR_MALFORMED, " [,1] "},
    {ERROR_MALFORMED, " [ 3[ 4]] "},
    {ERROR_MALFORMED, " [1 true] "},
    {ERROR_MALFORMED, " [\"a\" \"b\"] "},
    {ERROR_MALFORMED, " [--2.] "},
    {ERROR_MALFORMED, " [1.] "},
    {ERROR_MALFORMED, " [2.e3] "},
    {ERROR_MALFORMED, " [2.e-3] "},
    {ERROR_MALFORMED, " [2.e+3] "},
    {ERROR_MALFORMED, " [0.e1] "},
    {ERROR_MALFORMED, " [-2.] "},
    {ERROR_MALFORMED, " \xef\xbb\xbf{} "},
    {ERROR_MALFORMED, STRING(" [\x00\"\x00\xe9\x00\"\x00]\x00 ")},
    {ERROR_MALFORMED, STRING(" \x00[\x00\"\x00\xe9\x00\"\x00] ")},
    {SUCCESS, kHuge},
    {SUCCESS, R"([[[[[[[[[[[[[[[[[[["Not too deep"]]]]]]]]]]]]]]]]]]])"},
    {SUCCESS, R"({
    "JSON Test Pattern pass3": {
        "The outermost value": "must be an object or array.",
        "In this test": "It is an object."
    }
}
)"},
};

void EstimateSizeTest()
{
  static const char* inputs[] = {
      "null",
      R"({"empty":[],"scalars":[null,true,false,0,-1],"nested":[["text"],{"key":"value"}]})",
      R"({"key00":0,"key01":1,"key02":2,"key03":3,"key04":4,"key05":5,"key06":6,"key07":7,"key08":8,"key09":9,"key10":10,"key11":11,"key12":12,"key13":13,"key14":14,"key15":15,"key16":16})",
      R"("escaped\nstring\uD834\uDD1E")",
      "1.00000000000000011102230246251565404236316680908203125",
      R"([[[[[[[[[[[[[[[[[[[0]]]]]]]]]]]]]]]]]]])",
  };
  if (EstimateSize((const char*)nullptr, 0) || EstimateSize((const char*)nullptr, 1) != SIZE_MAX || EstimateSize("null") != EstimateSize("null", 4))
    exit(218);

  for (size_t i = 0; i < ARRAYLEN(inputs); ++i) {
    size_t size     = strlen(inputs[i]);
    size_t estimate = EstimateSize(inputs[i], size);
    FixedArena<64 * 1024> bufferStorage;
    Arena buffer     = bufferStorage;
    const Node* bufferRoot = nullptr;
    if (estimate == SIZE_MAX || estimate > sizeof(bufferStorage.bytes))
      exit(219);
    buffer.capacity = estimate;
    if (ParseJSON(inputs[i], size, &buffer, &bufferRoot) != SUCCESS)
      exit(220);
  }
}

void RoundTripTest()
{

  for (size_t i = 0; i < ARRAYLEN(kRoundTrip); ++i) {
    FixedArena<65536> aStorage;
    Arena a     = aStorage;
    const Node* aRoot = nullptr;
    size_t inputSize  = strlen(kRoundTrip[i].before);
    size_t estimate   = EstimateSize(kRoundTrip[i].before, inputSize);
    if (estimate == SIZE_MAX || estimate > sizeof(aStorage.bytes))
      exit(221);
    a.capacity          = estimate;
    Result status = ParseJSON(kRoundTrip[i].before, inputSize, &a, &aRoot);
    if (status != SUCCESS) {
      printf("error: ParseJSON returned Node::%s but wanted Node::%s: %s\n", string_Result(status), string_Result(SUCCESS), kRoundTrip[i].before);
      exit(10);
    }
    const Node* pJson = aRoot;
    char got[65536];
    if (pJson->ToString(got) != SUCCESS)
      exit(11);
    if (strcmp(got, kRoundTrip[i].after)) {
      printf("error: ParseJSON(%s).ToString() was %s but should have "
             "been %s\n",
             kRoundTrip[i].before, got, kRoundTrip[i].after);
      exit(11);
    }
  }
}

void JsonTestSuite()
{

  for (size_t i = 0; i < ARRAYLEN(kJsonTestSuite); ++i) {
    FixedArena<256 * 1024> aStorage;
    Arena a     = aStorage;
    const Node* aRoot = nullptr;
    size_t inputSize  = kJsonTestSuite[i].size ? kJsonTestSuite[i].size : strlen(kJsonTestSuite[i].json);
    if (kJsonTestSuite[i].error == SUCCESS) {
      size_t estimate = EstimateSize(kJsonTestSuite[i].json, inputSize);
      if (estimate == SIZE_MAX || estimate > sizeof(aStorage.bytes))
        exit(222);
      a.capacity = estimate;
    }
    Result status = ParseJSON(kJsonTestSuite[i].json, inputSize, &a, &aRoot);
    if (status != kJsonTestSuite[i].error) {
      printf("error: ParseJSON returned Node::%s but wanted Node::%s: %s\n", string_Result(status), string_Result(kJsonTestSuite[i].error), kJsonTestSuite[i].json);
      exit(12);
    }
  }
}

void AflRegression()
{
  FixedArena<65536> aStorage;
  Arena a     = aStorage;
  const Node* aRoot = nullptr;
  auto parse        = [&](const char* pText) { ParseJSON(pText, strlen(pText), &a, &aRoot); };
  parse("[{\"\":1,3:14,]\n");
  parse("[\n"
        "\n"
        "3E14,\n"
        "{\"!\":4,733:4,[\n"
        "\n"
        "3EL%,3E14,\n"
        "{][1][1,,]");
  parse("[\n"
        "null,\n"
        "1,\n"
        "3.14,\n"
        "{\"a\": \"b\",\n"
        "3:14,ull}\n"
        "]");
  parse("[\n"
        "\n"
        "3E14,\n"
        "{\"a!!!!!!!!!!!!!!!!!!\":4, \n"
        "\n"
        "3:1,,\n"
        "3[\n"
        "\n"
        "]");
  parse("[\n"
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
  parse("[\n"
        "\n"
        "3E14,\n"
        "{\"!\":4,733:4,[\n"
        "\n"
        "3E1%,][1,,]");
  parse("[\n"
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

static const char* getJsonTestSuitePath()
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
  DOC_PANIC("Could not find JSONTestSuite directory.");
}

static void JsonTestSuiteFiles()
{
  int failures = 0;
  FixedArena<1024 * 1024> arenaStorage;
  Arena arena     = arenaStorage;
  const Node* arenaRoot = nullptr;
  const char* basePath  = getJsonTestSuitePath();
  for (size_t i = 0; i < ARRAYLEN(kParsingTests); ++i) {
    char path[512];
    snprintf(path, sizeof(path), "%s%s", basePath, kParsingTests[i]);
    FILE* input = fopen(path, "rb");
    DOC_REQUIRE(input, "Could not open JSONTestSuite input '%s'.", path);
    DOC_REQUIRE(!fseek(input, 0, SEEK_END), "Could not seek JSONTestSuite input '%s'.", path);
    long inputSize = ftell(input);
    DOC_REQUIRE(inputSize >= 0, "Could not size JSONTestSuite input '%s'.", path);
    rewind(input);
    char* inputData = (char*)malloc((size_t)inputSize + 1);
    DOC_REQUIRE(inputData, "Could not allocate JSONTestSuite input '%s'.", path);
    DOC_REQUIRE(fread(inputData, 1, (size_t)inputSize, input) == (size_t)inputSize, "Could not read JSONTestSuite input '%s'.", path);
    fclose(input);
    arena.Reset();
    arena.capacity = sizeof(arenaStorage.bytes);
    if (kParsingTests[i][0] == 'y') {
      size_t estimate = EstimateSize(inputData, (size_t)inputSize);
      DOC_REQUIRE(estimate != SIZE_MAX && estimate <= sizeof(arenaStorage.bytes), "JSON size estimate failed for '%s'.", path);
      arena.capacity = estimate;
    }
    Result status = ParseJSON(inputData, (size_t)inputSize, &arena, &arenaRoot);
    free(inputData);
    const char* color  = "";
    const char* reason = "";
    switch (kParsingTests[i][0])
    {
      case 'y':
        if (status == SUCCESS) {
          color  = HI_GOOD;
          reason = "PASSED";
        } else {
          color  = HI_BAD;
          reason = "SHOULD_HAVE_PASSED";
          ++failures;
        }
        break;
      case 'n':
        if (status != SUCCESS) {
          color  = HI_GOOD;
          reason = "REJECTED";
        } else {
          color  = HI_BAD;
          reason = "SHOULD_HAVE_FAILED";
          ++failures;
        }
        break;
      case 'i':
        color  = HI_OK;
        reason = status == SUCCESS ? "IMPLEMENTATION_PASS" : "IMPLEMENTATION_FAIL";
        break;
      default:
        DOC_PANIC("Unknown JSONTestSuite test class.");
    }
    printf("%-70s %s%s%s", kParsingTests[i], color, reason, HI_RESET);
    if (status != SUCCESS)
      printf(" (%s)", string_Result(status));
    printf("\n");
  }
  if (failures)
    exit(failures);
}

int main()
{
  ObjectTest();
  DirectSerializationTest();
  PublicSoftFailureTest();
  FileMapRoundTripTest();
  WritableFileRoundTripTest();
  LargeObjectIndexTest();
  MediumObjectLookupTest();
  NumericArenaTest();
  FastDecimalDifferentialTest();
  StrictStringTest();
  GeneratedDocumentFuzzTest();
  MutationFuzzTest();
  NumericBitPatternFuzzTest();
  OutputBoundaryCanaryTest();
  ObjectThresholdFuzzTest();
  EmbeddedNulKeyTest();
  NestingAndRollbackFuzzTest();
  ImmutableLayoutTest();
  DeepTest();
  StaticArenaTest();
  StackArenaTest();
  ParseTest();
  EstimateSizeTest();
  RoundTripTest();
  AflRegression();
  JsonTestSuite();
  JsonTestSuiteFiles();

  if (!getenv("FLAT_JSON_SKIP_BENCHMARKS")) {
    BENCH(2000, 1, ObjectTest());
    BENCH(2000, 1, DeepTest());
    BENCH(2000, 1, ParseTest());
    BENCH(2000, 1, RoundTripTest());
    BENCH(2000, 1, JsonTestSuite());
  }
}
