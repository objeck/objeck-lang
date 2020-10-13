/***************************************************************************
 * Translates a parse tree into an intermediate format.  This format is
 * used for optimizations and target output.
 *
 * Copyright (c) 2008-2020, Randy Hollines
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

#ifndef __INTERMEDIATE_H__
#define __INTERMEDIATE_H__

#include "tree.h"
#include "emit.h"

using namespace frontend;
using namespace backend;

class IntermediateEmitter;

/****************************
 * Translates a parse tree
 * into intermediate code. Also
 * identifies basic blocks
 * for optimization.
 ****************************/
class IntermediateEmitter {  
  ParsedProgram* parsed_program;
  ParsedBundle* parsed_bundle;
  IntermediateBlock* imm_block;
  IntermediateProgram* imm_program;
  IntermediateClass* imm_klass;
  IntermediateMethod* imm_method;
  Class* current_class;
  Method* current_method;
  SymbolTable* current_table;
  int conditional_label;
  int unconditional_label;
  bool is_lib;
  bool is_debug;
  friend class SelectArrayTree;
  bool is_new_inst;
  // NOTE: used to determine if two instantiated 
  // objects instances need to be swapped as 
  // method parameters
  int new_char_str_count; 
  int cur_line_num;
  LibraryClass*  string_cls;
  int string_cls_id;
  stack<pair<int, int> > break_labels;
  bool is_str_array;
  queue<OperationAssignment*>post_statements;
  
  // emit operations
  void EmitStrings();
  void EmitLibraries(Linker* linker);
  void EmitBundles();
  IntermediateEnum* EmitEnum(Enum* eenum);
  IntermediateClass* EmitClass(Class* klass);
  IntermediateMethod* EmitMethod(Method* method);
  void EmitLambda(Lambda* lambda);
  void EmitStatement(Statement* statement);
  void EmitIf(If* if_stmt);
  void EmitIf(If* if_stmt, int next_label, int end_label);
  void EmitDoWhile(DoWhile* do_while_stmt);
  void EmitWhile(While* while_stmt);
  void EmitSelect(Select* select_stmt);
  void EmitFor(For* for_stmt);
  void EmitCriticalSection(CriticalSection* critical_stmt);
  void EmitIndices(ExpressionList* indices);
  void EmitExpressions(ExpressionList* parameters);
  void EmitExpression(Expression* expression);
  void EmitStaticArray(StaticArray* array);
  void EmitCharacterString(CharacterString* char_str);
  void EmitCharacterStringSegment(CharacterStringSegment* segment, CharacterString* char_str);
  void EmitAppendCharacterStringSegment(CharacterStringSegment* segment, CharacterString* char_str);
  void EmitConditional(Cond* conditional);
  void EmitAndOr(CalculatedExpression* expression);
  void EmitCalculation(CalculatedExpression* expression);
  void EmitCast(Expression* expression);
  void EmitVariable(Variable* variable);
  void EmitAssignment(Assignment* assignment);
  void EmitStringConcat(OperationAssignment* assignment);
  void EmitDeclaration(Declaration* declaration);
  void EmitMethodCallParameters(MethodCall* method_call);
  void EmitMethodCallExpression(MethodCall* method_call, bool is_variable = false, bool is_closure = false);
  void EmitMethodCall(MethodCall* method_call, bool is_nested);
  void EmitMethodCallStatement(MethodCall* method_call);
  void EmitSystemDirective(SystemStatement* statement);
  int CalculateEntrySpace(SymbolTable* table, int &index,
                          IntermediateDeclarations* parameters, bool is_static);
  int CalculateEntrySpace(IntermediateDeclarations* parameters, bool is_static);
  
  // emits class cast checks
  void EmitClassCast(Expression* expression);

  // determines if a method call returns an unused value
  int OrphanReturn(MethodCall* method_call);

  Class* SearchProgramClasses(const wstring &klass_name);

  Enum* SearchProgramEnums(const wstring &eenum_name);

  wstring ToString(int v) {
    wostringstream str;
    str << v;
    return str.str();
  }

  wstring ToString(double d) {
    wostringstream str;
    str << d;
    return str.str();
  }

  // creates a new basic block
  void NewBlock() {
    // new basic block
    imm_block = new IntermediateBlock;
    if(current_method) {
      imm_method->AddBlock(imm_block);
    } else {
      imm_klass->AddBlock(imm_block);
    }
  }

  inline bool IntStringHolderEqual(IntStringHolder* lhs, IntStringHolder* rhs) {
    if(lhs->length != rhs->length) {
      return false;
    }

    for(int i = 0; i < lhs->length; ++i) {
      if(lhs->value[i] != rhs->value[i]) {
        return false;
      }
    }

    return true;
  }

  inline bool FloatStringHolderEqual(FloatStringHolder* lhs, FloatStringHolder* rhs) {
    if(lhs->length != rhs->length) {
      return false;
    }

    for(int i = 0; i < lhs->length; ++i) {
      if(lhs->value[i] != rhs->value[i]) {
        return false;
      }
    }

    return true;
  }
  
  void EmitOperatorVariable(Variable* variable, MemoryContext mem_context);
  
 public:
  IntermediateEmitter(ParsedProgram* p, bool l, bool d) {      
    parsed_program = p;
    is_lib = l;
    is_debug = d;
    // note: negative numbers are used
    // for method inlining in VM
    imm_program = IntermediateProgram::Instance();
    // 1,073,741,824 conditional labels
    conditional_label = -1;
    // 1,073,741,824 unconditional labels
    unconditional_label = (2 << 29) - 1;
    is_new_inst = false;
    new_char_str_count = 0;
    cur_line_num = -1;
    string_cls = nullptr;
    string_cls_id = -1;
    is_str_array = false;
  }

  ~IntermediateEmitter() {
  }

  void Translate();

  IntermediateProgram* GetProgram() {
    return imm_program;
  }

  int GetUnconditionalLabel() {
    return unconditional_label;
  }
};

/****************************
 * Creates a binary search tree
 * for case statements.
 ****************************/
enum SelectOperation {
  CASE_LESS,
  CASE_EQUAL,
  CASE_LESS_OR_EQUAL
};

class SelectNode {
  int id;
  int value;
  int value2;
  SelectOperation operation;
  SelectNode* left;
  SelectNode* right;

public:
  SelectNode(int i, int v) {
    id = i;
    value = v;
    operation = CASE_EQUAL;
    left = nullptr;
    right = nullptr;
  }

  SelectNode(int i, int v, SelectNode* l, SelectNode* r) {
    id = i;
    operation = CASE_LESS;
    value = v;
    left = l;
    right = r;
  }

  SelectNode(int i, int v, int v2, SelectNode* l, SelectNode* r) {
    id = i;
    operation = CASE_LESS_OR_EQUAL;
    value = v;
    value2 = v2;
    left = l;
    right = r;
  }

  ~SelectNode() {
    if(left) {
      delete left;
      left = nullptr;
    }

    if(right) {
      delete right;
      right = nullptr;
    }
  }

  int GetId() {
    return id;
  }

  int GetValue() {
    return value;
  }

  int GetValue2() {
    return value2;
  }

  SelectOperation GetOperation() {
    return operation;
  }

  SelectNode* GetLeft() {
    return left;
  }

  SelectNode* GetRight() {
    return right;
  }
};

class SelectArrayTree {
  int* values;
  SelectNode* root;
  IntermediateEmitter* emitter;
  map<int, int> value_label_map;
  Select* select;
  int other_label;

  SelectNode* divide(int start, int end);
  void Emit(SelectNode* node, int end_label);

public:
  SelectArrayTree(Select* s, IntermediateEmitter* e);

  ~SelectArrayTree() {
    delete[] values;
    values = nullptr;

    delete root;
    root = nullptr;
  }

  void Emit();
};
#endif
