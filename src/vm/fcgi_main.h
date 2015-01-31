/***************************************************************************
* Starting point for FastCGI module
*
* Copyright (c) 2008-2013, Randy Hollines
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

#ifndef __VM_FCGI_H__
#define __VM_FCGI_H__

#include "fcgi_config.h"
#include "fcgiapp.h"
#include "vm.h"

#ifdef _WIN32
#include <process.h>
#else
extern char **environ;
#endif

// support function
void PrintEnv(FCGX_Stream* out, const char* label, char** envp);

/******************************
* Reads/writes INI files
******************************/
class IniManager {
  map<const wstring, map<const wstring, wstring>*> section_map;
  wstring filename, input;
  wchar_t cur_char, next_char;
  size_t cur_pos;
  bool locked;
  wstring objeck_path;
  wstring indent_spacing;
  wstring line_ending;
  
  wstring LoadFile(const wstring &fn);
  void NextChar();
  void Clear();
  wstring Serialize();
  void Deserialize();
  
public:
  IniManager(const wstring &fn);
  ~IniManager();

  // writes a file
  static bool WriteFile(const wstring &fn, const wstring &out);

  // set/retrieve values
  wstring GetValue(const wstring &sec, const wstring &key);
  bool SetValue(const wstring &sec, const wstring &key, const wstring &value);
  
  // set/retrieve/remove list values
  wxArrayString GetListValues(const wstring &sec, const wstring &key);
  bool AddListValue(const wstring &sec, const wstring &key, const wstring &val);
  bool RemoveListValue(const wstring &sec, const wstring &key, const wstring &val);

  // load and save to file 
  bool Load();
  bool Save();
  bool IsLocked() { 
    return locked; 
  }
};

#endif
