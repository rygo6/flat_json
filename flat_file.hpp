////////////////////////////////////////////////////////////////////////////////
// @author rygo6
// flat_file.hpp
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

////////////////////////////////////////////////////////////////////////////////
// Logging and error handling
////////////////////////////////////////////////////////////////////////////////

#ifndef FILE_INFO
#define FILE_INFO(format, ...) fprintf(stderr, __FILE__ ":%-*d[FILE] "       format, (int)(20 - sizeof(__FILE__)), __LINE__ __VA_OPT__(,) __VA_ARGS__)
#endif

#ifndef FILE_WARN
#define FILE_WARN(format, ...) fprintf(stderr, __FILE__ ":%-*d[FILE] WARN: " format, (int)(20 - sizeof(__FILE__)), __LINE__ __VA_OPT__(,) __VA_ARGS__)
#endif

#ifndef FILE_ERR
#define FILE_ERR(format, ...) fprintf(stderr, __FILE__ ":%-*d[FILE] ERR: "  format, (int)(20 - sizeof(__FILE__)), __LINE__ __VA_OPT__(,) __VA_ARGS__)
#endif

#ifndef FILE_PANIC
#define FILE_PANIC(format, ...) ({                                           \
  fprintf(stderr, __FILE__ ":%-*d[FILE] PANIC: " format "\n",                \
          (int)(20 - sizeof(__FILE__)), __LINE__ __VA_OPT__(,) __VA_ARGS__); \
  __builtin_trap();                                                          \
})
#endif

#ifndef FILE_REQUIRE
#define FILE_REQUIRE(expr, ...) ({                                 \
  if (!(expr)) [[unlikely]] {                                      \
    fprintf(stderr, __FILE__ ":%-*d[FILE] ERR: FILE_REQUIRE: %s",  \
            (int)(20 - sizeof(__FILE__)), __LINE__, #expr);        \
    __VA_OPT__(fputs(": ", stderr); fprintf(stderr, __VA_ARGS__);) \
    fputc('\n', stderr);                                           \
    __builtin_trap();                                              \
  }                                                                \
})
#endif

#ifndef FILE_ASSERT
#ifdef DEBUG
#define FILE_ASSERT(expr, ...) ({                                  \
  if (!(expr)) [[unlikely]] {                                      \
    fprintf(stderr, __FILE__ ":%-*d[FILE] ERR: FILE_ASSERT: %s",   \
            (int)(20 - sizeof(__FILE__)), __LINE__, #expr);        \
    __VA_OPT__(fputs(": ", stderr); fprintf(stderr, __VA_ARGS__);) \
    fputc('\n', stderr);                                           \
    __builtin_trap();                                              \
  }                                                                \
})
#else
#define FILE_ASSERT(expr, ...) ((void)0)
#endif
#endif

////////////////////////////////////////////////////////////////////////////////
namespace flat {
////////////////////////////////////////////////////////////////////////////////

using u8 = uint8_t;

///////////////////////////////////////////////////////
// File
//  RAII read-only C file stream (fopen "rb"). Panic-free: IsValid() is false when the open failed.
///////////////////////////////////////////////////////
struct File
{
  FILE* pFile = nullptr;

  explicit File(const char* pPath) : pFile(pPath ? fopen(pPath, "rb") : nullptr) {}

  ~File()
  {
    if (pFile)
      fclose(pFile);
  }

  File(const File&) = delete;
  File& operator=(const File&) = delete;
  File(File&&) = delete;
  File& operator=(File&&) = delete;

  bool IsValid() const { return pFile != nullptr; }

  // Reads up to `bytes` into pOut; returns the number of bytes actually read.
  size_t Read(size_t bytes, void* pOut) const { return pFile ? fread(pOut, 1, bytes, pFile) : 0; }
};

///////////////////////////////////////////////////////
// WritableFile
//  RAII writable C file stream. Truncates/creates by default; pass append=true to open "ab" and add
//  to the end instead. Panic-free: IsValid() is false when the open failed.
///////////////////////////////////////////////////////
struct WritableFile
{
  FILE* pFile = nullptr;
  bool failed = false;

  explicit WritableFile(const char* pPath, bool append = false)
  {
    FILE_ASSERT(pPath, "WritableFile needs a path");
    if (!pPath)
      return;

    pFile = fopen(pPath, append ? "ab" : "wb");
    if (!pFile)
      FILE_WARN("WritableFile open %s failed: %s\n", pPath, strerror(errno));
  }

  ~WritableFile()
  {
    if (pFile)
      fclose(pFile);
  }

  WritableFile(const WritableFile&) = delete;
  WritableFile& operator=(const WritableFile&) = delete;
  WritableFile(WritableFile&&) = delete;
  WritableFile& operator=(WritableFile&&) = delete;

  bool IsValid() const { return pFile != nullptr; }
  bool HasError() const { return failed || (pFile && ferror(pFile)); }

  // Writes `bytes` from pData; true only when the entire payload was written.
  bool Write(const void* pData, size_t bytes)
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

  // Writes a printf-formatted chunk; returns the byte count written, or negative on error.
  __attribute__((format(printf, 2, 3)))
  int WriteFormat(const char* pFormat, ...)
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

  void Add(char value)
  {
    FILE_ASSERT(pFile, "WritableFile Add on a closed file (check IsValid() first)");
    if (failed || !pFile)
      return;

    if (fputc((unsigned char)value, pFile) != EOF)
      return;

    FILE_WARN("WritableFile Add failed: %s\n", strerror(errno));
    failed = true;
  }
  void Append(const char* pData, size_t size) { Write(pData, size); }
  template<size_t Size>
  void Append(const char (&text)[Size]) { Append(text, Size - 1); }

  void AppendQuoted(const char* pData, size_t size)
  {
    Add('"');
    Append(pData, size);
    Add('"');
  }

  bool Flush()
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
};

///////////////////////////////////////////////////////
// FileMap
//  RAII read-only memory-mapped view of a file. Panic-free: on failure
//  (missing / unreadable / empty) `data` is null and `size` is 0 — `IsValid()`
//  reports it and the caller decides whether that's fatal. Suits both required
//  assets (caller traps on !IsValid) and optional config files (caller falls back).
///////////////////////////////////////////////////////
struct FileMap
{
  const u8* data = nullptr;
  size_t size = 0;

  explicit FileMap(const char* path)
  {
    if (!path)
      return;
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

  ~FileMap()
  {
    if (data)
      munmap((void*)data, size);
  }

  FileMap(const FileMap&) = delete;
  FileMap& operator=(const FileMap&) = delete;
  FileMap(FileMap&&) = delete;
  FileMap& operator=(FileMap&&) = delete;

  bool IsValid() const { return data != nullptr; }
};

///////////////////////////////////////////////////////
// WritableFileMap
//  RAII writable memory-mapped file. Creates or replaces a file at the requested
//  capacity; the destructor flushes, unmaps, and closes it on every exit path.
///////////////////////////////////////////////////////
struct WritableFileMap
{
  u8* data = nullptr;
  size_t size = 0;
  int _handle = -1;

  WritableFileMap(size_t capacity, const char* path)
  {
    FILE_ASSERT(path && capacity, "WritableFileMap needs a path and a nonzero capacity");
    if (!path || !capacity)
      return;

    _handle = open(path, O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (_handle < 0) {
      FILE_WARN("WritableFileMap open %s failed: %s\n", path, strerror(errno));
      return;
    }

    if (ftruncate(_handle, (off_t)capacity)) {
      FILE_WARN("WritableFileMap ftruncate %s to %zu bytes failed: %s\n", path, capacity, strerror(errno));
      close(_handle);
      _handle = -1;
      return;
    }

    void* p = mmap(nullptr, capacity, PROT_READ | PROT_WRITE, MAP_SHARED, _handle, 0);
    if (p == MAP_FAILED) {
      FILE_WARN("WritableFileMap mmap %s (%zu bytes) failed: %s\n", path, capacity, strerror(errno));
      ftruncate(_handle, 0);
      close(_handle);
      _handle = -1;
      return;
    }

    data = (u8*)p;
    size = capacity;
  }

  ~WritableFileMap()
  {
    if (data) {
      msync(data, size, MS_SYNC);
      munmap(data, size);
    }
    if (_handle >= 0)
      close(_handle);
  }

  WritableFileMap(const WritableFileMap&) = delete;
  WritableFileMap& operator=(const WritableFileMap&) = delete;
  WritableFileMap(WritableFileMap&&) = delete;
  WritableFileMap& operator=(WritableFileMap&&) = delete;

  bool IsValid() const { return data != nullptr; }
};

////////////////////////////////////////////////////////////////////////////////
}  // namespace flat
////////////////////////////////////////////////////////////////////////////////
