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

#include <iostream>
#include <string.h>
#include <string>
#include <vector>

#ifdef _WIN32
#include "windows.h"
#include "process.h"
#else
#include <unistd.h>
#endif

#define MAX_ENV_PATH 1024 * 32

using namespace std;

/**
 * Build the environment variables
 */
static char** BuildEnviromentParameters(const string &path_env_str, const string &lib_env_str, char** env_ptrs);

/**
 * Free the built environment variables
 */
static void FreeEnviromentVariables(char** envp);

/**
 * Get the obr execution path
 */
static const string GetExecutablePath(const string working_dir);

/**
 * Get environment arguments
 */
static char** BuildArguments(const string& spawn_path, int argc, char* argv[]);

/**
 * Get the current working directory
 */
static const string GetWorkingDirectory();

/**
 * Get the environment PATH value
 */
static const string BuildPathVariable(const string& working_dir);

/**
 * Get the environment OBJECK_LIB_PATH value
 */
static const string BuildObjeckLibVariable(const string& working_dir);

/**
 * Checks to start of a string
 */
bool StartWith(const char* str_ptr, const char* check_ptr) {
  return strncmp(check_ptr, str_ptr, strlen(check_ptr)) == 0;
}

/**
 * Execute
 */
static int Spawn(const char* spawn_path, char** spawn_args, char** spawn_env);

#endif
