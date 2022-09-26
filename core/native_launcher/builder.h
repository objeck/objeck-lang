/***************************************************************************
 * Native executable builder
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

#ifndef __NATIVE_BUILDER__
#define __NATIVE_BUILDER__

#include "common.h"
#include "../shared/version.h"

#include <iostream>

namespace fs = std::filesystem;
using namespace std;

void remove_all_file_types(fs::path& from_dir, fs::path ext_type) {
  try {
    for(const auto& inter : fs::directory_iterator(from_dir)) {
      if(inter.path().extension() == ext_type) {
        fs::remove(inter.path());
      }
    }
  }
  catch(std::exception& e) {
    throw e;
  }
}

static bool EndsWith(wstring const& str, wstring const& ending)
{
  if(str.length() >= ending.length()) {
    return str.compare(str.length() - ending.length(), ending.length(), ending) == 0;
  }

  return false;
}

static map<const wstring, wstring> ParseCommnadLine(int argc, char* argv[]) {
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
  wstring path_string = BytesToUnicode(path);

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

static wstring GetUsage() {
  wstring usage;

  usage += L"Usage: obb -src <input *.obe file> -to_dir <output directory> -to_name <name of target exe> -install <root Objeck directory>\n\n";
  usage += L"Options:\n";
  usage += L"  -src:      [input] source *.obe file\n";
  usage += L"  -to_dir:   [output] output file directory\n";
  usage += L"  -to_name:  [output] output app name\n";
  usage += L"  -install:  [input] root Objeck directory to copy the runtime from\n";
  usage += L"\nExample: \"obb -install objeck-lang -dest /tmp/ -src /tmp/hello.obe\"\n\nVersion: ";
  usage += VERSION_STRING;

  return usage;
}

#endif