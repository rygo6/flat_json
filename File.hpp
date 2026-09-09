////////////////////////////////////////////////////////////////////////////////
// @author rygo6
// File.hpp — Flat RAII file streams, memory maps, and image maps.
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdarg.h>
#include <stdlib.h>


#include "Container.hpp"

#include <stdio.h>
#include <sys/mman.h>


////////////////////////////////////////////////////////////////////////////////
namespace Flat {
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////
// File
//  RAII read-only C file stream (fopen "rb"). Panic-free: IsValid() is false when the open failed.
///////////////////////////////////////////////////////
struct File {
  FILE* pFile = nullptr;

  explicit File(const char* pPath) : pFile(fopen(pPath, "rb")) {}
  ~File() { if (pFile) fclose(pFile); }

  File(const File&) = delete;
  void operator=(const File&) = delete;

  bool IsValid() const { return pFile != nullptr; }

  // Reads up to `bytes` into pOut; returns the number of bytes actually read.
  size_t Read(size_t bytes, void* pOut) const { return pFile ? fread(pOut, 1, bytes, pFile) : 0; }
};

///////////////////////////////////////////////////////
// WritableFile
//  RAII writable C file stream. Truncates/creates by default; pass append=true to open "ab" and add
//  to the end instead. Panic-free: IsValid() is false when the open failed.
///////////////////////////////////////////////////////
struct WritableFile {
  FILE* pFile  = nullptr;
  bool  failed = false;

  explicit WritableFile(const char* pPath, bool append = false);
  ~WritableFile() { if (pFile) fclose(pFile); }

  WritableFile(const WritableFile&) = delete;
  void operator=(const WritableFile&) = delete;

  bool IsValid()  const { return pFile != nullptr; }
  bool HasError() const { return failed || (pFile && ferror(pFile)); }

  bool Write(const void* pData, size_t bytes);   // true only when the whole payload was written
  void Add(char value);
  void AppendQuoted(const char* pData, size_t size);
  bool Flush();

  [[gnu::format(printf, 2, 3)]]
  int WriteFormat(const char* pFormat, ...);     // bytes written, or negative on error

  void Append(const char* pData, size_t size) { Write(pData, size); }
  template <size_t Size>
  void Append(const char (&text)[Size]) { Append(text, Size - 1); }
};

///////////////////////////////////////////////////////
// FileMap
//  RAII read-only memory-mapped view of a file. Panic-free: on failure
//  (missing / unreadable / empty) `data` is null and `size` is 0 — `IsValid()`
//  reports it and the caller decides whether that's fatal. Suits both required
//  assets (caller traps on !IsValid) and optional config files (caller falls back).
///////////////////////////////////////////////////////
struct FileMap {
  const u8* data = nullptr;
  size_t    size = 0;

  explicit FileMap(const char* path);
  ~FileMap() { if (data) munmap((void*)data, size); }

  FileMap(const FileMap&) = delete;
  void operator=(const FileMap&) = delete;

  bool IsValid() const { return data != nullptr; }

  // Implicitly view the mapped bytes as a SpanOf: raw bytes, text, or u32 words for SPIR-V.
  template <SpanOf<const u8> S>
  operator S() const { return S(size, data); }
  template <SpanOf<const char> S>
  operator S() const { return S(size, (const char*)data); }
  template <SpanOf<const u32> S>
  operator S() const { return S(size / sizeof(u32), (const u32*)data); }
};

///////////////////////////////////////////////////////
// WritableFileMap
//  RAII writable memory-mapped file. Creates or replaces a file at the requested
//  capacity; the destructor flushes, unmaps, and closes it on every exit path.
///////////////////////////////////////////////////////
struct WritableFileMap {
  u8*    data   = nullptr;
  size_t size   = 0;
  int    handle = -1;

  WritableFileMap(size_t capacity, const char* path);
  ~WritableFileMap();

  WritableFileMap(const WritableFileMap&) = delete;
  void operator=(const WritableFileMap&) = delete;

  bool IsValid() const { return data != nullptr; }
};

////////////////////////////////////////////////////////////////////////////////
}  // namespace Flat
////////////////////////////////////////////////////////////////////////////////
