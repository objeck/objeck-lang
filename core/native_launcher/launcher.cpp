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

int main(int argc, char* argv[])
{
  const string working_dir = GetWorkingDirectory();
  if(working_dir.empty()) {
    cerr << ">>> Unable to find the working directory <<<" << endl;
    return 1;
  }
  const string spawn_path = GetExecPath(working_dir);
  
  char** spawn_args = GetArgsPath(spawn_path, argc, argv);
  
  
  if(!spawn_args) {
    cerr << ">>> Unable to initialize environment <<<" << endl;
    return 1;
  }
  
  const string path_env = GetEnviromentPath(working_dir);
  const string lib_env = GetLibraryPath(working_dir);
  if(path_env.empty() || lib_env.empty()) {
    cerr << ">>> Unable to determine the current working directory <<<" << endl;
    return 1;
  }

#ifdef _WIN32
  char* path_env_ptr = _strdup(path_env.c_str());
  char* lib_env_ptr = _strdup(lib_env.c_str());
#else 
  char* path_env_ptr = strdup(path_env.c_str());
  char* lib_env_ptr = strdup(lib_env.c_str());
#endif
  char* spawn_env[] = { path_env_ptr, lib_env_ptr, nullptr };

  // cout << "spawn_path=|" << spawn_path << "|\nenv_path=|" << path_env << "|\nlib_env=|" << lib_env << '|' << endl;

  const int status = Spawn(spawn_path.c_str(), spawn_args, spawn_env);

  free(path_env_ptr);
  path_env_ptr = nullptr;

  free(lib_env_ptr);
  lib_env_ptr = nullptr;

  return status;
}
