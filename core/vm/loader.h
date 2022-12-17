/***************************************************************************
 * Program loader.
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

#ifndef __LOADER_H__
#define __LOADER_H__

#include "common.h"
#include <string.h>

class Loader {
  static StackProgram* program;
  std::vector<std::wstring> arguments;
  int num_float_strings;
  int num_int_strings;
  int num_char_strings;
  StackMethod* init_method;
  int string_cls_id;
  std::wstring filename;
  char* buffer;
  char* alloc_buffer;
  size_t buffer_size;
  size_t buffer_pos;
  int start_class_id;
  int start_method_id;
  bool is_web;
  std::map<const std::wstring, const int> params;

  inline int ReadInt() {
    int32_t value = *((int32_t*)buffer);
    buffer += sizeof(value);
    return value;
  }

  inline uint32_t ReadUnsigned() {
    uint32_t value = *((uint32_t*)buffer);
    buffer += sizeof(value);
    return value;
  }
  
  inline int ReadByte() {
    char value = *((char*)buffer);
    buffer += sizeof(value);
    return value;
  }

  inline std::wstring ReadString() {
    const int size = ReadInt();
    std::string in(buffer, size);
    buffer += size;    
    
    std::wstring out;
    if(!BytesToUnicode(in, out)) {
      std::wcerr << L">>> Unable to read unicode std::string <<<" << std::endl;
      exit(1);
    }
    
    return out;
  }

  inline wchar_t ReadChar() {
    wchar_t out;
    
    const int size = ReadInt(); 
    if(size) {
      std::string in(buffer, size);
      buffer += size;
      if(!BytesToCharacter(in, out)) {
        std::wcerr << L">>> Unable to read character <<<" << std::endl;
        exit(1);
      }
    }
    else {
      out = L'\0';
    }
    
    return out;
  }

  inline FLOAT_VALUE ReadDouble() {
    FLOAT_VALUE value = *((FLOAT_VALUE*)buffer);
    buffer += sizeof(value);
    return value;
  }

  // loads a file into memory
  char* LoadFileBuffer(std::wstring filename, size_t &buffer_size);

  void ReadFile() {
    buffer_pos = 0;
    alloc_buffer = buffer = LoadFileBuffer(filename, buffer_size);
  }

  // loading functions
  void LoadClasses();
  void LoadMethods(StackClass* cls, bool is_debug);
  StackDclr** LoadDeclarations(const int num_dclrs, const bool is_debug);
  void LoadInitializationCode(StackMethod* mthd);
  void LoadStatements(StackMethod* mthd, bool is_debug);
  void LoadConfiguration();
  
public:
  Loader(const wchar_t* arg) {
    filename = arg;
    if(!EndsWith(filename, L".obe")) {
      filename += L".obe";
    }

    string_cls_id = -1;
    is_web = false;
    ReadFile();
    program = new StackProgram;
  }

  Loader(const int argc, wchar_t** argv) {
    filename = argv[1];
    if(!EndsWith(filename, L".obe")) {
      filename += L".obe";
    }

    for(int i = 2; i < argc; ++i) {
      arguments.push_back(argv[i]);
    }
    string_cls_id = -1;
    is_web = false;
    ReadFile();
    program = new StackProgram;
  }

  ~Loader() {
    if(alloc_buffer) {
      free(alloc_buffer);
      alloc_buffer = nullptr;
    }

    delete program;
    program = nullptr;
  }

  static StackProgram* GetProgram();

  StackMethod* GetStartMethod() {
    StackClass* cls = program->GetClass(start_class_id);
    if(cls) {
      return cls->GetMethod(start_method_id);
    }

    return nullptr;
  }
  
  inline int GetConfigurationParameter(const std::wstring& key) {
    std::map<const std::wstring, const int>::iterator result = params.find(key);
    if(result != params.end()) {
      return result->second;
    }

    return 0;
  }
  
  inline bool IsWeb() {
    return is_web;
  }
  
  void Load();
};

#endif
