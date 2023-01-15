/***************************************************************************
* Defines how the intermediate code is written to output files
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
* - Neither the name of the Objeck team nor the names of its
* contributors may be used to endorse or promote products derived
* from this software without specific prior written permission.fd
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

#ifndef __FILE_TARGET_H__
#define __FILE_TARGET_H__

#include "types.h"
#include "linker.h"
#include "tree.h"
#include "../shared/instrs.h"
#include "../shared/version.h"

using namespace instructions;

namespace backend {
  class IntermediateClass;
  
  std::wstring ReplaceSubstring(std::wstring s, const std::wstring &f, const std::wstring &r);
  
  /****************************
   * Intermediate class
   ****************************/
  class Intermediate {

  public:
    Intermediate() {
    }

    virtual ~Intermediate() {
    }

  protected:
    inline void WriteString(const std::wstring &in, OutputStream& out_stream) {
      out_stream.WriteString(in);
    }

    inline void WriteByte(char value, OutputStream& out_stream) {
      out_stream.WriteByte(value);
    }

    inline void WriteInt(long value, OutputStream& out_stream) {
      out_stream.WriteInt(value);
    }

    inline void WriteUnsigned(unsigned long value, OutputStream& out_stream) {
      out_stream.WriteUnsigned(value);
    }

    inline void WriteChar(wchar_t value, OutputStream& out_stream) {
      out_stream.WriteChar(value);
    }

    inline void WriteDouble(FLOAT_VALUE value, OutputStream& out_stream) {
      out_stream.WriteDouble(value);
    }
  };

  /****************************
  * IntermediateInstruction class
  ****************************/
  class IntermediateInstruction : public Intermediate {
    friend class IntermediateFactory;

    InstructionType type;
    long operand;
    long operand2;
    long operand3;
    FLOAT_VALUE operand4;
    std::wstring operand5;
    std::wstring operand6;
    long line_num;
    frontend::Statement* statement;
    frontend::Expression* expression;

    IntermediateInstruction(frontend::Statement* s, frontend::Expression* e, long l, InstructionType t) {
      line_num = l;
      statement = s;
      expression = e;
      type = t;
    }

    IntermediateInstruction(frontend::Statement* s, frontend::Expression* e, long l, InstructionType t, long o1) {
      line_num = l;
      statement = s;
      expression = e;
      type = t;
      operand = o1;
    }

    IntermediateInstruction(frontend::Statement* s, frontend::Expression* e, long l, InstructionType t, long o1, long o2) {
      line_num = l;
      statement = s;
      expression = e;
      type = t;
      operand = o1;
      operand2 = o2;
    }

    IntermediateInstruction(frontend::Statement* s, frontend::Expression* e, long l, InstructionType t, long o1, long o2, long o3) {
      line_num = l;
      statement = s;
      expression = e;
      type = t;
      operand = o1;
      operand2 = o2;
      operand3 = o3;
    }

    IntermediateInstruction(frontend::Statement* s, frontend::Expression* e, long l, InstructionType t, FLOAT_VALUE o4) {
      line_num = l;
      statement = s;
      expression = e;
      type = t;
      operand4 = o4;
    }

    IntermediateInstruction(frontend::Statement* s, frontend::Expression* e, long l, InstructionType t, std::wstring o5) {
      line_num = l;
      statement = s;
      expression = e;
      type = t;
      operand5 = o5;
    }

    IntermediateInstruction(frontend::Statement* s, frontend::Expression* e, long l, InstructionType t, long o3, std::wstring o5, std::wstring o6) {
      line_num = l;
      statement = s;
      expression = e;
      type = t;
      operand3 = o3;
      operand5 = o5;
      operand6 = o6;
    }

    IntermediateInstruction(LibraryInstr* lib_instr) {
      type = lib_instr->GetType();
      line_num = lib_instr->GetLineNumber();
      statement = nullptr;
      expression = nullptr;
      operand = lib_instr->GetOperand();
      operand2 = lib_instr->GetOperand2();
      operand3 = lib_instr->GetOperand3();
      operand4 = lib_instr->GetOperand4();
      operand5 = lib_instr->GetOperand5();
      operand6 = lib_instr->GetOperand6();
    }

    ~IntermediateInstruction() {
    }

  public:
    InstructionType GetType() {
      return type;
    }

    frontend::Statement* GetStatement() {
      return statement;
    }

    frontend::Expression* GetExpression() {
      return expression;
    }

    long GetOperand() {
      return operand;
    }

    long GetOperand2() {
      return operand2;
    }

    FLOAT_VALUE GetOperand4() {
      return operand4;
    }

    void SetOperand(long o1) {
      operand = o1;
    }
    
    void SetOperand3(long o3) {
      operand3 = o3;
    }

    void Debug(size_t i);

    void Write(bool is_debug, OutputStream& out_stream);
  };

  /****************************
   * IntermediateFactory class
   ****************************/
  class IntermediateFactory {
    static IntermediateFactory* instance;
    std::vector<IntermediateInstruction*> instructions;

  public:
    static IntermediateFactory* Instance();

    void Clear() {
      while(!instructions.empty()) {
        IntermediateInstruction* tmp = instructions.front();
        instructions.erase(instructions.begin());
        // delete
        delete tmp;
        tmp = nullptr;
      }

      delete instance;
      instance = nullptr;
    }

    IntermediateInstruction* MakeInstruction(frontend::Statement* s, long l, InstructionType t) {
      IntermediateInstruction* tmp = new IntermediateInstruction(s, nullptr, l, t);
      instructions.push_back(tmp);
      return tmp;
    }

    IntermediateInstruction* MakeInstruction(frontend::Statement* s, long l, InstructionType t, long o1) {
      IntermediateInstruction* tmp = new IntermediateInstruction(s, nullptr, l, t, o1);
      instructions.push_back(tmp);
      return tmp;
    }

    IntermediateInstruction* MakeInstruction(frontend::Statement* s, long l, InstructionType t, long o1, long o2) {
      IntermediateInstruction* tmp = new IntermediateInstruction(s, nullptr, l, t, o1, o2);
      instructions.push_back(tmp);
      return tmp;
    }

    IntermediateInstruction* MakeInstruction(frontend::Statement* s, long l, InstructionType t, long o1, long o2, long o3) {
      IntermediateInstruction* tmp = new IntermediateInstruction(s, nullptr, l, t, o1, o2, o3);
      instructions.push_back(tmp);
      return tmp;
    }

    IntermediateInstruction* MakeInstruction(frontend::Statement* s, long l, InstructionType t, FLOAT_VALUE o4) {
      IntermediateInstruction* tmp = new IntermediateInstruction(s, nullptr, l, t, o4);
      instructions.push_back(tmp);
      return tmp;
    }

    IntermediateInstruction* MakeInstruction(frontend::Statement* s, long l, InstructionType t, std::wstring o5) {
      IntermediateInstruction* tmp = new IntermediateInstruction(s, nullptr, l, t, o5);
      instructions.push_back(tmp);
      return tmp;
    }

    IntermediateInstruction* MakeInstruction(frontend::Statement* s, long l, InstructionType t, long o3, std::wstring o5, std::wstring o6) {
      IntermediateInstruction* tmp = new IntermediateInstruction(s, nullptr, l, t, o3, o5, o6);
      instructions.push_back(tmp);
      return tmp;
    }

    IntermediateInstruction* MakeInstruction(frontend::Statement* s, frontend::Expression* e, long l, InstructionType t) {
      IntermediateInstruction* tmp = new IntermediateInstruction(s, e, l, t);
      instructions.push_back(tmp);
      return tmp;
    }

    IntermediateInstruction* MakeInstruction(frontend::Statement* s, frontend::Expression* e, long l, InstructionType t, long o1) {
      IntermediateInstruction* tmp = new IntermediateInstruction(s, e, l, t, o1);
      instructions.push_back(tmp);
      return tmp;
    }

    IntermediateInstruction* MakeInstruction(frontend::Statement* s, frontend::Expression* e, long l, InstructionType t, long o1, long o2) {
      IntermediateInstruction* tmp = new IntermediateInstruction(s, e, l, t, o1, o2);
      instructions.push_back(tmp);
      return tmp;
    }

    IntermediateInstruction* MakeInstruction(frontend::Statement* s, frontend::Expression* e, long l, InstructionType t, long o1, long o2, long o3) {
      IntermediateInstruction* tmp = new IntermediateInstruction(s, e, l, t, o1, o2, o3);
      instructions.push_back(tmp);
      return tmp;
    }

    IntermediateInstruction* MakeInstruction(frontend::Statement* s, frontend::Expression* e, long l, InstructionType t, FLOAT_VALUE o4) {
      IntermediateInstruction* tmp = new IntermediateInstruction(s, e, l, t, o4);
      instructions.push_back(tmp);
      return tmp;
    }

    IntermediateInstruction* MakeInstruction(frontend::Statement* s, frontend::Expression* e, long l, InstructionType t, std::wstring o5) {
      IntermediateInstruction* tmp = new IntermediateInstruction(s, e, l, t, o5);
      instructions.push_back(tmp);
      return tmp;
    }

    IntermediateInstruction* MakeInstruction(frontend::Statement* s, frontend::Expression* e, long l, InstructionType t, long o3, std::wstring o5, std::wstring o6) {
      IntermediateInstruction* tmp = new IntermediateInstruction(s, e, l, t, o3, o5, o6);
      instructions.push_back(tmp);
      return tmp;
    }

    //
    // instructions without related parse nodes
    //
    IntermediateInstruction* MakeInstruction(long l, InstructionType t) {
      IntermediateInstruction* tmp = new IntermediateInstruction(nullptr, nullptr, l, t);
      instructions.push_back(tmp);
      return tmp;
    }

    IntermediateInstruction* MakeInstruction(long l, InstructionType t, long o1) {
      IntermediateInstruction* tmp = new IntermediateInstruction(nullptr, nullptr, l, t, o1);
      instructions.push_back(tmp);
      return tmp;
    }

    IntermediateInstruction* MakeInstruction(long l, InstructionType t, long o1, long o2) {
      IntermediateInstruction* tmp = new IntermediateInstruction(nullptr, nullptr, l, t, o1, o2);
      instructions.push_back(tmp);
      return tmp;
    }

    IntermediateInstruction* MakeInstruction(long l, InstructionType t, long o1, long o2, long o3) {
      IntermediateInstruction* tmp = new IntermediateInstruction(nullptr, nullptr, l, t, o1, o2, o3);
      instructions.push_back(tmp);
      return tmp;
    }

    IntermediateInstruction* MakeInstruction(long l, InstructionType t, FLOAT_VALUE o4) {
      IntermediateInstruction* tmp = new IntermediateInstruction(nullptr, nullptr, l, t, o4);
      instructions.push_back(tmp);
      return tmp;
    }

    IntermediateInstruction* MakeInstruction(long l, InstructionType t, long o3, std::wstring o5, std::wstring o6) {
      IntermediateInstruction* tmp = new IntermediateInstruction(nullptr, nullptr, l, t, o3, o5, o6);
      instructions.push_back(tmp);
      return tmp;
    }

    //
    // library instructions
    //
    IntermediateInstruction* MakeInstruction(LibraryInstr* lib_instr) {
      IntermediateInstruction* tmp = new IntermediateInstruction(lib_instr);
      instructions.push_back(tmp);
      return tmp;
    }
  };

  /****************************
   * Block class
   ****************************/
  class IntermediateBlock : public Intermediate {
    std::vector<IntermediateInstruction*> instructions;

  public:
    IntermediateBlock() {
    }

    ~IntermediateBlock() {
    }

    void AddInstruction(IntermediateInstruction* i) {
      instructions.push_back(i);
    }

    void AddInstructions(std::vector<IntermediateInstruction*> &i) {
      instructions = i;
    }

    void Remove(std::pair<size_t, size_t> &range) {
      const size_t start_edit = range.first;
      const size_t end_edit = range.second;

      if(start_edit < end_edit && end_edit < instructions.size()) {
        instructions.erase(instructions.begin() + start_edit, instructions.begin() + end_edit);
      }
    }

    std::vector<IntermediateInstruction*> GetInstructions() {
      return instructions;
    }

    int GetSize() {
      return (int)instructions.size();
    }

    void Clear() {
      instructions.clear();
    }

    bool IsEmpty() {
      return instructions.size() == 0;
    }

    void Write(bool is_debug, OutputStream& out_stream);

    void Debug() {
      if(instructions.size() > 0) {
        for(size_t i = 0; i < instructions.size(); ++i) {
          instructions[i]->Debug(i);
        }
        GetLogger() << L"--" << std::endl;
      }
    }
  };

  /****************************
   * Method class
   **************************/
  class IntermediateMethod : public Intermediate {
    int id;
    std::wstring name;
    std::wstring rtrn_name;
    int space;
    int params;
    frontend::MethodType type;
    bool is_native;
    bool is_function;
    bool is_lib;
    bool is_virtual;
    bool has_and_or;
    bool is_lambda;
    int instr_count;
    std::vector<IntermediateBlock*> blocks;
    IntermediateDeclarations* entries;
    IntermediateClass* klass;

  public:
    IntermediateMethod(int i, const std::wstring &n, bool v, bool h, bool l, const std::wstring &r,
                       frontend::MethodType t, bool nt, bool f, int c, int p,
                       IntermediateDeclarations* e, IntermediateClass* k) {
      id = i;
      name = n;
      is_virtual = v;
      has_and_or = h;
      is_lambda = l;
      rtrn_name = r;
      type = t;
      is_native = nt;
      is_function = f;
      space = c;
      params = p;
      entries = e;
      is_lib = false;
      klass = k;
      instr_count = 0;
    }

    IntermediateMethod(LibraryMethod* lib_method, IntermediateClass* k) {
      // set attributes
      id = lib_method->GetId();
      name = lib_method->GetName();
      is_virtual = lib_method->IsVirtual();
      has_and_or = lib_method->HasAndOr();
      is_lambda = lib_method->IsLambda();
      rtrn_name = lib_method->GetEncodedReturn();
      type = lib_method->GetMethodType();
      is_native = lib_method->IsNative();
      is_function = lib_method->IsStatic();
      space = lib_method->GetSpace();
      params = lib_method->GetNumParams();
      entries = lib_method->GetEntries();
      is_lib = true;
      instr_count = 0;
      klass = k;
      
      // process instructions
      IntermediateBlock* block = new IntermediateBlock;
      std::vector<LibraryInstr*> lib_instructions = lib_method->GetInstructions();
      for(size_t i = 0; i < lib_instructions.size(); ++i) {
        block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(lib_instructions[i]));
      }
      AddBlock(block);
    }

    ~IntermediateMethod() {
      // clean up
      while(!blocks.empty()) {
        IntermediateBlock* tmp = blocks.front();
        blocks.erase(blocks.begin());
        // delete
        delete tmp;
        tmp = nullptr;
      }
    }

    int GetId() {
      return id;
    }
    
    IntermediateDeclarations* GetEntries() {
      return entries;
    }
    
    IntermediateClass* GetClass() {
      return klass;
    }

    int GetSpace() {
      return space;
    }

    void SetSpace(int s) {
      space = s;
    }

    std::wstring GetName() {
      return name;
    }

    bool IsVirtual() {
      return is_virtual;
    }

    bool IsLibrary() {
      return is_lib;
    }

    void AddBlock(IntermediateBlock* b) {
      instr_count += b->GetSize();
      blocks.push_back(b);
    }

    int GetInstructionCount() {
      return instr_count;
    }

    bool HasAndOr() {
      return has_and_or;
    }

    int GetNumParams() {
      return params;
    }

    std::vector<IntermediateBlock*> GetBlocks() {
      return blocks;
    }

    void SetBlocks(std::vector<IntermediateBlock*> b) {
      blocks = b;
    }

    void Write(bool emit_lib, bool is_debug, OutputStream &out_stream);

    void Debug();
  };

  /****************************
   * Class class
   ****************************/
  class IntermediateClass : public Intermediate {
    int id;
    std::wstring name;
    int pid;
    std::vector<int> interface_ids;
    std::wstring parent_name;
    std::vector<std::wstring> interface_names;
    std::vector<std::wstring> generic_classes;
    int cls_space;
    int inst_space;
    std::vector<IntermediateBlock*> blocks;
    std::vector<IntermediateMethod*> methods;
    std::map<int, IntermediateMethod*> method_map;
    IntermediateDeclarations* cls_entries;
    IntermediateDeclarations* inst_entries;
    std::map<IntermediateDeclarations*, std::pair<std::wstring, int> > closure_entries;
    bool is_lib;
    bool is_interface;
    bool is_public;
    bool is_virtual;
    bool is_debug;
    std::wstring file_name;
    
  public:
    IntermediateClass(int i, const std::wstring &n, int pi, const std::wstring &p, std::vector<int> infs, std::vector<std::wstring> in, bool is_inf, bool is_pub,
                      std::vector<std::wstring> gen, bool is_vrtl, int cs, int is, IntermediateDeclarations* ce, IntermediateDeclarations* ie, 
                      const std::wstring &fn, bool d) {
      id = i;
      name = n;
      pid = pi;
      parent_name = p;
      interface_ids = infs;
      interface_names = in;
      generic_classes = gen;
      is_interface = is_inf;
      is_public = is_pub;
      is_virtual = is_vrtl;
      cls_space = cs;
      inst_space = is;
      cls_entries = ce;
      inst_entries = ie;
      is_lib = false;
      is_debug = d;
      file_name = fn;
    }
    
    IntermediateClass(LibraryClass* lib_klass, int pi) {
      // set attributes
      id = lib_klass->GetId();
      name = lib_klass->GetName();
      pid = pi;
      interface_ids = lib_klass->GetInterfaceIds();
      parent_name = lib_klass->GetParentName();
      interface_names = lib_klass->GetInterfaceNames();
      is_interface = lib_klass->IsInterface();
      is_public = lib_klass->IsPublic();
      is_virtual = lib_klass->IsVirtual();
      is_debug = lib_klass->IsDebug();
      cls_space = lib_klass->GetClassSpace();
      inst_space = lib_klass->GetInstanceSpace();
      cls_entries = lib_klass->GetClassEntries();
      inst_entries = lib_klass->GetInstanceEntries();
      closure_entries = lib_klass->GetLambaEntries();

      // process methods
      std::map<const std::wstring, LibraryMethod*> lib_methods = lib_klass->GetMethods();
      std::map<const std::wstring, LibraryMethod*>::iterator mthd_iter;
      for(mthd_iter = lib_methods.begin(); mthd_iter != lib_methods.end(); ++mthd_iter) {
        LibraryMethod* lib_method = mthd_iter->second;
        IntermediateMethod* imm_method = new IntermediateMethod(lib_method, this);
        AddMethod(imm_method);
      }

      file_name = lib_klass->GetFileName();
      is_lib = true;
    }

    ~IntermediateClass() {
      // clean up
      while(!blocks.empty()) {
        IntermediateBlock* tmp = blocks.front();
        blocks.erase(blocks.begin());
        // delete
        delete tmp;
        tmp = nullptr;
      }
      
      // clean up
      while(!methods.empty()) {
        IntermediateMethod* tmp = methods.front();
        methods.erase(methods.begin());
        // delete
        delete tmp;
        tmp = nullptr;
      }

      std::map<IntermediateDeclarations*, std::pair<std::wstring, int> >::iterator entries_iter;
      for(entries_iter = closure_entries.begin(); entries_iter != closure_entries.end(); ++entries_iter) {
        IntermediateDeclarations* tmp = entries_iter->first;
        delete tmp;
        tmp = nullptr;
      }
      closure_entries.clear();
    }

    int GetId() {
      return id;
    }

    const std::wstring &GetName() {
      return name;
    }

    bool IsLibrary() {
      return is_lib;
    }

    int GetInstanceSpace() {
      return inst_space;
    }

    void SetInstanceSpace(int s) {
      inst_space = s;
    }

    int GetClassSpace() {
      return cls_space;
    }

    void SetClassSpace(int s) {
      cls_space = s;
    }
    
    void AddMethod(IntermediateMethod* m) {
      methods.push_back(m);
      method_map.insert(std::pair<int, IntermediateMethod*>(m->GetId(), m));
    }

    void AddBlock(IntermediateBlock* b) {
      blocks.push_back(b);
    }

    IntermediateMethod* GetMethod(int id) {
      std::map<int, IntermediateMethod*>::iterator result = method_map.find(id);
#ifdef _DEBUG
      assert(result != method_map.end());
#endif
      return result->second;
    }

    std::vector<IntermediateMethod*> GetMethods() {
      return methods;
    }

    void AddClosureDeclarations(const std::wstring mthd_name, const int mthd_id, IntermediateDeclarations* dclrs) {
      closure_entries[dclrs] = std::pair<std::wstring, int>(mthd_name, mthd_id);
    }

    void Write(bool emit_lib, OutputStream& out_stream);
    
    void Debug() {
      GetLogger() << L"=========================================================" << std::endl;
      GetLogger() << L"Class: id=" << id << L"; name='" << name << L"'; parent='" << parent_name
            << L"'; pid=" << pid << L";\n interface=" << (is_interface ? L"true" : L"false") 
            << L"; virtual=" << is_virtual << L"; num_generics=" << generic_classes.size() 
            << L"; num_methods=" << methods.size() << L"; class_mem_size=" << cls_space 
            << L";\n instance_mem_size=" << inst_space << L"; is_debug=" 
            << (is_debug  ? L"true" : L"false") << std::endl;      
      GetLogger() << std::endl << "Interfaces:" << std::endl;
      for(size_t i = 0; i < interface_names.size(); ++i) {
        GetLogger() << L"\t" << interface_names[i] << std::endl;
      }      
      GetLogger() << L"=========================================================" << std::endl;
      cls_entries->Debug(false);
      GetLogger() << L"---------------------------------------------------------" << std::endl;
      inst_entries->Debug(false);
      GetLogger() << L"=========================================================" << std::endl;
      for(size_t i = 0; i < blocks.size(); ++i) {
        blocks[i]->Debug();
      }

      for(size_t i = 0; i < methods.size(); ++i) {
        methods[i]->Debug();
      }
    }
  };

  /****************************
   * EnumItem class
   ****************************/
  class IntermediateEnumItem : public Intermediate {
    std::wstring name;
    INT_VALUE id;

  public:
    IntermediateEnumItem(const std::wstring &n, const INT_VALUE i) {
      name = n;
      id = i;
    }

    IntermediateEnumItem(LibraryEnumItem* i) {
      name = i->GetName();
      id = i->GetId();
    }

    void Write(OutputStream& out_stream);

    void Debug() {
      GetLogger() << L"Item: name='" << name << L"'; id='" << id << std::endl;
    }
  };

  /****************************
   * Enum class
   ****************************/
  class IntermediateEnum : public Intermediate {
    std::wstring name;
    INT_VALUE offset;
    std::vector<IntermediateEnumItem*> items;

  public:
    IntermediateEnum(const std::wstring &n, const INT_VALUE o) {
      name = n;
      offset = o;
    }

    IntermediateEnum(LibraryEnum* e) {
      name = e->GetName();
      offset = e->GetOffset();
      // write items
      std::map<const std::wstring, LibraryEnumItem*> items = e->GetItems();
      std::map<const std::wstring, LibraryEnumItem*>::iterator iter;
      for(iter = items.begin(); iter != items.end(); ++iter) {
        LibraryEnumItem* lib_enum_item = iter->second;
        IntermediateEnumItem* imm_enum_item = new IntermediateEnumItem(lib_enum_item);
        AddItem(imm_enum_item);
      }
    }

    ~IntermediateEnum() {
      while(!items.empty()) {
        IntermediateEnumItem* tmp = items.back();
        items.pop_back();
        // delete
        delete tmp;
        tmp = nullptr;
      }
    }

    void AddItem(IntermediateEnumItem* i) {
      items.push_back(i);
    }

    void Write(OutputStream& out_stream);

    void Debug() {
      GetLogger() << L"=========================================================" << std::endl;
      GetLogger() << L"Enum: name='" << name << L"'; items=" << items.size() << std::endl;
      GetLogger() << L"=========================================================" << std::endl;

      for(size_t i = 0; i < items.size(); ++i) {
        items[i]->Debug();
      }
    }
  };

  /****************************
   * Program class
   ****************************/
  class IntermediateProgram : public Intermediate {
    static IntermediateProgram* instance;
    int class_id;
    int method_id;
    std::vector<std::wstring> alias_encodings;
    std::vector<IntermediateEnum*> enums;
    std::vector<IntermediateClass*> classes;
    std::map<int, IntermediateClass*> class_map;
    std::vector<std::wstring> char_strings;
    std::vector<frontend::IntStringHolder*> int_strings;
    std::vector<frontend::FloatStringHolder*> float_strings;
    std::vector<std::wstring> bundle_names;
    std::wstring aliases_str;
    int num_src_classes;
    int num_lib_classes;
    int string_cls_id;

    IntermediateProgram() {
      num_src_classes = num_lib_classes = 0;
      string_cls_id = -1;
    }

  public:
    static IntermediateProgram* Instance() {
      if(!instance) {
        instance = new IntermediateProgram();
      }

      return instance;
    }

    ~IntermediateProgram() {
      // clean up
      while(!enums.empty()) {
        IntermediateEnum* tmp = enums.back();
        enums.pop_back();
        // delete
        delete tmp;
        tmp = nullptr;
      }

      while(!classes.empty()) {
        IntermediateClass* tmp = classes.back();
        classes.pop_back();
        // delete
        delete tmp;
        tmp = nullptr;
      }

      while(!int_strings.empty()) {
        frontend::IntStringHolder* tmp = int_strings.back();
        delete[] tmp->value;
        tmp->value = nullptr;
        int_strings.pop_back();
        delete tmp;
        tmp = nullptr;
      }

      while(!float_strings.empty()) {
        frontend::FloatStringHolder* tmp = float_strings.back();
        delete[] tmp->value;
        tmp->value = nullptr;
        float_strings.pop_back();
        // delete
        delete tmp;
        tmp = nullptr;
      }

      IntermediateFactory::Instance()->Clear();
    }

    void AddClass(IntermediateClass* c) {
      classes.push_back(c);
      class_map.insert(std::pair<int, IntermediateClass*>(c->GetId(), c));
    }

    IntermediateClass* GetClass(int id) {
      std::map<int, IntermediateClass*>::iterator result = class_map.find(id);
#ifdef _DEBUG
      // assert(result != class_map.end());
#endif
      return result->second;
    }

    void AddAliasEncoding(const std::wstring &a) {
      alias_encodings.push_back(a);
    }

    void AddEnum(IntermediateEnum* e) {
      enums.push_back(e);
    }

    std::vector<IntermediateClass*> GetClasses() {
      return classes;
    }

    void SetCharStrings(std::vector<std::wstring> s) {
      char_strings = s;
    }

    void SetIntStrings(std::vector<frontend::IntStringHolder*> s) {
      int_strings = s;
    }

    void SetFloatStrings(std::vector<frontend::FloatStringHolder*> s) {
      float_strings = s;
    }

    void SetStartIds(int c, int m) {
      class_id = c;
      method_id = m;
    }

    int GetStartClassId() {
      return class_id;
    }

    int GetStartMethodId() {
      return method_id;
    }

    void SetBundleNames(std::vector<std::wstring> &n) {
      bundle_names = n;
    }

    void SetStringClassId(int i) {
      string_cls_id = i;
    }

    void SetAliasesString(const std::wstring &a) {
      aliases_str = a;
    }

    const std::wstring GetAliasesString() {
      return aliases_str;
    }

    void Write(bool emit_lib, bool is_debug, bool is_web, OutputStream& out_stream);

    void Debug() {
/*
      GetLogger() << L"Strings:" << std::endl;
      for(size_t i = 0; i < char_strings.size(); ++i) {
        GetLogger() << L"string id=" << i << L", size=" << ToString((int)char_strings[i].size()) << std::endl;
      }
      GetLogger() << std::endl;
*/
      GetLogger() << L"Program: enums=" << enums.size() << L", classes=" << classes.size() << L"; start_ids=" << class_id << L"," << method_id << std::endl;
      // enums
      for(size_t i = 0; i < enums.size(); ++i) {
        enums[i]->Debug();
      }
      // classes
      for(size_t i = 0; i < classes.size(); ++i) {
        classes[i]->Debug();
      }
    }

    inline std::wstring ToString(int v) {
      std::wostringstream str;
      str << v;
      return str.str();
    }
  };

  /****************************
   * FileEmitter class
   ****************************/
  class FileEmitter {
    IntermediateProgram* program;
    std::wstring file_name;
    bool emit_lib;
    bool is_debug;
    bool is_web;
    bool show_asm;

    std::string ReplaceExt(const std::string &org, const std::string &ext) {
      std::string str(org);
      
      size_t i = str.rfind('.', str.size());
      if(i != std::string::npos) {
        str.replace(i + 1, ext.size(), ext);
      }

      return str;
    }

  public:
    FileEmitter(IntermediateProgram* p, bool l, bool d, bool w, bool s, const std::wstring &n) {
      program = p;
      emit_lib = l;
      is_debug = d;
      is_web = w;
      show_asm = s;
      file_name = n;

#ifndef _DEBUG
      if(show_asm) {
        OpenLogger(ReplaceExt(UnicodeToBytes(file_name), "obm"));
      }
#endif
    }

    ~FileEmitter() {
#ifndef _DEBUG
      if(show_asm) {
        CloseLogger();
      }
#endif
      delete program;
      program = nullptr;
    }

    void Emit();
  };
}

#endif
