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

#define MAX_FILE_PATH 256

namespace fs = std::filesystem;
using namespace std;

/**
 * Delete all files with a given extension
 */
void remove_all_file_types(const fs::path& from_dir, const fs::path ext_type) {
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

/**
 * Checks the ending of a string
 */
static bool EndsWith(const wstring &str, const wstring &ending)
{
  if(str.length() >= ending.length()) {
    return str.compare(str.length() - ending.length(), ending.length(), ending) == 0;
  }

  return false;
}

/**
 * Parses the command line
 */
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

/**
 * Removed unneeded directory slashed
 */
static void TrimFilename(wstring& filename) {
  if(!filename.empty() && filename.back() == fs::path::preferred_separator) {
    filename.pop_back();
  }
}

/**
 * Gets a command line parameter
 */
static wstring GetCommandParameter(const wstring& key, map<const wstring, wstring> &cmd_params, list<wstring> &argument_options, bool optional = false) {
  wstring value;
  map<const wstring, wstring>::iterator result = cmd_params.find(key);
  if(result != cmd_params.end()) {
    value = result->second;
    argument_options.remove(key);
  }
  else if(false) {
    argument_options.remove(key);
  }

  TrimFilename(value);
  return value;
}

/**
 * Gets the program usage
 */
static wstring GetUsage() {
  wstring usage;

  usage += L"Usage: obb -src <input *.obe file> -to_dir <output directory> -to_name <name of target exe> -install <root Objeck directory>\n\n";
  usage += L"Options:\n";
  usage += L"  -src_file: [input] source .obe file\n";
  usage += L"  -src_dir:  [optional] directory of content to copy to app/resources\n";
  usage += L"  -to_dir:   [output] output file directory\n";
  usage += L"  -to_name:  [output] output app name\n";
  usage += L"  -install:  [optional] root Objeck directory to copy the runtime from\n";
  usage += L"\nExample: \"obb -src /tmp/hello.obe -to_dir /tmp -to_name hello -install /opt/objeck-lang\"\n\nVersion: ";
  usage += VERSION_STRING;

  return usage;
}

/**
 * Get runtime install directory 
 */
static wstring GetInstallDirectory() {
  wstring install_dir;

#ifdef _WIN32  
  char install_path[MAX_FILE_PATH];
  DWORD status = GetModuleFileNameA(nullptr, install_path, sizeof(install_path));
  if(status > 0) {
    string exe_path(install_path);
    size_t index = exe_path.find("\\app\\");
    if(index != string::npos) {
      install_dir = BytesToUnicode(exe_path.substr(0, index));
    }
  }
#else
  ssize_t status = 0;
  char install_path[MAX_FILE_PATH] = { 0 };
#ifdef _OSX
  uint32_t size = MAX_FILE_PATH;
  if(_NSGetExecutablePath(install_path, &size) != 0) {
    status = -1;
  }
#else
  status = ::readlink("/proc/self/exe", install_path, sizeof(install_path) - 1);
  if(status != -1) {
    install_path[status] = '\0';
  }
#endif
  if(status != -1) {
    string exe_path(install_path);
    size_t install_index = exe_path.find_last_of('/');
    if(install_index != string::npos) {
      exe_path = exe_path.substr(0, install_index);
      install_index = exe_path.find_last_of('/');
      if(install_index != string::npos) {
        install_dir = BytesToUnicode(exe_path.substr(0, install_index));
      }
    }
  }
#endif

  return install_dir;
}

/**
 * Validate the runtime directory structure
 */
static bool CheckInstallDir(const wstring &install_dir) {
  // sanity check
  fs::path readme_path(install_dir);
  readme_path += fs::path::preferred_separator;
  readme_path += L"readme.html";

  fs::path license_path(install_dir);
  license_path += fs::path::preferred_separator;
  license_path += L"LICENSE";

  return fs::exists(readme_path) && fs::exists(license_path);
}

#endif