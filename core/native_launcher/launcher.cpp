/***************************************************************************
 * Native executable launcher 
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
#ifdef _WIN32
    new_env_ptrs[i] = _strdup(parameters[i].c_str());
#else
    new_env_ptrs[i] = strdup(parameters[i].c_str());
#endif
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
#ifdef _WIN32
  return "OBJECK_LIB_PATH=" + working_dir + "\\runtime\\lib";
#else
  return "OBJECK_LIB_PATH=" + working_dir + "/runtime/lib";
#endif
}

/**
 * Get the environment PATH value
 */
const string BuildPathVariable(const string& working_dir) {
  char* path_ptr = nullptr;

#ifdef _WIN32
  size_t path_len;
  _dupenv_s(&path_ptr, &path_len, "PATH");

  if(path_ptr) {
    string path_str(path_ptr);

    free(path_ptr);
    path_ptr = nullptr;

    const string objk_bin_str = working_dir + "\\runtime\\bin";
    const string objk_native_str = working_dir + "\\runtime\\lib\\native";
    const string objk_sdl_str = working_dir + "\\runtime\\lib\\sdl";

    return "PATH=" + path_str + ';' + objk_sdl_str + ';' + objk_native_str + ';' + objk_bin_str;
  }
#else
  path_ptr = getenv("PATH");
  if(path_ptr) {
    const string objk_bin_str = working_dir + "/runtime/bin";
    const string objk_native_str = working_dir + "/runtime/lib/native";

    return "PATH=" + string(path_ptr) + ':' + objk_native_str + ':' + objk_bin_str;
  }
#endif

  return "";
}

const string GetExecutablePath(const string working_dir) 
{
#ifdef _WIN32
  return working_dir + "\\runtime\\bin\\obr.exe";
#else
  return working_dir + "/runtime/bin/obr";
#endif
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
#ifdef _WIN32
  spawn_args[0] = _strdup(spawn_str.c_str());
  spawn_args[1] = _strdup(".\\app\\app.obe");
#else
  spawn_args[0] = strdup(spawn_str.c_str());
  spawn_args[0] = spawn_args[1] = strdup("./app/app.obe");
#endif

  for(int i = 1; i < argc; ++i) {
#ifdef _WIN32
    spawn_args[i + 1] = _strdup(argv[i]);
#else
    spawn_args[i + 1] = strdup(argv[i]);
#endif
  }
  spawn_args[argc + 1] = nullptr;

  return spawn_args;
}

const string GetWorkingDirectory() 
{
#ifdef _WIN32
  TCHAR exe_full_path[MAX_ENV_PATH] = { 0 };
  GetModuleFileName(nullptr, exe_full_path, MAX_ENV_PATH);
  const string dir_full_path = exe_full_path;
  size_t dir_full_path_index = dir_full_path.find_last_of('\\');

  if(dir_full_path_index != string::npos) {
    return dir_full_path.substr(0, dir_full_path_index);
  }

  return "";
#else
  char exe_full_path[MAX_ENV_PATH] = { 0 };
  if(!getcwd(exe_full_path, MAX_ENV_PATH)) {
    return "";
  }
  return string(exe_full_path);
#endif
}

int Spawn(const char* spawn_path, char** spawn_args, char** spawn_env) 
{
#ifdef _WIN32
  intptr_t result = _spawnve(P_WAIT, spawn_path, spawn_args, spawn_env);
#else
  int result = execve(spawn_path, spawn_args, spawn_env);
#endif

  delete[] spawn_args;
  spawn_args = nullptr;

  return result == 0 ? 0 : 1;
}
