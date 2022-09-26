/***************************************************************************
 * Native executable common functions
 *
 * Copyright (c) 2022, Randy Hollines
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in
 * the documentation and/or other materials provided with the distribution.
 * - Neither the name of the Objeck team nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ***************************************************************************/

#ifndef __NATIVE_COMMON__
#define __NATIVE_COMMON__

#if defined(_WIN32)
#include <windows.h>
#include <stdint.h>
#elif defined(_ARM64)
#include <codecvt>
#endif

#include <string.h>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <filesystem>

using namespace std;

/****************************
 * Converts UTF-8 bytes to a
 * native Unicode string
 ****************************/
static bool BytesToUnicode(const string& in, wstring& out) {
#ifdef _WIN32
  // allocate space
  const int wsize = MultiByteToWideChar(CP_UTF8, 0, in.c_str(), -1, nullptr, 0);
  if(wsize == 0) {
    return false;
  }
  wchar_t* buffer = new wchar_t[wsize];

  // convert
  const int check = MultiByteToWideChar(CP_UTF8, 0, in.c_str(), -1, buffer, wsize);
  if(check == 0) {
    delete[] buffer;
    buffer = nullptr;
    return false;
  }

  // create string
  out.append(buffer, wsize - 1);

  // clean up
  delete[] buffer;
  buffer = nullptr;
#else
  // allocate space
  size_t size = mbstowcs(nullptr, in.c_str(), in.size());
  if(size == (size_t)-1) {
    return false;
  }
  wchar_t* buffer = new wchar_t[size + 1];

  // convert
  size_t check = mbstowcs(buffer, in.c_str(), in.size());
  if(check == (size_t)-1) {
    delete[] buffer;
    buffer = nullptr;
    return false;
  }
  buffer[size] = L'\0';

  // create string
  out.append(buffer, size);

  // clean up
  delete[] buffer;
  buffer = nullptr;
#endif

  return true;
}

static wstring BytesToUnicode(const string& in) {
  wstring out;
  if(BytesToUnicode(in, out)) {
    return out;
  }

  return L"";
}

/**
 * Converts UTF-8 bytes to
 * native a unicode character
 */
static bool BytesToCharacter(const string& in, wchar_t& out) {
  wstring buffer;
  if(!BytesToUnicode(in, buffer)) {
    return false;
  }

  if(buffer.size() != 1) {
#if defined(_ARM64) && defined(_OSX)
    std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> cvt;
    buffer = cvt.from_bytes(in);
    if(buffer.size() != 1) {
      return false;
    }

    out = buffer[0];
    return true;
#else
    return false;
#endif
  }

  out = buffer[0];
  return true;
}

/**
 * Converts a native string
 * to UTF-8 bytes
 */
static bool UnicodeToBytes(const wstring& in, string& out) {
#ifdef _WIN32
  // allocate space
  const int size = WideCharToMultiByte(CP_UTF8, 0, in.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if(size == 0) {
    return false;
  }
  char* buffer = new char[size];

  // convert string
  const int check = WideCharToMultiByte(CP_UTF8, 0, in.c_str(), -1, buffer, size, nullptr, nullptr);
  if(check == 0) {
    delete[] buffer;
    buffer = nullptr;
    return false;
  }

  // append output
  out.append(buffer, size - 1);

  // clean up
  delete[] buffer;
  buffer = nullptr;
#else
  // convert string
  size_t size = wcstombs(nullptr, in.c_str(), in.size());
  if(size == (size_t)-1) {
    return false;
  }
  char* buffer = new char[size + 1];

  wcstombs(buffer, in.c_str(), size);
  if(size == (size_t)-1) {
    delete[] buffer;
    buffer = nullptr;
    return false;
  }
  buffer[size] = '\0';

  // create string      
  out.append(buffer, size);

  // clean up
  delete[] buffer;
  buffer = nullptr;
#endif

  return true;
}

static string UnicodeToBytes(const wstring& in) {
  string out;
  if(UnicodeToBytes(in, out)) {
    return out;
  }

  return "";
}

/**
 * Converts a native character
 * to UTF-8 bytes
 */
static bool CharacterToBytes(wchar_t in, string& out) {
  if(in == L'\0') {
    return true;
  }

  wchar_t buffer[2];
  buffer[0] = in;
  buffer[1] = L'\0';

  if(!UnicodeToBytes(buffer, out)) {
    return false;
  }

  return true;
}

#endif