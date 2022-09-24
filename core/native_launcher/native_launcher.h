/***************************************************************************
 * Native executable launcher for
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

#ifndef __NATIVE_LAUNCHER__
#define __NATIVE_LAUNCHER__

#include <string>
#include <iostream>

#ifdef _WIN32
#include "process.h"
#include "windows.h"
#endif

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

using namespace std;

/**
 * Converts a native string to UTF-8 bytes
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
 * Get the current working directory
 */
static const string GetWorkingDirectory() {
  string working_dir;

#ifdef _WIN32
  TCHAR exe_full_path[MAX_PATH] = { 0 };
  GetModuleFileName(nullptr, exe_full_path, MAX_PATH);
  const string dir_full_path = UnicodeToBytes(exe_full_path);
  size_t dir_full_path_index = dir_full_path.find_last_of('\\');
#else
  char dir_full_path[MAX_PATH];
  getcwd(&dir_full_path, MAX_PATH);
  size_t dir_full_path_index = dir_full_path.find_last_of('\\');
#endif

  if(dir_full_path_index != string::npos) {
    return dir_full_path.substr(0, dir_full_path_index);
  }

  return "";
}

static const string GetEnviromentPath(const string& working_dir) {
  char* cur_env = nullptr;

#ifdef _WIN32
  size_t cur_env_len;
  _dupenv_s(&cur_env, &cur_env_len, "PATH");
  if(cur_env) {
    return "PATH=" + string(cur_env) + ';' + working_dir + "\\runtime\\bin" + ';' + working_dir + "\\runtime\\lib\\native";
  }
#else
  cur_env = getenv("PATH");
  if(cur_env) {
    return "PATH=" + string(cur_env) + ';' + working_dir + "/runtime/bin" + ';' + working_dir + "/runtime/lib/native";
  }
#endif

  return "";
}

static const string GetLibraryPath(const string& working_dir) {
#ifdef _WIN32
  return "OBJECK_LIB_PATH=" + working_dir + "\\runtime\\lib";
#else
  return "OBJECK_LIB_PATH=" + working_dir + "/runtime/lib";
#endif
}

#endif