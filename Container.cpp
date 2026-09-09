////////////////////////////////////////////////////////////////////////////////
// @author rygo6
// Container.cpp — The scanning string routines behind Container.hpp's bounded string API.
////////////////////////////////////////////////////////////////////////////////

#include "Container.hpp"


////////////////////////////////////////////////////////////////////////////////
namespace Flat {
////////////////////////////////////////////////////////////////////////////////

const char* StrFindSubstring(const char* pText, u32 textLength, Literal needle)
{
  u32 needleLength = needle.size - 1;
  if (!needleLength)
    return pText;

  if (needleLength > textLength)
    return nullptr;

  for (u32 index = 0; index <= textLength - needleLength; ++index) {
    if (StrStartsWith(pText + index, needle.pText, needleLength))
      return pText + index;
  }

  return nullptr;
}

bool StrCaseEqual(const char* pText, const char* pOther, u32 length)
{
  for (u32 index = 0; index < length; ++index) {
    if (FoldASCIICase(pText[index]) != FoldASCIICase(pOther[index]))
      return false;
  }

  return true;
}

const char* StrCaseFind(const char* pText, u32 textLength, const char* pNeedle, u32 needleLength)
{
  if (!needleLength)
    return pText;

  if (needleLength > textLength)
    return nullptr;

  for (u32 index = 0; index <= textLength - needleLength; ++index) {
    if (StrCaseEqual(pText + index, pNeedle, needleLength))
      return pText + index;
  }

  return nullptr;
}

////////////////////////////////////////////////////////////////////////////////
}  // namespace Flat
////////////////////////////////////////////////////////////////////////////////
