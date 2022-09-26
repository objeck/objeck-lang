/***************************************************************************
 * Copyright (c) 2008-2022, Randy Hollines
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
 * - Neither the name of the StackVM Team nor the names of its
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

#ifndef __SYS_H__
#define __SYS_H__

// define windows type
#if defined(_WIN32)
#include <windows.h>
#include <stdint.h>
#elif defined(_ARM64)
#include <codecvt>
#endif

#include <zlib.h>
#include <string.h>
#include <fstream>
#include <string>
#include <vector>
#include <map>

#include "logger.h"

// memory size for local stack frames
#define LOCAL_SIZE 1024
#define INT_VALUE int32_t
#define INT64_VALUE int64_t
#define FLOAT_VALUE double

using namespace std;

namespace instructions {
  // vm types
  enum MemoryType {
    NIL_TYPE = 1000,
    BYTE_ARY_TYPE,
    CHAR_ARY_TYPE,
    INT_TYPE,
    FLOAT_TYPE,
    FUNC_TYPE
  };

  // garbage types
  enum ParamType {
    CHAR_PARM = -1500,
    INT_PARM,
    FLOAT_PARM,
    BYTE_ARY_PARM,
    CHAR_ARY_PARM,
    INT_ARY_PARM,
    FLOAT_ARY_PARM,
    OBJ_PARM,
    OBJ_ARY_PARM,
    FUNC_PARM,
  };
}

static wstring GetLibraryPath() {
  wstring path;

#ifdef _WIN32
  char* path_str_ptr; size_t len;
  if(_dupenv_s(&path_str_ptr, &len, "OBJECK_LIB_PATH")) {
    return L"";
  }
#else
  const char* path_str_ptr = getenv("OBJECK_LIB_PATH");
#endif
  if(path_str_ptr && strlen(path_str_ptr) > 0) {
    string path_str(path_str_ptr);
    path = wstring(path_str.begin(), path_str.end());
#ifdef _WIN32
    if(path[path.size() - 1] != '\\') {
      path += L"\\";
    }
#else
    if(path[path.size() - 1] != '/') {
      path += L'/';
    }
#endif
  }
  else {
#ifdef _WIN32
    path += L"..\\lib\\";
#else
    path += L"../lib/";
#endif
  }

  return path;
}

/****************************
 * Converts UTF-8 bytes to a 
 * native Unicode string 
 ****************************/
static bool BytesToUnicode(const string &in, wstring &out) {    
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

static wstring BytesToUnicode(const string &in) {
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
static bool BytesToCharacter(const string &in, wchar_t &out) {
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
static bool UnicodeToBytes(const wstring &in, string &out) {
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

static string UnicodeToBytes(const wstring &in) {
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
static bool CharacterToBytes(wchar_t in, string &out) {
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

/**
 * Byte output stream
 */
class OutputStream {
  wstring file_name;
  vector<char> out_buffer;

public:
  OutputStream(const wstring &n) {
    file_name = n;
  }

  ~OutputStream() {
  }

  bool WriteFile() {
    const string open_filename = UnicodeToBytes(file_name);
    ofstream file_out(open_filename.c_str(), ofstream::binary);
    if(!file_out.is_open()) {
      wcerr << L"Unable to write file: '" << file_name << L"'" << endl;
      return false;
    }

    uLong dest_len;
    char* compressed = Compress(out_buffer.data(), (uLong)out_buffer.size(), dest_len);
    if(!compressed) {
      wcerr << L"Unable to compress file: '" << file_name << L"'" << endl;
      file_out.close();
      return false;
    }
#ifdef _DEBUG
    GetLogger() << L"--- file out: compressed=" << dest_len << L", uncompressed=" << out_buffer.size() << L" ---" << endl;
#endif
    file_out.write(compressed, dest_len);
    free(compressed);
    compressed = nullptr;

    file_out.close();

    return true;
  }

  inline void WriteString(const wstring &in) {
    string out;
    if(!UnicodeToBytes(in, out)) {
      wcerr << L">>> Unable to write unicode string <<<" << endl;
      exit(1);
    }
    WriteInt((int)out.size());

    for(size_t i = 0; i < out.size(); ++i) {
      out_buffer.push_back(out[i]);
    }
  }

  inline void WriteByte(char value) {
    out_buffer.push_back(value);
  }

  inline void WriteInt(int32_t value) {
    char temp[sizeof(value)];
    memcpy(temp, &value, sizeof(value));
    std::copy(begin(temp), end(temp), std::back_inserter(out_buffer));
  }

  inline void WriteUnsigned(uint32_t value) {
    char temp[sizeof(value)];
    memcpy(temp, &value, sizeof(value));
    std::copy(begin(temp), end(temp), std::back_inserter(out_buffer));
  }

  inline void WriteChar(wchar_t value) {
    string buffer;
    if(!CharacterToBytes(value, buffer)) {
      wcerr << L">>> Unable to write character <<<" << endl;
      exit(1);
    }

    // write bytes
    if(buffer.size()) {
      WriteInt((int)buffer.size());
      for(size_t i = 0; i < buffer.size(); ++i) {
        out_buffer.push_back(buffer[i]);
      }
    }
    else {
      WriteInt(0);
    }
  }

  inline void WriteDouble(FLOAT_VALUE value) {
    char temp[sizeof(value)];
    memcpy(temp, &value, sizeof(value));
    std::copy(begin(temp), end(temp), std::back_inserter(out_buffer));
  }

  //
  // compresses a stream
  //
  static char* Compress(const char* src, uLong src_len, uLong &out_len) {
    const uLong buffer_max = compressBound(src_len);
    char* buffer = (char*)calloc(buffer_max, sizeof(char));

    out_len = buffer_max;
    const int status = compress((Bytef*)buffer, &out_len, (Bytef*)src, src_len);
    if(status != Z_OK) {
      free(buffer);
      buffer = nullptr;
      return nullptr;
    }

    return buffer;
  }

  //
  // compresses a stream
  //
  static char* Uncompress(const char* src, uLong src_len, uLong &out_len) {
    const uLong buffer_limit = 67108864; // 64 MB

    uLong buffer_max = src_len << 3;
    char* buffer = (char*)calloc(buffer_max, sizeof(char));

    bool success = false;
    do {
      out_len = buffer_max;
      const int status = uncompress((Bytef*)buffer, &out_len, (Bytef*)src, src_len);
      switch(status) {
      case Z_OK: // caller frees buffer
        return buffer;

      case Z_BUF_ERROR:
        free(buffer);
        buffer_max <<= 1;
        buffer = (char*)calloc(buffer_max, sizeof(char));
        break;

      case Z_MEM_ERROR:
      case Z_DATA_ERROR:
        free(buffer);
        buffer = nullptr;
        return nullptr;
      }
    } while(buffer_max < buffer_limit && !success);

    free(buffer);
    buffer = nullptr;
    return nullptr;
  }
};

static map<const wstring, wstring> ParseCommnadLine(int argc, char* argv[], wstring &path_string) {
  map<const wstring, wstring> arguments;

  // reconstruct command line
  string path;
  for(int i = 1; i < 1024 && i < argc; ++i) {
    path += ' ';
    char* cmd_param = argv[i];
    if(strlen(cmd_param) > 0 && cmd_param[0] != L'\'' && (strrchr(cmd_param, L' ') || strrchr(cmd_param, L'\t'))) {
      path += '\'';
      path += cmd_param;
      path += '\'';
    }
    else {
      path += cmd_param;
    }
  }

  // get command line parameters
  path_string = BytesToUnicode(path);

  size_t pos = 0;
  size_t end = path_string.size();
  while(pos < end) {
    // ignore leading white space
    while(pos < end && (path_string[pos] == L' ' || path_string[pos] == L'\t')) {
      pos++;
    }
    if(path_string[pos] == L'-' && pos > 0 && path_string[pos - 1] == L' ') {
      // parse key
      size_t start = ++pos;
      while(pos < end && path_string[pos] != L' ' && path_string[pos] != L'\t') {
        pos++;
      }
      const wstring key = path_string.substr(start, pos - start);
      // parse value
      while(pos < end && (path_string[pos] == L' ' || path_string[pos] == L'\t')) {
        pos++;
      }
      start = pos;
      bool is_string = false;
      if(pos < end && path_string[pos] == L'\'') {
        is_string = true;
        start++;
        pos++;
      }
      bool not_end = true;
      while(pos < end && not_end) {
        // check for end
        if(is_string) {
          not_end = path_string[pos] != L'\'';
        }
        else {
          not_end = !(path_string[pos] == L' ' || path_string[pos] == L'\t');
        }
        // update position
        if(not_end) {
          pos++;
        }
      }
      wstring value = path_string.substr(start, pos - start);

      // close string and add
      if(path_string[pos] == L'\'') {
        pos++;
      }

      map<const wstring, wstring>::iterator found = arguments.find(key);
      if(found != arguments.end()) {
        value += L',';
        value += found->second;
      }
      arguments[key] = value;
    }
    else {
      pos++;
    }
  }

  return arguments;
}

#endif