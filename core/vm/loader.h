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
  StackInstr** cached_instrs;
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
  std::map<const std::wstring, const int> params;
  
  inline long ReadInt() {
    int32_t value = *((int32_t*)buffer);
    buffer += sizeof(value);
    return value;
  }

  inline INT64_VALUE ReadInt64() {
    INT64_VALUE value = *((INT64_VALUE*)buffer);
    buffer += sizeof(value);
    return value;
  }

  inline unsigned long ReadUnsigned() {
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
  Loader(char* b) {
    LoadOperInstrs();

    string_cls_id = -1;
    buffer_pos = 0;
    alloc_buffer = buffer = b;
    program = new StackProgram;
  }

  Loader(const wchar_t* arg) {
    filename = arg;
    if(!::EndsWith(filename, L".obe") && !::EndsWith(filename, L".obw")) {
      filename += L".obe";
    }
    LoadOperInstrs();

    string_cls_id = -1;
    ReadFile();
    program = new StackProgram;
  }

  Loader(const int argc, wchar_t** argv) {
    filename = argv[1];
    if(!::EndsWith(filename, L".obe")) {
      filename += L".obe";
    }
    LoadOperInstrs();

    for(int i = 2; i < argc; ++i) {
      arguments.push_back(argv[i]);
    }
    string_cls_id = -1;
    ReadFile();
    program = new StackProgram;
  }

  void LoadOperInstrs() {
    cached_instrs = new StackInstr * [END_STMTS] {};

    // cache instructions
    cached_instrs[ADD_INT] = new StackInstr(-1, ADD_INT);
    cached_instrs[SUB_INT] = new StackInstr(-1, SUB_INT);
    cached_instrs[MUL_INT] = new StackInstr(-1, MUL_INT);
    cached_instrs[DIV_INT] = new StackInstr(-1, DIV_INT);
    cached_instrs[MOD_INT] = new StackInstr(-1, MOD_INT);
    cached_instrs[LES_INT] = new StackInstr(-1, LES_INT);
    cached_instrs[GTR_INT] = new StackInstr(-1, GTR_INT);
    cached_instrs[EQL_INT] = new StackInstr(-1, EQL_INT);
    cached_instrs[NEQL_INT] = new StackInstr(-1, NEQL_INT);
    cached_instrs[LES_EQL_INT] = new StackInstr(-1, LES_EQL_INT);
    cached_instrs[GTR_EQL_INT] = new StackInstr(-1, GTR_EQL_INT);

    cached_instrs[ADD_FLOAT] = new StackInstr(-1, ADD_FLOAT);
    cached_instrs[SUB_FLOAT] = new StackInstr(-1, SUB_FLOAT);
    cached_instrs[MUL_FLOAT] = new StackInstr(-1, MUL_FLOAT);
    cached_instrs[DIV_FLOAT] = new StackInstr(-1, DIV_FLOAT);
    cached_instrs[MOD_FLOAT] = new StackInstr(-1, MOD_FLOAT);
    cached_instrs[LES_FLOAT] = new StackInstr(-1, LES_FLOAT);
    cached_instrs[GTR_FLOAT] = new StackInstr(-1, GTR_FLOAT);
    cached_instrs[EQL_FLOAT] = new StackInstr(-1, EQL_FLOAT);
    cached_instrs[NEQL_FLOAT] = new StackInstr(-1, NEQL_FLOAT);
    cached_instrs[LES_EQL_FLOAT] = new StackInstr(-1, LES_EQL_FLOAT);
    cached_instrs[GTR_EQL_FLOAT] = new StackInstr(-1, GTR_EQL_FLOAT);

    cached_instrs[I2F] = new StackInstr(-1, I2F);
    cached_instrs[F2I] = new StackInstr(-1, F2I);
    cached_instrs[S2I] = new StackInstr(-1, S2I);
    cached_instrs[S2F] = new StackInstr(-1, S2F);
    cached_instrs[I2S] = new StackInstr(-1, I2S);
    cached_instrs[F2S] = new StackInstr(-1, F2S);

    cached_instrs[LOAD_ARY_SIZE] = new StackInstr(-1, LOAD_ARY_SIZE);
    cached_instrs[EXT_LIB_LOAD] = new StackInstr(-1, EXT_LIB_LOAD);
    cached_instrs[EXT_LIB_UNLOAD] = new StackInstr(-1, EXT_LIB_UNLOAD);
    cached_instrs[EXT_LIB_FUNC_CALL] = new StackInstr(-1, EXT_LIB_FUNC_CALL);
    cached_instrs[THREAD_JOIN] = new StackInstr(-1, THREAD_JOIN);
    cached_instrs[THREAD_SLEEP] = new StackInstr(-1, THREAD_SLEEP);
    cached_instrs[THREAD_MUTEX] = new StackInstr(-1, THREAD_MUTEX);
    cached_instrs[CRITICAL_START] = new StackInstr(-1, CRITICAL_START);
    cached_instrs[CRITICAL_END] = new StackInstr(-1, CRITICAL_END);

    cached_instrs[CPY_BYTE_ARY] = new StackInstr(-1, CPY_BYTE_ARY);
    cached_instrs[CPY_CHAR_ARY] = new StackInstr(-1, CPY_CHAR_ARY);
    cached_instrs[CPY_INT_ARY] = new StackInstr(-1, CPY_INT_ARY);
    cached_instrs[CPY_FLOAT_ARY] = new StackInstr(-1, CPY_FLOAT_ARY);

    cached_instrs[POP_INT] = new StackInstr(-1, POP_INT);
    cached_instrs[POP_FLOAT] = new StackInstr(-1, POP_FLOAT);
    cached_instrs[RTRN] = new StackInstr(-1, RTRN);

    cached_instrs[ZERO_BYTE_ARY] = new StackInstr(-1, ZERO_BYTE_ARY);
    cached_instrs[ZERO_CHAR_ARY] = new StackInstr(-1, ZERO_CHAR_ARY);
    cached_instrs[ZERO_INT_ARY] = new StackInstr(-1, ZERO_INT_ARY);
    cached_instrs[ZERO_FLOAT_ARY] = new StackInstr(-1, ZERO_FLOAT_ARY);

    cached_instrs[OR_INT] = new StackInstr(-1, OR_INT);
    cached_instrs[AND_INT] = new StackInstr(-1, AND_INT);
    cached_instrs[SWAP_INT] = new StackInstr(-1, SWAP_INT);

    cached_instrs[BIT_AND_INT] = new StackInstr(-1, BIT_AND_INT);
    cached_instrs[BIT_OR_INT] = new StackInstr(-1, BIT_OR_INT);
    cached_instrs[BIT_XOR_INT] = new StackInstr(-1, BIT_XOR_INT);

    cached_instrs[CEIL_FLOAT] = new StackInstr(-1, CEIL_FLOAT);
    cached_instrs[FLOR_FLOAT] = new StackInstr(-1, FLOR_FLOAT);
    cached_instrs[SIN_FLOAT] = new StackInstr(-1, SIN_FLOAT);
    cached_instrs[COS_FLOAT] = new StackInstr(-1, COS_FLOAT);
    cached_instrs[TAN_FLOAT] = new StackInstr(-1, TAN_FLOAT);
    cached_instrs[SQRT_FLOAT] = new StackInstr(-1, SQRT_FLOAT);
    cached_instrs[RAND_FLOAT] = new StackInstr(-1, RAND_FLOAT);

    cached_instrs[ASIN_FLOAT] = new StackInstr(-1, ASIN_FLOAT);
    cached_instrs[ACOS_FLOAT] = new StackInstr(-1, ACOS_FLOAT);
    cached_instrs[ATAN_FLOAT] = new StackInstr(-1, ATAN_FLOAT);
    cached_instrs[ATAN2_FLOAT] = new StackInstr(-1, ATAN2_FLOAT);
    cached_instrs[ACOSH_FLOAT] = new StackInstr(-1, ACOSH_FLOAT);
    cached_instrs[ASINH_FLOAT] = new StackInstr(-1, ASINH_FLOAT);
    cached_instrs[ATANH_FLOAT] = new StackInstr(-1, ATANH_FLOAT);
    cached_instrs[LOG_FLOAT] = new StackInstr(-1, LOG_FLOAT);
    cached_instrs[ROUND_FLOAT] = new StackInstr(-1, ROUND_FLOAT);
    cached_instrs[EXP_FLOAT] = new StackInstr(-1, EXP_FLOAT);
    cached_instrs[LOG10_FLOAT] = new StackInstr(-1, LOG10_FLOAT);
    cached_instrs[POW_FLOAT] = new StackInstr(-1, POW_FLOAT);
    cached_instrs[GAMMA_FLOAT] = new StackInstr(-1, GAMMA_FLOAT);

    cached_instrs[SHL_INT] = new StackInstr(-1, SHL_INT);
    cached_instrs[SHR_INT] = new StackInstr(-1, SHR_INT);
  }

  ~Loader() {
    if(alloc_buffer) {
      free(alloc_buffer);
      alloc_buffer = nullptr;
    }

    delete program;
    program = nullptr;

    for(size_t i = 0; i < END_STMTS; ++i) {
      StackInstr* tmp = cached_instrs[i];
      if(tmp) {
        delete tmp;
        tmp = nullptr;
      }
    }
    delete[] cached_instrs;
    cached_instrs = nullptr;
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
  
  void Load();
};

#endif
