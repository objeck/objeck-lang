/***************************************************************************
 * REPL shell
 *
 * Copyright (c) 2023, Randy Hollines
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduceC the above copyright
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

#ifndef __REPL_H__
#define __REPL_H__

#include "editor.h"
#include "../module/lang.h"

constexpr wchar_t SYNTAX_ERROR[] = L"Huh, cannot do that. Try help '/h'?";

constexpr wchar_t HELP_PARAM[] = L"help";
constexpr wchar_t HELP_ALT_PARAM[] = L"h";

constexpr wchar_t FILE_PARAM[] = L"file";
constexpr wchar_t FILE_ALT_PARAM[] = L"f";

constexpr wchar_t INLINE_PARAM[] = L"inline";
constexpr wchar_t INLINE_ALT_PARAM[] = L"i";

constexpr wchar_t LIBS_PARAM[] = L"lib";
constexpr wchar_t LIBS_ALT_PARAM[] = L"l";

constexpr wchar_t OPT_PARAM[] = L"opt";
constexpr wchar_t OPT_ALT_PARAM[] = L"o";

constexpr wchar_t EXIT_PARAM[] = L"quit";
constexpr wchar_t EXIT_ALT_PARAM[] = L"q";

static std::wstring GetArgument(std::map<const std::wstring, std::wstring> arguments, const std::wstring values[]);
static void RemoveArgument(std::map<const std::wstring, std::wstring>& arguments, const std::wstring values[]);
static bool HasArgument(std::map<const std::wstring, std::wstring> arguments, const std::wstring values[]);

static void Usage();
void SetEnv();

#endif
