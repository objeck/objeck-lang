/***************************************************************************
 * Defines internal language types.
 *
 * Copyright (c) 2025, Randy Hollines
 * All rights reserved.
 *
 * Redistribution and uses in source and binary forms, with or without
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

#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <assert.h>
#ifndef _WIN32
#include <stdint.h>
#include <stdlib.h>
#else
#include <windows.h>
#endif
#include "../shared/instrs.h"
#include "../shared/sys.h"
#include "../shared/traps.h"

#ifdef _DEBUG
#include "../shared/logger.h"
#endif

namespace frontend {
  /****************************
   * ParseNode base class
   ****************************/
  class ParseNode {
  protected:
    std::wstring file_name;
    int line_num;
    int line_pos;

  public:
    ParseNode(const std::wstring &f, const int l, const int p) {
      file_name = f;
      line_num = l;
      line_pos = p;
    }
    
    virtual ~ParseNode() {
    }
    
    inline const std::wstring GetFileName() {
      return file_name;
    }

    inline const int GetLineNumber() {
      return line_num;
    }

    inline const int GetLinePosition() {
      return line_pos;
    }

    void SetLinePosition(int p) {
      line_pos = p;
    }
  };

  /****************************
   * Entry types
   ****************************/
  enum EntryType {
    NIL_TYPE = -4000,
    BOOLEAN_TYPE,
    BYTE_TYPE,
    CHAR_TYPE,
    INT_TYPE,
    FLOAT_TYPE,
    CLASS_TYPE,
    FUNC_TYPE,
    ALIAS_TYPE,
    VAR_TYPE
  };

  /****************************
   * Method types
   ****************************/
  enum MethodType {
    NEW_PUBLIC_METHOD = -5000,
    NEW_PRIVATE_METHOD,
    PUBLIC_METHOD,
    PRIVATE_METHOD
  };

  /****************************
   * Method types
   ****************************/
  enum MethodCallType {    
    ENUM_CALL = -6000,
    NEW_INST_CALL,
    NEW_ARRAY_CALL,
    NEW_COPY_ARRAY_CALL,
    METHOD_CALL,
    PARENT_CALL
  };

  /******************************
   * Type class
   ****************************/
  class Type {
    friend class TypeFactory;
    EntryType type;
    int dimension;
    std::wstring class_name;
    std::vector<Type*> func_params;
    Type* func_rtrn;
    int func_param_count;
    std::vector<Type*> generic_types;
    std::wstring file_name;
    int line_num;
    int line_pos;

    bool is_resolved;
    void* klass_cache_ptr;
    void* lib_klass_cache_ptr;
    
    Type(Type* t) {
#ifdef _DEBUG
      assert(t);
#endif
      if(t) {
        type = t->type;
        dimension = t->dimension;
        class_name = t->class_name;
        func_rtrn = t->func_rtrn;
        func_params = t->func_params;
        func_param_count = -1;
        line_num = t->line_num;
        line_pos = t->line_pos;
        generic_types = t->generic_types;
        is_resolved = t->is_resolved;
        klass_cache_ptr = t->klass_cache_ptr;
        lib_klass_cache_ptr = t->lib_klass_cache_ptr;
      } 
    }
    
    Type(EntryType t, const std::wstring &n, const std::wstring& f, int l, int p) {
      type = t;
      class_name = n;
      file_name = f;
      line_num = l;
      line_pos = p;
      dimension = 0;
      func_rtrn = nullptr;
      func_param_count = -1;
      
      is_resolved = false;
      klass_cache_ptr = nullptr;
      lib_klass_cache_ptr = nullptr;
    }

    Type(EntryType t, const std::wstring f, const int l, const int p) {
      type = t;
      dimension = 0;
      func_rtrn = nullptr;
      func_param_count = -1;
      file_name = f;
      line_num = l;
      line_pos = p;
      is_resolved = false;
      klass_cache_ptr = nullptr;
      lib_klass_cache_ptr = nullptr;
    }
    
    Type(std::vector<Type*>& p, Type* r) {
      type = FUNC_TYPE;
      dimension = 0;
      func_params = p;
      func_rtrn = r;
      func_param_count = -1;
      line_num = line_pos = -1;

      is_resolved = false;
      klass_cache_ptr = nullptr;
      lib_klass_cache_ptr = nullptr;
    }
    
    ~Type() {
    }

  public:
    static Type* CharStringType();

    void SetType(EntryType t) {
      type = t;

      is_resolved = false;
      klass_cache_ptr = nullptr;
      lib_klass_cache_ptr = nullptr;
    }

    const EntryType GetType() {
      return type;
    }

    void SetGenerics(const std::vector<Type*>& g) {
      generic_types = g;

      is_resolved = false;
      klass_cache_ptr = nullptr;
      lib_klass_cache_ptr = nullptr;
    }

    std::vector<Type*> GetGenerics() {
      return generic_types;
    }

    bool HasGeneric(Type* type) {
       bool found = false;

       for(auto& generic_type : generic_types) {
          found = generic_type->GetName() == type->GetName();
       }

       return found;
    }

    bool HasGenerics() {
      return generic_types.size() > 0;
    }

    void SetDimension(int d) {
      dimension = d;

      is_resolved = false;
      klass_cache_ptr = nullptr;
      lib_klass_cache_ptr = nullptr;
    }
    
    void SetResolved(bool r) {
      is_resolved = r;
    }
    
    bool IsResolved() {
      return is_resolved;
    }

    void SetClassPtr(void* k) {
      klass_cache_ptr = k;
    }

    void* GetClassPtr() {
      return klass_cache_ptr;
    }

    void SetLibraryClassPtr(void* l) {
      lib_klass_cache_ptr = l;
    }

    void* GetLibraryClassPtr() {
      return lib_klass_cache_ptr;
    }

    std::vector<Type*> GetFunctionParameters() {
      return func_params;
    }

    void SetFunctionParameters(const std::vector<Type*> &p) {
      func_params = p;
    }

    int GetFunctionParameterCount() {
      if(func_param_count < 0) {
        return (int)func_params.size();
      }

      return func_param_count;
    }
    
    void SetFunctionParameterCount(int c) {
      func_param_count = c;

      is_resolved = false;
      klass_cache_ptr = nullptr;
      lib_klass_cache_ptr = nullptr;
    }
    
    Type* GetFunctionReturn() {
      return func_rtrn;
    }

    void SetFunctionReturn(Type* r) {
      func_rtrn = r;

      is_resolved = false;
      klass_cache_ptr = nullptr;
      lib_klass_cache_ptr = nullptr;
    }
    
    const int GetDimension() {
      return dimension;
    }

    void SetName(const std::wstring &n) {
      class_name = n;

      is_resolved = false;
      klass_cache_ptr = nullptr;
      lib_klass_cache_ptr = nullptr;
    }

    const std::wstring GetName() {
      return class_name;
    }

    inline void Set(Type* t) {
      if(t) {
        type = t->type;
        dimension = t->dimension;
        class_name = t->class_name;
        func_rtrn = t->func_rtrn;
        func_params = t->func_params;
        func_param_count = -1;
        line_num = t->line_num;
        generic_types = t->generic_types;
        is_resolved = t->is_resolved;
        klass_cache_ptr = t->klass_cache_ptr;
        lib_klass_cache_ptr = t->lib_klass_cache_ptr;
      }
    }

    const std::wstring GetFileName() {
      return file_name;
    }

    const int GetLineNumber() {
      return line_num;
    }

    const int GetLinePosition() {
      return line_pos;
    }
  };

  /******************************
   * TypeFactory class
   ****************************/
  class TypeFactory {
    static TypeFactory* instance;
    std::vector<Type*> types;

    TypeFactory() {
    }

    ~TypeFactory() {
    }

  public:
    static TypeFactory* Instance();

    void Clear() {
      while(!types.empty()) {
        Type* tmp = types.back();
        types.pop_back();
        // delete
        delete tmp;
        tmp = nullptr;
      }

      delete instance;
      instance = nullptr;
    }

    Type* MakeType(EntryType type, const std::wstring file_name, const int line_num, const int line_pos) {
      Type* tmp = new Type(type, file_name, line_num, line_pos);
      types.push_back(tmp);
      return tmp;
    }

    Type* MakeType(EntryType type, const std::wstring &name) {
      Type* tmp = new Type(type, name, L"", -1, -1);
      types.push_back(tmp);
      return tmp;
    }

    Type* MakeType(EntryType type) {
      Type* tmp = new Type(type, L"", -1, -1);
      types.push_back(tmp);
      return tmp;
    }

    Type* MakeType(EntryType type, const std::wstring& name, const std::wstring& file_name, const int line_num, const int line_pos) {
      Type* tmp = new Type(type, name, file_name, line_num, line_pos);
      types.push_back(tmp);
      return tmp;
    }
    
    Type* MakeType(std::vector<Type*>& func_params, Type* rtrn_type) {
      Type* tmp = new Type(func_params, rtrn_type);
      types.push_back(tmp);
      return tmp;
    }
    
    Type* MakeType(Type* type) {
      Type* tmp = new Type(type);
      types.push_back(tmp);
      return tmp;
    }

    std::vector<Type*>& GetTypes() {
      return types;
    }
  };

  /********************************
   * Routines for parsing library
   * encode strings
   ********************************/
  class TypeParser {
    static void ParseFuncStr(const std::wstring& param_str, size_t& index);
    static void SetFuncType(frontend::Type* func_type);

  public:
    static std::vector<frontend::Type*> ParseParameters(const std::wstring& param_str);
    static frontend::Type* ParseType(const std::wstring& type_name);
    static std::vector<frontend::Type*> ParseGenerics(size_t& index, const std::wstring& param_str);
  };
  
  // static array holders
  struct IntStringHolder {
    INT64_VALUE* value;
    int length;
  };
  
  struct FloatStringHolder {
    FLOAT_VALUE* value;
    int length;
  };

  struct ByteStringHolder {
    char* value;
    int length;
  };

  struct BoolStringHolder {
    bool* value;
    int length;
  };

  bool EndsWith(std::wstring const& str, std::wstring const& ending);
  void RemoveSubString(std::wstring& str_in, const std::wstring& find);
}

namespace backend {
  /****************************
   * IntermediateDeclaration
   * class
   ****************************/
  class IntermediateDeclaration {
    instructions::ParamType type;
    std::wstring name;

  public:
    IntermediateDeclaration(const std::wstring &n, instructions::ParamType t) {
      type = t;
      name = n;
    }

    instructions::ParamType GetType() {
      return type;
    }

    const std::wstring GetName() {
      return name;
    }

    IntermediateDeclaration* Copy() {
      return new IntermediateDeclaration(name, type);
    }
  };

  /****************************
   * IntermediateDeclarations
   * class
   ****************************/
  class IntermediateDeclarations {
    std::vector<IntermediateDeclaration*> declarations;

    void WriteInt(int value, OutputStream &out_stream) {
      out_stream.WriteInt(value);
    }

    void WriteString(const std::wstring &in, OutputStream &out_stream) {
      out_stream.WriteString(in);
    }

  public:
    IntermediateDeclarations() {
    }
  
    ~IntermediateDeclarations() {
      while(!declarations.empty()) {
        IntermediateDeclaration* tmp = declarations.back();
        declarations.pop_back();
        // delete
        delete tmp;
        tmp = nullptr;
      }
    }

    void AddParameter(IntermediateDeclaration* parameter) {
      declarations.push_back(parameter);
    }

    std::vector<IntermediateDeclaration*> GetParameters() {
      return declarations;
    }
    
    void Debug(bool has_and_or) {
      if(declarations.size() > 0) {
        size_t index = has_and_or ? 1 : 0;
        GetLogger() << L"memory types:" << std::endl;
        if(has_and_or) {
          GetLogger() << L"  0: INT_PARM" << std::endl;
        }

        for(size_t i = 0; i < declarations.size(); ++i, ++index) {   
          IntermediateDeclaration* entry = declarations[i];   
    
          switch(entry->GetType()) {  
          case instructions::CHAR_PARM:   
            GetLogger() << L"  " << index << L": CHAR_PARM" << std::endl;   
            break;
      
          case instructions::INT_PARM:   
            GetLogger() << L"  " << index << L": INT_PARM" << std::endl;   
            break;   
    
          case instructions::FLOAT_PARM:   
            GetLogger() << L"  " << index << L": FLOAT_PARM" << std::endl;   
            break;   
    
          case instructions::BYTE_ARY_PARM:   
            GetLogger() << L"  " << index << L": BYTE_ARY_PARM" << std::endl;   
            break;   
    
          case instructions::CHAR_ARY_PARM:   
            GetLogger() << L"  " << index << L": CHAR_ARY_PARM" << std::endl;   
            break;

          case instructions::INT_ARY_PARM:   
            GetLogger() << L"  " << index << L": INT_ARY_PARM" << std::endl;   
            break;   
    
          case instructions::FLOAT_ARY_PARM:   
            GetLogger() << L"  " << index << L": FLOAT_ARY_PARM" << std::endl;   
            break;   
    
          case instructions::OBJ_PARM:   
            GetLogger() << L"  " << index << L": OBJ_PARM" << std::endl;   
            break;
       
          case instructions::OBJ_ARY_PARM:   
            GetLogger() << L"  " << index << L": OBJ_ARY_PARM" << std::endl;   
            break;
      
          case instructions::FUNC_PARM:   
            GetLogger() << L"  " << index << L": FUNC_PARM" << std::endl;   
            break;
    
          default:   
            break;   
          }   
        }   
      }  
      else {
        GetLogger() << L"memory types: none" << std::endl;
      }
    }

    void Write(bool is_debug, OutputStream &out_stream) {
      WriteInt((int)declarations.size(), out_stream);
      for(size_t i = 0; i < declarations.size(); ++i) {
        IntermediateDeclaration* entry = declarations[i];
        WriteInt(entry->GetType(), out_stream);
        if(is_debug) {
          WriteString(entry->GetName(), out_stream);
        }
      }
    }
  };
}
