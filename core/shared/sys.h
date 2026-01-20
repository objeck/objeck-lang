/***************************************************************************
 * Copyright (c) 2025, Randy Hollines
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
 * - Neither the name of the Objeck Team nor the names of its
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

#pragma once

// define windows type
#if defined(_WIN32)
#include <windows.h>
#include <stdint.h>
#include <fcntl.h>
#include <io.h>
#elif defined(_ARM64)
#include <codecvt>
#endif

#include <math.h>
#include <zlib.h>
#include <string.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <stdint.h>

#include "logger.h"

// memory size for local stack frames
#define LOCAL_SIZE 768
#define INT_VALUE int32_t
#define INT64_VALUE int64_t
#define FLOAT_VALUE double
#define COMPRESS_BUFFER_LIMIT 2 << 28 // 512 MB

#define SMALL_BUFFER_MAX 1024
#define MID_BUFFER_MAX 8192
#define LARGE_BUFFER_MAX 32768

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

/**
 * Hashes a UTF-8 Unicode string
 */
static size_t HashString(const wchar_t* char_ary, const size_t char_ary_pos) {
  if(!char_ary || !char_ary_pos) {
    return 0;
  }

  size_t hash = 14695981039346656037ull; // FNV offset basis (64-bit)

  for(size_t i = 0; i < char_ary_pos; ++i) {
    hash ^= static_cast<size_t>(char_ary[i]);
    hash *= 1099511628211ull; // FNV prime (64-bit)
  }

  return hash;
}

static size_t HashString(const wchar_t* char_ary) {
  return HashString(char_ary, wcslen(char_ary));
}


/**
 * Converts UTF-8 bytes a 
 * Unicode string 
 */
static bool BytesToUnicode(const std::string &in, std::wstring &out) {    
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
  out.append(buffer, static_cast<std::basic_string<wchar_t, std::char_traits<wchar_t>, std::allocator<wchar_t>>::size_type>(wsize) - 1);

  // clean up
  delete[] buffer;
  buffer = nullptr;  
#else
  // allocate space
  size_t size = mbstowcs(nullptr, in.c_str(), in.size());
  if(size == (size_t)-1) {
    return false;
  }

  if(size < in.size()) {
    size = in.size();
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

static std::wstring BytesToUnicode(const std::string &in) {
  std::wstring out;
  if(BytesToUnicode(in, out)) {
    return out;
  }

  return L"";
}

/**
 * Converts UTF-8 bytes to a Unicode character
 */
static bool BytesToCharacter(const std::string &in, wchar_t &out) {
  std::wstring buffer;
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
 * Converts a Unicode character to UTF-8 bytes
 */
static bool UnicodeToBytes(const std::wstring &in, std::string &out) {
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
  out.append(buffer, static_cast<std::basic_string<char, std::char_traits<char>, std::allocator<char>>::size_type>(size) - 1);

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
  
  size_t check = wcstombs(buffer, in.c_str(), size);
  if(check == (size_t)-1) {
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

static std::string UnicodeToBytes(const std::wstring &in) {
  std::string out;
  if(UnicodeToBytes(in, out)) {
    return out;
  }

  return "";
}

/**
 * Converts a Unicode character to UTF-8 bytes
 */
static bool CharacterToBytes(wchar_t in, std::string &out) {
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
 * Byte output stream buffer
 */
class OutputStream {
  std::wstring file_name;
  std::vector<char> out_buffer;

public:
  OutputStream(const std::wstring &n = L"") {
    file_name = n;
  }

  ~OutputStream() {
  }

  bool WriteFile() {
    const std::string open_filename = UnicodeToBytes(file_name);
    std::ofstream file_out(open_filename.c_str(), std::ofstream::binary);
    if(!file_out.is_open()) {
      std::wcerr << L"Unable to write file: '" << file_name << L"'" << std::endl;
      return false;
    }

    unsigned long dest_len;
    char* compressed = CompressZlib(out_buffer.data(), (unsigned long)out_buffer.size(), dest_len);
    if(!compressed) {
      std::wcerr << L"Unable to compress file: '" << file_name << L"'" << std::endl;
      file_out.close();
      return false;
    }
#ifdef _DEBUG
    double compress_ratio = (double)out_buffer.size() / (double)dest_len;
    GetLogger() << L"[file out: uncompressed=" << out_buffer.size() << L", compressed=" << dest_len << L", ratio = " << round(compress_ratio) << L"x]" << std::endl;
#endif
    file_out.write(compressed, dest_len);
    free(compressed);
    compressed = nullptr;

    file_out.close();

    return true;
  }

  char* Get(size_t &size) {
    size = out_buffer.size();

    char* buffer = (char*)malloc(size);
    if(buffer) {
      memcpy(buffer, out_buffer.data(), size);
      return buffer;
    }

    return nullptr;
  }

  inline void WriteString(const std::wstring &in) {
    std::string out;
    if(!UnicodeToBytes(in, out)) {
      std::wcerr << L">>> Unable to write unicode string <<<" << std::endl;
      exit(1);
    }
    WriteInt((int)out.size());

    for(size_t i = 0; i < out.size(); ++i) {
      out_buffer.push_back(out[i]);
    }
  }

  inline void WriteByte(uint8_t value) {
    out_buffer.push_back(value);
  }

  inline void WriteInt(int32_t value) {
    char temp[sizeof(value)];
    memcpy(temp, &value, sizeof(value));
    std::copy(std::begin(temp), std::end(temp), std::back_inserter(out_buffer));
  }

  inline void WriteInt64(int64_t value) {
    char temp[sizeof(value)];
    memcpy(temp, &value, sizeof(value));
    std::copy(std::begin(temp), std::end(temp), std::back_inserter(out_buffer));
  }

  inline void WriteUnsigned(int32_t value) {
    char temp[sizeof(value)];
    memcpy(temp, &value, sizeof(value));
    std::copy(std::begin(temp), std::end(temp), std::back_inserter(out_buffer));
  }

  inline void WriteChar(wchar_t value) {
    std::string buffer;
    if(!CharacterToBytes(value, buffer)) {
      std::wcerr << L">>> Unable to write character <<<" << std::endl;
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
    std::copy(std::begin(temp), std::end(temp), std::back_inserter(out_buffer));
  }

  //
  // zlib stream compression
  //
  static char* CompressZlib(const char* src, unsigned long src_len, unsigned long &out_len) {
    const unsigned long buffer_max = compressBound(src_len);
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

  static char* UncompressZlib(const char* src, unsigned long src_len, unsigned long &out_len) {

    unsigned long buffer_max = src_len << 2;
    if(buffer_max > COMPRESS_BUFFER_LIMIT) {
      buffer_max = COMPRESS_BUFFER_LIMIT;
    }
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
    } 
    while(buffer_max < COMPRESS_BUFFER_LIMIT && !success);

    free(buffer);
    buffer = nullptr;
    return nullptr;
  }

  //
  // gzip stream compression
  //
  static char* CompressGzip(const char* src, unsigned long src_len, unsigned long& out_len) {
    const unsigned long buffer_max = compressBound(src_len);
    char* buffer = (char*)calloc(buffer_max, sizeof(char));

    // setup stream
    z_stream stream;

    // input
    stream.next_in = (Bytef*)src;
    stream.avail_in = (uInt)src_len;

    // output
    stream.next_out = (Bytef*)buffer;
    stream.avail_out = (uInt)buffer_max;

    out_len = buffer_max;
    if(deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, MAX_WBITS | 16, MAX_MEM_LEVEL, Z_DEFAULT_STRATEGY) != Z_OK) {
      free(buffer);
      buffer = nullptr;
      return nullptr;
    }

    if(deflate(&stream, Z_FINISH) != Z_STREAM_END) {
      free(buffer);
      buffer = nullptr;
      return nullptr;
    }

    if(deflateEnd(&stream) != Z_OK) {
      free(buffer);
      buffer = nullptr;
      return nullptr;
    }

    out_len = stream.total_out;
    return buffer;
  }

  static char* UncompressGzip(const char* src, unsigned long src_len, unsigned long& out_len) {
    // setup stream
    z_stream stream;

    // input
    stream.next_in = (Bytef*)src;
    stream.avail_in = (uInt)src_len;

    unsigned long buffer_max = src_len << 2;
    if(buffer_max > COMPRESS_BUFFER_LIMIT) {
      buffer_max = COMPRESS_BUFFER_LIMIT;
    }
        
    char* buffer = (char*)calloc(buffer_max, sizeof(char));
    if(!buffer) {
      return nullptr;
    }

    if(inflateInit2(&stream, MAX_WBITS | 16) != Z_OK) {
      free(buffer);
      buffer = nullptr;
      return nullptr;
    }

    bool success = false;
    do {
      if(stream.total_out >= buffer_max) {
        char* temp = (char*)calloc(sizeof(char), static_cast<size_t>(buffer_max) << 1);
        if(!temp) {
          free(buffer);
          buffer = nullptr;
          return nullptr;
        }
        
        memcpy(temp, buffer, buffer_max);
        buffer_max <<= 1;

        free(buffer);
        buffer = temp;
      }

      stream.next_out = (Bytef*)(buffer + stream.total_out);
      stream.avail_out = (uInt)buffer_max - stream.total_out;

      const int status = inflate(&stream, Z_SYNC_FLUSH);
      if(status == Z_STREAM_END) {
        success = true;
      }
      else if(status != Z_OK) {
        free(buffer);
        buffer = nullptr;
        return nullptr;
      }
    } 
    while(buffer_max < COMPRESS_BUFFER_LIMIT && !success);

    if(inflateEnd(&stream) != Z_OK) {
      free(buffer);
      buffer = nullptr;
      return nullptr;
    }

    out_len = stream.total_out;
    return buffer;
  }

  //
  // br (deflate) stream compression
  //
  static char* CompressBr(const char* src, unsigned long src_len, unsigned long& out_len) {
    const unsigned long buffer_max = compressBound(src_len);
    char* buffer = (char*)calloc(buffer_max, sizeof(char));

    // setup stream
    z_stream stream;

    // input
    stream.next_in = (Bytef*)src;
    stream.avail_in = (uInt)src_len;

    // output
    stream.next_out = (Bytef*)buffer;
    stream.avail_out = (uInt)buffer_max;

    out_len = buffer_max;
    if(deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, MAX_MEM_LEVEL, Z_DEFAULT_STRATEGY) != Z_OK) {
      free(buffer);
      buffer = nullptr;
      return nullptr;
    }

    if(deflate(&stream, Z_FINISH) != Z_STREAM_END) {
      free(buffer);
      buffer = nullptr;
      return nullptr;
    }

    if(deflateEnd(&stream) != Z_OK) {
      free(buffer);
      buffer = nullptr;
      return nullptr;
    }

    out_len = stream.total_out;
    return buffer;
  }

  static char* UncompressBr(const char* src, unsigned long src_len, unsigned long& out_len) {
    // setup stream
    z_stream stream;

    // input
    stream.next_in = (Bytef*)src;
    stream.avail_in = (uInt)src_len;

    unsigned long buffer_max = src_len << 2;
    if(buffer_max > COMPRESS_BUFFER_LIMIT) {
      buffer_max = COMPRESS_BUFFER_LIMIT;
    }

    char* buffer = (char*)calloc(buffer_max, sizeof(char));
    if(!buffer) {
      return nullptr;
    }

    if(inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
      free(buffer);
      buffer = nullptr;
      return nullptr;
    }

    bool success = false;
    do {
      if(stream.total_out >= buffer_max) {
        char* temp = (char*)calloc(sizeof(char), static_cast<size_t>(buffer_max) << 1);
        if(!temp) {
          free(buffer);
          buffer = nullptr;
          return nullptr;
        }

        memcpy(temp, buffer, buffer_max);
        buffer_max <<= 1;

        free(buffer);
        buffer = temp;
      }

      stream.next_out = (Bytef*)(buffer + stream.total_out);
      stream.avail_out = (uInt)buffer_max - stream.total_out;

      const int status = inflate(&stream, Z_SYNC_FLUSH);
      if(status == Z_STREAM_END) {
        success = true;
      }
      else if(status != Z_OK) {
        free(buffer);
        buffer = nullptr;
        return nullptr;
      }
    } 
    while(buffer_max < COMPRESS_BUFFER_LIMIT && !success);

    if(inflateEnd(&stream) != Z_OK) {
      free(buffer);
      buffer = nullptr;
      return nullptr;
    }

    out_len = stream.total_out;
    return buffer;
  }
};

/**
 * Result structure for command line parsing
 */
struct CommandLineParseResult {
  std::map<const std::wstring, std::wstring> arguments;
  std::vector<std::wstring> errors;
  std::wstring reconstructed_path;
};

/**
 * Enhanced command line parser with GNU-style syntax support
 * Supports:
 *   - Legacy: -key value
 *   - GNU long: --long-option value, --long-option=value
 *   - GNU short: -s value, -s=value
 *   - Boolean flags: --flag (stored as empty string)
 *   - Quoted values: --option 'value with spaces'
 */
static CommandLineParseResult ParseCommandLine(int argc, const char* argv[]) {
  CommandLineParseResult result;

  // Reconstruct command line
  std::string path;
  for(int i = 1; i < 1024 && i < argc; ++i) {
    path += ' ';
    const char* cmd_param = argv[i];
    if(strlen(cmd_param) > 0 && cmd_param[0] != '\'' && (strrchr(cmd_param, ' ') || strrchr(cmd_param, '\t'))) {
      path += '\'';
      path += cmd_param;
      path += '\'';
    }
    else {
      path += cmd_param;
    }
  }
  result.reconstructed_path = BytesToUnicode(path);

  size_t pos = 0;
  size_t end = result.reconstructed_path.size();

  while(pos < end) {
    // Skip leading whitespace
    while(pos < end && (result.reconstructed_path[pos] == L' ' || result.reconstructed_path[pos] == L'\t')) {
      pos++;
    }

    if(pos >= end) {
      break;
    }

    // Check for option starting with '-'
    if(result.reconstructed_path[pos] == L'-') {
      size_t key_start = pos;
      pos++; // Skip first '-'

      // Determine option format: --long, -short, or legacy -key
      bool is_gnu_long = false;
      if(pos < end && result.reconstructed_path[pos] == L'-') {
        is_gnu_long = true;
        pos++; // Skip second '-'
      }

      // Parse key name
      size_t key_name_start = pos;
      while(pos < end && result.reconstructed_path[pos] != L' ' &&
            result.reconstructed_path[pos] != L'\t' &&
            result.reconstructed_path[pos] != L'=') {
        pos++;
      }

      if(key_name_start == pos) {
        // Empty key name
        result.errors.push_back(L"Empty option name at position " + std::to_wstring(key_start));
        continue;
      }

      std::wstring key = result.reconstructed_path.substr(key_name_start, pos - key_name_start);

      // Check for '=' separator
      bool has_equals = (pos < end && result.reconstructed_path[pos] == L'=');
      if(has_equals) {
        pos++; // Skip '='
      }

      // Skip whitespace before value
      while(pos < end && (result.reconstructed_path[pos] == L' ' || result.reconstructed_path[pos] == L'\t')) {
        pos++;
      }

      // Parse value
      std::wstring value;
      bool has_value = false;

      // Check if next token is a value (not another option)
      if(pos < end && result.reconstructed_path[pos] != L'-') {
        has_value = true;
        size_t value_start = pos;
        bool is_quoted = false;

        // Check for quoted value
        if(result.reconstructed_path[pos] == L'\'') {
          is_quoted = true;
          value_start++;
          pos++;

          // Find closing quote
          while(pos < end && result.reconstructed_path[pos] != L'\'') {
            pos++;
          }

          if(pos >= end) {
            result.errors.push_back(L"Unclosed quote for option: " + key);
          }

          value = result.reconstructed_path.substr(value_start, pos - value_start);

          if(pos < end && result.reconstructed_path[pos] == L'\'') {
            pos++; // Skip closing quote
          }
        }
        else {
          // Unquoted value - read until whitespace
          while(pos < end && result.reconstructed_path[pos] != L' ' && result.reconstructed_path[pos] != L'\t') {
            pos++;
          }
          value = result.reconstructed_path.substr(value_start, pos - value_start);
        }
      }
      else if(has_equals) {
        // Had '=' but no value - empty value
        has_value = true;
        value = L"";
      }

      // For GNU-style options without value, store as boolean flag (empty string)
      if(!has_value && (is_gnu_long || (!is_gnu_long && key.length() == 1))) {
        value = L"";
      }

      // Handle duplicate keys - concatenate with comma (backward compatible behavior)
      auto found = result.arguments.find(key);
      if(found != result.arguments.end()) {
        value = found->second + L"," + value;
      }

      result.arguments[key] = value;
    }
    else {
      // Not an option, skip this character
      pos++;
    }
  }

  return result;
}

/**
 * Legacy command line parser wrapper (for backward compatibility)
 * @deprecated Use ParseCommandLine() instead for enhanced GNU-style syntax support
 * Note: Function name contains typo (Commnad instead of Command) but kept for compatibility
 */
static std::map<const std::wstring, std::wstring> ParseCommnadLine(int argc, const char* argv[], std::wstring &path_string) {
  CommandLineParseResult result = ParseCommandLine(argc, argv);
  path_string = result.reconstructed_path;

  // Log errors to stderr if any
  for(const auto& error : result.errors) {
    std::wcerr << L"Warning: " << error << std::endl;
  }

  return result.arguments;
}

/**
 * Check if a command line argument exists
 */
static bool HasCommandLineArgument(
    const std::map<const std::wstring, std::wstring>& args,
    const std::wstring& key) {
  return args.find(key) != args.end();
}

/**
 * Get command line argument with default value
 */
static std::wstring GetCommandLineArgument(
    const std::map<const std::wstring, std::wstring>& args,
    const std::wstring& key,
    const std::wstring& default_value = L"") {
  auto found = args.find(key);
  return (found != args.end()) ? found->second : default_value;
}

/**
 * Check if argument is a boolean flag (present with empty value)
 */
static bool IsCommandLineFlag(
    const std::map<const std::wstring, std::wstring>& args,
    const std::wstring& key) {
  auto found = args.find(key);
  return (found != args.end() && found->second.empty());
}

/**
 * Get command line argument checking multiple aliases (for short/long form support)
 * Returns the value of the first matching alias, or default_value if none found
 */
static std::wstring GetCommandLineArgumentWithAliases(
    const std::map<const std::wstring, std::wstring>& args,
    const std::vector<std::wstring>& aliases,
    const std::wstring& default_value = L"") {
  for(const auto& alias : aliases) {
    auto found = args.find(alias);
    if(found != args.end()) {
      return found->second;
    }
  }
  return default_value;
}

/**
 * Check if any of the provided aliases exists in arguments
 */
static bool HasCommandLineArgumentWithAliases(
    const std::map<const std::wstring, std::wstring>& args,
    const std::vector<std::wstring>& aliases) {
  for(const auto& alias : aliases) {
    if(args.find(alias) != args.end()) {
      return true;
    }
  }
  return false;
}

static std::wstring GetLibraryPath() {
  std::wstring path;

#ifdef _WIN32
  size_t value_len;
  char path_str[SMALL_BUFFER_MAX];
  if(!getenv_s(&value_len, path_str, SMALL_BUFFER_MAX, "OBJECK_LIB_PATH") && strlen(path_str) > 0) {
#else
  const char* path_str = getenv("OBJECK_LIB_PATH");
  if(path_str) {
#endif    
    const std::string temp_str(path_str);
    path = BytesToUnicode(temp_str);
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

/**
 * Load a UTF-8 text into memory.
 */
static wchar_t* LoadFileBuffer(const std::wstring &filename, size_t& buffer_size)
{
  char* buffer;
  const std::string open_filename = UnicodeToBytes(filename);

  std::ifstream in(open_filename.c_str(), std::ios_base::in | std::ios_base::binary | std::ios_base::ate);
  if(in.good()) {
    // get file size
    in.seekg(0, std::ios::end);
    buffer_size = (size_t)in.tellg();
    in.seekg(0, std::ios::beg);
    buffer = (char*)calloc(buffer_size + 1, sizeof(char));
    if(!buffer) {
      in.close();
      return nullptr;
    }
  
    in.read(buffer, buffer_size);
    in.close();
  }
  else {
    return nullptr;;
  }

  // convert Unicode
#ifdef _WIN32
  const int wsize = MultiByteToWideChar(CP_UTF8, 0, buffer, -1, nullptr, 0);
  if(wsize == 0) {
    return nullptr;;
  }
  wchar_t* wbuffer = new wchar_t[wsize];
  const int check = MultiByteToWideChar(CP_UTF8, 0, buffer, -1, wbuffer, wsize);
  if(check == 0) {
    return nullptr;;
  }
#else
  size_t wsize = mbstowcs(nullptr, buffer, buffer_size);
  if(wsize == (size_t)-1) {
    free(buffer);
    return nullptr;;
  }
  
  if(wsize < buffer_size) {
    wsize = buffer_size;
  }
  wchar_t* wbuffer = new wchar_t[wsize + 1];

  size_t check = mbstowcs(wbuffer, buffer, buffer_size);
  if(check == (size_t)-1) {
    free(buffer);
    delete[] wbuffer;
    return nullptr;;
  }
  wbuffer[wsize] = L'\0';
#endif

  free(buffer);
  return wbuffer;
}
