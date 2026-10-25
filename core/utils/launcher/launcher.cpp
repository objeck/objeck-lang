/***************************************************************************
 * Native executable launcher 
 *
 * Copyright (c) 2023, Randy Hollines
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
 * contributors may be used to endorse or promote prod*ucts derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOTre
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

#include "launcher.h"

#ifdef _WIN32
// wmain takes the arguments as UTF-16; MinGW builds link with -municode to
// enter here
int wmain(int argc, wchar_t* argv[])
{
  const wstring working_dir = GetWorkingDirectory();
  if(working_dir.empty()) {
    cerr << ">>> Unable to find the working directory <<<" << endl;
    return 1;
  }
  const wstring spawn_path = working_dir + L"\\runtime\\bin\\obr.exe";

  // one command line, each argument quoted: _spawnve joined its arguments with
  // spaces and quoted none of them, so an install path or an argument with a
  // space reached obr split in two
  wstring command_line = QuoteArgument(spawn_path) + L' ' + QuoteArgument(L".\\app\\app.obe");
  for(int i = 1; i < argc; ++i) {
    command_line += L' ';
    command_line += QuoteArgument(argv[i]);
  }
  vector<wchar_t> writable_line(command_line.begin(), command_line.end());
  writable_line.push_back(L'\0');

  // PATH gains the runtime's directories; OBJECK_LIB_PATH names its libraries.
  // Variable names are matched whole and without regard to case, as Windows
  // matches them: a prefix test dropped PATHEXT and kept a second 'Path'. PATH
  // is read at any length: a 1 KB buffer failed every launch once PATH grew
  // past it.
  wstring path = GetVariable(L"PATH");
  if(!path.empty() && path.back() != L';') {
    path += L';';
  }
  path += working_dir + L"\\runtime\\lib\\sdl;";
  path += working_dir + L"\\runtime\\lib\\native;";
  path += working_dir + L"\\runtime\\bin";

  // The rest of the environment as the process has it, the per-drive
  // directories ('=C:=...') included. Not wmain's envp through _wspawnve: that
  // faulted inside ucrtbase, before obr started, whenever the launcher itself
  // had been started with an explicit environment block.
  vector<wchar_t> env_block;
  const wstring path_entry = L"PATH=" + path;
  const wstring lib_entry = L"OBJECK_LIB_PATH=" + working_dir + L"\\runtime\\lib";
  env_block.insert(env_block.end(), path_entry.c_str(), path_entry.c_str() + path_entry.size() + 1);
  env_block.insert(env_block.end(), lib_entry.c_str(), lib_entry.c_str() + lib_entry.size() + 1);
  wchar_t* strings = GetEnvironmentStringsW();
  if(strings) {
    for(const wchar_t* entry = strings; *entry; entry += wcslen(entry) + 1) {
      if(!IsVariable(entry, L"PATH") && !IsVariable(entry, L"OBJECK_LIB_PATH")) {
        env_block.insert(env_block.end(), entry, entry + wcslen(entry) + 1);
      }
    }
    FreeEnvironmentStringsW(strings);
  }
  env_block.push_back(L'\0');

  STARTUPINFOW start;
  ZeroMemory(&start, sizeof(start));
  start.cb = sizeof(start);
  start.dwFlags = STARTF_USESTDHANDLES;
  start.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  start.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  start.hStdError = GetStdHandle(STD_ERROR_HANDLE);

  PROCESS_INFORMATION proc;
  ZeroMemory(&proc, sizeof(proc));
  if(!CreateProcessW(spawn_path.c_str(), writable_line.data(), nullptr, nullptr, TRUE,
                     CREATE_UNICODE_ENVIRONMENT, env_block.data(), nullptr, &start, &proc)) {
    cerr << ">>> Unable to start the runtime <<<" << endl;
    return 1;
  }

  WaitForSingleObject(proc.hProcess, INFINITE);
  DWORD status = 1;
  if(!GetExitCodeProcess(proc.hProcess, &status)) {
    status = 1;
  }
  CloseHandle(proc.hProcess);
  CloseHandle(proc.hThread);

  // the program's own exit status, as execve gives it on POSIX
  return (int)status;
}

const wstring GetWorkingDirectory()
{
  // grow past MAX_PATH: a long install path was cut off and read as no directory
  vector<wchar_t> buffer(MAX_PATH);
  DWORD length = 0;
  while(true) {
    length = GetModuleFileNameW(nullptr, buffer.data(), (DWORD)buffer.size());
    if(length == 0) {
      return L"";
    }
    if(length < buffer.size()) {
      break;
    }
    buffer.resize(buffer.size() * 2);
  }

  const wstring exe_path(buffer.data(), length);
  const size_t index = exe_path.find_last_of(L'\\');
  if(index == wstring::npos) {
    return L"";
  }

  // Run relative to the application root, as the POSIX launcher does:
  // '.\\app\\app.obe' and the program's own relative resource paths resolved
  // against wherever the launch came from, so a start from another directory
  // (or a shortcut without 'Start in') found no program.
  const wstring exe_dir = exe_path.substr(0, index);
  if(!SetCurrentDirectoryW(exe_dir.c_str())) {
    return L"";
  }

  return exe_dir;
}

wstring QuoteArgument(const wstring& arg)
{
  if(!arg.empty() && arg.find_first_of(L" \t\n\v\"") == wstring::npos) {
    return arg;
  }

  wstring quoted = L"\"";
  for(size_t i = 0; ; ++i) {
    size_t slashes = 0;
    while(i < arg.size() && arg[i] == L'\\') {
      ++i;
      ++slashes;
    }

    if(i == arg.size()) {
      // double the trailing run so it cannot escape the closing quote
      quoted.append(slashes * 2, L'\\');
      break;
    }

    // a run of backslashes is only special immediately before a quote
    quoted.append(arg[i] == L'"' ? slashes * 2 + 1 : slashes, L'\\');
    quoted += arg[i];
  }

  return quoted + L'"';
}

bool IsVariable(const wchar_t* entry, const wchar_t* name)
{
  const size_t length = wcslen(name);
  return _wcsnicmp(entry, name, length) == 0 && entry[length] == L'=';
}

wstring GetVariable(const wchar_t* name)
{
  const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
  if(size == 0) {
    return L"";
  }

  vector<wchar_t> buffer(size);
  const DWORD length = GetEnvironmentVariableW(name, buffer.data(), size);
  if(length == 0 || length >= size) {
    return L"";
  }

  return wstring(buffer.data(), length);
}
#else

int main(int argc, char* argv[], char** envp)
{
  const string working_dir = GetWorkingDirectory();
  if(working_dir.empty()) {
    cerr << ">>> Unable to find the working directory <<<" << endl;
    return 1;
  }
  const string spawn_path = GetExecutablePath(working_dir);

  char** spawn_args = BuildArguments(spawn_path, argc, argv);
  if(!spawn_args) {
    cerr << ">>> Unable to initialize environment <<<" << endl;
    return 1;
  }

  const string path_env_str = BuildPathVariable(working_dir);
  const string lib_env_str = BuildObjeckLibVariable(working_dir);
  if(path_env_str.empty() || lib_env_str.empty()) {
    cerr << ">>> Unable to determine the current working directory <<<" << endl;
    return 1;
  }

  int status = 1;
  char** spawn_env = BuildEnviromentParameters(path_env_str, lib_env_str, envp);
  if(spawn_env) {
    status = Spawn(spawn_path.c_str(), spawn_args, spawn_env);
  }
  FreeEnviromentVariables(spawn_env);

  return status;
}

char** BuildEnviromentParameters(const string& path_env_str, const string& lib_env_str, char** env_ptrs)
{
  vector<string> parameters;

  parameters.push_back(path_env_str);
  parameters.push_back(lib_env_str);

  int index = 0;
  char* env_ptr = env_ptrs[index];
  while(env_ptr) {
    if(!StartWith(env_ptr, "PATH") && !StartWith(env_ptr, "OBJECK_LIB_PATH")) {
      parameters.push_back(env_ptr);
    }
    env_ptr = env_ptrs[++index];
  }

  char** new_env_ptrs = new char*[parameters.size() + 1];
  for(size_t i = 0; i < parameters.size(); ++i) {
    new_env_ptrs[i] = strdup(parameters[i].c_str());
  }
  new_env_ptrs[parameters.size()] = nullptr;

  return new_env_ptrs;
}

void FreeEnviromentVariables(char** env_ptrs)
{
  int index = 0;
  char* env_ptr = env_ptrs[index];
  while(env_ptr) {
    delete[] env_ptr;
    env_ptr = env_ptrs[++index];
  }

  delete[] env_ptrs;
  env_ptrs = nullptr;
}

const string BuildObjeckLibVariable(const string& working_dir) 
{
  return "OBJECK_LIB_PATH=" + working_dir + "/runtime/lib";
}

/**
 * Get the environment PATH value
 */
const string BuildPathVariable(const string& working_dir) {
  char* path_ptr = getenv("PATH");
  if(path_ptr) {
    const string objk_bin_str = working_dir + "/runtime/bin";
    const string objk_native_str = working_dir + "/runtime/lib/native";

    return "PATH=" + string(path_ptr) + ':' + objk_native_str + ':' + objk_bin_str;
  }

  return "";
}

const string GetExecutablePath(const string working_dir) 
{
  return working_dir + "/runtime/bin/obr";
}

char** BuildArguments(const string& spawn_str, int argc, char* argv[]) 
{
  if(argc < 1) {
    return nullptr;
  }

  // adding parameter for obr call to 'app.obe' and required null-terminated pointer
  char** spawn_args = new char*[argc + 2];
  if(!spawn_args) {
    return nullptr;
  }
  spawn_args[0] = strdup(spawn_str.c_str());
  spawn_args[1] = strdup("./app/app.obe");

  for(int i = 1; i < argc; ++i) {
    spawn_args[i + 1] = strdup(argv[i]);
  }
  spawn_args[argc + 1] = nullptr;

  return spawn_args;
}

const string GetWorkingDirectory() 
{
  // Resolve the directory containing this executable (mirrors the Windows
  // GetModuleFileName branch). The old getcwd() approach depended on how the
  // launcher was started: a Finder double-click (or running it from another
  // directory) gives a cwd that isn't the bundle root, so every relative path
  // (runtime/bin/obr, ./app/app.obe) resolved against the wrong directory.
  char exe_full_path[MAX_ENV_PATH] = { 0 };
#ifdef __APPLE__
  char raw_path[MAX_ENV_PATH] = { 0 };
  uint32_t size = MAX_ENV_PATH;
  // _NSGetExecutablePath may return a relative/symlinked path; realpath() makes it absolute
  if(_NSGetExecutablePath(raw_path, &size) != 0 || !realpath(raw_path, exe_full_path)) {
    return "";
  }
#else
  const ssize_t len = readlink("/proc/self/exe", exe_full_path, MAX_ENV_PATH - 1);
  if(len < 0) {
    return "";
  }
  exe_full_path[len] = '\0';
#endif
  const string dir_full_path = exe_full_path;
  const size_t dir_full_path_index = dir_full_path.find_last_of('/');
  if(dir_full_path_index != string::npos) {
    // run relative to the bundle root so './app/app.obe' and the program's own
    // relative resource paths resolve no matter where the launch came from
    const string exe_dir = dir_full_path.substr(0, dir_full_path_index);
    if(chdir(exe_dir.c_str())) {
      return "";
    }
    return exe_dir;
  }

  return "";
}

int Spawn(const char* spawn_path, char** spawn_args, char** spawn_env) 
{
  const int result = execve(spawn_path, spawn_args, spawn_env);

  delete[] spawn_args;
  spawn_args = nullptr;

  return result ? 1 : 0;
}
#endif
