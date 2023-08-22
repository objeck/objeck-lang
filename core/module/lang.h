/***************************************************************************
 * Objeck as a library
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

#ifndef __MODULE_H__
#define __MODULE_H__

#include "../compiler/compiler.h"
#include "../compiler/types.h"
#include "../vm/vm.h"
#include "../shared/logger.h"
#include "../shared/version.h"

//
// Objeck module
//
class ObjeckLang {
  std::wstring lib_uses;
  std::vector<std::wstring> errors;
  char* code;

public:
  ObjeckLang(const std::wstring &lib_uses);
  ~ObjeckLang();

  // compile code, 'file_source' are pairs of filename/source instances. this is done bacuase 
  // source code stored as a string still needs a filename. the 'opt_levl' are "s0" to "s3"
  bool Compile(std::vector<std::pair<std::wstring, std::wstring>>& file_source, const std::wstring opt_level);

  // gets compiler errors
  std::vector<std::wstring> GetErrors();

#ifdef _MODULE_STDIO
  // executes the program, returns the ouptut as string
  const std::wstring Execute(const std::wstring cmd_args);
  const std::wstring Execute();
#else
  // executes the program
  void Execute(const std::wstring cmd_args);
  void Execute();
#endif
};

#endif