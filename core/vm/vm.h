/***************************************************************************
* Defines the VM execution model.
*
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

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
#include "common.h"
#include "loader.h"
#include "interpreter.h"
#include <clocale>
#include <locale>

extern "C"
{
#ifdef _WIN32
  __declspec(dllexport) int Execute(int argc, const char* argv[], bool is_stdio_binary, size_t gc_threshold);
#else
  int Execute(int argc, const char* argv[], size_t gc_threshold);
#endif
}

wchar_t** ProcessCommandLine(int &argc, const char* argv[]);
void CleanUpCommandLine(const int argc, wchar_t** wide_args);
void SetEnv();
// posix_main.cpp calls it, and the MSYS2 debugger build (obd) compiles
// posix_main.cpp with _WIN32 AND _MSYS2 defined -- so _MSYS2 needs it too.
#if !defined(_WIN32) || defined(_MSYS2)
// The locale std::wcout converts wide characters through, built from what
// setlocale(LC_ALL, "") returned (NULL included). Never throws: a name the C++
// library cannot construct falls back to one it can (see vm.cpp).
std::locale ConsoleLocale(const char* name);

// Puts the process in a UTF-8 locale and gives std::wcout the converter that
// writes wide characters as UTF-8. obr, and every tool that hosts the VM (the
// debugger), calls it first: with LANG unset the locale is "C", where a
// program's non-ASCII literal can neither be decoded nor printed.
void InitUtf8Console();
#endif
