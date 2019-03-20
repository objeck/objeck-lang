/***************************************************************************
 * Translates a parse tree into an intermediate format.  This format is
 * used for optimizations and target output.
 *
 * Copyright (c) 2008-2018, Randy Hollines
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
#ifdef _SCRIPTER
#include "../scripter/loader.h"
#else
#include "emit.h"
#endif

using namespace frontend;
using namespace backend;

class IntermediateEmitter;

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
    left = NULL;
    right = NULL;
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
      left = NULL;
    }

    if(right) {
      delete right;
      right = NULL;
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
    values = NULL;

    delete root;
    root = NULL;
  }

  void Emit();
};

/****************************
 * Translates a parse tree
 * into intermediate code. Also
 * identifies basic bloks
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
  stack<int> break_labels;
  bool is_str_array;
  queue<OperationAssignment*>post_statements;
  
  // emit operations
  void EmitStrings();
  void EmitLibraries(Linker* linker);
  void EmitBundles();
  IntermediateEnum* EmitEnum(Enum* eenum);
  IntermediateClass* EmitClass(Class* klass);
  IntermediateMethod* EmitMethod(Method* method);
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
  void EmitMethodCallExpression(MethodCall* method_call, bool is_variable = false);
  void EmitMethodCall(MethodCall* method_call, bool is_nested);
  void EmitMethodCallStatement(MethodCall* method_call);
  void EmitSystemDirective(SystemStatement* statement);
  int CalculateEntrySpace(SymbolTable* table, int &index,
                          IntermediateDeclarations* parameters, bool is_static);
  int CalculateEntrySpace(IntermediateDeclarations* parameters, bool is_static);
  
  // emits class cast checks
  void EmitClassCast(Expression* expression) {
    // ensure scalar cast
    if(expression->GetExpressionType() == METHOD_CALL_EXPR) {
      if(static_cast<MethodCall*>(expression)->GetLibraryMethod()) {
        if(static_cast<MethodCall*>(expression)->GetLibraryMethod()->GetReturn()->GetDimension() > 0) {
          return;
        }
      }
      else if(static_cast<MethodCall*>(expression)->GetMethod()) {
        if(static_cast<MethodCall*>(expression)->GetMethod()->GetReturn()->GetDimension() > 0) {
          return;
        }
      }
    }
    else if(expression->GetExpressionType() == VAR_EXPR) {
      if(static_cast<Variable*>(expression)->GetEntry()->GetType()->GetDimension() > 0 && 
         !static_cast<Variable*>(expression)->GetIndices()) {
        return;
      }
    }
    
    // class cast
    if(expression->GetToClass() && !(expression->GetExpressionType() == METHOD_CALL_EXPR &&
                                     static_cast<MethodCall*>(expression)->GetCallType() == NEW_ARRAY_CALL)) {
      if(is_lib) {
	imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LIB_OBJ_INST_CAST, expression->GetToClass()->GetName()));
      } 
      else {
	imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, OBJ_INST_CAST, expression->GetToClass()->GetId()));
      }
    } 
    // class library cast
    else if(expression->GetToLibraryClass() && !(expression->GetExpressionType() == METHOD_CALL_EXPR &&
                                                 static_cast<MethodCall*>(expression)->GetCallType() == NEW_ARRAY_CALL)) {
      if(is_lib) {
	imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LIB_OBJ_INST_CAST, expression->GetToLibraryClass()->GetName()));
      } 
      else {
	imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, OBJ_INST_CAST, expression->GetToLibraryClass()->GetId()));
      }
    }
  }

  // determines if a method call returns an unused value
  int OrphanReturn(MethodCall* method_call) {
    if(!method_call) {
      return -1;
    }

    if(method_call->GetCallType() == ENUM_CALL) {
      return 0;
    }

    Type* rtrn = NULL;
    if(method_call->GetMethod()) {
      rtrn = method_call->GetMethod()->GetReturn();
    } 
    else if(method_call->GetLibraryMethod()) {
      rtrn = method_call->GetLibraryMethod()->GetReturn();
    }
    else if(method_call->IsDynamicFunctionCall()) {
      rtrn =  method_call->GetDynamicFunctionEntry()->GetType()->GetFunctionReturn();
    }
    
    if(rtrn) {
      if(rtrn->GetType() != frontend::NIL_TYPE) {
        if(rtrn->GetDimension() > 0) {
          return 0;
        } else {
          switch(rtrn->GetType()) {
          case frontend::BOOLEAN_TYPE:
          case frontend::BYTE_TYPE:
          case frontend::CHAR_TYPE:
          case frontend::INT_TYPE:
          case frontend::CLASS_TYPE:
            return 0;

          case frontend::FLOAT_TYPE:
            return 1;

	  case frontend::FUNC_TYPE:
            return 2;

#ifdef _DEBUG
	  default:
	    assert(false);
	    break;
#else
	  default:
	    break;
#endif
          }
        }
      }
    }

    return -1;
  }

  inline Class* SearchProgramClasses(const wstring &klass_name) {
    Class* klass = parsed_program->GetClass(klass_name);
    if(!klass) {
      klass = parsed_program->GetClass(parsed_bundle->GetName() + L"." + klass_name);
      if(!klass) {
        vector<wstring> uses = parsed_program->GetUses();
        for(size_t i = 0; !klass && i < uses.size(); ++i) {
          klass = parsed_program->GetClass(uses[i] + L"." + klass_name);
        }
      }
    }

    return klass;
  }

  inline Enum* SearchProgramEnums(const wstring &eenum_name) {
    Enum* eenum = parsed_program->GetEnum(eenum_name);
    if(!eenum) {
      eenum = parsed_program->GetEnum(parsed_bundle->GetName() + L"." + eenum_name);
      if(!eenum) {
        vector<wstring> uses = parsed_program->GetUses();
        for(size_t i = 0; !eenum && i < uses.size(); ++i) {
          eenum = parsed_program->GetEnum(uses[i] + L"." + eenum_name);
        }
      }
    }

    return eenum;
  }

  void Show(const wstring &msg, const int line_num, int depth) {
    GetLogger() << setw(4) << line_num << ": ";
    for(int i = 0; i < depth; i++) {
      GetLogger() << L"  ";
    }
    GetLogger() << msg << endl;
  }

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
    
    for(int i = 0; i < lhs->length; i++) {
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
    
    for(int i = 0; i < lhs->length; i++) {
      if(lhs->value[i] != rhs->value[i]) {
	return false;
      }
    }
    
    return true;
  }
  
  void EmitOperatorVariable(Variable* variable, MemoryContext mem_context) {
    // indices
    ExpressionList* indices = variable->GetIndices();
    
    // array variable
    if(indices) {
      int dimension = (int)indices->GetExpressions().size();
      EmitIndices(indices);

      // load instance or class memory
      if(mem_context == INST) {
	imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INST_MEM));
      } else if(mem_context == CLS) {
	imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_CLS_MEM));
      }

      switch(variable->GetBaseType()->GetType()) {
      case frontend::BYTE_TYPE:
	imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_VAR, variable->GetId(), mem_context));
	imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_BYTE_ARY_ELM, dimension, mem_context));
	break;

      case frontend::CHAR_TYPE:
	imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_VAR, variable->GetId(), mem_context));
	imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_CHAR_ARY_ELM, dimension, mem_context));
	break;
	
      case frontend::INT_TYPE:
	imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_VAR, variable->GetId(), mem_context));
	imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_ARY_ELM, dimension, mem_context));
	break;
	
      case frontend::FLOAT_TYPE:
	imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_VAR, variable->GetId(), mem_context));
	imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_FLOAT_ARY_ELM, dimension, mem_context));
	break;
	
      default:
	break;
      }
    }
    else {
      switch(variable->GetBaseType()->GetType()) {
      case frontend::BOOLEAN_TYPE:
      case frontend::BYTE_TYPE:
      case frontend::CHAR_TYPE:
      case frontend::INT_TYPE:
      case frontend::CLASS_TYPE:
	imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_VAR, variable->GetId(), mem_context));
	break;
      
      case frontend::FLOAT_TYPE:
	imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_FLOAT_VAR, variable->GetId(), mem_context));
	break;

      default:
	break;
      }
    }
  }
  
 public:
  IntermediateEmitter(ParsedProgram* p, bool l, bool d) {      
    parsed_program = p;
    is_lib = l;
    is_debug = d;
    // note: negative numbers are used
    // for method inlining in VM
    imm_program = new IntermediateProgram;
    // 1,073,741,824 conditional labels
    conditional_label = -1;
    // 1,073,741,824 unconditional labels
    unconditional_label = (2 << 29) - 1;
    is_new_inst = false;
    new_char_str_count = 0;
    cur_line_num = -1;
    string_cls = NULL;
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

#endif
