////////////////////////////////////////////////////////////////////////////////
// @author rygo6
// File.cpp — Open, map, and decode paths for the File.hpp RAII wrappers.
////////////////////////////////////////////////////////////////////////////////



#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "Terminal.hpp"
#include "File.hpp"

#include <fcntl.h>
#include <sys/stat.h>


////////////////////////////////////////////////////////////////////////////////
// Logging
////////////////////////////////////////////////////////////////////////////////

#define FILE_INFO(format, ...) INFO(FILE, (170,190,210), format, ##__VA_ARGS__)
#define FILE_WARN(format, ...) WARN(FILE, format, ##__VA_ARGS__)
#define FILE_ERR(format, ...)  ERR(FILE, format, ##__VA_ARGS__)

#define FILE_PANIC(format, ...)  PANIC(FILE, format, ##__VA_ARGS__)
#define FILE_REQUIRE(expr, ...)  REQUIRE(FILE, expr, "" __VA_OPT__(__VA_ARGS__))
#define FILE_ASSERT(expr, ...)   ASSERT(FILE, expr, "" __VA_OPT__(__VA_ARGS__))


////////////////////////////////////////////////////////////////////////////////
namespace Flat {
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////
// WritableFile::WritableFile
//  Opens for truncate or append; a failed open leaves IsValid() false rather than trapping.
///////////////////////////////////////////////////////
WritableFile::WritableFile(const char* pPath, bool append)
{
  FILE_ASSERT(pPath, "WritableFile needs a path");
  if (!pPath)
    return;

  pFile = fopen(pPath, append ? "ab" : "wb");
  if (!pFile)
    FILE_WARN("WritableFile open %s failed: %s\n", pPath, strerror(errno));
}

///////////////////////////////////////////////////////
// WritableFile::Write
//  A short write latches `failed`, so one check at the end covers a whole run of appends.
///////////////////////////////////////////////////////
bool WritableFile::Write(const void* pData, size_t bytes)
{
  FILE_ASSERT(pFile, "WritableFile Write on a closed file (check IsValid() first)");
  if (failed || !pFile)
    return false;

  if (!bytes || fwrite(pData, 1, bytes, pFile) == bytes)
    return true;

  FILE_WARN("WritableFile Write fell short of %zu bytes: %s\n", bytes, strerror(errno));
  failed = true;
  return false;
}

///////////////////////////////////////////////////////
// WritableFile::WriteFormat
///////////////////////////////////////////////////////
int WritableFile::WriteFormat(const char* pFormat, ...)
{
  FILE_ASSERT(pFile, "WritableFile WriteFormat on a closed file (check IsValid() first)");
  if (failed || !pFile)
    return -1;

  va_list args;
  va_start(args, pFormat);
  int written = vfprintf(pFile, pFormat, args);
  va_end(args);

  if (written < 0) {
    FILE_WARN("WritableFile WriteFormat failed: %s\n", strerror(errno));
    failed = true;
  }
  return written;
}

///////////////////////////////////////////////////////
// WritableFile::Add
///////////////////////////////////////////////////////
void WritableFile::Add(char value)
{
  FILE_ASSERT(pFile, "WritableFile Add on a closed file (check IsValid() first)");
  if (failed || !pFile)
    return;

  if (fputc((unsigned char)value, pFile) != EOF)
    return;

  FILE_WARN("WritableFile Add failed: %s\n", strerror(errno));
  failed = true;
}

///////////////////////////////////////////////////////
// WritableFile::AppendQuoted
///////////////////////////////////////////////////////
void WritableFile::AppendQuoted(const char* pData, size_t size)
{
  Add('"');
  Append(pData, size);
  Add('"');
}

///////////////////////////////////////////////////////
// WritableFile::Flush
///////////////////////////////////////////////////////
bool WritableFile::Flush()
{
  FILE_ASSERT(pFile, "WritableFile Flush on a closed file (check IsValid() first)");
  if (failed || !pFile)
    return false;

  if (!fflush(pFile))
    return true;

  FILE_WARN("WritableFile Flush failed: %s\n", strerror(errno));
  failed = true;
  return false;
}

///////////////////////////////////////////////////////
// FileMap::FileMap
//  The descriptor closes immediately: the mapping keeps the file alive on its own.
///////////////////////////////////////////////////////
FileMap::FileMap(const char* path)
{
  int handle = open(path, O_RDONLY | O_CLOEXEC);
  if (handle < 0)
    return;

  struct stat st = {};
  if (fstat(handle, &st) == 0 && st.st_size > 0) {
    void* p = mmap(nullptr, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, handle, 0);
    if (p != MAP_FAILED) {
      data = (const u8*)p;
      size = (size_t)st.st_size;
    }
  }

  close(handle);
}

///////////////////////////////////////////////////////
// WritableFileMap::WritableFileMap
//  Every failure unwinds what the earlier steps claimed, so a partial file never survives.
///////////////////////////////////////////////////////
WritableFileMap::WritableFileMap(size_t capacity, const char* path)
{
  FILE_ASSERT(path && capacity, "WritableFileMap needs a path and a nonzero capacity");
  if (!path || !capacity)
    return;

  handle = open(path, O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  if (handle < 0) {
    FILE_WARN("WritableFileMap open %s failed: %s\n", path, strerror(errno));
    return;
  }

  if (ftruncate(handle, (off_t)capacity)) {
    FILE_WARN("WritableFileMap ftruncate %s to %zu bytes failed: %s\n", path, capacity, strerror(errno));
    close(handle);
    handle = -1;
    return;
  }

  void* p = mmap(nullptr, capacity, PROT_READ | PROT_WRITE, MAP_SHARED, handle, 0);
  if (p == MAP_FAILED) {
    FILE_WARN("WritableFileMap mmap %s (%zu bytes) failed: %s\n", path, capacity, strerror(errno));
    ftruncate(handle, 0);
    close(handle);
    handle = -1;
    return;
  }

  data = (u8*)p;
  size = capacity;
}

///////////////////////////////////////////////////////
// WritableFileMap::~WritableFileMap
///////////////////////////////////////////////////////
WritableFileMap::~WritableFileMap()
{
  if (data) {
    msync(data, size, MS_SYNC);
    munmap(data, size);
  }

  if (handle >= 0)
    close(handle);
}


////////////////////////////////////////////////////////////////////////////////
}  // namespace Flat
////////////////////////////////////////////////////////////////////////////////
