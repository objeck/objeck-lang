/***************************************************************************
 * Links pre-compiled code into existing program.
 *
 * Copyright (c) 2008-2021, Randy Hollines
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

#ifndef __LINKER_H__
#define __LINKER_H__

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

using namespace std;

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
  wstring operand5;
  wstring operand6;
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

  LibraryInstr(int l, instructions::InstructionType t, wstring o5) {
    line_num = l;
    type = t;
    operand5 = o5;
  }

  LibraryInstr(int l, instructions::InstructionType t, int o3, wstring o5, wstring o6) {
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

  double GetOperand4() {
    return operand4;
  }

  const wstring &GetOperand5() const {
    return operand5;
  }

  const wstring &GetOperand6() const {
    return operand6;
  }
};

/******************************
 * LibraryMethod class
 ****************************/
class LibraryMethod {
  int id;
  wstring name;
  wstring user_name;
  wstring rtrn_name;
  frontend::Type* rtrn_type;
  vector<LibraryInstr*> instrs;
  LibraryClass* lib_cls;
  frontend::MethodType type;
  bool is_native;
  bool is_lambda;
  bool is_static;
  bool is_virtual;
  bool has_and_or;
  int num_params;
  int mem_size;
  vector<frontend::Type*> declarations;
  backend::IntermediateDeclarations* entries;
  
  void ParseDeclarations();
  
  void ParseReturn() {
    rtrn_type = frontend::TypeParser::ParseType(rtrn_name);
  }

  wstring ReplaceSubstring(wstring s, const wstring &f, const wstring &r) {
    const size_t index = s.find(f);
    if(index != string::npos) {
      s.replace(index, f.size(), r);
    }

    return s;
  }

  wstring EncodeUserType(frontend::Type* type);

  void EncodeUserName();
  
 public:
  LibraryMethod(int i, const wstring &n, const wstring &r, frontend::MethodType t, bool v,  bool h,
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

  const wstring &GetName() const {
    return name;
  }

  const wstring &GetUserName() {
    if(user_name.size() == 0) {
      EncodeUserName();
    }

    return user_name;
  }

  const wstring &GetEncodedReturn() const {
    return rtrn_name;
  }

  frontend::MethodType GetMethodType() {
    return type;
  }
  
  vector<frontend::Type*> GetDeclarationTypes() {
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

  void AddInstructions(vector<LibraryInstr*> is) {
    instrs = is;
  }

  frontend::Type* GetReturn() {
    return rtrn_type;
  }

  vector<LibraryInstr*> GetInstructions() {
    return instrs;
  }
};

/****************************
 * LibraryEnumItem class
 ****************************/
class LibraryEnumItem {
  wstring name;
  int id;
  LibraryEnum* lib_eenum;

 public:
  LibraryEnumItem(const wstring &n, int i, LibraryEnum* e) {
    name = n;
    id = i;
    lib_eenum = e;
  }

  ~LibraryEnumItem() {
  }

  const wstring &GetName() const {
    return name;
  }

  int GetId() {
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
  wstring name;
  int offset;
  map<const wstring, LibraryEnumItem*> items;

 public:
  LibraryEnum(const wstring &n, const int o) {
    name = n;
    offset = o;
  }

  ~LibraryEnum() {
    // clean up
    map<const wstring, LibraryEnumItem*>::iterator iter;
    for(iter = items.begin(); iter != items.end(); ++iter) {
      LibraryEnumItem* tmp = iter->second;
      delete tmp;
      tmp = nullptr;
    }
    items.clear();
  }

  const wstring &GetName() const {
    return name;
  }

  int GetOffset() {
    return offset;
  }

  void AddItem(LibraryEnumItem* i) {
    items.insert(pair<wstring, LibraryEnumItem*>(i->GetName(), i));
  }

  LibraryEnumItem* GetItem(const wstring &n) {
    map<const wstring, LibraryEnumItem*>::iterator result = items.find(n);
    if(result != items.end()) {
      return result->second;
    }

    return nullptr;
  }

  map<const wstring, LibraryEnumItem*> GetItems() {
    return items;
  }
};

/****************************
 * LibraryClass class
 ****************************/
class LibraryClass {
  int id;
  wstring name;
  wstring parent_name;
  vector<wstring>interface_names;
  vector<wstring>generic_name_types;
  vector<int>interface_ids;
  int cls_space;
  int inst_space;
  map<const wstring, LibraryMethod*> methods;
  multimap<const wstring, LibraryMethod*> unqualified_methods;
  backend::IntermediateDeclarations* cls_entries;
  backend::IntermediateDeclarations* inst_entries;
  map<wstring, backend::IntermediateDeclarations*> lib_closure_entries;
  bool is_interface;
  bool is_public;
  bool is_virtual;
  bool is_generic;
  Library* library;
  vector<LibraryClass*> lib_children;
  vector<frontend::ParseNode*> children;
  bool was_called;
  bool is_debug;
  wstring file_name;
  vector<LibraryClass*> generic_classes;
  frontend::Type* generic_interface;

  map<backend::IntermediateDeclarations*, pair<wstring, int>> CopyClosureEntries();
  
 public:
   LibraryClass(const wstring &n, const wstring &g) {
     name = n;
     if(g.empty()) {
       generic_interface = nullptr;
     }
     else {
       generic_interface = frontend::TypeFactory::Instance()->MakeType(frontend::CLASS_TYPE, g);
     }
     is_generic = true;
     library = nullptr;
   }
   
   LibraryClass(const wstring& n, const wstring& p, const vector<wstring> i, bool is, bool ip, const vector<wstring> g, 
                bool v, const int cs, const int in, backend::IntermediateDeclarations* ce, backend::IntermediateDeclarations* ie, 
                map<wstring, backend::IntermediateDeclarations*> le, Library* l, const wstring &fn, bool d);
   
  ~LibraryClass() {   
    // clean up
    map<const wstring, LibraryMethod*>::iterator iter;
    for(iter = methods.begin(); iter != methods.end(); ++iter) {
      LibraryMethod* tmp = iter->second;
      delete tmp;
      tmp = nullptr;
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

  const wstring &GetFileName() const {
    return file_name;
  }
  
  int GetId() {
    return id;
  }

  const wstring &GetName() const {
    return name;
  }

  wstring GetBundleName() {
    const size_t index = name.find_last_of(L'.');
    if(index != string::npos) {
      return name.substr(0, index);
    }

    return L"Default";
  }
  
  vector<wstring> GetInterfaceNames() {
    return interface_names;
  }

  vector<int> GetInterfaceIds() {
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

  int GenericIndex(const wstring& n) {
    for(size_t i = 0; i < generic_classes.size(); ++i) {
      if(n == generic_classes[i]->GetName()) {
        return (int)i;
      }
    }

    return -1;
  }

  const vector<LibraryClass*> GetGenericClasses() {
    return generic_classes;
  }

  LibraryClass* GetGenericClass(const wstring& n) {
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

  const wstring &GetParentName() const {
    return parent_name;
  }

  vector<frontend::ParseNode*> GetChildren() {
    return children;
  }

  void AddChild(frontend::ParseNode* c) {
    children.push_back(c);
  }

  vector<LibraryClass*> GetLibraryChildren();

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

  map<backend::IntermediateDeclarations*, pair<wstring, int> > GetLambaEntries() {
    if(lib_closure_entries.empty()) {
      return {};
    }

    return CopyClosureEntries();
  }
  
  LibraryMethod* GetMethod(const wstring &name) {
    map<const wstring, LibraryMethod*>::iterator result = methods.find(name);
    if(result != methods.end()) {
      return result->second;
    }

    return nullptr;
  }

  vector<LibraryMethod*> GetUnqualifiedMethods(const wstring &n);

  map<const wstring, LibraryMethod*> GetMethods() {
    return methods;
  }

  void AddMethod(LibraryMethod* method);
};

/****************************
 * LibraryAlias class
 ****************************/
class LibraryAlias {
  wstring name;
  map<wstring, frontend::Type*> aliases;

public:
  LibraryAlias(const wstring &n, map<wstring, frontend::Type*> &a) {
    name = n;
    aliases = a;
  }

  ~LibraryAlias() {
  }

  const wstring GetName() const {
    return name;
  }

  frontend::Type* GetType(const wstring& n) {
    map<wstring, frontend::Type*>::iterator result = aliases.find(n);
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
  wstring value;
  vector<LibraryInstr*> instrs;
};

struct IntStringInstruction {
  frontend::IntStringHolder* value;
  vector<LibraryInstr*> instrs;
};

struct FloatStringInstruction {
  frontend::FloatStringHolder* value;
  vector<LibraryInstr*> instrs;
};

class Library {
  wstring lib_path;
  char* buffer;
  char* alloc_buffer;
  size_t buffer_size;
  long buffer_pos;
  map<const wstring, LibraryAlias*> aliases;
  vector<LibraryAlias*> aliases_list;
  map<const wstring, LibraryEnum*> enums;
  vector<LibraryEnum*> enum_list;
  map<const wstring, LibraryClass*> named_classes;
  vector<LibraryClass*> class_list;
  map<const wstring, const wstring> hierarchies;
  vector<CharStringInstruction*> char_strings;
  vector<IntStringInstruction*> int_strings;
  vector<FloatStringInstruction*> float_strings;
  vector<wstring> bundle_names;
  
  inline int ReadInt() {
    int32_t value = *((int32_t*)buffer);
    buffer += sizeof(value);
    return value;
  }

  inline void ReadDummyInt() {
    buffer += sizeof(int32_t);
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

  inline wchar_t ReadChar() {
    wchar_t out;

    int size = ReadInt();
    if(size) {
      string in(buffer, size);
      buffer += size;
      if(!BytesToCharacter(in, out)) {
        wcerr << L">>> Unable to read character <<<" << endl;
        exit(1);
      }
    }
    else {
      out = L'\0';
    }

    return out;
  }
  
  inline wstring ReadString() {
    const int size = ReadInt(); 
    string in(buffer, size);
    buffer += size;    
   
    wstring out;
    if(!BytesToUnicode(in, out)) {
      wcerr << L">>> Unable to read unicode string <<<" << endl;
      exit(1);
    }

    return out;
  }

  inline void ReadDummyString() {
    buffer += ReadInt();
  }

  double ReadDouble() {
    FLOAT_VALUE value = *((FLOAT_VALUE*)buffer);
    buffer += sizeof(value);
    return value;
  }

  // loads a file into memory
  char* LoadFileBuffer(wstring filename, size_t& buffer_size);

  void ReadFile(const wstring &file) {
    buffer_pos = 0;
    alloc_buffer = buffer = LoadFileBuffer(file, buffer_size);
  }

  void AddEnum(LibraryEnum* e) {
    enums.insert(pair<wstring, LibraryEnum*>(e->GetName(), e));
    enum_list.push_back(e);
  }

  void AddAlias(LibraryAlias* a) {
    aliases.insert(pair<wstring, LibraryAlias*>(a->GetName(), a));
    aliases_list.push_back(a);
  }
  
  void AddClass(LibraryClass* cls) {
    if(cls->GetName() == L"System.wstring") {
      cls->SetCalled(true);
    }    
    named_classes.insert(pair<wstring, LibraryClass*>(cls->GetName(), cls));
    class_list.push_back(cls);
  }

  backend::IntermediateDeclarations* LoadEntries(bool is_debug) {
    backend::IntermediateDeclarations* entries = new backend::IntermediateDeclarations;
    int num_params = ReadInt();
    for(int i = 0; i < num_params; ++i) {
      instructions::ParamType type = (instructions::ParamType)ReadInt();
      wstring var_name;
      if(is_debug) {
        var_name = ReadString();
      }
      entries->AddParameter(new backend::IntermediateDeclaration(var_name, type));
    }

    return entries;
  }

  
  // loading functions
  void LoadFile(const wstring &file_name);
  void LoadLambdas();
  void LoadEnums();
  void LoadClasses();
  void LoadMethods(LibraryClass* cls, bool is_debug);
  void LoadStatements(LibraryMethod* mthd, bool is_debug);

 public:
  Library(const wstring &p) {
    lib_path = p;
    alloc_buffer = nullptr;
  }

  ~Library() {
    // clean up
    map<const wstring, LibraryAlias*>::iterator alias_iter;
    for(alias_iter = aliases.begin(); alias_iter != aliases.end(); ++alias_iter) {
      LibraryAlias* tmp = alias_iter->second;
      delete tmp;
      tmp = nullptr;
    }
    aliases.clear();
    aliases_list.clear(); 

    map<const wstring, LibraryEnum*>::iterator enum_iter;
    for(enum_iter = enums.begin(); enum_iter != enums.end(); ++enum_iter) {
      LibraryEnum* tmp = enum_iter->second;
      delete tmp;
      tmp = nullptr;
    }
    enums.clear();
    enum_list.clear();

    map<const wstring, LibraryClass*>::iterator cls_iter;
    for(cls_iter = named_classes.begin(); cls_iter != named_classes.end(); ++cls_iter) {
      LibraryClass* tmp = cls_iter->second;
      delete tmp;
      tmp = nullptr;
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
  
  bool HasBundleName(const wstring &name) {
    vector<wstring>::iterator found = find(bundle_names.begin(), bundle_names.end(), name);
    return found != bundle_names.end();
  }

  LibraryClass* GetClass(const wstring &name) {
    map<const wstring, LibraryClass*>::iterator result = named_classes.find(name);
    if(result != named_classes.end()) {
      return result->second;
    }

    return nullptr;
  }

  LibraryEnum* GetEnum(const wstring &name) {
    map<const wstring, LibraryEnum*>::iterator result = enums.find(name);
    if(result != enums.end()) {
      return result->second;
    }

    return nullptr;
  }

  LibraryAlias* GetAlias(const wstring &n) {
    map<const wstring, LibraryAlias*>::iterator result = aliases.find(n);
    if(result != aliases.end()) {
      return result->second;
    }

    return nullptr;
  }

  vector<LibraryEnum*> GetEnums() {
    return enum_list;
  }

  vector<LibraryAlias*> GetAliases() {
    return aliases_list;
  }

  vector<LibraryClass*> GetClasses() {
    return class_list;
  }

  map<const wstring, const wstring> GetHierarchies() {
    return hierarchies;
  }

  vector<CharStringInstruction*> GetCharStringInstructions() {
    return char_strings;
  }

  vector<IntStringInstruction*> GetIntStringInstructions() {
    return int_strings;
  }
  
  vector<FloatStringInstruction*> GetFloatStringInstructions() {
    return float_strings;
  }

  void Load();
};

/********************************
 * Manages shared libraries
 ********************************/
class Linker {
  map<const wstring, Library*> libraries;
  vector <LibraryAlias*> all_aliases;
  unordered_map<wstring, LibraryAlias*> all_aliases_map;
  vector<LibraryClass*> all_classes;
  unordered_map<wstring, LibraryClass*> all_classes_map;
  vector<LibraryEnum*> all_enums;
  unordered_map<wstring, LibraryEnum*> all_enums_map;
  wstring master_path;
  vector<wstring> paths;

 public:
#ifdef _DEBUG
  static void Debug(const wstring &msg, const int line_num, int depth) {
    GetLogger() << setw(4) << line_num << L": ";
    for(int i = 0; i < depth; ++i) {
      GetLogger() << L"  ";
    }
    GetLogger() << msg << endl;
  }
#endif

  static wstring ToString(int v) {
    wostringstream str;
    str << v;
    return str.str();
  }
  
 public:
  Linker(const wstring &p) {
    master_path = p;
  }

  ~Linker() {
    // clean up
    map<const wstring, Library*>::iterator iter;
    for(iter = libraries.begin(); iter != libraries.end(); ++iter) {
      Library* tmp = iter->second;
      delete tmp;
      tmp = nullptr;
    }
    libraries.clear();

    paths.clear();
  }

  void ResloveExternalClass(LibraryClass* klass);
  void ResloveExternalClasses();
  void ResolveExternalMethodCalls();

  vector<wstring> GetLibraryPaths() {
    return paths;
  }

  Library* GetLibrary(const wstring &name) {
    map<const wstring, Library*>::iterator result = libraries.find(name);
    if(result != libraries.end()) {
      return result->second;
    }

    return nullptr;
  }

  // get all libraries
  map<const wstring, Library*> GetAllLibraries() {
    return libraries;
  }

  // get all used libraries
  vector<Library*> GetAllUsedLibraries();

  // returns all aliases including duplicates
  unordered_map<std::wstring, LibraryAlias*> GetAllAliasesMap();

  // returns all aliases including duplicates
  vector<LibraryAlias*> GetAllAliases();

  // returns all classes including duplicates
  unordered_map<wstring, LibraryClass*> GetAllClassesMap();

  // returns all classes including duplicates
  vector<LibraryClass*> GetAllClasses();

  // returns all enums including duplicates
  unordered_map<wstring, LibraryEnum*> GetAllEnumsMap();

  // returns all enums including duplicates
  vector<LibraryEnum*> GetAllEnums();

  LibraryClass* SearchClassLibraries(const wstring &name) {
    unordered_map<wstring, LibraryClass*> klass_map = GetAllClassesMap();
    return klass_map[name];
  }

  // check to see if bundle name exists
  bool HasBundleName(const wstring &name);

  // finds the first alias match; note multiple matches may exist
  LibraryAlias* SearchAliasLibraries(const wstring& name, vector<wstring> uses);

  // finds the first class match; note multiple matches may exist
  LibraryClass* SearchClassLibraries(const wstring& name, vector<wstring> uses);

  // finds the first enum match; note multiple matches may exist
  LibraryEnum* SearchEnumLibraries(const wstring& name, vector<wstring> uses);

  void Load();
};

#endif
