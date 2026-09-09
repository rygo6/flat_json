////////////////////////////////////////////////////////////////////////////////
// @author: codex
// arena_capacity_regression.cpp - Checks scratch separation and arena rollback.
////////////////////////////////////////////////////////////////////////////////

#include "Document.hpp"

using namespace Flat;
using namespace Flat::Document;

#include <stdio.h>
#include <string.h>

////////////////////////////////////////////////////////////////////////////////
namespace Flat {
////////////////////////////////////////////////////////////////////////////////

static constexpr u32 StorageCapacity = 16384;
static constexpr u32 OutputCapacity  = 4096;

///////////////////////////////////////////////////////
// CheckCapacities
//  Every bounded parse either preserves the document or rolls back completely.
///////////////////////////////////////////////////////
static bool CheckCapacities(String text)
{
  FixedArena<StorageCapacity> storage;
  FixedArray<char, OutputCapacity> output;

  for (u32 capacity = 0; capacity <= StorageCapacity; ++capacity) {
    Arena arena(capacity, storage.bytes);
    const Node* pRoot   = nullptr;
    Result status = ParseJSON(text.data, text.size, &arena, &pRoot);
    if (status == ERROR_INSUFFICIENT_SPACE) {
      if (pRoot || arena.offset)
        return false;

      continue;
    }

    if (status != SUCCESS || !pRoot || pRoot->ToString(output) != SUCCESS || memcmp(output.data, text.data, text.size) || output.data[text.size])
      return false;
  }

  return true;
}

///////////////////////////////////////////////////////
// CheckInvalidArena
//  Public failures clear the root without modifying the caller's cursor.
///////////////////////////////////////////////////////
static bool CheckInvalidArena()
{
  FixedArena<128> storage;
  Arena arena       = storage;
  const Node* pRoot = (const Node*)storage.bytes;
  if (ParseJSON("null", (Arena*)nullptr, &pRoot) != ERROR_INVALID_ARGUMENT || pRoot)
    return false;

  arena.offset = 1;
  if (ParseJSON("null", &arena, &pRoot) != ERROR_INVALID_ARGUMENT || pRoot || arena.offset != 1)
    return false;

  // Numeric scratch lives on the stack now, so even a tiny arena parses a long
  // decimal that once needed reserved scratch capacity.
  FixedArena<64> smallStorage;
  Arena small = smallStorage;
  const Node* pSmall = nullptr;
  return ParseJSON("1.00000000000000011102230246251565404236316680908203125", &small, &pSmall) == SUCCESS
         && pSmall && pSmall->IsDouble();
}

////////////////////////////////////////////////////////////////////////////////
}  // namespace Flat
////////////////////////////////////////////////////////////////////////////////

int main()
{
  if (!CheckCapacities(R"({"a":1,"b":2})") || !CheckCapacities(R"({"a":[1,{"b":2},3],"c":{"d":[false,null,"text"]}})") || !CheckInvalidArena()) {
    fprintf(stderr, "Arena capacity or rollback regression.\n");
    return 1;
  }

  return 0;
}
