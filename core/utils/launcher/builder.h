/***************************************************************************
 * Builder for native execution environment
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

#include <iostream>
#include <string.h>
#include <string>
#include <map>
#include <list>
#include <vector>
#include <filesystem>
#include "../../shared/version.h"

#ifdef _WIN32
#include <windows.h>
#elif _OSX
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

#define MAX_ENV_PATH 32768

namespace fs = std::filesystem;
using namespace std;

/**
 * Delete all files with a given extension
 */
void DeleteAllFileTypes(const fs::path& from_dir, const fs::path ext_type);

/**
 * Checks the ending of a string
 */
static bool EndsWith(const wstring& str, const wstring& ending);

/**
 * Parses the command line
 */
static map<const wstring, wstring> ParseCommnadLine(int argc, const char* argv[], list<wstring>& options);

/**
 * Removed unneeded directory slashed
 */
static void TrimFileEnding(wstring& filename);

/**
 * Gets a command line parameter
 */
static wstring GetCommandParameter(const wstring& key, map<const wstring, wstring>& cmd_params, list<wstring>& argument_options, bool optional = false);

/**
 * Gets the program usage
 */
static wstring GetUsage();

/**
 * Get runtime install directory 
 */
static wstring GetInstallDirectory();

/**
 * Validate the runtime directory structure
 */
static bool CheckInstallDir(const wstring& install_dir);

/**
 * Converts UTF-8 bytes to a
 * native Unicode string
 */
static bool BytesToUnicode(const string& in, wstring& out);
static wstring BytesToUnicode(const string& in);

#endif
