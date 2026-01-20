/***************************************************************************
 * Links pre-compiled code into existing program.
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
 * - Neither the name of the VM Team nor the names of its
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

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <algorithm>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include "types.h"
#include "../shared/instrs.h"
#include "../shared/sys.h"

#ifdef _DEBUG
#include "../shared/logger.h"
#endif

class Library;
class LibraryClass;
class LibraryEnum;

/********************************
 * LibraryInstr class.
 ********************************/
class LibraryInstr {
  instructions::InstructionType type;
  int operand;
  int operand2;
  int operand3;
  double operand4;
  std::wstring operand5;
  std::wstring operand6;
  INT64_VALUE operand7;
  int line_num;

 public:
  LibraryInstr(int l, instructions::InstructionType t) {
    line_num = l;
    type = t;
    operand = 0;
  }

  LibraryInstr(int l, instructions::InstructionType t, int o) {
    line_num = l;
    type = t;
    operand = o;
  }

  LibraryInstr(int l, instructions::InstructionType t, INT64_VALUE o7) {
    line_num = l;
    type = t;
    operand7 = o7;
  }

  LibraryInstr(int l, instructions::InstructionType t, double fo) {
    line_num = l;
    type = t;
    operand4 = fo;
    operand = 0;
  }

  LibraryInstr(int l, instructions::InstructionType t, int o, int o2) {
    line_num = l;
    type = t;
    operand = o;
    operand2 = o2;
  }

  LibraryInstr(int l, instructions::InstructionType t, int o, int o2, int o3) {
    line_num = l;
    type = t;
    operand = o;
    operand2 = o2;
    operand3 = o3;
  }

  LibraryInstr(int l, instructions::InstructionType t, std::wstring o5) {
    line_num = l;
    type = t;
    operand5 = o5;
  }

  LibraryInstr(int l, instructions::InstructionType t, int o3, std::wstring o5, std::wstring o6) {
    line_num = l;
    type = t;
    operand3 = o3;
    operand5 = o5;
    operand6 = o6;
  }

  ~LibraryInstr() {
  }

  instructions::InstructionType GetType() {
    return type;
  }

  int GetLineNumber() {
    return line_num;
  }
  
  void SetType(instructions::InstructionType t) {
    type = t;
  }

  int GetOperand() {
    return operand;
  }

  int GetOperand2() {
    return operand2;
  }

  int GetOperand3() {
    return operand3;
  }

  void SetOperand(int o) {
    operand = o;
  }

  void SetOperand2(int o2) {
    operand2 = o2;
  }

  void SetOperand3(int o3) {
    operand3 = o3;
  }

  void SetOperand7(INT64_VALUE o7) {
    operand7 = o7;
  }

  INT64_VALUE GetOperand7() {
    return operand7;
  }

  double GetOperand4() {
    return operand4;
  }

  const std::wstring &GetOperand5() const {
    return operand5;
  }

  const std::wstring &GetOperand6() const {
    return operand6;
  }
};

/******************************
 * LibraryMethod class
 ****************************/
class LibraryMethod {
  int id;
  std::wstring name;
  std::wstring user_name;
  std::wstring rtrn_name;
  frontend::Type* rtrn_type;
  std::vector<LibraryInstr*> instrs;
  LibraryClass* lib_cls;
  frontend::MethodType type;
  bool is_native;
  bool is_lambda;
  bool is_static;
  bool is_virtual;
  bool has_and_or;
  int num_params;
  int mem_size;
  std::vector<frontend::Type*> declarations;
  backend::IntermediateDeclarations* entries;
  
  void ParseDeclarations();
  
  void ParseReturn() {
    rtrn_type = frontend::TypeParser::ParseType(rtrn_name);
  }

  std::wstring ReplaceSubstring(std::wstring s, const std::wstring &f, const std::wstring &r) {
    const size_t index = s.find(f);
    if(index != std::string::npos) {
      s.replace(index, f.size(), r);
    }

    return s;
  }

  std::wstring EncodeUserType(frontend::Type* type);

  void EncodeUserName();
  
 public:
  LibraryMethod(int i, const std::wstring &n, const std::wstring &r, frontend::MethodType t, bool v,  bool h,
                bool nt, bool s, bool l, int p, int m, LibraryClass* c, backend::IntermediateDeclarations* e) {
    id = i;
    name = n;
    rtrn_name = r;
    type = t;
    is_virtual = v;
    has_and_or = h;
    is_native = nt;
    is_static = s;
    is_lambda = l;
    num_params = p;
    mem_size = m;
    lib_cls = c;
    entries = e;
    rtrn_type = nullptr;

    ParseDeclarations();
    ParseReturn();
  }

  ~LibraryMethod() {
    // clean up
    while(!instrs.empty()) {
      LibraryInstr* tmp = instrs.front();
      instrs.erase(instrs.begin());
      // delete
      delete tmp;
      tmp = nullptr;
    }

    if(entries) {
      delete entries;
      entries = nullptr;
    }
  }

  int GetId() {
    return id;
  }

  bool IsVirtual() {
    return is_virtual;
  }

  bool HasAndOr() {
    return has_and_or;
  }

  bool IsLambda() {
    return is_lambda;
  }

  const std::wstring &GetName() const {
    return name;
  }

  const std::wstring &GetUserName() {
    if(user_name.empty()) {
      EncodeUserName();
    }

    return user_name;
  }

  const std::wstring &GetEncodedReturn() const {
    return rtrn_name;
  }

  frontend::MethodType GetMethodType() {
    return type;
  }
  
  std::vector<frontend::Type*> GetDeclarationTypes() {
    return declarations;
  }
  
  int GetSpace() {
    return mem_size;
  }

  int GetNumParams() {
    return num_params;
  }

  backend::IntermediateDeclarations* GetEntries() {
    return entries;
  }
  
  bool IsNative() {
    return is_native;
  }

  bool IsStatic() {
    return is_static;
  }

  LibraryClass* GetLibraryClass() {
    return lib_cls;
  }

  void AddInstructions(std::vector<LibraryInstr*> is) {
    instrs = is;
  }

  frontend::Type* GetReturn() {
    return rtrn_type;
  }

  std::vector<LibraryInstr*> GetInstructions() {
    return instrs;
  }
};

/****************************
 * LibraryEnumItem class
 ****************************/
class LibraryEnumItem {
  std::wstring name;
  INT64_VALUE id;
  LibraryEnum* lib_eenum;

 public:
  LibraryEnumItem(const std::wstring &n, INT64_VALUE i, LibraryEnum* e) {
    name = n;
    id = i;
    lib_eenum = e;
  }

  ~LibraryEnumItem() {
  }

  const std::wstring &GetName() const {
    return name;
  }

  INT64_VALUE GetId() {
    return id;
  }

  LibraryEnum* GetEnum() {
    return lib_eenum;
  }
};

/****************************
 * LibraryEnum class
 ****************************/
class LibraryEnum {
  std::wstring name;
  INT64_VALUE offset;
  std::map<const std::wstring, LibraryEnumItem*> items;

 public:
  LibraryEnum(const std::wstring &n, const INT64_VALUE o) {
    name = n;
    offset = o;
  }

  ~LibraryEnum() {
    // clean up
    for(auto& pair : items) {
      delete pair.second;
    }
    items.clear();
  }

  const std::wstring &GetName() const {
    return name;
  }

  INT64_VALUE GetOffset() {
    return offset;
  }

  void AddItem(LibraryEnumItem* i) {
    items.insert(std::pair<std::wstring, LibraryEnumItem*>(i->GetName(), i));
  }

  LibraryEnumItem* GetItem(const std::wstring &n) {
    std::map<const std::wstring, LibraryEnumItem*>::iterator result = items.find(n);
    if(result != items.end()) {
      return result->second;
    }

    return nullptr;
  }

  std::map<const std::wstring, LibraryEnumItem*> GetItems() {
    return items;
  }
};

/****************************
 * LibraryClass class
 ****************************/
class LibraryClass {
  int id;
  std::wstring name;
  std::wstring parent_name;
  std::vector<std::wstring>interface_names;
  std::vector<std::wstring>generic_name_types;
  std::vector<int>interface_ids;
  int cls_space;
  int inst_space;
  std::map<const std::wstring, LibraryMethod*> methods;
  std::multimap<const std::wstring, LibraryMethod*> unqualified_methods;
  backend::IntermediateDeclarations* cls_entries;
  backend::IntermediateDeclarations* inst_entries;
  std::map<std::wstring, backend::IntermediateDeclarations*> lib_closure_entries;
  bool is_interface;
  bool is_public;
  bool is_virtual;
  bool is_generic;
  Library* library;
  std::vector<LibraryClass*> lib_children;
  std::vector<frontend::ParseNode*> children;
  bool was_called;
  bool is_debug;
  std::wstring file_name;
  std::vector<LibraryClass*> generic_classes;
  frontend::Type* generic_interface;

  std::map<backend::IntermediateDeclarations*, std::pair<std::wstring, int>> CopyClosureEntries();
  
 public:
   LibraryClass(const std::wstring &n, const std::wstring &g) {
     name = n;
     if(g.empty()) {
       generic_interface = nullptr;
     }
     else {
       generic_interface = frontend::TypeFactory::Instance()->MakeType(frontend::CLASS_TYPE, g);
     }
     is_generic = true;
     
     library = nullptr;
     cls_entries= inst_entries = nullptr;
   }
   
   LibraryClass(const std::wstring& n, const std::wstring& p, const std::vector<std::wstring> i, bool is, bool ip, const std::vector<std::wstring> g, 
                bool v, const int cs, const int in, backend::IntermediateDeclarations* ce, backend::IntermediateDeclarations* ie, 
                std::map<std::wstring, backend::IntermediateDeclarations*> le, Library* l, const std::wstring &fn, bool d);
   
  ~LibraryClass() {
    // clean up
    for(auto& pair : methods) {
      delete pair.second;
    }
    methods.clear();

    lib_children.clear();

    while(!generic_classes.empty()) {
      LibraryClass* tmp = generic_classes.back();
      generic_classes.pop_back();
      // delete
      delete tmp;
      tmp = nullptr;
    }

    // clean up
    if(cls_entries) {
      delete cls_entries;
      cls_entries = nullptr;
    }

    if(inst_entries) {
      delete inst_entries;
      inst_entries = nullptr;
    }
  }
  
  void SetId(int i) {
    id = i;
  }

  void SetCalled(bool c) {
    was_called = c;
  }

  bool GetCalled() {
    return was_called;
  }

  bool IsDebug() {
    return is_debug;
  }

  bool IsGeneric() {
    return is_generic;
  }

  const std::wstring &GetFileName() const {
    return file_name;
  }
  
  int GetId() {
    return id;
  }

  const std::wstring &GetName() const {
    return name;
  }

  std::wstring GetBundleName() {
    const size_t index = name.find_last_of(L'.');
    if(index != std::string::npos) {
      return name.substr(0, index);
    }

    return L"Default";
  }
  
  std::vector<std::wstring> GetInterfaceNames() {
    return interface_names;
  }

  std::vector<int> GetInterfaceIds() {
    return interface_ids;
  }
  
  void AddInterfaceId(int id) {
    interface_ids.push_back(id);
  }

  bool IsInterface() {
    return is_interface;
  }

  bool IsPublic() {
    return is_public;
  }

  bool HasGenerics() {
    return !generic_classes.empty();
  }

  int GenericIndex(const std::wstring& n) {
    for(size_t i = 0; i < generic_classes.size(); ++i) {
      if(n == generic_classes[i]->GetName()) {
        return static_cast<int>(i);
      }
    }

    return -1;
  }

  const std::vector<LibraryClass*> GetGenericClasses() {
    return generic_classes;
  }

  LibraryClass* GetGenericClass(const std::wstring& n) {
    const int index = GenericIndex(n);
    if(index > -1) {
      return generic_classes[index];
    }

    return nullptr;
  }

  frontend::Type* GetGenericInterface() {
    return generic_interface;
  }

  bool HasGenericInterface() {
    return generic_interface != nullptr;
  }

  const std::wstring &GetParentName() const {
    return parent_name;
  }

  std::vector<frontend::ParseNode*> GetChildren() {
    return children;
  }

  void AddChild(frontend::ParseNode* c) {
    children.push_back(c);
  }

  std::vector<LibraryClass*> GetLibraryChildren();

  bool IsVirtual() {
    return is_virtual;
  }

  int GetClassSpace() {
    return cls_space;
  }

  int GetInstanceSpace() {
    return inst_space;
  }

  backend::IntermediateDeclarations* GetClassEntries() {
    return cls_entries;
  }
  
  backend::IntermediateDeclarations* GetInstanceEntries() {
    return inst_entries;
  }

  std::map<backend::IntermediateDeclarations*, std::pair<std::wstring, int> > GetLambaEntries() {
    if(lib_closure_entries.empty()) {
      return {};
    }

    return CopyClosureEntries();
  }
  
  LibraryMethod* GetMethod(const std::wstring &name) {
    std::map<const std::wstring, LibraryMethod*>::iterator result = methods.find(name);
    if(result != methods.end()) {
      return result->second;
    }

    return nullptr;
  }

  std::vector<LibraryMethod*> GetUnqualifiedMethods(const std::wstring &n);

  std::map<const std::wstring, LibraryMethod*> GetMethods() {
    return methods;
  }

  void AddMethod(LibraryMethod* method);
};

/****************************
 * LibraryAlias class
 ****************************/
class LibraryAlias {
  std::wstring name;
  std::map<std::wstring, frontend::Type*> aliases;

public:
  LibraryAlias(const std::wstring &n, std::map<std::wstring, frontend::Type*> &a) {
    name = n;
    aliases = a;
  }

  ~LibraryAlias() {
  }

  const std::wstring GetName() const {
    return name;
  }

  frontend::Type* GetType(const std::wstring& n) {
    std::map<std::wstring, frontend::Type*>::iterator result = aliases.find(n);
    if(result != aliases.end()) {
      return result->second;
    }

    return nullptr;
  }
};

/******************************
 * String classes
 ****************************/
struct CharStringInstruction {
  std::wstring value;
  std::vector<LibraryInstr*> instrs;
};

struct ByteStringInstruction {
  frontend::ByteStringHolder* value;
  std::vector<LibraryInstr*> instrs;
};

struct BoolStringInstruction {
  frontend::BoolStringHolder* value;
  std::vector<LibraryInstr*> instrs;
};

struct IntStringInstruction {
  frontend::IntStringHolder* value;
  std::vector<LibraryInstr*> instrs;
};

struct FloatStringInstruction {
  frontend::FloatStringHolder* value;
  std::vector<LibraryInstr*> instrs;
};

class Library {
  std::wstring lib_path;
  char* buffer;
  char* alloc_buffer;
  size_t buffer_size;
  long buffer_pos;
  std::map<const std::wstring, LibraryAlias*> aliases;
  std::vector<LibraryAlias*> aliases_list;
  std::map<const std::wstring, LibraryEnum*> enums;
  std::vector<LibraryEnum*> enum_list;
  std::map<const std::wstring, LibraryClass*> named_classes;
  std::vector<LibraryClass*> class_list;
  std::map<const std::wstring, const std::wstring> hierarchies;
  std::vector<CharStringInstruction*> char_strings;
  std::vector<BoolStringInstruction*> bool_strings;
  std::vector<ByteStringInstruction*> byte_strings;
  std::vector<IntStringInstruction*> int_strings;
  std::vector<FloatStringInstruction*> float_strings;
  std::vector<std::wstring> bundle_names;
  
  inline int32_t ReadInt() {
    int32_t value = *reinterpret_cast<int32_t*>(buffer);
    buffer += sizeof(value);
    return value;
  }

  inline int64_t ReadInt64() {
    int64_t value = *reinterpret_cast<int64_t*>(buffer);
    buffer += sizeof(value);
    return value;
  }

  inline void ReadDummyInt() {
    buffer += sizeof(int32_t);
  }

  inline uint32_t ReadUnsigned() {
    uint32_t value = *reinterpret_cast<uint32_t*>(buffer);
    buffer += sizeof(value);
    return value;
  }

  inline int ReadByte() {
    uint8_t value = *reinterpret_cast<uint8_t*>(buffer);
    buffer += sizeof(value);
    return value;
  }

  inline wchar_t ReadChar() {
    wchar_t out;

    int size = ReadInt();
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

  inline void ReadDummyString() {
    buffer += ReadInt();
  }

  double ReadDouble() {
    FLOAT_VALUE value = *reinterpret_cast<FLOAT_VALUE*>(buffer);
    buffer += sizeof(value);
    return value;
  }

  // loads a file into memory
  char* LoadFileBuffer(std::wstring filename, size_t& buffer_size);

  void ReadFile(const std::wstring &file) {
    buffer_pos = 0;
    alloc_buffer = buffer = LoadFileBuffer(file, buffer_size);
    if(!alloc_buffer || !buffer) {
      exit(1);
    }
  }

  void AddEnum(LibraryEnum* e) {
    enums.insert(std::pair<std::wstring, LibraryEnum*>(e->GetName(), e));
    enum_list.push_back(e);
  }

  void AddAlias(LibraryAlias* a) {
    aliases.insert(std::pair<std::wstring, LibraryAlias*>(a->GetName(), a));
    aliases_list.push_back(a);
  }
  
  void AddClass(LibraryClass* cls) {
    if(cls->GetName() == L"System.wstring") {
      cls->SetCalled(true);
    }    
    named_classes.insert(std::pair<std::wstring, LibraryClass*>(cls->GetName(), cls));
    class_list.push_back(cls);
  }

  backend::IntermediateDeclarations* LoadEntries(bool is_debug) {
    backend::IntermediateDeclarations* entries = new backend::IntermediateDeclarations;
    int num_params = ReadInt();
    for(int i = 0; i < num_params; ++i) {
      instructions::ParamType type = static_cast<instructions::ParamType>(ReadInt());
      std::wstring var_name;
      if(is_debug) {
        var_name = ReadString();
      }
      entries->AddParameter(new backend::IntermediateDeclaration(var_name, type));
    }

    return entries;
  }

  
  // loading functions
  void LoadFile(const std::wstring &file_name);
  void LoadLambdas();
  void LoadEnums();
  void LoadClasses();
  void LoadMethods(LibraryClass* cls, bool is_debug);
  void LoadStatements(LibraryMethod* mthd, bool is_debug);

 public:
  Library(const std::wstring &p) {
    lib_path = p;
    alloc_buffer = nullptr;
  }

  ~Library() {
    // clean up
    for(auto& pair : aliases) {
      delete pair.second;
    }
    aliases.clear();
    aliases_list.clear();

    for(auto& pair : enums) {
      delete pair.second;
    }
    enums.clear();
    enum_list.clear();

    for(auto& pair : named_classes) {
      delete pair.second;
    }
    named_classes.clear();
    class_list.clear();

    while(!char_strings.empty()) {
      CharStringInstruction* tmp = char_strings.front();
      char_strings.erase(char_strings.begin());
      // delete
      delete tmp;
      tmp = nullptr;
    }

    while(!int_strings.empty()) {
      IntStringInstruction* tmp = int_strings.front();
      int_strings.erase(int_strings.begin());
      // delete
      delete tmp;
      tmp = nullptr;
    }

    while(!bool_strings.empty()) {
      BoolStringInstruction* tmp = bool_strings.front();
      bool_strings.erase(bool_strings.begin());
      // delete
      delete tmp;
      tmp = nullptr;
    }

    while(!byte_strings.empty()) {
      ByteStringInstruction* tmp = byte_strings.front();
      byte_strings.erase(byte_strings.begin());
      // delete
      delete tmp;
      tmp = nullptr;
    }

    while(!float_strings.empty()) {
      FloatStringInstruction* tmp = float_strings.front();
      float_strings.erase(float_strings.begin());
      // delete
      delete tmp;
      tmp = nullptr;
    }

    if(alloc_buffer) {
      delete[] alloc_buffer;
      alloc_buffer = nullptr;
    }
  }
  
  bool HasBundleName(const std::wstring &name) {
    std::vector<std::wstring>::iterator found = find(bundle_names.begin(), bundle_names.end(), name);
    return found != bundle_names.end();
  }

  LibraryClass* GetClass(const std::wstring &name) {
    std::map<const std::wstring, LibraryClass*>::iterator result = named_classes.find(name);
    if(result != named_classes.end()) {
      return result->second;
    }

    return nullptr;
  }

  LibraryEnum* GetEnum(const std::wstring &name) {
    std::map<const std::wstring, LibraryEnum*>::iterator result = enums.find(name);
    if(result != enums.end()) {
      return result->second;
    }

    return nullptr;
  }

  LibraryAlias* GetAlias(const std::wstring &n) {
    std::map<const std::wstring, LibraryAlias*>::iterator result = aliases.find(n);
    if(result != aliases.end()) {
      return result->second;
    }

    return nullptr;
  }

  std::vector<LibraryEnum*> GetEnums() {
    return enum_list;
  }

  std::vector<LibraryAlias*> GetAliases() {
    return aliases_list;
  }

  std::vector<LibraryClass*> GetClasses() {
    return class_list;
  }

  std::map<const std::wstring, const std::wstring> GetHierarchies() {
    return hierarchies;
  }

  std::vector<CharStringInstruction*> GetCharStringInstructions() {
    return char_strings;
  }

  std::vector<BoolStringInstruction*> GetBoolStringInstructions() {
    return bool_strings;
  }

  std::vector<ByteStringInstruction*> GetByteStringInstructions() {
    return byte_strings;
  }

  std::vector<IntStringInstruction*> GetIntStringInstructions() {
    return int_strings;
  }
  
  std::vector<FloatStringInstruction*> GetFloatStringInstructions() {
    return float_strings;
  }

  void Load();
};

/********************************
 * Manages shared libraries
 ********************************/
class Linker {
  std::map<const std::wstring, Library*> libraries;
  std::vector <LibraryAlias*> all_aliases;
  std::unordered_map<std::wstring, LibraryAlias*> all_aliases_map;
  std::vector<LibraryClass*> all_classes;
  std::unordered_map<std::wstring, LibraryClass*> all_classes_map;
  std::vector<LibraryEnum*> all_enums;
  std::unordered_map<std::wstring, LibraryEnum*> all_enums_map;
  std::wstring master_path;
  std::vector<std::wstring> paths;

 public:
#ifdef _DEBUG
  static void Debug(const std::wstring &msg, const int line_num, int depth) {
    GetLogger() << std::setw(4) << line_num << L": ";
    for(int i = 0; i < depth; ++i) {
      GetLogger() << L"  ";
    }
    GetLogger() << msg << std::endl;
  }
#endif

  static std::wstring ToString(int v) {
    std::wostringstream str;
    str << v;
    return str.str();
  }
  
  // parse ini file
  std::map<std::wstring, std::vector<std::pair<std::wstring, std::wstring>>> ParseIni(const std::wstring& filename);
  const std::wstring TrimNameValue(const std::wstring& name_value);

public:
  Linker(const std::wstring &p) {
    master_path = p;
  }

  ~Linker() {
    // clean up
    for(auto& pair : libraries) {
      delete pair.second;
    }
    libraries.clear();

    paths.clear();
  }

  void ResloveExternalClass(LibraryClass* klass);
  void ResloveExternalClasses();
  void ResolveExternalMethodCalls();

  std::vector<std::wstring> GetLibraryPaths() {
    return paths;
  }

  Library* GetLibrary(const std::wstring &name) {
    std::map<const std::wstring, Library*>::iterator result = libraries.find(name);
    if(result != libraries.end()) {
      return result->second;
    }

    return nullptr;
  }

  // get all libraries
  std::map<const std::wstring, Library*> GetAllLibraries() {
    return libraries;
  }

  // get all used libraries
  std::vector<Library*> GetAllUsedLibraries();

  // returns all aliases including duplicates
  std::unordered_map<std::wstring, LibraryAlias*> GetAllAliasesMap();

  // returns all aliases including duplicates
  std::vector<LibraryAlias*> GetAllAliases();

  // returns all classes including duplicates
  std::unordered_map<std::wstring, LibraryClass*> GetAllClassesMap();

  // returns all classes including duplicates
  std::vector<LibraryClass*> GetAllClasses();

  // returns all enums including duplicates
  std::unordered_map<std::wstring, LibraryEnum*> GetAllEnumsMap();

  // returns all enums including duplicates
  std::vector<LibraryEnum*> GetAllEnums();

  LibraryClass* SearchClassLibraries(const std::wstring &name) {
    std::unordered_map<std::wstring, LibraryClass*> klass_map = GetAllClassesMap();
    return klass_map[name];
  }

  // check to see if bundle name exists
  bool HasBundleName(const std::wstring &name);

  // finds the first alias match; note multiple matches may exist
  LibraryAlias* SearchAliasLibraries(const std::wstring& name, std::vector<std::wstring> uses);

  // finds the first class match; note multiple matches may exist
  LibraryClass* SearchClassLibraries(const std::wstring& name, std::vector<std::wstring> uses);

  // finds the first enum match; note multiple matches may exist
  LibraryEnum* SearchEnumLibraries(const std::wstring& name, std::vector<std::wstring> uses);

  void Load(bool is_lib);
};
