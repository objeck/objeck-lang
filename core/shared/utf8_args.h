/***************************************************************************
 * Windows program arguments as UTF-8.
 *
 * The CRT's narrow argv (and envp) is in the ANSI code page: an argument with
 * a character outside it reaches main as '?', and one inside it as bytes that
 * a UTF-8 decoder replaces. Every tool therefore enters at wmain on Windows --
 * under MSVC and MinGW alike (MinGW links with -municode) -- and converts its
 * UTF-16 arguments here, once, into the UTF-8 form argv has on POSIX.
 *
 * Header-only and dependency-free on purpose, like exe_path.h: obu and the
 * native launcher link neither zlib nor the rest of sys.h, and sys.h includes
 * this for the compiler, VM, debugger and REPL.
 *
 * Copyright (c) 2026, Randy Hollines
 * All rights reserved.
 ***************************************************************************/

#ifndef __UTF8_ARGS_H__
#define __UTF8_ARGS_H__

#ifdef _WIN32
#include <windows.h>
#include <string>
#include <vector>

/**
 * UTF-16 to UTF-8. An unpaired surrogate becomes U+FFFD, as everywhere else
 * WideCharToMultiByte meets one.
 */
inline std::string Utf8FromWide(const std::wstring& in)
{
  if(in.empty()) {
    return std::string();
  }

  const int size = WideCharToMultiByte(CP_UTF8, 0, in.data(), (int)in.size(), nullptr, 0, nullptr, nullptr);
  if(size <= 0) {
    return std::string();
  }

  std::string out((size_t)size, '\0');
  WideCharToMultiByte(CP_UTF8, 0, in.data(), (int)in.size(), &out[0], size, nullptr, nullptr);
  return out;
}

/**
 * UTF-8 to UTF-16, the form every Windows file, process and environment API
 * takes. Invalid bytes become U+FFFD.
 */
inline std::wstring WideFromUtf8(const std::string& in)
{
  if(in.empty()) {
    return std::wstring();
  }

  const int size = MultiByteToWideChar(CP_UTF8, 0, in.data(), (int)in.size(), nullptr, 0);
  if(size <= 0) {
    return std::wstring();
  }

  std::wstring out((size_t)size, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, in.data(), (int)in.size(), &out[0], size);
  return out;
}

/**
 * The program arguments as UTF-8: construct from wmain's arguments and hand
 * Count() and Values() on as argc and argv.
 */
class Utf8Args {
  std::vector<std::string> values;
  std::vector<const char*> pointers;

public:
  Utf8Args(int argc, wchar_t* wargv[]) {
    for(int i = 0; i < argc; ++i) {
      values.push_back(Utf8FromWide(wargv[i] ? wargv[i] : L""));
    }
    for(const std::string& value : values) {
      pointers.push_back(value.c_str());
    }
    pointers.push_back(nullptr);
  }

  Utf8Args(const Utf8Args&) = delete;
  Utf8Args& operator=(const Utf8Args&) = delete;

  int Count() const {
    return (int)values.size();
  }

  const char** Values() {
    return pointers.data();
  }
};
#endif

#endif
