#ifndef __NATIVE_LAUNCHER__
#define __NATIVE_LAUNCHER__

#include <string>
#include <iostream>

#ifdef _WIN32
#include "windows.h"
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

#endif