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

#define SYNTAX_ERROR "Huh, cannot do that. Try help '/h'?"

#define HELP_PARAM L"help"
#define HELP_ALT_PARAM L"h"

#define FILE_PARAM L"file"
#define FILE_ALT_PARAM L"f"

#define INLINE_PARAM L"inline"
#define INLINE_ALT_PARAM L"i"

#define LIBS_PARAM L"lib"
#define LIBS_ALT_PARAM L"l"

#define OPT_PARAM L"opt"
#define OPT_ALT_PARAM L"o"

#define EXIT_PARAM L"exit"
#define EXIT_ALT_PARAM L"e"

static std::wstring GetArg(std::map<const std::wstring, std::wstring> arguments, const std::wstring values[]);
static void RemoveArg(std::map<const std::wstring, std::wstring>& arguments, const std::wstring values[]);

static void Usage();
static void SetEnv();

#endif
