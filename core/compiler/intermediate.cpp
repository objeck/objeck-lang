/***************************************************************************
 * Translates a parse tree into an intermediate format.  This format
 * is used for optimizations and target output.
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
 * noTice, this list of conditions and the following disclaimer in
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

#include "intermediate.h"
#include "linker.h"
/****************************
 * CaseArrayTree constructor
 ****************************/
SelectArrayTree::SelectArrayTree(Select* s, IntermediateEmitter* e)
{
  select = s;
  emitter = e;
  std::map<INT64_VALUE, StatementList*> label_statements = select->GetLabelStatements();
  values = new INT64_VALUE[label_statements.size()];
  // map and sort values
  int i = 0;
  std::map<INT64_VALUE, StatementList*>::iterator iter;
  for(iter = label_statements.begin(); iter != label_statements.end(); ++iter) {
    values[i] = iter->first;
    value_label_map[iter->first] = ++emitter->conditional_label;
    i++;
  }
  // map other
  if(select->GetOther()) {
    other_label = ++emitter->conditional_label;
  }
  // create tree
  root = Divide(0, (int)label_statements.size() - 1);
}

/****************************
 * Divides value array
 ****************************/
SelectNode* SelectArrayTree::Divide(int start, int end)
{
  const int size =  end - start + 1;
  if(size < 4) {
    if(size == 2) {
      SelectNode* node = new SelectNode(++emitter->conditional_label, values[start + 1],
                                        new SelectNode(++emitter->conditional_label, values[start]),
                                        new SelectNode(++emitter->conditional_label, values[start + 1]));
      return node;  
    }
    else {
      SelectNode* node = new SelectNode(++emitter->conditional_label, 
                                        values[start + 1], values[start + 2],
                                        new SelectNode(++emitter->conditional_label, values[start]),
                                        new SelectNode(++emitter->conditional_label, values[start + 2]));
      return node;  
    }
  }
  else {
    SelectNode* node;
    const int middle = size / 2 + start;
    if(size % 2 == 0) {
      SelectNode* left = Divide(start, middle - 1);
      SelectNode* right = Divide(middle, end);
      node = new SelectNode(++emitter->conditional_label, 
                            values[middle], left, right);
    }
    else {
      SelectNode* left = Divide(start, middle - 1);
      SelectNode* right = Divide(middle + 1, end);
      node = new SelectNode(++emitter->conditional_label, values[middle], 
                            values[middle], left, right);
    }

    return node;
  }  
}

/****************************
 * Emits code for a case statement
 ****************************/
void SelectArrayTree::Emit()
{
  emitter->cur_line_num = select->GetLineNumber();
      
  emitter->EmitExpression(select->GetAssignment()->GetExpression());
  emitter->imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(select, emitter->cur_line_num, STOR_INT_VAR, 0, LOCL));

  int end_label = ++emitter->unconditional_label;
  Emit(root, end_label);
  // write statements
  std::map<INT64_VALUE, StatementList*> label_statements = select->GetLabelStatements();
  std::map<INT64_VALUE, StatementList*>::iterator iter;
  for(iter = label_statements.begin(); iter != label_statements.end(); ++iter) {
    emitter->imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(select, emitter->cur_line_num, LBL, (long)value_label_map[iter->first]));
    StatementList* statement_list = iter->second;
    std::vector<Statement*> statements = statement_list->GetStatements();
    for(size_t i = 0; i < statements.size(); ++i) {
      emitter->EmitStatement(statements[i]);
    }
    emitter->imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(select, emitter->cur_line_num, JMP, end_label, -1));
  }

  if(select->GetOther()) {
    emitter->imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(select, emitter->cur_line_num, LBL, (long)other_label));
    StatementList* statement_list = select->GetOther();
    std::vector<Statement*> statements = statement_list->GetStatements();
    for(size_t i = 0; i < statements.size(); ++i) {
      emitter->EmitStatement(statements[i]);
    }
    emitter->imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(select, emitter->cur_line_num, JMP, end_label, -1));
  }
  emitter->imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(select, emitter->cur_line_num, LBL, (long)end_label));
}

/****************************
 * Emits code for a case statement
 ****************************/
void SelectArrayTree::Emit(SelectNode* node, int end_label)
{
  if(node != nullptr) {
    SelectNode* left = node->GetLeft();
    SelectNode* right = node->GetRight();
    
    emitter->imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(emitter->cur_line_num, LBL, (long)node->GetId()));
    if(node->GetOperation() == CASE_LESS) {
      const INT64_VALUE value = node->GetValue();
      // evaluate less then
      emitter->imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(emitter->cur_line_num, value));
      emitter->imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(emitter->cur_line_num, LOAD_INT_VAR, 0, LOCL));
      emitter->imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(emitter->cur_line_num, LES_INT));
    } 
    else if(node->GetOperation() == CASE_EQUAL) {
      const INT64_VALUE value = node->GetValue();
      // evaluate equal to
      emitter->imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(emitter->cur_line_num, value));
      emitter->imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(emitter->cur_line_num, LOAD_INT_VAR, 0, LOCL));
      emitter->imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(emitter->cur_line_num, EQL_INT));
      // true
      emitter->imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(emitter->cur_line_num, JMP, value_label_map[value], true));
      // false
      if(select->GetOther()) {
        emitter->imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(emitter->cur_line_num, JMP, other_label, -1));
      } 
      else {
        emitter->imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(emitter->cur_line_num, JMP, end_label, -1));
      }
    } 
    else {
      // evaluate equal to
      const INT64_VALUE value = node->GetValue();
      emitter->imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(emitter->cur_line_num, value));
      emitter->imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(emitter->cur_line_num, LOAD_INT_VAR, 0, LOCL));
      emitter->imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(emitter->cur_line_num, EQL_INT));
      // true
      emitter->imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(emitter->cur_line_num, JMP, value_label_map[value], true));
      // evaluate less then
      emitter->imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(emitter->cur_line_num, node->GetValue2()));
      emitter->imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(emitter->cur_line_num, LOAD_INT_VAR, 0, LOCL));
      emitter->imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(emitter->cur_line_num, LES_INT));
    }

    if(left != nullptr && right != nullptr) {
      emitter->imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(emitter->cur_line_num, JMP, left->GetId(), true));
      emitter->imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(emitter->cur_line_num, JMP, right->GetId(), -1));
    }

    Emit(left, end_label);
    Emit(right, end_label);
  }
}

/****************************
 * Starts the process of
 * translating the parse tree.
 ****************************/
void IntermediateEmitter::Translate()
{
  parsed_program->GetLinker()->ResloveExternalClasses();
  int class_id = 0;
  
#ifndef _SYSTEM
  std::vector<LibraryClass*> lib_classes = parsed_program->GetLinker()->GetAllClasses();
  for(size_t i = 0; i < lib_classes.size(); ++i) {
    LibraryClass* lib_class = lib_classes[i];    
    // find "System.String"
    if(string_cls_id < 0 && lib_class->GetName() == L"System.String") {
      string_cls = lib_class;
      string_cls_id = class_id;
    }
    // assign class ids
    if(is_lib || lib_class->GetCalled()) {
      lib_class->SetId(class_id++);
    }    
  }

  // resolve referenced interfaces
  for(size_t i = 0; i < lib_classes.size(); ++i) {
    LibraryClass* lib_class = lib_classes[i]; 
    std::vector<std::wstring> interface_names = lib_class->GetInterfaceNames();
    for(size_t j = 0; j < interface_names.size(); ++j) {
      LibraryClass* inf_klass = parsed_program->GetLinker()->SearchClassLibraries(interface_names[j], parsed_program->GetLibUses());
      if(inf_klass) {
        lib_class->AddInterfaceId(inf_klass->GetId());
      }
    }
  }
#endif
  
  // process bundles
  std::vector<ParsedBundle*> bundles = parsed_program->GetBundles();
  for(size_t i = 0; i < bundles.size(); ++i) {
    std::vector<Class*> classes = bundles[i]->GetClasses();
    for(size_t j = 0; j < classes.size(); ++j) {
      classes[j]->SetId(class_id++);
    }
  }
  
  // emit strings
  EmitStrings();
  // emit libraries
  EmitLibraries(parsed_program->GetLinker());
  // emit program
  EmitBundles();
  
  imm_program->SetStringClassId(string_cls_id);

  Class* start_class = parsed_program->GetStartClass();
  Method* start_method = parsed_program->GetStartMethod();
  imm_program->SetStartIds((start_class ? start_class->GetId() : -1), (start_method ? start_method->GetId() : -1));
  
#ifdef _DEBUG
  assert(break_labels.empty());
#endif
}

/****************************
 * Translates libraries
 ****************************/
void IntermediateEmitter::EmitLibraries(Linker* linker)
{
  if(linker && !is_lib) {
    // resolve external libraries
    linker->ResolveExternalMethodCalls();
    // write enums
    std::vector<LibraryEnum*> lib_enums = linker->GetAllEnums();
    for(size_t i = 0; i < lib_enums.size(); ++i) {
      imm_program->AddEnum(new IntermediateEnum(lib_enums[i]));
    }
    // write classes
    std::vector<LibraryClass*> lib_classes = linker->GetAllClasses();
    for(size_t i = 0; i < lib_classes.size(); ++i) {
      if(is_lib || lib_classes[i]->GetCalled()) {
        LibraryClass* parent_class = linker->SearchClassLibraries(lib_classes[i]->GetParentName(), parsed_program->GetLibUses());
        imm_program->AddClass(new IntermediateClass(lib_classes[i], parent_class ? parent_class->GetId() : -1));
      }
    }
  }
}

/****************************
 * Emits strings
 ****************************/
void IntermediateEmitter::EmitStrings()
{
  std::vector<std::wstring> char_string_values = parsed_program->GetCharStrings();
  std::vector<IntStringHolder*> int_string_values = parsed_program->GetIntStrings();
  std::vector<BoolStringHolder*> bool_string_values = parsed_program->GetBoolStrings();
  std::vector<ByteStringHolder*> byte_string_values = parsed_program->GetByteStrings();
  std::vector<FloatStringHolder*> float_string_values = parsed_program->GetFloatStrings();
  
  Linker* linker = parsed_program->GetLinker();
  if(linker && !is_lib) {
    std::vector<Library*> libraries = linker->GetAllUsedLibraries();
    
    // create master list of library strings
    std::vector<std::wstring> lib_char_string_values;
    std::vector<IntStringHolder*> lib_int_string_values;
    std::vector<BoolStringHolder*> lib_bool_string_values;
    std::vector<ByteStringHolder*> lib_byte_string_values;
    std::vector<FloatStringHolder*> lib_float_string_values;

    for(size_t i = 0; i < libraries.size(); ++i) {
      Library* library = libraries[i];

      // byte string processing
      std::vector<ByteStringInstruction*> byte_str_insts = library->GetByteStringInstructions();
      for(size_t i = 0; i < byte_str_insts.size(); ++i) {
        // check for duplicates
        bool found = false;

        // byte string processing
        for(size_t j = 0; !found && j < lib_byte_string_values.size(); ++j) {
          if(ByteStringHolderEqual(byte_str_insts[i]->value, lib_byte_string_values[j])) {
            found = true;
          }
        }
        // add string
        if(!found) {
          lib_byte_string_values.push_back(byte_str_insts[i]->value);
        }
      }
      // bool string processing
      std::vector<BoolStringInstruction*> bool_str_insts = library->GetBoolStringInstructions();
      for(size_t i = 0; i < bool_str_insts.size(); ++i) {
        // check for duplicates
        bool found = false;

        // boolean string processing
        for(size_t j = 0; !found && j < lib_bool_string_values.size(); ++j) {
          if(BoolStringHolderEqual(bool_str_insts[i]->value, lib_bool_string_values[j])) {
            found = true;
          }
        }
        // add string
        if(!found) {
          lib_bool_string_values.push_back(bool_str_insts[i]->value);
        }
      }
      // char string processing
      std::vector<CharStringInstruction*> char_str_insts = library->GetCharStringInstructions();
      for(size_t i = 0; i < char_str_insts.size(); ++i) {
        // check for duplicate
        bool found = false;
        for(size_t j = 0; !found && j < lib_char_string_values.size(); ++j) {
          if(char_str_insts[i]->value == lib_char_string_values[j]) {
            found = true;
          }
        }
        // add string
        if(!found) {
          lib_char_string_values.push_back(char_str_insts[i]->value);
        }
      }
      // int string processing
      std::vector<IntStringInstruction*> int_str_insts = library->GetIntStringInstructions();
      for(size_t i = 0; i < int_str_insts.size(); ++i) {
        // check for duplicates
        bool found = false;
        for(size_t j = 0; !found && j < lib_int_string_values.size(); ++j) {
          if(IntStringHolderEqual(int_str_insts[i]->value, lib_int_string_values[j])) {
            found = true;
          }
        }
        // add string
        if(!found) {
          lib_int_string_values.push_back(int_str_insts[i]->value);
        }
      }
      // float string processing
      std::vector<FloatStringInstruction*> float_str_insts = library->GetFloatStringInstructions();
      for(size_t i = 0; i < float_str_insts.size(); ++i) {
        // check for duplicates
        bool found = false;
        for(size_t j = 0; !found && j < lib_float_string_values.size(); ++j) {
          if(FloatStringHolderEqual(float_str_insts[i]->value, lib_float_string_values[j])) {
            found = true;
          }
        }
        // add string
        if(!found) {
          lib_float_string_values.push_back(float_str_insts[i]->value);
        }
      }
    }

    //
    // merge in library strings
    //

    // bool string processing
    for(size_t i = 0; i < lib_bool_string_values.size(); ++i) {
      // check for duplicates
      bool found = false;
      for(size_t j = 0; !found && j < bool_string_values.size(); ++j) {
        if(lib_bool_string_values[i] == bool_string_values[j]) {
          found = true;
        }
      }
      // add string
      if(!found) {
        bool_string_values.push_back(lib_bool_string_values[i]);
      }
    }
    
    // byte string processing
    for(size_t i = 0; i < lib_byte_string_values.size(); ++i) {
      // check for duplicates
      bool found = false;
      for(size_t j = 0; !found && j < byte_string_values.size(); ++j) {
        if(lib_byte_string_values[i] == byte_string_values[j]) {
          found = true;
        }
      }
      // add string
      if(!found) {
        byte_string_values.push_back(lib_byte_string_values[i]);
      }
    }
    
    // char string processing
    for(size_t i = 0; i < lib_char_string_values.size(); ++i) {
      // check for duplicates
      bool found = false;
      for(size_t j = 0; !found && j < char_string_values.size(); ++j) {
        if(lib_char_string_values[i] == char_string_values[j]) {
          found = true;
        }
      }
      // add string
      if(!found) {
        char_string_values.push_back(lib_char_string_values[i]);
      }
    }

    // int string processing
    for(size_t i = 0; i < lib_int_string_values.size(); ++i) {
      // check for duplicates
      bool found = false;
      for(size_t j = 0; !found && j < int_string_values.size(); ++j) {
        if(IntStringHolderEqual(lib_int_string_values[i], int_string_values[j])) {
          found = true;
        }
      }
      // add string
      if(!found) {
        int_string_values.push_back(lib_int_string_values[i]);
      }
    }

    // float string processing
    for(size_t i = 0; i < lib_float_string_values.size(); ++i) {
      // check for duplicates
      bool found = false;
      for(size_t j = 0; !found && j < float_string_values.size(); ++j) {
        if(FloatStringHolderEqual(lib_float_string_values[i], float_string_values[j])) {
          found = true;
        }
      }
      // add string
      if(!found) {
        float_string_values.push_back(lib_float_string_values[i]);
      }
    }
    
    // update indices
    for(size_t i = 0; i < libraries.size(); ++i) {
      Library* library = libraries[i];

      // char string processing
      std::vector<CharStringInstruction*> char_str_insts = library->GetCharStringInstructions();
      for(size_t i = 0; i < char_str_insts.size(); ++i) {
        bool found = false;
        for(size_t j = 0; !found && j < char_string_values.size(); ++j) {
          if(char_str_insts[i]->value == char_string_values[j]) {
            std::vector<LibraryInstr*> instrs = char_str_insts[i]->instrs;
            for(size_t k = 0; k < instrs.size(); ++k) {
              instrs[k]->SetOperand7(j);
            }
            found = true;
          }
        }
#ifdef _DEBUG
        assert(found);
#endif
      }
      // int string processing
      std::vector<IntStringInstruction*> int_str_insts = library->GetIntStringInstructions();
      for(size_t i = 0; i < int_str_insts.size(); ++i) {
        bool found = false;
        for(size_t j = 0; !found && j < int_string_values.size(); ++j) {
          if(IntStringHolderEqual(int_str_insts[i]->value, int_string_values[j])) {
            std::vector<LibraryInstr*> instrs = int_str_insts[i]->instrs;
            for(size_t k = 0; k < instrs.size(); ++k) {
              instrs[k]->SetOperand7(j);
            }
            found = true;
          }
        }
#ifdef _DEBUG
        assert(found);
#endif
      }
      // float string processing
      std::vector<FloatStringInstruction*> float_str_insts = library->GetFloatStringInstructions();
      for(size_t i = 0; i < float_str_insts.size(); ++i) {
        bool found = false;
        for(size_t j = 0; !found && j < float_string_values.size(); ++j) {
          if(FloatStringHolderEqual(float_str_insts[i]->value, float_string_values[j])) {
            std::vector<LibraryInstr*> instrs = float_str_insts[i]->instrs;
            for(size_t k = 0; k < instrs.size(); ++k) {
              instrs[k]->SetOperand7(j);
            }
            found = true;
          }
        }
#ifdef _DEBUG
        assert(found);
#endif
      }
    }
  }
  // set static strings
  imm_program->SetCharStrings(char_string_values);
  imm_program->SetBoolStrings(bool_string_values);
  imm_program->SetByteStrings(byte_string_values);
  imm_program->SetIntStrings(int_string_values);
  imm_program->SetFloatStrings(float_string_values);
}

/****************************
 * Translates bundles
 ****************************/
void IntermediateEmitter::EmitBundles()
{
  // translate program into intermediate form process bundles
  std::vector<std::wstring> bundle_names;
  std::vector<ParsedBundle*> bundles = parsed_program->GetBundles();
  for(size_t i = 0; i < bundles.size(); ++i) {
    parsed_bundle = bundles[i];
    bundle_names.push_back(parsed_bundle->GetName());

    // emit alias
    std::vector<Alias*> aliases = parsed_bundle->GetAliases();
    for(size_t j = 0; j < aliases.size(); ++j) {
      imm_program->AddAliasEncoding(aliases[j]->GetEncodedName());
    }

    // emit enums and consts
    std::vector<Enum*> enums = parsed_bundle->GetEnums();
    for(size_t j = 0; j < enums.size(); ++j) {
      IntermediateEnum* eenum = EmitEnum(enums[j]);
      if(eenum) {
        imm_program->AddEnum(eenum);
      }
    }

    // emit classes
    std::vector<Class*> classes = parsed_bundle->GetClasses();
    for(size_t j = 0; j < classes.size(); ++j) {
      if(is_lib || classes[j]->GetCalled()) {
        IntermediateClass* klass = EmitClass(classes[j]);
        if(klass) {
          imm_program->AddClass(klass);
        }
      }
    }
  }
  imm_program->SetBundleNames(bundle_names);
}

/****************************
 * Translates a enum
 ****************************/
IntermediateEnum* IntermediateEmitter::EmitEnum(Enum* eenum)
{
  cur_line_num = eenum->GetLineNumber();
  
  IntermediateEnum* imm_eenum = new IntermediateEnum(eenum->GetName(), eenum->GetOffset());
  std::map<const std::wstring, EnumItem*>items =  eenum->GetItems();
  // copy items
  std::map<const std::wstring, EnumItem*>::iterator iter;
  for(iter = items.begin(); iter != items.end(); ++iter) {
    imm_eenum->AddItem(new IntermediateEnumItem(iter->second->GetName(), iter->second->GetId()));
  }

  return imm_eenum;
}

/****************************
 * Translates a class
 ****************************/
IntermediateClass* IntermediateEmitter::EmitClass(Class* klass)
{
  cur_line_num = klass->GetLineNumber();
  imm_klass = nullptr;
  
  current_class = klass;
  current_table = current_class->GetSymbolTable();
  current_method = nullptr;
  current_statement = nullptr;
  
  // entries
#ifdef _DEBUG
  GetLogger() << L"---------- Intermediate ---------" << std::endl;
  GetLogger() << L"Class variables (class): name=" << klass->GetName() << std::endl;
#endif
  IntermediateDeclarations* cls_entries = new IntermediateDeclarations;
  int cls_space = CalculateEntrySpace(cls_entries, true);
  
#ifdef _DEBUG
  GetLogger() << L"Class variables (instance): name=" << klass->GetName() << std::endl;
#endif
  IntermediateDeclarations* inst_entries = new IntermediateDeclarations;
  int inst_space = CalculateEntrySpace(inst_entries, false);
  
  // set parent id
  std::wstring parent_name;
  int pid = -1;
  Class* parent = current_class->GetParent();
  if(parent) {
    pid = parent->GetId();
    parent_name = parent->GetName();
  } 
  else {
    LibraryClass* lib_parent = current_class->GetLibraryParent();
    if(lib_parent) {
      pid = lib_parent->GetId();
      parent_name = lib_parent->GetName();
    }
  }

  // process interfaces
  std::vector<int> interface_ids;
  std::vector<Class*> interfaces = current_class->GetInterfaces();
  for(size_t i = 0; i < interfaces.size(); ++i) {
    interface_ids.push_back(interfaces[i]->GetId());
  }
  std::vector<LibraryClass*> lib_interfaces = current_class->GetLibraryInterfaces();
  for(size_t i = 0; i < lib_interfaces.size(); ++i) {
    interface_ids.push_back(lib_interfaces[i]->GetId());
  }

  // get short file name
  const std::wstring &file_name = current_class->GetFileName();
  size_t offset = file_name.rfind(L"/\\");
  std::wstring short_file_name;
  if(offset == std::wstring::npos) {
    short_file_name = file_name;
  }
  else {
    short_file_name = file_name.substr(offset + 1);
  }
  
  imm_klass = new IntermediateClass(current_class->GetId(), current_class->GetName(),  pid, parent_name, interface_ids, current_class->GetInterfaceNames(), 
                                    current_class->IsInterface(), current_class->IsPublic(), current_class->GetGenericStrings(),  current_class->IsVirtual(), 
                                    cls_space, inst_space, cls_entries, inst_entries, short_file_name, is_debug);
  // block
  NewBlock();
  
  // declarations
  std::vector<Statement*> statements = klass->GetStatements();
  for(size_t i = 0; i < statements.size(); ++i) {
    EmitDeclaration(static_cast<Declaration*>(statements[i]));
  }

  // methods
  std::vector<Method*> methods = klass->GetMethods();
  for(size_t i = 0; i < methods.size(); ++i) {
    imm_klass->AddMethod(EmitMethod(methods[i]));
  }

  current_class = nullptr;

  return imm_klass;
}

/****************************
 * Translates a method
 ****************************/
IntermediateMethod* IntermediateEmitter::EmitMethod(Method* method)
{
  cur_line_num = method->GetLineNumber();
  
  current_method = method;
  current_table = current_method->GetSymbolTable();

  // gather information
  IntermediateDeclarations* entries = new IntermediateDeclarations;

#ifdef _DEBUG
  GetLogger() << L"Method variables (local): name=" << method->GetName() << std::endl;
#endif
  int space = CalculateEntrySpace(entries, false);
  if(space > LOCAL_SIZE) {
    const std::wstring error_msg =  current_method->GetFileName() + L':' + 
      ToString(current_method->GetLineNumber()) + L": Local space has been exceeded by " + 
      ToString(space - LOCAL_SIZE) + L" bytes.";
    std::wcerr << error_msg << std::endl;
    exit(1);
  }

  std::vector<Declaration*> declarations = method->GetDeclarations()->GetDeclarations();
  int num_params = 0;
  for(size_t i = 0; i < declarations.size(); ++i) {
    if(declarations[i]->GetEntry()->GetType()->GetType() == frontend::FUNC_TYPE) {
      num_params += 2;
    }
    else {
      num_params++;
    }
  }
  imm_method = new IntermediateMethod(method->GetId(), method->GetEncodedName(), method->IsVirtual(), 
                                      method->HasAndOr(), method->IsLambda(), method->GetEncodedReturn(),  
                                      method->GetMethodType(), method->IsNative(), method->IsStatic(), 
                                      space, num_params, entries, imm_klass);

  if(!method->IsVirtual()) {
    // block
    NewBlock();

    // declarations
    for(int i = (int)declarations.size() - 1; i >= 0; i--) {
      SymbolEntry* entry = declarations[i]->GetEntry();
      if(!entry->IsSelf()) {
        switch(entry->GetType()->GetType()) {
        case frontend::BOOLEAN_TYPE:
        case frontend::BYTE_TYPE:
        case frontend::CHAR_TYPE:
        case frontend::INT_TYPE:
        case frontend::CLASS_TYPE:
          imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, STOR_INT_VAR, entry->GetId(), LOCL));
          break;

        case frontend::FLOAT_TYPE:
          if(entry->GetType()->GetDimension() > 0) {
            imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, STOR_INT_VAR, entry->GetId(), LOCL));
          } 
          else {
            imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, STOR_FLOAT_VAR, entry->GetId(), LOCL));
          }
          break;
    
        case frontend::FUNC_TYPE:
          imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, STOR_FUNC_VAR, entry->GetId(), LOCL));
          break;

        default:
          break;
        }
      }
    }

    // statements
    bool end_return = false;
    StatementList* statement_list = method->GetStatements();
    if(statement_list) {
      std::vector<Statement*> statements = statement_list->GetStatements();
      for(size_t i = 0; i < statements.size(); ++i) {
        EmitStatement(statements[i]);
      }
      // check to see if the last statement was a return statement
      if(statements.size() > 0 && statements.back()->GetStatementType() == RETURN_STMT) {
        end_return = true;
      }
    }

    // return instance if this is constructor call
    if(!method->IsAlt() && (method->GetMethodType() == NEW_PUBLIC_METHOD ||
          method->GetMethodType() == NEW_PRIVATE_METHOD)) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INST_MEM));
    }

    if(method->IsAlt()) {
      Method* original = method->GetOriginal();
      std::vector<Declaration*> original_params = original->GetDeclarations()->GetDeclarations();

      const int diff_offset = original->HasAndOr() ? 1 : 0;
      for(size_t i = 0; i < original_params.size(); ++i) {
        SymbolEntry* var_entry = original_params[i]->GetEntry();
        switch(var_entry->GetType()->GetType()) {
        case frontend::BOOLEAN_TYPE:
        case frontend::BYTE_TYPE:
        case frontend::CHAR_TYPE:
        case frontend::INT_TYPE:
        case frontend::CLASS_TYPE:
          imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_VAR, var_entry->GetId() - diff_offset, LOCL));
          break;

        case frontend::FLOAT_TYPE:
          if(var_entry->GetType()->GetDimension() > 0) {
            imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_VAR, var_entry->GetId() - diff_offset, LOCL));
          }
          else {
            imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_FLOAT_VAR, var_entry->GetId() - diff_offset, LOCL));
          }
          break;

        case frontend::FUNC_TYPE:
          imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_FUNC_VAR, var_entry->GetId() - diff_offset, LOCL));
          break;

        default:
          break;
        }
      }

      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INST_MEM));

      if(is_lib) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LIB_MTHD_CALL, 0, original->GetClass()->GetName(), original->GetEncodedName()));
      }
      else {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, MTHD_CALL, original->GetClass()->GetId(), original->GetId(), 0L));
      }
    }

    // add return statement if one hasn't been added
    if(!end_return) {
      if(method->GetLeaving()) {
        std::vector<Statement*> leavings = method->GetLeaving()->GetStatements()->GetStatements();
        for(size_t i = 0; i < leavings.size(); ++i) {
          EmitStatement(leavings[i]);
        }
      }
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, RTRN));
    }
  }
  
  current_method = nullptr;
  current_table = nullptr;

  return imm_method;
}

/****************************
 * Translates a lambda expression
 ****************************/
void IntermediateEmitter::EmitLambda(Lambda* lambda)
{
  long closure_space = 0;
  IntermediateDeclarations* closure_dclrs = new IntermediateDeclarations;
  std::vector<std::pair<SymbolEntry*, SymbolEntry*> > closure_copies = lambda->GetClosures();
  for(size_t i = 0; i < closure_copies.size(); ++i) {
    SymbolEntry* entry = closure_copies[i].second;
    switch(entry->GetType()->GetType()) {
    case frontend::BOOLEAN_TYPE:
      if(entry->GetType()->GetDimension() > 0) {
#ifdef _DEBUG
        GetLogger() << L"\t" << entry->GetId() << L": INT_ARY_PARM: name=" << entry->GetName()
          << L", dim=" << entry->GetType()->GetDimension() << std::endl;
#endif
        closure_dclrs->AddParameter(new IntermediateDeclaration(entry->GetName(), INT_ARY_PARM));
      }
      else {
#ifdef _DEBUG
        GetLogger() << L"\t" << entry->GetId() << L": INT_PARM: name=" << entry->GetName() << std::endl;
#endif
        closure_dclrs->AddParameter(new IntermediateDeclaration(entry->GetName(), INT_PARM));
      }
      closure_space++;
      break;

    case frontend::BYTE_TYPE:
      if(entry->GetType()->GetDimension() > 0) {
#ifdef _DEBUG
        GetLogger() << L"\t" << entry->GetId() << L": BYTE_ARY_PARM: name=" << entry->GetName() << std::endl;
#endif
        closure_dclrs->AddParameter(new IntermediateDeclaration(entry->GetName(), BYTE_ARY_PARM));
      }
      else {
#ifdef _DEBUG
        GetLogger() << L"\t" << entry->GetId() << L": INT_PARM: name=" << entry->GetName() << std::endl;
#endif
        closure_dclrs->AddParameter(new IntermediateDeclaration(entry->GetName(), INT_PARM));
      }
      closure_space++;
      break;

    case frontend::INT_TYPE:
      if(entry->GetType()->GetDimension() > 0) {
#ifdef _DEBUG
        GetLogger() << L"\t" << entry->GetId() << L": INT_ARY_PARM: name=" << entry->GetName()
          << L", dim=" << entry->GetType()->GetDimension() << std::endl;
#endif
        closure_dclrs->AddParameter(new IntermediateDeclaration(entry->GetName(), INT_ARY_PARM));
      }
      else {
#ifdef _DEBUG
        GetLogger() << L"\t" << entry->GetId() << L": INT_PARM: name=" << entry->GetName() << std::endl;
#endif
        closure_dclrs->AddParameter(new IntermediateDeclaration(entry->GetName(), INT_PARM));
      }
      closure_space++;
      break;

    case frontend::CHAR_TYPE:
      if(entry->GetType()->GetDimension() > 0) {
#ifdef _DEBUG
        GetLogger() << L"\t" << entry->GetId() << L": CHAR_ARY_PARM: name=" << entry->GetName() << std::endl;
#endif
        closure_dclrs->AddParameter(new IntermediateDeclaration(entry->GetName(), CHAR_ARY_PARM));
      }
      else {
#ifdef _DEBUG
        GetLogger() << L"\t" << entry->GetId() << L": CHAR_PARM: name=" << entry->GetName() << std::endl;
#endif
        closure_dclrs->AddParameter(new IntermediateDeclaration(entry->GetName(), CHAR_PARM));
      }
      closure_space++;
      break;

    case frontend::CLASS_TYPE:
      // object array
      if(entry->GetType()->GetDimension() > 0) {
        if(parsed_program->GetClass(entry->GetType()->GetName())) {
#ifdef _DEBUG
          GetLogger() << L"\t" << entry->GetId() << L": OBJ_ARY_PARM: name=" << entry->GetName() << std::endl;
#endif
          closure_dclrs->AddParameter(new IntermediateDeclaration(entry->GetName(), OBJ_ARY_PARM));
        }
        else if(SearchProgramEnums(entry->GetType()->GetName())) {
#ifdef _DEBUG
          GetLogger() << L"\t" << entry->GetId() << L": INT_ARY_PARM: name=" << entry->GetName() << std::endl;
#endif
          closure_dclrs->AddParameter(new IntermediateDeclaration(entry->GetName(), INT_ARY_PARM));
        }
        else if(SearchProgramEnums(current_class->GetName() + L"#" + entry->GetType()->GetName())) {
#ifdef _DEBUG
          GetLogger() << L"\t" << entry->GetId() << L": INT_ARY_PARM: name=" << entry->GetName() << std::endl;
#endif
          closure_dclrs->AddParameter(new IntermediateDeclaration(entry->GetName(), INT_PARM));
        }
        else if(parsed_program->GetLinker()->SearchEnumLibraries(entry->GetType()->GetName(), parsed_program->GetLibUses())) {
#ifdef _DEBUG
          GetLogger() << L"\t" << entry->GetId() << L": INT_ARY_PARM: name=" << entry->GetName() << std::endl;
#endif
          closure_dclrs->AddParameter(new IntermediateDeclaration(entry->GetName(), INT_ARY_PARM));
        }
        else {
#ifdef _DEBUG
          GetLogger() << L"\t" << entry->GetId() << L": OBJ_ARY_PARM: name=" << entry->GetName() << std::endl;
#endif
          closure_dclrs->AddParameter(new IntermediateDeclaration(entry->GetName(), OBJ_ARY_PARM));
        }
      }
      // object
      else {
        if(SearchProgramClasses(entry->GetType()->GetName())) {
#ifdef _DEBUG
          GetLogger() << L"\t" << entry->GetId() << L": OBJ_PARM: name=" << entry->GetName() << std::endl;
#endif
          closure_dclrs->AddParameter(new IntermediateDeclaration(entry->GetName(), OBJ_PARM));
        }
        else if(SearchProgramEnums(entry->GetType()->GetName())) {
#ifdef _DEBUG
          GetLogger() << L"\t" << entry->GetId() << L": INT_PARM: name=" << entry->GetName() << std::endl;
#endif
          closure_dclrs->AddParameter(new IntermediateDeclaration(entry->GetName(), INT_PARM));
        }
        else if(SearchProgramEnums(current_class->GetName() + L"#" + entry->GetType()->GetName())) {
#ifdef _DEBUG
          GetLogger() << L"\t" << entry->GetId() << L": INT_PARM: name=" << entry->GetName() << std::endl;
#endif
          closure_dclrs->AddParameter(new IntermediateDeclaration(entry->GetName(), INT_PARM));
        }
        else if(parsed_program->GetLinker()->SearchEnumLibraries(entry->GetType()->GetName(), parsed_program->GetLibUses())) {
#ifdef _DEBUG
          GetLogger() << L"\t" << entry->GetId() << L": INT_PARM: name=" << entry->GetName() << std::endl;
#endif
          closure_dclrs->AddParameter(new IntermediateDeclaration(entry->GetName(), INT_PARM));
        }
        else {
#ifdef _DEBUG
          GetLogger() << L"\t" << entry->GetId() << L": OBJ_PARM: name=" << entry->GetName() << std::endl;
#endif
          closure_dclrs->AddParameter(new IntermediateDeclaration(entry->GetName(), OBJ_PARM));
        }
      }
      closure_space++;
      break;

    case frontend::FLOAT_TYPE:
      if(entry->GetType()->GetDimension() > 0) {
#ifdef _DEBUG
        GetLogger() << L"\t" << entry->GetId() << L": FLOAT_ARY_PARM: name=" << entry->GetName() << std::endl;
#endif
        closure_dclrs->AddParameter(new IntermediateDeclaration(entry->GetName(), FLOAT_ARY_PARM));
        closure_space++;
      }
      else {
#ifdef _DEBUG
        GetLogger() << L"\t" << entry->GetId() << L": FLOAT_PARM: name=" << entry->GetName() << std::endl;
#endif
        closure_dclrs->AddParameter(new IntermediateDeclaration(entry->GetName(), FLOAT_PARM));
        closure_space++;
      }
      break;

    case frontend::FUNC_TYPE:
#ifdef _DEBUG
      GetLogger() << L"\t" << entry->GetId() << L": FUNC_PARM: name=" << entry->GetName() << std::endl;
#endif
      closure_dclrs->AddParameter(new IntermediateDeclaration(entry->GetName(), FUNC_PARM));
      closure_space += 2;
      break;

    default:
      break;
    }
  }
  closure_space *= sizeof(INT64_VALUE);

  // add closure
  MethodCall* method_call = lambda->GetMethodCall();
  imm_klass->AddClosureDeclarations(lambda->GetMethod()->GetEncodedName(), method_call->GetMethod()->GetId(), closure_dclrs);

  // allocate closure space and copy variables
  if(closure_space > 0) {
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, lambda, cur_line_num, NEW_FUNC_INST, closure_space));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, lambda, cur_line_num, STOR_INT_VAR, 0, LOCL));

    for(size_t i = 0; i < closure_copies.size(); ++i) {
      std::pair<SymbolEntry*, SymbolEntry*> copy = closure_copies[i];
      SymbolEntry* var_entry = copy.first;
      SymbolEntry* capture_entry = copy.second;

      // memory context
      MemoryContext mem_context;
      if(capture_entry->IsLocal()) {
        mem_context = LOCL;
      }
      else if(capture_entry->IsStatic()) {
        mem_context = CLS;
      }
      else {
        mem_context = INST;
      }

      if(mem_context == INST) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, lambda, cur_line_num, LOAD_INST_MEM));
      }
      else if(mem_context == CLS) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, lambda, cur_line_num, LOAD_CLS_MEM));
      }

      switch(capture_entry->GetType()->GetType()) {
      case frontend::BOOLEAN_TYPE:
      case frontend::BYTE_TYPE:
      case frontend::CHAR_TYPE:
      case frontend::INT_TYPE:
      case frontend::CLASS_TYPE:
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, lambda, cur_line_num, LOAD_INT_VAR, capture_entry->GetId(), mem_context));
        break;
      case frontend::FLOAT_TYPE:
        if(capture_entry->GetType()->GetDimension() > 0) {
          imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, lambda, cur_line_num, LOAD_INT_VAR, capture_entry->GetId(), mem_context));
        }
        else {
          imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, lambda, cur_line_num, LOAD_FLOAT_VAR, capture_entry->GetId(), mem_context));
        }
        break;
      case frontend::FUNC_TYPE:
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, lambda, cur_line_num, LOAD_FUNC_VAR, capture_entry->GetId(), mem_context));
        break;
      default:
        break;
      }

      // memory context
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, lambda, cur_line_num, LOAD_INT_VAR, 0, LOCL));

      switch(var_entry->GetType()->GetType()) {
      case frontend::BOOLEAN_TYPE:
      case frontend::BYTE_TYPE:
      case frontend::CHAR_TYPE:
      case frontend::INT_TYPE:
      case frontend::CLASS_TYPE:
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, lambda, cur_line_num, STOR_INT_VAR, var_entry->GetId(), INST));
        break;
      case frontend::FLOAT_TYPE:
        if(var_entry->GetType()->GetDimension() > 0) {
          imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, lambda, cur_line_num, STOR_INT_VAR, var_entry->GetId(), INST));
        }
        else {
          imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, lambda, cur_line_num, STOR_FLOAT_VAR, var_entry->GetId(), INST));
        }
        break;
      case frontend::FUNC_TYPE:
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, lambda, cur_line_num, STOR_FUNC_VAR, var_entry->GetId(), INST));
        break;
      default:
        break;
      }
    }

    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, lambda, cur_line_num, LOAD_INT_VAR, 0, LOCL));
  }
  // no closures
  else {
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(current_statement, lambda, cur_line_num, 0L));
  }

  EmitMethodCallExpression(method_call, false, true);
}

/****************************
 * Translates a statement
 ****************************/
void IntermediateEmitter::EmitStatement(Statement* statement)
{
  cur_line_num = statement->GetLineNumber();

  current_statement = statement;
  
  switch(statement->GetStatementType()) {
  case DECLARATION_STMT: {
    Declaration* declaration = static_cast<Declaration*>(statement);
    if(declaration->GetChild()) {
      // build stack declarations
      std::stack<Declaration*> declarations;
      while(declaration) {
        declarations.push(declaration);
        declaration = declaration->GetChild();
      }
      // process declarations
      while(!declarations.empty()) {
        EmitDeclaration(declarations.top());
        declarations.pop();
      }
    }
    else {
      EmitDeclaration(static_cast<Declaration*>(statement));
    }
  }
    break;
    
  case METHOD_CALL_STMT:
    EmitMethodCallStatement(static_cast<MethodCall*>(statement));
    break;

  case ADD_ASSIGN_STMT:
    if(static_cast<OperationAssignment*>(statement)->IsStringConcat()) {  
      EmitStringConcat(static_cast<OperationAssignment*>(statement));
      new_char_str_count = 0;
    }
    else {
      EmitAssignment(static_cast<Assignment*>(statement));
    }
    break;
    
  case ASSIGN_STMT: {
    Assignment* assignment = static_cast<Assignment*>(statement);
    if(assignment->GetChild()) {
      // build stack assignments
      std::stack<Assignment*> assignments;
      while(assignment) {
        assignments.push(assignment);
        assignment = assignment->GetChild();
      }
      // process assignments
      while(!assignments.empty()) {
        EmitAssignment(assignments.top());
        assignments.pop();
      }
    }
    else {
      EmitAssignment(assignment);
    }
  }
    break;

  case SUB_ASSIGN_STMT:
  case MUL_ASSIGN_STMT:
  case DIV_ASSIGN_STMT:
    EmitAssignment(static_cast<Assignment*>(statement));
    break;
    
  case SIMPLE_STMT:
    EmitExpression(static_cast<SimpleStatement*>(statement)->GetExpression());
    break;
    
  case RETURN_STMT: {
    Expression* expression = static_cast<Return*>(statement)->GetExpression();
    if(expression) {
      EmitExpression(expression);
    }
    // post statements
    if(!post_statements.empty()) {
      EmitAssignment(post_statements.front());
      post_statements.pop();
    }
    // leaving
    if(current_method->GetLeaving()) {
      std::vector<Statement*> leavings = current_method->GetLeaving()->GetStatements()->GetStatements();
      for(size_t i = 0; i < leavings.size(); ++i) {
        EmitStatement(leavings[i]);
      }
    }
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, RTRN));
    new_char_str_count = 0;
  }
    break;
    
  case IF_STMT:
    EmitIf(static_cast<If*>(statement));
    break;

  case SELECT_STMT:
    EmitSelect(static_cast<Select*>(statement));
    break;

  case DO_WHILE_STMT:
    EmitDoWhile(static_cast<DoWhile*>(statement));
    break;
    
  case WHILE_STMT:
    EmitWhile(static_cast<While*>(statement));
    break;

  case FOR_STMT:
    EmitFor(static_cast<For*>(statement));
    break;

  case BREAK_STMT: {
    const std::pair<int, int> break_continue_label = break_labels.top();
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, JMP, break_continue_label.first, -1));
  }
    break;

  case CONTINUE_STMT: {
    const std::pair<int, int> break_continue_label = break_labels.top();
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, JMP, break_continue_label.second, -1));
  }
    break;

  case CRITICAL_STMT:
    EmitCriticalSection(static_cast<CriticalSection*>(statement));
    break;
    
  case SYSTEM_STMT:
    EmitSystemDirective(static_cast<SystemStatement*>(statement));
    break;

  case LEAVING_STMT:
  case EMPTY_STMT:
    break;
    
  default:
    std::wcerr << L"internal error" << std::endl;
    exit(1);
  }
  
  if(!post_statements.empty()) {
    EmitAssignment(post_statements.front());
    post_statements.pop();
  }

  current_statement = nullptr;
}

/****************************
 * Translates a nested method 
 * call
 ****************************/
void IntermediateEmitter::EmitMethodCallStatement(MethodCall* method_call)
{
  // find end of nested call
  if(method_call->IsFunctionDefinition()) {
    if(method_call->GetMethod()) {
      if(!method_call->GetMethod()->IsLambda()) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(static_cast<Statement*>(method_call), cur_line_num, 0L));
      }

      if(is_lib) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(static_cast<Statement*>(method_call), cur_line_num, LIB_FUNC_DEF, -1,
                                                                                   method_call->GetMethod()->GetClass()->GetName(),
                                                                                   method_call->GetMethod()->GetEncodedName()));
      }
      else {
        const int16_t method_id = method_call->GetMethod()->GetId();
        const int16_t cls_id = method_call->GetMethod()->GetClass()->GetId();
        const int method_cls_id = (cls_id << 16) | method_id;
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(static_cast<Statement*>(method_call), cur_line_num, method_cls_id));
      }
    }
    else {
      if(!method_call->GetLibraryMethod()->IsLambda()) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(static_cast<Statement*>(method_call), cur_line_num, 0L));
      }

      if(is_lib) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(static_cast<Statement*>(method_call), cur_line_num, LIB_FUNC_DEF, -1,
                                                                                   method_call->GetLibraryMethod()->GetLibraryClass()->GetName(),
                                                                                   method_call->GetLibraryMethod()->GetName()));
      }
      else {
        const int16_t method_id = method_call->GetLibraryMethod()->GetId();
        const int16_t cls_id = method_call->GetLibraryMethod()->GetLibraryClass()->GetId();
        const int method_cls_id = (cls_id << 16) | method_id;
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(static_cast<Statement*>(method_call), cur_line_num, method_cls_id));
      }
    }
  }
  else if(method_call->IsFunctionalCall()) {
    MethodCall* tail = method_call;
    while(tail->GetMethodCall()) {
      tail = tail->GetMethodCall();
    }

    // emit parameters for nested call
    MethodCall* temp = tail;
    while(temp) {
      EmitMethodCallParameters(temp);
      // update
      temp = static_cast<MethodCall*>(temp->GetPreviousExpression());
    }

    // emit function variable
    MemoryContext mem_context;
    SymbolEntry* entry = method_call->GetFunctionalEntry();
    if(entry->IsLocal()) {
      mem_context = LOCL;
    } 
    else if(entry->IsStatic()) {
      mem_context = CLS;
    } 
    else {
      mem_context = INST;
    }
    
    // emit the correct self variable
    if(mem_context == INST) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(static_cast<Statement*>(method_call), cur_line_num, LOAD_INST_MEM));
    } 
    else if(mem_context == CLS) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(static_cast<Statement*>(method_call), cur_line_num, LOAD_CLS_MEM));
    }      
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(static_cast<Statement*>(method_call), cur_line_num, LOAD_FUNC_VAR, entry->GetId(), mem_context));

    // emit dynamic call
    switch(method_call->GetRougeReturn()) {
    case instructions::INT_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(static_cast<Statement*>(method_call), cur_line_num, DYN_MTHD_CALL, entry->GetType()->GetFunctionParameterCount(), instructions::INT_TYPE));
      if(!method_call->GetMethodCall()) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(static_cast<Statement*>(method_call), cur_line_num, POP_INT));
      }
      break;
  
    case instructions::FLOAT_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(static_cast<Statement*>(method_call), cur_line_num, DYN_MTHD_CALL, entry->GetType()->GetFunctionParameterCount(), instructions::FLOAT_TYPE));
      if(!method_call->GetMethodCall()) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(static_cast<Statement*>(method_call), cur_line_num, POP_FLOAT));
      }
      break;
  
    case instructions::FUNC_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(static_cast<Statement*>(method_call), cur_line_num, DYN_MTHD_CALL, entry->GetType()->GetFunctionParameterCount(), instructions::FUNC_TYPE));
      if(!method_call->GetMethodCall()) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(static_cast<Statement*>(method_call), cur_line_num, POP_INT));
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(static_cast<Statement*>(method_call), cur_line_num, POP_INT));  
      }
      break;
  
    default:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(static_cast<Statement*>(method_call), cur_line_num, DYN_MTHD_CALL, entry->GetType()->GetFunctionParameterCount(), instructions::NIL_TYPE));
      break;
    }

    // check nesting
    Type* nested_type = entry->GetType();
    if(nested_type->GetType() == frontend::FUNC_TYPE) {
      nested_type = nested_type->GetFunctionReturn();
    }
    bool is_nested = method_call->GetMethodCall() && nested_type->GetType() == CLASS_TYPE;

    // emit nested method calls
    method_call = method_call->GetMethodCall();
    while(method_call) {
      EmitMethodCall(method_call, is_nested);
      EmitCast(method_call);
      
      // pop return value if not used
      if(!method_call->GetMethodCall()) {
        switch(method_call->GetRougeReturn()) {
        case instructions::INT_TYPE:
          imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(static_cast<Statement*>(method_call), cur_line_num, POP_INT));
          break;

        case instructions::FLOAT_TYPE:
          imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(static_cast<Statement*>(method_call), cur_line_num, POP_FLOAT));
          break;

        case instructions::FUNC_TYPE:
          imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(static_cast<Statement*>(method_call), cur_line_num, POP_INT));
          imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(static_cast<Statement*>(method_call), cur_line_num, POP_INT));
          break;

        default:
          break;
        }
      }
      // next call
      if(method_call->GetMethod()) {
        Method* method = method_call->GetMethod();
        if(method->GetReturn()->GetType() == CLASS_TYPE) {
          bool is_enum = parsed_program->GetLinker()->SearchEnumLibraries(method->GetReturn()->GetName(), parsed_program->GetLibUses()) || 
            SearchProgramEnums(method->GetReturn()->GetName());
          if(!is_enum) {
            is_nested = true;
          }
          else {
            is_nested = false;
          }
        } 
        else {
          is_nested = false;
        }
      } 
      else if(method_call->GetLibraryMethod()) {
        LibraryMethod* lib_method = method_call->GetLibraryMethod();
        if(lib_method->GetReturn()->GetType() == CLASS_TYPE) {
          bool is_enum = parsed_program->GetLinker()->SearchEnumLibraries(lib_method->GetReturn()->GetName(), parsed_program->GetLibUses()) || 
            SearchProgramEnums(lib_method->GetReturn()->GetName());
          if(!is_enum) {
            is_nested = true;
          }
          else {
            is_nested = false;
          }
        } 
        else {
          is_nested = false;
        }
      } 
      else {
        is_nested = false;
      }
      method_call = method_call->GetMethodCall();
    } 
  }
  else {
    MethodCall* tail = method_call;
    while(tail->GetMethodCall()) {
      tail = tail->GetMethodCall();
    }
    
    // emit parameters for nested call
    MethodCall* temp = tail;
    while(temp) {
      EmitMethodCallParameters(temp);
      // update
      temp = static_cast<MethodCall*>(temp->GetPreviousExpression());
    }

    // emit method calls
    bool is_nested = false;
    do {
      EmitMethodCall(method_call, is_nested);
      EmitCast(method_call);
      
      // pop return value if not used
      if(!method_call->GetMethodCall()) {
        switch(method_call->GetRougeReturn()) {
        case instructions::INT_TYPE:
          imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(static_cast<Statement*>(method_call), cur_line_num, POP_INT));
          break;

        case instructions::FLOAT_TYPE:
          imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(static_cast<Statement*>(method_call), cur_line_num, POP_FLOAT));
          break;

        case instructions::FUNC_TYPE:
          imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(static_cast<Statement*>(method_call), cur_line_num, POP_INT));
          imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(static_cast<Statement*>(method_call), cur_line_num, POP_INT));
          break;

        default:
          break;
        }
      }

      // next call
      if(method_call->GetMethod()) {
        Method* method = method_call->GetMethod();
        if(method->GetReturn()->GetType() == CLASS_TYPE) {
          bool is_enum = parsed_program->GetLinker()->SearchEnumLibraries(method->GetReturn()->GetName(), parsed_program->GetLibUses()) || 
            SearchProgramEnums(method->GetReturn()->GetName());
          if(!is_enum) {
            is_nested = true;
          }
          else {
            is_nested = false;
          }
        } 
        else {
          is_nested = false;
        }
      }
      else if(method_call->GetLibraryMethod()) {
        LibraryMethod* lib_method = method_call->GetLibraryMethod();
        if(lib_method->GetReturn()->GetType() == CLASS_TYPE) {
          bool is_enum = parsed_program->GetLinker()->SearchEnumLibraries(lib_method->GetReturn()->GetName(), parsed_program->GetLibUses()) || 
            SearchProgramEnums(lib_method->GetReturn()->GetName());
          if(!is_enum) {
            is_nested = true;
          }
          else {
            is_nested = false;
          }
        } 
        else {
          is_nested = false;
        }
      } 
      else {
        is_nested = false;
      }
      
      // class cast
      if(!method_call->GetVariable()) {
        EmitClassCast(method_call);
      }
      
      // update
      method_call = method_call->GetMethodCall();
    } 
    while(method_call);
  }
}

/****************************
 * Translates a system
 * directive. Only used in
 * bootstrap.
 ****************************/
void IntermediateEmitter::EmitSystemDirective(SystemStatement* statement)
{
  cur_line_num = statement->GetLineNumber();
  
  switch(statement->GetId()) {
  case ASSERT_TRUE:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::ASSERT_TRUE));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;
    
  case EXIT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::EXIT));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;
    
    //----------- copy instructions -----------
  case CPY_BYTE_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 3, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 4, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, CPY_BYTE_ARY));
    break;

  case CPY_CHAR_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 3, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 4, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, CPY_CHAR_ARY));
    break;
    
  case CPY_INT_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 3, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 4, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, CPY_INT_ARY));
    break;
    
  case CPY_FLOAT_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 3, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 4, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, CPY_FLOAT_ARY));
    break;

  case instructions::ZERO_BYTE_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, ZERO_BYTE_ARY));
    break;

  case instructions::ZERO_CHAR_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, ZERO_CHAR_ARY));
    break;

  case instructions::ZERO_INT_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, ZERO_INT_ARY));
    break;

  case instructions::ZERO_FLOAT_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, ZERO_FLOAT_ARY));
    break;
    
    //----------- math instructions -----------
  case instructions::S2I:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, INST));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, S2I));
    break;
    
  case instructions::S2F:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, INST));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, S2F));
    break;

  case instructions::I2S:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, I2S));
    break;
    
  case instructions::F2S:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_FLOAT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, F2S));
    break;

  case instructions::LOAD_INST_UID:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    break;
    
  case instructions::LOAD_ARY_SIZE:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_ARY_SIZE));
    break;
    
  case instructions::FLOR_FLOAT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_FLOAT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, FLOR_FLOAT));
    break;
    
  case instructions::CEIL_FLOAT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_FLOAT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, CEIL_FLOAT));
    break;

  case instructions::TRUNC_FLOAT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_FLOAT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRUNC_FLOAT));
    break;

  case instructions::SQRT_FLOAT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_FLOAT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, SQRT_FLOAT));
    break;

  case instructions::GAMMA_FLOAT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_FLOAT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, GAMMA_FLOAT));
    break;

  case instructions::NAN_INT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, NAN_INT));
    break;

  case instructions::INF_INT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, INF_INT));
    break;

  case instructions::NEG_INF_INT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, NEG_INF_INT));
    break;

  case instructions::NAN_FLOAT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, NAN_FLOAT));
    break;

  case instructions::INF_FLOAT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, INF_FLOAT));
    break;

  case instructions::NEG_INF_FLOAT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, NEG_INF_FLOAT));
    break;

  case instructions::SIN_FLOAT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_FLOAT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, SIN_FLOAT));
    break;
    
  case instructions::COS_FLOAT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_FLOAT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, COS_FLOAT));
    break;
    
  case instructions::TAN_FLOAT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_FLOAT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TAN_FLOAT));
    break;
    
  case instructions::ASIN_FLOAT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_FLOAT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, ASIN_FLOAT));
    break;
    
  case instructions::ACOS_FLOAT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_FLOAT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, ACOS_FLOAT));
    break;
    
  case instructions::ATAN_FLOAT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_FLOAT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, ATAN_FLOAT));
    break;

  case instructions::LOG2_FLOAT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_FLOAT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOG2_FLOAT));
    break;

  case instructions::CBRT_FLOAT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_FLOAT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, CBRT_FLOAT));
    break;
    
  case instructions::ATAN2_FLOAT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_FLOAT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_FLOAT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, ATAN2_FLOAT));
    break;

  case instructions::ACOSH_FLOAT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_FLOAT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, ACOSH_FLOAT));
    break;

  case instructions::ASINH_FLOAT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_FLOAT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, ASINH_FLOAT));
    break;

  case instructions::ATANH_FLOAT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_FLOAT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, ATANH_FLOAT));
    break;

  case instructions::COSH_FLOAT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_FLOAT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, COSH_FLOAT));
    break;

  case instructions::SINH_FLOAT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_FLOAT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, SINH_FLOAT));
    break;

  case instructions::TANH_FLOAT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_FLOAT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TANH_FLOAT));
    break;

  case instructions::MOD_FLOAT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_FLOAT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_FLOAT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, MOD_FLOAT));
    break;
    
  case instructions::LOG_FLOAT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_FLOAT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOG_FLOAT));
    break;

  case instructions::ROUND_FLOAT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_FLOAT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, ROUND_FLOAT));
    break;

  case instructions::EXP_FLOAT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_FLOAT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, EXP_FLOAT));
    break;

  case instructions::LOG10_FLOAT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_FLOAT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOG10_FLOAT));
    break;

  case instructions::RAND_FLOAT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, RAND_FLOAT));
    break;
    
  case instructions::POW_FLOAT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_FLOAT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_FLOAT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, POW_FLOAT));
    break;
        
  case ASYNC_MTHD_CALL:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, ASYNC_MTHD_CALL, -1, 1L, -1L));
    break;
    
  case EXT_LIB_LOAD:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, EXT_LIB_LOAD));
    break;

  case EXT_LIB_UNLOAD:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, EXT_LIB_UNLOAD));
    break;
    
  case EXT_LIB_FUNC_CALL:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, EXT_LIB_FUNC_CALL));
    break;
    
  case THREAD_MUTEX:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, THREAD_MUTEX));
    break;
    
  case THREAD_JOIN:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, THREAD_JOIN));
    break;
    
  case THREAD_SLEEP:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, THREAD_SLEEP));
    break;
    
  case CRITICAL_START:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, CRITICAL_START));
    break;
    
  case CRITICAL_END:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, CRITICAL_END));
    break;
    
    /////////////////////////////////////////
    // -------------- traps -------------- //
    /////////////////////////////////////////
  case instructions::LOAD_MULTI_ARY_SIZE:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::LOAD_MULTI_ARY_SIZE));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP_RTRN, 2L));
    break;   
    
  case instructions::BYTES_TO_UNICODE:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::BYTES_TO_UNICODE));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP_RTRN, 2L));
    break;
    
  case instructions::UNICODE_TO_BYTES:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::UNICODE_TO_BYTES));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP_RTRN, 2L));
    break;

  case instructions::LOAD_NEW_OBJ_INST:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::LOAD_NEW_OBJ_INST));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP_RTRN, 2L));
    break;

  case instructions::LOAD_CLS_INST_ID:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::LOAD_CLS_INST_ID));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP_RTRN, 2L));
    break;

  case instructions::STRING_HASH_ID:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::STRING_HASH_ID));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP_RTRN, 2L));
    break;

  case instructions::LOAD_CLS_BY_INST:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::LOAD_CLS_BY_INST));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP_RTRN, 1L));
    break;
    
  case SYS_TIME:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SYS_TIME));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 1L));
    break;

  case GMT_TIME:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::GMT_TIME));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 1L));
    break;
    
  case instructions::DATE_TIME_SET_1:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 3, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::DATE_TIME_SET_1));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 6L));
    break;
    
  case instructions::DATE_TIME_SET_2:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 3, LOCL));    
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 4, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 5, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 6, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::DATE_TIME_SET_2));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 9L));
    break;
    
  case instructions::DATE_TIME_ADD_DAYS:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::DATE_TIME_ADD_DAYS));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;
    
  case instructions::DATE_TIME_ADD_HOURS:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::DATE_TIME_ADD_HOURS));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;
    
  case instructions::DATE_TIME_ADD_MINS:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::DATE_TIME_ADD_MINS));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;

  case instructions::DATE_TIME_ADD_SECS:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::DATE_TIME_ADD_SECS));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;

  case instructions::DATE_FROM_UNIX_GMT_TIME:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::DATE_FROM_UNIX_GMT_TIME));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;

  case instructions::DATE_FROM_UNIX_LOCAL_TIME:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::DATE_FROM_UNIX_LOCAL_TIME));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;
    
  case instructions::DATE_TO_UNIX_TIME:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::DATE_TO_UNIX_TIME));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;
    
  case GET_PLTFRM:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::GET_PLTFRM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 1L));
    break;

  case GET_UUID:
     imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::GET_UUID));
     imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 1L));
     break;
    
  case GET_VERSION:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::GET_VERSION));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 1L));
    break;
    
  case GET_SYS_PROP:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::GET_SYS_PROP));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;
    
  case SET_SYS_PROP:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SET_SYS_PROP));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;

  case GET_SYS_ENV:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::GET_SYS_ENV));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case SET_SYS_ENV:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SET_SYS_ENV));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;

  case TIMER_START:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::TIMER_START));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case TIMER_END:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::TIMER_END));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;
    
  case TIMER_ELAPSED:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::TIMER_ELAPSED));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;
    
    // -------------- standard i/o --------------
  case instructions::STD_OUT_BOOL:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::STD_OUT_BOOL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case instructions::STD_OUT_BYTE:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::STD_OUT_BYTE));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case instructions::STD_OUT_CHAR:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::STD_OUT_CHAR));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case instructions::STD_OUT_INT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::STD_OUT_INT));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case instructions::STD_OUT_FLOAT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_FLOAT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::STD_OUT_FLOAT));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case instructions::STD_OUT_STRING:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::STD_OUT_STRING));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;
    
  case instructions::STD_OUT_BYTE_ARY_LEN:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::STD_OUT_BYTE_ARY_LEN));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 5L));
    break;

  case instructions::STD_OUT_CHAR_ARY_LEN:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::STD_OUT_CHAR_ARY_LEN));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 5L));
    break;

  case instructions::STD_IN_BYTE_ARY_LEN:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::STD_IN_BYTE_ARY_LEN));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 5L));
    break;

  case instructions::STD_IN_CHAR_ARY_LEN:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::STD_IN_CHAR_ARY_LEN));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 5L));
    break;
    
  case instructions::STD_IN_STRING:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::STD_IN_STRING));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case instructions::STD_INT_FMT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::STD_INT_FMT));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case instructions::STD_FLOAT_FMT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::STD_FLOAT_FMT));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case instructions::STD_FLOAT_PER:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::STD_FLOAT_PER));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case instructions::STD_WIDTH:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::STD_WIDTH));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case instructions::STD_FILL:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::STD_FILL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

    // -------------- standard error i/o --------------
  case instructions::STD_ERR_BOOL:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::STD_ERR_BOOL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case instructions::STD_ERR_BYTE:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::STD_ERR_BYTE));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case instructions::STD_ERR_CHAR:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::STD_ERR_CHAR));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case instructions::STD_ERR_INT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::STD_ERR_INT));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case instructions::STD_ERR_FLOAT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_FLOAT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::STD_ERR_FLOAT));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case instructions::STD_ERR_STRING:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::STD_ERR_STRING));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;
    
  case instructions::STD_ERR_BYTE_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::STD_ERR_BYTE_ARY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 5L));
    break;

  case instructions::STD_ERR_CHAR_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::STD_ERR_CHAR_ARY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 5L));
    break;

  case instructions::STD_FLUSH:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::STD_FLUSH));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 1L));
    break;

  case instructions::STD_ERR_FLUSH:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::STD_ERR_FLUSH));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 1L));
    break;
    
    //----------- ip socket methods -----------
  case instructions::SOCK_TCP_HOST_NAME: {
    // copy and create Char[]
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, (256 + 1)));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, NEW_CHAR_ARY, 1L));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_HOST_NAME));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP_RTRN, 2L));
#ifdef _SYSTEM
    if(string_cls_id < 0) {
      Class* klass = SearchProgramClasses(L"System.String");
#ifdef _DEBUG
      assert(klass);
#endif
      string_cls_id = klass->GetId();
    }
#endif
    
    // create 'System.String' instance
    if(is_lib) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LIB_NEW_OBJ_INST, L"System.String"));
    } 
    else {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, NEW_OBJ_INST, (long)string_cls_id));
    }
    // note: method ID is position dependent
    if(is_lib) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LIB_MTHD_CALL, 0, 
                                                                                 L"System.String", L"System.String:New:c*,"));
    }
    else {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, MTHD_CALL, string_cls_id, 2L, 0L)); 
    }
        
  }    
    break;
   
  case SOCK_TCP_RESOLVE_NAME:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_RESOLVE_NAME));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;
  
  case instructions::SOCK_TCP_CONNECT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_CONNECT));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 4L));
    break;
    
  case instructions::SOCK_TCP_BIND:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_BIND));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;

  case instructions::SOCK_UDP_CREATE:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_UDP_CREATE));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 4L));
    break;

  case instructions::SOCK_UDP_BIND:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_UDP_BIND));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;

  case instructions::SOCK_UDP_CLOSE:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_UDP_CLOSE));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case instructions::SOCK_UDP_IN_BYTE:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_UDP_IN_BYTE));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case instructions::SOCK_UDP_IN_BYTE_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_UDP_IN_BYTE_ARY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 5L));
    break;

  case instructions::SOCK_UDP_IN_CHAR_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_UDP_IN_CHAR_ARY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 5L));
    break;

  case instructions::SOCK_UDP_OUT_BYTE:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_UDP_OUT_BYTE));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;

  case instructions::SOCK_UDP_OUT_BYTE_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_UDP_OUT_BYTE_ARY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 5L));
    break;

  case instructions::SOCK_UDP_OUT_CHAR_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_UDP_OUT_CHAR_ARY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 5L));
    break;

  case instructions::SOCK_UDP_OUT_STRING:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_UDP_OUT_STRING));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;

  case instructions::SOCK_UDP_IN_STRING:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_UDP_IN_STRING));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;

  case instructions::SOCK_TCP_LISTEN:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_LISTEN));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;
    
  case instructions::SOCK_TCP_ACCEPT:    
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_ACCEPT));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case instructions::SOCK_TCP_SELECT:
     imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
     imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
     imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_SELECT));
     imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
     break;

  case instructions::SOCK_TCP_SSL_LISTEN:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_SSL_LISTEN));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case instructions::SOCK_TCP_SSL_ACCEPT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_SSL_ACCEPT));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case instructions::SOCK_TCP_SSL_SELECT:
     imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
     imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
     imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_SSL_SELECT));
     imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
     break;

  case instructions::SOCK_TCP_SSL_SRV_CERT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_SSL_SRV_CERT));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;
  
  case instructions::SOCK_TCP_SSL_SRV_CLOSE:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_SSL_SRV_CLOSE));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case instructions::SOCK_TCP_SSL_ERROR:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_SSL_ERROR));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 1L));
    break;

  case instructions::SOCK_IP_ERROR:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_IP_ERROR));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 1L));
    break;
    
  case instructions::SOCK_TCP_IS_CONNECTED:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_IS_CONNECTED));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;
    
  case instructions::SOCK_TCP_CLOSE:    
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_CLOSE));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case instructions::SOCK_TCP_IN_BYTE:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_IN_BYTE));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case instructions::SOCK_TCP_IN_BYTE_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_IN_BYTE_ARY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 5L));
    break;
    
  case instructions::SOCK_TCP_IN_CHAR_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_IN_CHAR_ARY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 5L));
    break;

  case instructions::SOCK_TCP_OUT_BYTE:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_OUT_BYTE));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;

  case instructions::SOCK_TCP_OUT_BYTE_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_OUT_BYTE_ARY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 5L));
    break;

  case instructions::SOCK_TCP_OUT_CHAR_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_OUT_CHAR_ARY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 5L));
    break;
    
  case instructions::SOCK_TCP_OUT_STRING:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_OUT_STRING));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;

  case instructions::SOCK_TCP_IN_STRING:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_IN_STRING));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;
    
    //----------- secure ip socket methods -----------  
  case instructions::SOCK_TCP_SSL_CONNECT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_SSL_CONNECT));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 5L));
    break;
      
  case instructions::SOCK_TCP_SSL_ISSUER:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_SSL_ISSUER));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case instructions::SOCK_TCP_SSL_SUBJECT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_SSL_SUBJECT));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;
    
  case instructions::SOCK_TCP_SSL_CLOSE:    
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_SSL_CLOSE));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case instructions::SOCK_TCP_SSL_IN_BYTE:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_SSL_IN_BYTE));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case instructions::SOCK_TCP_SSL_IN_BYTE_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_SSL_IN_BYTE_ARY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 5L));
    break;
    
  case instructions::SOCK_TCP_SSL_IN_CHAR_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_SSL_IN_CHAR_ARY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 5L));
    break;
    
  case instructions::SOCK_TCP_SSL_OUT_BYTE:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_SSL_OUT_BYTE));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;

  case instructions::SOCK_TCP_SSL_OUT_BYTE_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_SSL_OUT_BYTE_ARY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 5L));
    break;
    
  case instructions::SOCK_TCP_SSL_OUT_CHAR_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_SSL_OUT_CHAR_ARY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 5L));
    break;
    
  case instructions::SOCK_TCP_SSL_OUT_STRING:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_SSL_OUT_STRING));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;

  case instructions::SOCK_TCP_SSL_IN_STRING:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SOCK_TCP_SSL_IN_STRING));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;
    
    //----------- serialization methods -----------  
  case SERL_CHAR:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, SERL_CHAR));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 1L));
    break;
    
  case SERL_INT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, SERL_INT));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 1L));
    break;
    
  case SERL_FLOAT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, SERL_FLOAT));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 1L));
    break;
    
  case SERL_OBJ_INST:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, SERL_OBJ_INST));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 1L));
    break;
    
  case SERL_BYTE_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, SERL_BYTE_ARY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 1L));
    break;

  case SERL_CHAR_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, SERL_CHAR_ARY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 1L));
    break;
    
  case SERL_INT_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, SERL_INT_ARY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 1L));
    break;
    
  case SERL_OBJ_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, SERL_OBJ_ARY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 1L));
    break;
    
  case SERL_FLOAT_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, SERL_FLOAT_ARY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 1L));
    break;

  case DESERL_CHAR:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, DESERL_CHAR));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 1L));
    break;
    
  case DESERL_INT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, DESERL_INT));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 1L));
    break;

  case DESERL_FLOAT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, DESERL_FLOAT));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 1L));   
    break;

  case DESERL_OBJ_INST:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, DESERL_OBJ_INST));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 1L));
    break;

  case DESERL_BYTE_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, DESERL_BYTE_ARY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 1L));
    break;

  case DESERL_CHAR_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, DESERL_CHAR_ARY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 1L));
    break;

  case DESERL_INT_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, DESERL_INT_ARY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 1L));
    break;

  case DESERL_OBJ_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, DESERL_OBJ_ARY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 1L));
    break;
    
  case DESERL_FLOAT_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, DESERL_FLOAT_ARY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 1L));
    break;

    //----------- compression methods -----------
  case instructions::COMPRESS_ZLIB_BYTES:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::COMPRESS_ZLIB_BYTES));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP_RTRN, 2L));
    break;

  case instructions::UNCOMPRESS_ZLIB_BYTES:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::UNCOMPRESS_ZLIB_BYTES));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP_RTRN, 2L));
    break;

  case instructions::COMPRESS_GZIP_BYTES:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::COMPRESS_GZIP_BYTES));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP_RTRN, 2L));
    break;

  case instructions::UNCOMPRESS_GZIP_BYTES:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::UNCOMPRESS_GZIP_BYTES));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP_RTRN, 2L));
    break;

  case instructions::COMPRESS_BR_BYTES:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::COMPRESS_BR_BYTES));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP_RTRN, 2L));
    break;

  case instructions::UNCOMPRESS_BR_BYTES:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::UNCOMPRESS_BR_BYTES));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP_RTRN, 2L));
    break;

  case instructions::CRC32_BYTES:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::CRC32_BYTES));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP_RTRN, 2L));
    break;
    
    //----------- file methods -----------
  case instructions::FILE_OPEN_READ:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::FILE_OPEN_READ));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;

  case instructions::FILE_OPEN_APPEND:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::FILE_OPEN_APPEND));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;
    
  case FILE_OPEN_WRITE:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::FILE_OPEN_WRITE));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;

  case FILE_OPEN_READ_WRITE:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::FILE_OPEN_READ_WRITE));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;

  case instructions::FILE_IN_BYTE:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::FILE_IN_BYTE));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case instructions::FILE_IN_BYTE_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::FILE_IN_BYTE_ARY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 5L));
    break;

  case instructions::FILE_IN_CHAR_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::FILE_IN_CHAR_ARY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 5L));
    break;

  case instructions::FILE_IN_STRING:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::FILE_IN_STRING));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;

  case instructions::FILE_CLOSE:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::FILE_CLOSE));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case instructions::FILE_FLUSH:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::FILE_FLUSH));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;
    
  case FILE_OUT_BYTE:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::FILE_OUT_BYTE));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;

  case FILE_OUT_BYTE_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::FILE_OUT_BYTE_ARY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 5L));
    break;
    
  case FILE_OUT_CHAR_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::FILE_OUT_CHAR_ARY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 5L));
    break;
    
  case FILE_OUT_STRING:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::FILE_OUT_STRING));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;

  case FILE_SEEK:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::FILE_SEEK));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;

  case FILE_EOF:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::FILE_EOF));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case FILE_REWIND:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::FILE_REWIND));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case FILE_IS_OPEN:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::FILE_IS_OPEN));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

    //----------- file functions -----------
  case FILE_EXISTS:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::FILE_EXISTS));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;
  
  case FILE_CAN_READ_ONLY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::FILE_CAN_READ_ONLY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;
      
  case FILE_CAN_WRITE_ONLY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::FILE_CAN_WRITE_ONLY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case FILE_CAN_READ_WRITE:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::FILE_CAN_READ_WRITE));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case FILE_SIZE:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::FILE_SIZE));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case FILE_FULL_PATH:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::FILE_FULL_PATH));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case FILE_TEMP_NAME:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::FILE_TEMP_NAME));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;
    
  case FILE_DELETE:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::FILE_DELETE));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case FILE_RENAME:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::FILE_RENAME));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;

  case FILE_COPY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::FILE_COPY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 4L));
    break;
    
  case FILE_CREATE_TIME:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::FILE_CREATE_TIME));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;
    
  case FILE_ACCOUNT_OWNER:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::FILE_ACCOUNT_OWNER));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;
    
  case FILE_GROUP_OWNER:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::FILE_GROUP_OWNER));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;
    
  case FILE_MODIFIED_TIME:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::FILE_MODIFIED_TIME));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;
    
  case FILE_ACCESSED_TIME:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::FILE_ACCESSED_TIME));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;

    //----------- pipe functions -----------
  case PIPE_CREATE:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::PIPE_CREATE));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 4L));
    break;

  case PIPE_OPEN:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::PIPE_OPEN));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 4L));
    break;

  case PIPE_IN_BYTE:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::PIPE_IN_BYTE));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case PIPE_OUT_BYTE:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::PIPE_OUT_BYTE));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;

  case PIPE_IN_BYTE_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::PIPE_IN_BYTE_ARY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 5L));
    break;

  case PIPE_IN_CHAR_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::PIPE_IN_CHAR_ARY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 5L));
    break;

  case PIPE_OUT_BYTE_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::PIPE_OUT_BYTE_ARY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 5L));
    break;

  case PIPE_OUT_CHAR_ARY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::PIPE_OUT_CHAR_ARY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 5L));
    break;

  case PIPE_IN_STRING:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::PIPE_IN_STRING));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;

  case PIPE_OUT_STRING:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::PIPE_OUT_STRING));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;

  case PIPE_CLOSE:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INST_MEM));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::PIPE_CLOSE));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

    //----------- directory functions -----------
  case DIR_CREATE:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::DIR_CREATE));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case DIR_SLASH:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::DIR_SLASH));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 1L));
    break;

  case DIR_EXISTS:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::DIR_EXISTS));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case DIR_LIST:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::DIR_LIST));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case DIR_DELETE:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::DIR_DELETE));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case DIR_COPY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 2, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::DIR_COPY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 4L));
    break;

  case DIR_GET_CUR:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::DIR_GET_CUR));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 1L));
    break;

  case DIR_SET_CUR:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::DIR_SET_CUR));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case SYM_LINK_CREATE:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SYM_LINK_CREATE));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;

  case SYM_LINK_COPY:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SYM_LINK_COPY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;

  case SYM_LINK_LOC:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SYM_LINK_LOC));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case SYM_LINK_EXISTS:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SYM_LINK_EXISTS));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case HARD_LINK_CREATE:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::HARD_LINK_CREATE));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;

  case SYS_CMD:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SYS_CMD));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;

  case SYS_CMD_OUT:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SYS_CMD_OUT));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;

  case SET_SIGNAL:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 1, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::SET_SIGNAL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 3L));
    break;

  case RAISE_SIGNAL:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(statement, cur_line_num, instructions::RAISE_SIGNAL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(statement, cur_line_num, TRAP, 2L));
    break;
  }
}

/****************************
 * Translates an 'select' statement
 ****************************/
void IntermediateEmitter::EmitSelect(Select* select_stmt)
{
  cur_line_num = select_stmt->GetLineNumber();
  
  if(select_stmt->GetLabelStatements().size() > 1) {
    SelectArrayTree tree(select_stmt, this);
    tree.Emit();
  } 
  else {
    // get statement and value
    std::map<INT64_VALUE, StatementList*> label_statements = select_stmt->GetLabelStatements();
    std::map<INT64_VALUE, StatementList*>::iterator iter = label_statements.begin();
    INT64_VALUE value = iter->first;
    StatementList* statement_list = iter->second;

    // set labels
    long end_label = ++unconditional_label;
    long other_label = 0;
    if(select_stmt->GetOther()) {
      other_label = ++conditional_label;
    }
    
    // emit code
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(select_stmt, cur_line_num, value));
    EmitExpression(select_stmt->GetAssignment()->GetExpression());
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(select_stmt, cur_line_num, EQL_INT));
    if(select_stmt->GetOther()) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(select_stmt, cur_line_num, JMP, other_label, false));
    } 
    else {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(select_stmt, cur_line_num, JMP, end_label, false));
    }

    // label statements
    std::vector<Statement*> statements = statement_list->GetStatements();
    for(size_t i = 0; i < statements.size(); ++i) {
      EmitStatement(statements[i]);
    }
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(select_stmt, cur_line_num, JMP, end_label, -1));
    

    // other statements
    if(select_stmt->GetOther()) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(select_stmt, cur_line_num, LBL, other_label));
      StatementList* statement_list = select_stmt->GetOther();
      std::vector<Statement*> statements = statement_list->GetStatements();
      for(size_t i = 0; i < statements.size(); ++i) {
        EmitStatement(statements[i]);
      }
    }
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(select_stmt, cur_line_num, LBL, end_label));
  }
}

/****************************
 * Translates a 'do/while' statement
 ****************************/
void IntermediateEmitter::EmitDoWhile(DoWhile* do_while_stmt)
{
  cur_line_num = do_while_stmt->GetLineNumber();

  // continue label
  const long unconditional_continue = ++unconditional_label;
  const long break_label = ++unconditional_label;
  break_labels.push(std::pair<int, int>(break_label, unconditional_continue));
  
  // conditional expression
  const long conditional = ++conditional_label;
  imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(do_while_stmt, cur_line_num, LBL, conditional));

  // statements
  std::vector<Statement*> do_while_statements = do_while_stmt->GetStatements()->GetStatements();
  for(size_t i = 0; i < do_while_statements.size(); ++i) {
    EmitStatement(do_while_statements[i]);
  }

  imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(do_while_stmt, cur_line_num, LBL, unconditional_continue));
  
  // conditional
  EmitExpression(do_while_stmt->GetExpression());

  if(!post_statements.empty()) {
    EmitAssignment(post_statements.front());
    post_statements.pop();
  }

  imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(do_while_stmt, cur_line_num, JMP, conditional, true));
  
  std::pair<int, int> break_continue_label = break_labels.top();
  break_labels.pop();
  imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(do_while_stmt, cur_line_num, LBL, (long)break_continue_label.first));
}

/****************************
 * Translates a 'while' statement
 ****************************/
void IntermediateEmitter::EmitWhile(While* while_stmt)
{
  cur_line_num = while_stmt->GetLineNumber();
  
  // conditional expression
  const long unconditional = ++unconditional_label;
  imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(while_stmt, cur_line_num, LBL, unconditional));
  EmitExpression(while_stmt->GetExpression());
  
  const int break_label = ++conditional_label;
  break_labels.push(std::pair<int, int>(break_label, unconditional));
  imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(while_stmt, cur_line_num, JMP, break_label, false));
  

  // statements
  std::vector<Statement*> while_statements = while_stmt->GetStatements()->GetStatements();
  for(size_t i = 0; i < while_statements.size(); ++i) {
    EmitStatement(while_statements[i]);
  }

  if(!post_statements.empty()) {
    EmitAssignment(post_statements.front());
    post_statements.pop();
  }
  
  imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(while_stmt, cur_line_num, JMP, unconditional, -1));
  
  std::pair<int, int> break_continue_label = break_labels.top();
  break_labels.pop();  
  imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(while_stmt, cur_line_num, LBL, (long)break_continue_label.first));
}

/****************************
 * Translates an 'for' statement
 ****************************/
void IntermediateEmitter::EmitFor(For* for_stmt)
{
  cur_line_num = for_stmt->GetLineNumber();

  //
  // TODO: error handling
  //

  if(for_stmt->IsRange()) {
    CalculatedExpression* calc_expr = static_cast<CalculatedExpression*>(for_stmt->GetExpression());
    Expression* right_expr = calc_expr->GetRight();

    bool is_float = false;
    long range_id = -1;
    if(right_expr->GetExpressionType() == VAR_EXPR) {
      Variable* variable = static_cast<Variable*>(right_expr);
      variable->SetId(variable->GetEntry()->GetId());
      EmitVariable(variable);
      range_id = variable->GetId();

      if(variable->GetEntry()) {
        const std::wstring range_type_name = variable->GetEntry()->GetType()->GetName();
        is_float = range_type_name == L"System.FloatRange";
      }
    }
    else if(right_expr->GetExpressionType() == METHOD_CALL_EXPR) {
      // load pre-condition
      MethodCall* mthd_call = static_cast<MethodCall*>(right_expr);
      EmitMethodCallParameters(mthd_call);
      EmitMethodCall(mthd_call, false);
      range_id = for_stmt->GetRangeEntry()->GetId();
      
      if(mthd_call->GetLibraryMethod() && mthd_call->GetLibraryMethod()->GetLibraryClass()) {
        LibraryClass* bar = mthd_call->GetLibraryMethod()->GetLibraryClass();
        const std::wstring range_type_name = bar->GetName();
        is_float = range_type_name == L"System.FloatRange";
      }
    }

    // 'FloatRange'
    if(is_float) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, STOR_INT_VAR, range_id, LOCL));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, LOAD_INT_VAR, range_id, LOCL));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, LOAD_FLOAT_VAR, 0, INST));
      Declaration* dclr_stmt = (static_cast<Declaration*>(for_stmt->GetPreStatements()->GetStatements().front()));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, STOR_FLOAT_VAR, dclr_stmt->GetEntry()->GetId(), LOCL));

      // 0
      long unconditional = ++unconditional_label;
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, LBL, unconditional));

      // 1
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, LOAD_INT_VAR, range_id, LOCL));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, LOAD_FLOAT_VAR, 1, INST));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, LOAD_FLOAT_VAR, dclr_stmt->GetEntry()->GetId(), LOCL));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, LES_FLOAT));

      // 2
      const long break_label = ++conditional_label;
      const long unconditional_continue = ++unconditional_label;
      break_labels.push(std::pair<int, int>(break_label, unconditional_continue));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, JMP, break_label, false));

      // 3
      std::vector<Statement*> for_statements = for_stmt->GetStatements()->GetStatements();
      for(size_t i = 0; i < for_statements.size(); ++i) {
        EmitStatement(for_statements[i]);
      }

      // 4
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, LBL, unconditional_continue));

      // 5
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, LOAD_INT_VAR, range_id, LOCL));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, LOAD_FLOAT_VAR, 2, INST));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, LOAD_FLOAT_VAR, dclr_stmt->GetEntry()->GetId(), LOCL));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, ADD_FLOAT));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, STOR_FLOAT_VAR, dclr_stmt->GetEntry()->GetId(), LOCL));

      // 6
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, JMP, unconditional, -1));
      std::pair<int, int> break_continue_label = break_labels.top();
      break_labels.pop();
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, LBL, (long)break_continue_label.first));
    }
    // 'CharRange' and 'IntRange'
    else {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, STOR_INT_VAR, range_id, LOCL));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, LOAD_INT_VAR, range_id, LOCL));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, LOAD_INT_VAR, 0, INST));
      Declaration* dclr_stmt = (static_cast<Declaration*>(for_stmt->GetPreStatements()->GetStatements().front()));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, STOR_INT_VAR, dclr_stmt->GetEntry()->GetId(), LOCL));

      // 0
      long unconditional = ++unconditional_label;
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, LBL, unconditional));

      // 1
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, LOAD_INT_VAR, range_id, LOCL));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, LOAD_INT_VAR, 1, INST));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, LOAD_INT_VAR, dclr_stmt->GetEntry()->GetId(), LOCL));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, LES_INT));

      // 2
      const long break_label = ++conditional_label;
      const long unconditional_continue = ++unconditional_label;
      break_labels.push(std::pair<int, int>(break_label, unconditional_continue));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, JMP, break_label, false));

      // 3
      std::vector<Statement*> for_statements = for_stmt->GetStatements()->GetStatements();
      for(size_t i = 0; i < for_statements.size(); ++i) {
        EmitStatement(for_statements[i]);
      }

      // 4
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, LBL, unconditional_continue));

      // 5
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, LOAD_INT_VAR, range_id, LOCL));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, LOAD_INT_VAR, 2, INST));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, LOAD_INT_VAR, dclr_stmt->GetEntry()->GetId(), LOCL));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, ADD_INT));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, STOR_INT_VAR, dclr_stmt->GetEntry()->GetId(), LOCL));

      // 6
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, JMP, unconditional, -1));
      std::pair<int, int> break_continue_label = break_labels.top();
      break_labels.pop();
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, LBL, (long)break_continue_label.first));
    }
#ifdef _DEBUG
    assert(range_id > -1);
#endif
  }
  // declared values
  else {
    std::vector<Statement*> pre_statements = for_stmt->GetPreStatements()->GetStatements();
    for(size_t i = 0; i < pre_statements.size(); ++i) {
      EmitStatement(pre_statements[i]);
    }

    // conditional expression
    long unconditional = ++unconditional_label;
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, LBL, unconditional));
    EmitExpression(for_stmt->GetExpression());

    // break and continue
    const long break_label = ++conditional_label;
    const long unconditional_continue = ++unconditional_label;
    break_labels.push(std::pair<int, int>(break_label, unconditional_continue));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, JMP, break_label, false));

    // statements
    std::vector<Statement*> for_statements = for_stmt->GetStatements()->GetStatements();
    for(size_t i = 0; i < for_statements.size(); ++i) {
      EmitStatement(for_statements[i]);
    }

    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, LBL, unconditional_continue));

    // update statement
    std::vector<Statement*> update_statements = for_stmt->GetUpdateStatements()->GetStatements();
    for(size_t i = 0; i < update_statements.size(); ++i) {
      EmitStatement(update_statements[i]);
    }

    // conditional jump
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, JMP, unconditional, -1));

    std::pair<int, int> break_continue_label = break_labels.top();
    break_labels.pop();
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(for_stmt, cur_line_num, LBL, (long)break_continue_label.first));
  }
}

/****************************
 * Translates an 'if' statement
 ****************************/
void IntermediateEmitter::EmitIf(If* if_stmt)
{
  cur_line_num = static_cast<Statement*>(if_stmt)->GetLineNumber();
  
  long end_label = ++unconditional_label;
  EmitIf(if_stmt, ++conditional_label, end_label);
  imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(if_stmt, cur_line_num, LBL, end_label));
}

void IntermediateEmitter::EmitIf(If* if_stmt, int next_label, int end_label)
{
  if(if_stmt) {
    // expression
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(if_stmt, cur_line_num, LBL, (long)next_label));
    EmitExpression(if_stmt->GetExpression());

    // if-else
    long conditional = ++conditional_label;
    if(if_stmt->GetNext() || if_stmt->GetElseStatements()) {      
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(if_stmt, cur_line_num, JMP, conditional, false));
    }
    else {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(if_stmt, cur_line_num, JMP, end_label, false));
    }
    
    // statements
    std::vector<Statement*> if_statements = if_stmt->GetIfStatements()->GetStatements();
    for(size_t i = 0; i < if_statements.size(); ++i) {
      EmitStatement(if_statements[i]);
    }
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(if_stmt, cur_line_num, JMP, end_label, -1));
    
    // edge case for empty 'if' statements
    if(if_statements.size() == 0 && !if_stmt->GetNext()) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(if_stmt, cur_line_num, POP_INT)); 
    }

    // if-else
    if(if_stmt->GetNext()) {
      EmitIf(if_stmt->GetNext(), conditional, end_label);
    }
    // else
    if(if_stmt->GetElseStatements()) {
      // label
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(if_stmt, cur_line_num, LBL, conditional));
      // statements
      std::vector<Statement*> else_statements = if_stmt->GetElseStatements()->GetStatements();
      for(size_t i = 0; i < else_statements.size(); ++i) {
        EmitStatement(else_statements[i]);
      }
    }
  }
}

/****************************
 * Translates critical section
 ****************************/
void IntermediateEmitter::EmitCriticalSection(CriticalSection* critical_stmt)
{
  cur_line_num = critical_stmt->GetLineNumber();

  StatementList* statement_list = critical_stmt->GetStatements();
  std::vector<Statement*> statements = statement_list->GetStatements();

  EmitVariable(critical_stmt->GetVariable());
  imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(critical_stmt, cur_line_num, CRITICAL_START));

  for(size_t i = 0; i < statements.size(); ++i) {
    EmitStatement(statements[i]);
  }

  EmitVariable(critical_stmt->GetVariable());
  imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(critical_stmt, cur_line_num, CRITICAL_END));
}

/****************************
 * Translates an expression
 ****************************/
void IntermediateEmitter::EmitExpression(Expression* expression)
{
  cur_line_num = expression->GetLineNumber();
  
  switch(expression->GetExpressionType()) {
  case LAMBDA_EXPR:
    EmitLambda(static_cast<Lambda*>(expression));
    break;

  case COND_EXPR:
    EmitConditional(static_cast<Cond*>(expression));
    break;
    
  case CHAR_STR_EXPR:
    EmitCharacterString(static_cast<CharacterString*>(expression));
    break;

  case STAT_ARY_EXPR:
    EmitStaticArray(static_cast<StaticArray*>(expression));
    break;

  case STR_CONCAT_EXPR:
    EmitStringConcat(static_cast<StringConcat*>(expression));
    break;
    
  case METHOD_CALL_EXPR:
    EmitMethodCallExpression(static_cast<MethodCall*>(expression));
    break;
    
  case BOOLEAN_LIT_EXPR:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(current_statement, expression, cur_line_num, (long)static_cast<BooleanLiteral*>(expression)->GetValue()));
    break;

  case CHAR_LIT_EXPR:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, LOAD_CHAR_LIT, (long)static_cast<CharacterLiteral*>(expression)->GetValue()));
    EmitCast(expression);
    break;

  case INT_LIT_EXPR:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(current_statement, expression, cur_line_num, static_cast<IntegerLiteral*>(expression)->GetValue()));
    EmitCast(expression);
    break;

  case FLOAT_LIT_EXPR:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, LOAD_FLOAT_LIT, static_cast<FloatLiteral*>(expression)->GetValue()));
    EmitCast(expression);
    break;

  case NIL_LIT_EXPR:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(current_statement, expression, cur_line_num, 0L));
    break;

  case VAR_EXPR:
    EmitVariable(static_cast<Variable*>(expression));
    // catch dynamic class casting
    EmitClassCast(expression);
    break;

  case AND_EXPR:
  case OR_EXPR:
    EmitAndOr(static_cast<CalculatedExpression*>(expression));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, LOAD_INT_VAR, 0, LOCL));
    break;

  case BIT_NOT_EXPR:
    EmitExpression(static_cast<CalculatedExpression*>(expression)->GetLeft());
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, BIT_NOT_INT));
    break;
    
  case EQL_EXPR:
  case NEQL_EXPR:
  case LES_EXPR:
  case GTR_EXPR:
  case LES_EQL_EXPR:
  case GTR_EQL_EXPR:
  case ADD_EXPR:
  case SUB_EXPR:
  case MUL_EXPR:
  case DIV_EXPR:
  case MOD_EXPR:
  case SHL_EXPR:
  case SHR_EXPR:
  case BIT_AND_EXPR:
  case BIT_OR_EXPR:
  case BIT_XOR_EXPR:
    EmitCalculation(static_cast<CalculatedExpression*>(expression));
    break;
  }

  if(expression->GetTypeOf() && expression->GetExpressionType() != VAR_EXPR) {
    frontend::Type* type_of = expression->GetTypeOf();
#ifdef _DEBUG
    assert(type_of->GetType() == frontend::CLASS_TYPE);
#endif
    if(SearchProgramClasses(type_of->GetName())) {
      const long id = SearchProgramClasses(type_of->GetName())->GetId();
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, OBJ_TYPE_OF, id));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, LOAD_INST_MEM));
    }
    else {
      const long id = parsed_program->GetLinker()->SearchClassLibraries(type_of->GetName(), parsed_program->GetLibUses())->GetId();
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, OBJ_TYPE_OF, id));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, LOAD_INST_MEM));
    }
  }

  // note: all nested method calls of type METHOD_CALL_EXPR
  // are processed above
  if(expression->GetExpressionType() != METHOD_CALL_EXPR && expression->GetExpressionType() != VAR_EXPR) {
    // method call
    MethodCall* method_call = expression->GetMethodCall();
    
    // note: this is not clean. literals are considered 
    // expressions and turned into instances of 'System.String' 
    // objects are considered nested if a method call is present
    bool is_nested = false || expression->GetExpressionType() == CHAR_STR_EXPR;
    while(method_call) {
      // declarations
      std::vector<Expression*> expressions = method_call->GetCallingParameters()->GetExpressions();
      for(size_t i = 0; i < expressions.size(); ++i) {
        EmitExpression(expressions[i]);
        EmitClassCast(expressions[i]);
        // need to swap values
        if(!is_str_array && new_char_str_count > 0 && method_call->GetCallingParameters() && 
           method_call->GetCallingParameters()->GetExpressions().size() > 0) {
          imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, SWAP_INT));
        }
      }
      new_char_str_count = 0;
      
      // emit call
      EmitMethodCall(method_call, is_nested || expression->GetExpressionType() == COND_EXPR);

      // pop return value if not used
      if(!method_call->GetMethodCall()) {
        switch(method_call->GetRougeReturn()) {
        case instructions::INT_TYPE:
          imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(static_cast<Statement*>(method_call), cur_line_num, POP_INT));
          break;

        case instructions::FLOAT_TYPE:
          imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(static_cast<Statement*>(method_call), cur_line_num, POP_FLOAT));
          break;

        case instructions::FUNC_TYPE:
          imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(static_cast<Statement*>(method_call), cur_line_num, POP_INT));
          imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(static_cast<Statement*>(method_call), cur_line_num, POP_INT));
          break;

        default:
          break;
        }
      }


      EmitCast(method_call);
      if(!method_call->GetVariable()) {
        EmitClassCast(method_call);
      }
      
      // next call
      if(method_call->GetMethod()) {
        Method* method = method_call->GetMethod();
        if(method->GetReturn()->GetType() == CLASS_TYPE) {
          bool is_enum = parsed_program->GetLinker()->SearchEnumLibraries(method->GetReturn()->GetName(), parsed_program->GetLibUses()) || 
            SearchProgramEnums(method->GetReturn()->GetName());
          if(!is_enum) {
            is_nested = true;
          }
          else {
            is_nested = false;
          }
        } 
        else {
          is_nested = false;
        }
      }
      else if(method_call->GetLibraryMethod()) {
        LibraryMethod* lib_method = method_call->GetLibraryMethod();
        if(lib_method->GetReturn()->GetType() == CLASS_TYPE) {
          bool is_enum = parsed_program->GetLinker()->SearchEnumLibraries(lib_method->GetReturn()->GetName(), parsed_program->GetLibUses()) || 
            SearchProgramEnums(lib_method->GetReturn()->GetName());
          if(!is_enum) {
            is_nested = true;
          }
          else {
            is_nested = false;
          }
        } 
        else {
          is_nested = false;
        }
      } 
      else {
        is_nested = false;
      }

      method_call = method_call->GetMethodCall();
    }
  }
}

/****************************
 * Translates a method call
 * expression and supports
 * dynamic functions.
 ****************************/
void IntermediateEmitter::EmitMethodCallExpression(MethodCall* method_call, bool is_variable, bool is_closure) {
  // find end of nested call
  if(method_call->IsFunctionDefinition()) {
    if(method_call->GetMethod()) {
      if(!method_call->GetMethod()->IsLambda()) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, 0L));
      }

      if(is_lib) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LIB_FUNC_DEF, -1,
                                                                                   method_call->GetMethod()->GetClass()->GetName(),
                                                                                   method_call->GetMethod()->GetEncodedName()));
                       
      }
      else {
        const int16_t method_id = method_call->GetMethod()->GetId();
        const int16_t cls_id = method_call->GetMethod()->GetClass()->GetId();
        const long method_cls_id = (cls_id << 16) | method_id;
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, method_cls_id));
      }
    }
    else {
      if(!method_call->GetLibraryMethod()->IsLambda()) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, 0L));
      }

      if(is_lib) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LIB_FUNC_DEF, -1,
                                                                                   method_call->GetLibraryMethod()->GetLibraryClass()->GetName(),
                                                                                   method_call->GetLibraryMethod()->GetName()));
      }
      else {
        const int16_t method_id = method_call->GetLibraryMethod()->GetId();
        const int16_t cls_id = method_call->GetLibraryMethod()->GetLibraryClass()->GetId();
        const long method_cls_id = (cls_id << 16) | method_id;
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, method_cls_id));
      }
    }
  }
  else if(method_call->IsFunctionalCall()) {
    MethodCall* tail = method_call;
    while(tail->GetMethodCall()) {
      tail = tail->GetMethodCall();
    }
      
    // emit parameters for nested call
    MethodCall* temp = tail;
    while(temp) {
      EmitMethodCallParameters(temp);
      // update
      temp = static_cast<MethodCall*>(temp->GetPreviousExpression());
    }

    // emit function variable
    MemoryContext mem_context;
    SymbolEntry* entry = method_call->GetFunctionalEntry();
    if(entry->IsLocal()) {
      mem_context = LOCL;
    } 
    else if(entry->IsStatic()) {
      mem_context = CLS;
    } 
    else {
      mem_context = INST;
    }

    //
    if(mem_context == INST) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_INST_MEM));
    } 
    else if(mem_context == CLS) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_CLS_MEM));
    }      
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_FUNC_VAR, entry->GetId(), mem_context));
    
    switch(method_call->GetRougeReturn()) {
    case instructions::INT_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, DYN_MTHD_CALL, entry->GetType()->GetFunctionParameterCount(), instructions::INT_TYPE));
      if(!method_call->GetMethodCall()) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(static_cast<Statement*>(method_call), cur_line_num, POP_INT));
      }
      break;
  
    case instructions::FLOAT_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, DYN_MTHD_CALL, entry->GetType()->GetFunctionParameterCount(), instructions::FLOAT_TYPE));
      if(!method_call->GetMethodCall()) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(static_cast<Statement*>(method_call), cur_line_num, POP_FLOAT));
      }
      break;

    case instructions::FUNC_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, DYN_MTHD_CALL, entry->GetType()->GetFunctionParameterCount(), instructions::FUNC_TYPE));
      if(!method_call->GetMethodCall()) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(static_cast<Statement*>(method_call), cur_line_num, POP_INT));
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(static_cast<Statement*>(method_call), cur_line_num, POP_INT));
      }
      break;
      
    default:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, DYN_MTHD_CALL, entry->GetType()->GetFunctionParameterCount(), instructions::NIL_TYPE));
      break;
    }
      
    // emit nested calls
    bool is_nested = false; // function call
    method_call = method_call->GetMethodCall();
    while(method_call) {
      EmitMethodCall(method_call, is_nested);
      EmitCast(method_call);
      if(!method_call->GetVariable()) {
        EmitClassCast(method_call);
      }
      
      // next call
      if(method_call->GetMethod()) {
        Method* method = method_call->GetMethod();
        if(method->GetReturn()->GetType() == CLASS_TYPE) {
          bool is_enum = parsed_program->GetLinker()->SearchEnumLibraries(method->GetReturn()->GetName(), parsed_program->GetLibUses()) || 
            SearchProgramEnums(method->GetReturn()->GetName());
          if(!is_enum) {
            is_nested = true;
          }
          else {
            is_nested = false;
          }
        } 
        else {
          is_nested = false;
        }
      } 
      else if(method_call->GetLibraryMethod()) {
        LibraryMethod* lib_method = method_call->GetLibraryMethod();
        if(lib_method->GetReturn()->GetType() == CLASS_TYPE) {
          bool is_enum = parsed_program->GetLinker()->SearchEnumLibraries(lib_method->GetReturn()->GetName(), parsed_program->GetLibUses()) || 
            SearchProgramEnums(lib_method->GetReturn()->GetName());
          if(!is_enum) {
            is_nested = true;
          }
          else {
            is_nested = false;
          }
        } 
        else {
          is_nested = false;
        }
      } 
      else {
        is_nested = false;
      }
      method_call = method_call->GetMethodCall();
    } 
  }
  else {
    // rewind calling parameters    
    if(!is_variable) {
      MethodCall* tail = method_call;
      while(tail->GetMethodCall()) {
        tail = tail->GetMethodCall();
      }
      
      // emit parameters for nested call
      MethodCall* temp = tail;
      while(temp) {
        EmitMethodCallParameters(temp);
        temp = static_cast<MethodCall*>(temp->GetPreviousExpression());
      }
    }
    
    bool is_nested = is_variable ? true : false;
    do {
      EmitMethodCall(method_call, is_nested);
      EmitCast(method_call);
      if(!method_call->GetVariable()) {
        EmitClassCast(method_call);
      }
      
      // next call
      if(method_call->GetMethod()) {
        Method* method = method_call->GetMethod();
        if(method->GetReturn()->GetType() == CLASS_TYPE) {
          bool is_enum = parsed_program->GetLinker()->SearchEnumLibraries(method->GetReturn()->GetName(), parsed_program->GetLibUses()) || 
            SearchProgramEnums(method->GetReturn()->GetName());
          if(!is_enum) {
            is_nested = true;
          }
          else {
            is_nested = false;
          }
        } 
        else {
          is_nested = false;
        }
      } 
      else if(method_call->GetLibraryMethod()) {
        LibraryMethod* lib_method = method_call->GetLibraryMethod();
        if(lib_method->GetReturn()->GetType() == CLASS_TYPE) {
          bool is_enum = parsed_program->GetLinker()->SearchEnumLibraries(lib_method->GetReturn()->GetName(), parsed_program->GetLibUses()) || 
            SearchProgramEnums(lib_method->GetReturn()->GetName());
          if(!is_enum) {
            is_nested = true;
          }
          else {
            is_nested = false;
          }
        } 
        else {
          is_nested = false;
        }
      } 
      else {
        is_nested = false;
      }
      // process next method
      method_call = method_call->GetMethodCall();
    } 
    while(method_call);
  }
}

/****************************
 * Translates string concatenation expressions
 ****************************/
void IntermediateEmitter::EmitStringConcat(StringConcat* str_concat)
{
  // create 'System.String' instance
  SymbolEntry* concat_entry = str_concat->GetConcat();
  if(is_lib) {
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, str_concat, cur_line_num, LIB_NEW_OBJ_INST, L"System.String"));
  }
  else {
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, str_concat, cur_line_num, NEW_OBJ_INST, string_cls_id));
  }

  // note: method ID is position dependent
  if(is_lib) {
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, str_concat, cur_line_num, LIB_MTHD_CALL, 0L, L"System.String", L"System.String:New:"));
  }
  else {
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, str_concat, cur_line_num, MTHD_CALL, string_cls_id, 0L, 0L));
  }
  imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, str_concat, cur_line_num, STOR_INT_VAR, concat_entry->GetId(), LOCL));

  // append expression
  std::list<Expression*> concat_exprs = str_concat->GetExpressions();
  std::list<Expression*>::iterator iter;
  for(iter = concat_exprs.begin(); iter != concat_exprs.end(); iter++) {
    Expression* concat_expr = *iter;
    EmitExpression(concat_expr);

    switch(concat_expr->GetEvalType()->GetType()) {
    case frontend::BOOLEAN_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, str_concat, cur_line_num, LOAD_INT_VAR, concat_entry->GetId(), LOCL));
      if(is_lib) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, str_concat, cur_line_num, LIB_MTHD_CALL, 0, L"System.String", L"System.String:Append:l,"));
      }
      else {
        LibraryMethod* string_append_method = string_cls->GetMethod(L"System.String:Append:l,");
#ifdef _DEBUG
        assert(string_append_method);
#endif
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, str_concat, cur_line_num, MTHD_CALL,
          (INT_VALUE)string_cls->GetId(), string_append_method->GetId(), 0L));
      }
      new_char_str_count = 0;
      break;

    case frontend::BYTE_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, str_concat, cur_line_num, LOAD_INT_VAR, concat_entry->GetId(), LOCL));
      if(is_lib) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, str_concat, cur_line_num, LIB_MTHD_CALL, 0, L"System.String", L"System.String:Append:b,"));
      }
      else {
        LibraryMethod* string_append_method = string_cls->GetMethod(L"System.String:Append:b,");
#ifdef _DEBUG
        assert(string_append_method);
#endif
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, str_concat, cur_line_num, MTHD_CALL,
          (INT_VALUE)string_cls->GetId(), string_append_method->GetId(), 0L));
      }
      new_char_str_count = 0;
      break;

    case frontend::CHAR_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, str_concat, cur_line_num, LOAD_INT_VAR, concat_entry->GetId(), LOCL));
      if(is_lib) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, str_concat, cur_line_num, LIB_MTHD_CALL, 0, L"System.String", L"System.String:Append:c,"));
      }
      else {
        LibraryMethod* string_append_method = string_cls->GetMethod(L"System.String:Append:c,");
#ifdef _DEBUG
        assert(string_append_method);
#endif
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, str_concat, cur_line_num, MTHD_CALL,
          (INT_VALUE)string_cls->GetId(), string_append_method->GetId(), 0L));
      }
      new_char_str_count = 0;
      break;

    case frontend::INT_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, str_concat, cur_line_num, LOAD_INT_VAR,
        concat_entry->GetId(), LOCL));
      if(is_lib) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, str_concat, cur_line_num, LIB_MTHD_CALL, 0,
          L"System.String", L"System.String:Append:i,"));
      }
      else {
        LibraryMethod* string_append_method = string_cls->GetMethod(L"System.String:Append:i,");
#ifdef _DEBUG
        assert(string_append_method);
#endif
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, str_concat, cur_line_num, MTHD_CALL,
          (INT_VALUE)string_cls->GetId(),
          string_append_method->GetId(), 0L));
      }
      new_char_str_count = 0;
      break;

    case frontend::FLOAT_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, str_concat, cur_line_num, LOAD_INT_VAR, concat_entry->GetId(), LOCL));
      if(is_lib) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, str_concat, cur_line_num, LIB_MTHD_CALL, 0, L"System.String", L"System.String:Append:f,"));
      }
      else {
        LibraryMethod* string_append_method = string_cls->GetMethod(L"System.String:Append:f,");
#ifdef _DEBUG
        assert(string_append_method);
#endif
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, str_concat, cur_line_num, MTHD_CALL, string_cls->GetId(), string_append_method->GetId(), 0L));
      }
      new_char_str_count = 0;
      break;

    case frontend::CLASS_TYPE:
      // process string instance
      if(concat_expr->GetEvalType()->GetName() == L"System.String" || concat_expr->GetEvalType()->GetName() == L"String") {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, str_concat, cur_line_num, LOAD_INT_VAR, concat_entry->GetId(), LOCL));

        if(is_lib) {
          imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, str_concat, cur_line_num, LIB_MTHD_CALL, 0, L"System.String", L"System.String:Append:o.System.String,"));
        }
        else {
          LibraryMethod* string_append_method = string_cls->GetMethod(L"System.String:Append:o.System.String,");
#ifdef _DEBUG
          assert(string_append_method);
#endif
          imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, str_concat, cur_line_num, MTHD_CALL,
            (INT_VALUE)string_cls->GetId(), string_append_method->GetId(), 0L));
        }
      }
      else {
        Method* inst_mthd = str_concat->GetMethod(concat_expr);
        LibraryMethod* inst_lib_mthd = str_concat->GetLibraryMethod(concat_expr);
        EmitConcatToString(concat_entry, inst_mthd, inst_lib_mthd);
      }
      new_char_str_count = 0;
      break;

    default:
      break;
    }
  }

  imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, str_concat, cur_line_num, LOAD_INT_VAR, concat_entry->GetId(), LOCL));
}

/****************************
 * Translates an element array.
 * This creates a new array
 * and copies content.
 ****************************/
void IntermediateEmitter::EmitStaticArray(StaticArray* array) {
  cur_line_num = array->GetLineNumber();
  
  if(array->GetType() != frontend::CLASS_TYPE) {
    // emit dimensions
    std::vector<int> sizes = array->GetSizes();
    for(int i = (int)sizes.size() - 1; i > -1; i--) {      
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(current_statement, array, cur_line_num, sizes[i]));
    }
    
    // write copy instructions
    switch(array->GetType()) {
    case frontend::INT_TYPE:    
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, array, cur_line_num, NEW_INT_ARY, (long)array->GetDimension()));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(current_statement, array, cur_line_num, array->GetId()));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(current_statement, array, cur_line_num, instructions::CPY_INT_STR_ARY));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, array, cur_line_num, TRAP_RTRN, 3L));
      break;

    case frontend::BOOLEAN_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, array, cur_line_num, NEW_BYTE_ARY, (long)array->GetDimension()));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(current_statement, array, cur_line_num, array->GetId()));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(current_statement, array, cur_line_num, instructions::CPY_BOOL_STR_ARY));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, array, cur_line_num, TRAP_RTRN, 3L));
      break;

    case frontend::BYTE_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, array, cur_line_num, NEW_BYTE_ARY, (long)array->GetDimension()));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(current_statement, array, cur_line_num, array->GetId()));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(current_statement, array, cur_line_num, instructions::CPY_BYTE_STR_ARY));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, array, cur_line_num, TRAP_RTRN, 3L));
      break;

    case frontend::FLOAT_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, array, cur_line_num, NEW_FLOAT_ARY, (long)array->GetDimension()));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(current_statement, array, cur_line_num, array->GetId()));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(current_statement, array, cur_line_num, instructions::CPY_FLOAT_STR_ARY));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, array, cur_line_num, TRAP_RTRN, 3L));
      break;
    
    case frontend::CHAR_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, array, cur_line_num, NEW_CHAR_ARY, (long)array->GetDimension()));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(current_statement, array, cur_line_num, array->GetId()));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(current_statement, array, cur_line_num, instructions::CPY_CHAR_STR_ARY));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, array, cur_line_num, TRAP_RTRN, 3L));
      break;

    default:
      break;
    }
  }
  else {
    // create string literals
    is_str_array = true;
    std::vector<Expression*> all_elements = array->GetAllElements()->GetExpressions();
    for(int i = (int)all_elements.size() - 1; i > -1; i--) {
      EmitCharacterString(static_cast<CharacterString*>(all_elements[i]));
    }
    is_str_array = false;

    // emit dimensions
    std::vector<int> sizes = array->GetSizes();
    for(size_t i = 0; i < sizes.size(); ++i) {      
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(current_statement, array, cur_line_num, sizes[i]));
    }
    
    // create string array
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, array, cur_line_num, NEW_INT_ARY, (long)array->GetDimension()));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(current_statement, array, cur_line_num, instructions::CPY_CHAR_STR_ARYS));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, array, cur_line_num, TRAP_RTRN, (long)(all_elements.size() + 2)));
  }
}

/****************************
 * Translates a '?' expression.
 ****************************/
void IntermediateEmitter::EmitConditional(Cond* conditional)
{
  cur_line_num = static_cast<Expression*>(conditional)->GetLineNumber();
  
  // conditional
  long end_label = ++unconditional_label;
  EmitExpression(conditional->GetCondExpression());
  // if-expression
  long cond = ++conditional_label;
  imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, conditional, cur_line_num, JMP, cond, false));
  EmitExpression(conditional->GetExpression());
  EmitCast(conditional);
  imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, conditional, cur_line_num, STOR_INT_VAR, 0, LOCL));
  imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, conditional, cur_line_num, JMP, end_label, -1));
  new_char_str_count = 0;
  // else-expression
  imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, conditional, cur_line_num, LBL, cond));
  EmitExpression(conditional->GetElseExpression());
  EmitCast(conditional);
  imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, conditional, cur_line_num, STOR_INT_VAR, 0, LOCL));
  new_char_str_count = 0;
  // expression end
  imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, conditional, cur_line_num, LBL, end_label));
  imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, conditional, cur_line_num, LOAD_INT_VAR, 0, LOCL));
}

/****************************
 * Translates character std::wstring.
 * This creates a new byte array
 * and copies content.
 ****************************/
void IntermediateEmitter::EmitCharacterString(CharacterString* char_str)
{
  cur_line_num = char_str->GetLineNumber();
  
  std::vector<CharacterStringSegment*> segments = char_str->GetSegments();
  for(size_t i = 0; i < segments.size(); ++i) {
    if(i == 0) {
      EmitCharacterStringSegment(segments[i], char_str);
    }
    else {
      EmitAppendCharacterStringSegment(segments[i], char_str);
    }
  }

  if(segments.size() > 1) {
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, cur_line_num, LOAD_INT_VAR,
                                                                               char_str->GetConcat()->GetId(), LOCL));
  }
}

void IntermediateEmitter::EmitAppendCharacterStringSegment(CharacterStringSegment* segment, CharacterString* char_str)
{
  cur_line_num = char_str->GetLineNumber();
  
  SymbolEntry* concat_entry = char_str->GetConcat();
  if(segment->GetType() == STRING) {    
    EmitCharacterStringSegment(segment, nullptr);
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, LOAD_INT_VAR, 
                                                                               concat_entry->GetId(), LOCL));    
    if(is_lib) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, LIB_MTHD_CALL, 0, 
                                                                                 L"System.String", 
                                                                                 L"System.String:Append:o.System.String,"));
    }
    else {
      LibraryMethod* string_append_method = string_cls->GetMethod(L"System.String:Append:o.System.String,");
#ifdef _DEBUG
      assert(string_append_method);
#endif
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, MTHD_CALL, 
                                                                                 (INT_VALUE)string_cls->GetId(), 
                                                                                 string_append_method->GetId(), 0L)); 
    }
  }
  else {
    SymbolEntry* var_entry = segment->GetEntry();
    MemoryContext mem_context;
    if(var_entry->IsLocal()) {
      mem_context = LOCL;
    } 
    else if(var_entry->IsStatic()) {
      mem_context = CLS;
    } 
    else {
      mem_context = INST;
    }
    
    // emit the correct context
    if(mem_context == INST) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, LOAD_INST_MEM));
    } 
    else if(mem_context == CLS) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, LOAD_CLS_MEM));
    } 
    
    switch(var_entry->GetType()->GetType()) {
    case frontend::BOOLEAN_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, LOAD_INT_VAR,
        var_entry->GetId(), mem_context));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, LOAD_INT_VAR,
        concat_entry->GetId(), LOCL));
      if(is_lib) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, LIB_MTHD_CALL, 0,
          L"System.String", L"System.String:Append:l,"));
      }
      else {
        LibraryMethod* string_append_method = string_cls->GetMethod(L"System.String:Append:l,");
#ifdef _DEBUG
        assert(string_append_method);
#endif
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, MTHD_CALL,
          (INT_VALUE)string_cls->GetId(),
          string_append_method->GetId(), 0L));
      }
      new_char_str_count = 0;
      break;

    case frontend::BYTE_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, LOAD_INT_VAR,
        var_entry->GetId(), mem_context));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, LOAD_INT_VAR,
        concat_entry->GetId(), LOCL));
      if(is_lib) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, LIB_MTHD_CALL, 0,
          L"System.String", L"System.String:Append:b,"));
      }
      else {
        LibraryMethod* string_append_method = string_cls->GetMethod(L"System.String:Append:b,");
#ifdef _DEBUG
        assert(string_append_method);
#endif
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, MTHD_CALL,
          (INT_VALUE)string_cls->GetId(),
          string_append_method->GetId(), 0L));
      }
      new_char_str_count = 0;
      break;

    case frontend::CHAR_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, LOAD_INT_VAR,
        var_entry->GetId(), mem_context));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, LOAD_INT_VAR,
        concat_entry->GetId(), LOCL));
      if(is_lib) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, LIB_MTHD_CALL, 0,
          L"System.String", L"System.String:Append:c,"));
      }
      else {
        LibraryMethod* string_append_method = string_cls->GetMethod(L"System.String:Append:c,");
#ifdef _DEBUG
        assert(string_append_method);
#endif
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, MTHD_CALL,
          (INT_VALUE)string_cls->GetId(),
          string_append_method->GetId(), 0L));
      }
      new_char_str_count = 0;
      break;

    case frontend::INT_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, LOAD_INT_VAR,
        var_entry->GetId(), mem_context));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, LOAD_INT_VAR,
        concat_entry->GetId(), LOCL));
      if(is_lib) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, LIB_MTHD_CALL, 0,
          L"System.String", L"System.String:Append:i,"));
      }
      else {
        LibraryMethod* string_append_method = string_cls->GetMethod(L"System.String:Append:i,");
#ifdef _DEBUG
        assert(string_append_method);
#endif
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, MTHD_CALL,
          (INT_VALUE)string_cls->GetId(),
          string_append_method->GetId(), 0L));
      }
      new_char_str_count = 0;
      break;

    case frontend::FLOAT_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, LOAD_FLOAT_VAR,
        var_entry->GetId(), mem_context));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, LOAD_INT_VAR,
        concat_entry->GetId(), LOCL));
      if(is_lib) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, LIB_MTHD_CALL, 0,L"System.String", L"System.String:Append:f,"));
      }
      else {
        LibraryMethod* string_append_method = string_cls->GetMethod(L"System.String:Append:f,");
#ifdef _DEBUG
        assert(string_append_method);
#endif
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, MTHD_CALL, string_cls->GetId(), string_append_method->GetId(), 0L));
      }
      new_char_str_count = 0;
      break;

    case frontend::CLASS_TYPE:
      // process string instance
      if(var_entry->GetType()->GetName() == L"System.String" || var_entry->GetType()->GetName() == L"String") {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, LOAD_INT_VAR,
          var_entry->GetId(), mem_context));
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, LOAD_INT_VAR,
          concat_entry->GetId(), LOCL));
        if(is_lib) {
          imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, LIB_MTHD_CALL, 0,
            L"System.String",
            L"System.String:Append:o.System.String,"));
        }
        else {
          LibraryMethod* string_append_method = string_cls->GetMethod(L"System.String:Append:o.System.String,");
#ifdef _DEBUG
          assert(string_append_method);
#endif
          imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, MTHD_CALL,
            (INT_VALUE)string_cls->GetId(),
            string_append_method->GetId(), 0L));
        }
      }
      // process object instance
      else {
        // call object's 'ToString' method
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, LOAD_INT_VAR, var_entry->GetId() + 1, mem_context));
        Method* inst_mthd = segment->GetMethod();
        LibraryMethod* inst_lib_mthd = segment->GetLibraryMethod();
        EmitConcatToString(concat_entry, inst_mthd, inst_lib_mthd);
      }
      new_char_str_count = 0;
      break;

    default:
      break;
    }
  }
}

void IntermediateEmitter::EmitConcatToString(SymbolEntry* concat_entry, Method* inst_mthd, LibraryMethod* inst_lib_mthd) {
#ifdef _DEBUG
  assert(inst_mthd || inst_lib_mthd);
#endif

  if(inst_lib_mthd && inst_lib_mthd->GetEncodedReturn() != L"o.System.String") {
    // library output
    if(is_lib) {
      // program class
      if(inst_mthd) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LIB_MTHD_CALL, 0, inst_mthd->GetClass()->GetName(), inst_mthd->GetEncodedName()));
      }
      // library class
      else {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LIB_MTHD_CALL, 0, inst_lib_mthd->GetLibraryClass()->GetName(), inst_lib_mthd->GetName()));
      }
    }
    else {
      // program class
      if(inst_mthd) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, MTHD_CALL, inst_mthd->GetClass()->GetId(), inst_mthd->GetId(), 0L));

      }
      // library class
      else {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, MTHD_CALL, inst_lib_mthd->GetLibraryClass()->GetId(), inst_lib_mthd->GetId(), 0L));
      }
    }
  }

  // append string value
  imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_VAR,  concat_entry->GetId(), LOCL));
  if(is_lib) {
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LIB_MTHD_CALL, 0, L"System.String", L"System.String:Append:o.System.String,"));
  }
  else {
    LibraryMethod* string_append_method = string_cls->GetMethod(L"System.String:Append:o.System.String,");
#ifdef _DEBUG
    assert(string_append_method);
#endif
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, MTHD_CALL, string_cls->GetId(), string_append_method->GetId(), 0L));
  }
}

void IntermediateEmitter::EmitCharacterStringSegment(CharacterStringSegment* segment, CharacterString* char_str)
{
#ifdef _SYSTEM
  if(string_cls_id < 0) {
    Class* klass = SearchProgramClasses(L"System.String");
#ifdef _DEBUG
    assert(klass);
#endif
    string_cls_id = klass->GetId();
  }
#endif
  
  if(segment->GetString().size() > 0) {
    // copy and create Char[]
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(current_statement, char_str, cur_line_num, segment->GetString().size()));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, NEW_CHAR_ARY, 1L));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(current_statement, char_str, cur_line_num, segment->GetId()));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(current_statement, char_str, cur_line_num, instructions::CPY_CHAR_STR_ARY));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, TRAP_RTRN, 3L));
  
    // create 'System.String' instance
    if(is_lib) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, LIB_NEW_OBJ_INST, L"System.String"));
    } 
    else {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, NEW_OBJ_INST, string_cls_id));
    }
    // note: method ID is position dependent
    if(is_lib) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, LIB_MTHD_CALL, 0L, 
                                                                                 L"System.String", L"System.String:New:c*,"));
    }
    else {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, MTHD_CALL, string_cls_id, 2L, 0L)); 
    }    
  }
  else {
    // create 'System.String' instance
    if(is_lib) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, LIB_NEW_OBJ_INST, L"System.String"));
    } 
    else {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, NEW_OBJ_INST, string_cls_id));
    }
    // note: method ID is position dependent
    if(is_lib) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, LIB_MTHD_CALL, 0L, 
                                                                                 L"System.String", L"System.String:New:"));
    }
    else {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, MTHD_CALL, string_cls_id, 0L, 0L)); 
    }    
  }
  
  if(char_str && char_str->GetConcat()) {
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, STOR_INT_VAR, char_str->GetConcat()->GetId(), LOCL));
  }
  // check for stack swap
  new_char_str_count++;
  if(char_str && !is_str_array && new_char_str_count >= 2) {
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, char_str, cur_line_num, SWAP_INT));
    new_char_str_count = 0;
  }
}

/****************************
 * Translates a calculation
 ****************************/
void IntermediateEmitter::EmitAndOr(CalculatedExpression* expression)
{
  cur_line_num = expression->GetLineNumber();

  switch(expression->GetExpressionType()) {
  case AND_EXPR: {
    // emit right
    EmitExpression(expression->GetRight());
    long label = ++conditional_label;
    // AND jump
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, JMP, label, 1L));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(current_statement, expression, cur_line_num, 0L));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, STOR_INT_VAR, 0, LOCL));
    long end = ++unconditional_label;
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, JMP, end, -1));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, LBL, label));
    // emit left
    EmitExpression(expression->GetLeft());
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, STOR_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, LBL, end));
  }
    break;

  case OR_EXPR: {
    // emit right
    EmitExpression(expression->GetRight());
    long label = ++conditional_label;
    // OR jump
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, JMP, label, 0L));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(current_statement, expression, cur_line_num, 1L));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, STOR_INT_VAR, 0, LOCL));
    long end = ++unconditional_label;
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, JMP, end, -1));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, LBL, label));
    // emit left
    EmitExpression(expression->GetLeft());
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, STOR_INT_VAR, 0, LOCL));
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, LBL, end));
  }
    break;

  default:
    break;
  }
}

/****************************
 * Translates a calculation
 ****************************/
void IntermediateEmitter::EmitCalculation(CalculatedExpression* expression)
{
  cur_line_num = expression->GetLineNumber();
  
  Expression* right = expression->GetRight();
  switch(right->GetExpressionType()) {
  case EQL_EXPR:
  case NEQL_EXPR:
  case LES_EXPR:
  case GTR_EXPR:
  case LES_EQL_EXPR:
  case GTR_EQL_EXPR:
  case ADD_EXPR:
  case SUB_EXPR:
  case MUL_EXPR:
  case DIV_EXPR:
  case MOD_EXPR:
  case SHL_EXPR:
  case SHR_EXPR:
  case BIT_AND_EXPR:
  case BIT_OR_EXPR:
  case BIT_XOR_EXPR:
    EmitCalculation(static_cast<CalculatedExpression*>(right));
    if(right->GetMethodCall()) {
      EmitMethodCall(right->GetMethodCall(), false);
      EmitCast(right->GetMethodCall());
    }
    break;
    
  default:
    EmitExpression(right);
    break;
  }
  
  Expression* left = expression->GetLeft();
  switch(left->GetExpressionType()) {
  case EQL_EXPR:
  case NEQL_EXPR:
  case LES_EXPR:
  case GTR_EXPR:
  case LES_EQL_EXPR:
  case GTR_EQL_EXPR:
  case ADD_EXPR:
  case SUB_EXPR:
  case MUL_EXPR:
  case DIV_EXPR:
  case MOD_EXPR:
  case SHL_EXPR:
  case SHR_EXPR:
  case BIT_AND_EXPR:
  case BIT_OR_EXPR:
  case BIT_XOR_EXPR:
    EmitCalculation(static_cast<CalculatedExpression*>(left));
    if(left->GetMethodCall()) {
      EmitMethodCall(left->GetMethodCall(), false);
      EmitCast(left->GetMethodCall());
    }
    break;
    
  default:
    EmitExpression(left);
    break;
  }
  
  EntryType eval_type = expression->GetEvalType()->GetType();
  switch(expression->GetExpressionType()) {
  case EQL_EXPR:
    if((left->GetEvalType()->GetType() == frontend::FLOAT_TYPE && left->GetEvalType()->GetDimension() < 1) ||
       (right->GetEvalType()->GetType() == frontend::FLOAT_TYPE && right->GetEvalType()->GetDimension() < 1)) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, EQL_FLOAT));
    } else {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, EQL_INT));
    }
    EmitCast(expression);
    break;

  case NEQL_EXPR:
    if((left->GetEvalType()->GetType() == frontend::FLOAT_TYPE && left->GetEvalType()->GetDimension() < 1) ||
       (right->GetEvalType()->GetType() == frontend::FLOAT_TYPE && right->GetEvalType()->GetDimension() < 1)) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, NEQL_FLOAT));
    } else {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, NEQL_INT));
    }
    EmitCast(expression);
    break;

  case LES_EXPR:
    if(left->GetEvalType()->GetType() == frontend::FLOAT_TYPE ||
       right->GetEvalType()->GetType() == frontend::FLOAT_TYPE) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, LES_FLOAT));
    } else {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, LES_INT));
    }
    EmitCast(expression);
    break;

  case GTR_EXPR:
    if(left->GetEvalType()->GetType() == frontend::FLOAT_TYPE ||
       right->GetEvalType()->GetType() == frontend::FLOAT_TYPE) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, GTR_FLOAT));
    } else {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, GTR_INT));
    }
    EmitCast(expression);
    break;

  case LES_EQL_EXPR:
    if(left->GetEvalType()->GetType() == frontend::FLOAT_TYPE ||
       right->GetEvalType()->GetType() == frontend::FLOAT_TYPE) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, LES_EQL_FLOAT));
    } else {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, LES_EQL_INT));
    }
    EmitCast(expression);
    break;

  case GTR_EQL_EXPR:
    if(left->GetEvalType()->GetType() == frontend::FLOAT_TYPE ||
       right->GetEvalType()->GetType() == frontend::FLOAT_TYPE) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, GTR_EQL_FLOAT));
    } else {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, GTR_EQL_INT));
    }
    EmitCast(expression);
    break;

  case ADD_EXPR:
    if(eval_type == frontend::FLOAT_TYPE) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, ADD_FLOAT));
    } else {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, ADD_INT));
    }
    EmitCast(expression);
    break;

  case SUB_EXPR:
    if(eval_type == frontend::FLOAT_TYPE) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, SUB_FLOAT));
    } else {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, SUB_INT));
    }
    EmitCast(expression);
    break;

  case MUL_EXPR:
    if(eval_type == frontend::FLOAT_TYPE) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, MUL_FLOAT));
    } else {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, MUL_INT));
    }
    EmitCast(expression);
    break;

  case DIV_EXPR:
    if(eval_type == frontend::FLOAT_TYPE) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, DIV_FLOAT));
    } else {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, DIV_INT));
    }
    EmitCast(expression);
    break;

  case MOD_EXPR:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, MOD_INT));
    EmitCast(expression);
    break;

  case SHL_EXPR:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, SHL_INT));
    EmitCast(expression);
    break;
    
  case SHR_EXPR:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, SHR_INT));
    EmitCast(expression);
    break;
    
  case BIT_AND_EXPR:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, BIT_AND_INT));
    EmitCast(expression);
    break;
    
  case BIT_OR_EXPR:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, BIT_OR_INT));
    EmitCast(expression);
    break;
    
  case BIT_XOR_EXPR:
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, BIT_XOR_INT));
    EmitCast(expression);
    break;

  default:
    break;
  }
}

/****************************
 * Translates a type cast
 ****************************/
void IntermediateEmitter::EmitCast(Expression* expression)
{
  cur_line_num = expression->GetLineNumber();
  
  if(expression->GetCastType()) {
    frontend::Type* cast_type = expression->GetCastType();
    Type* base_type;
    if(expression->GetExpressionType() == METHOD_CALL_EXPR) {
      MethodCall* call = static_cast<MethodCall*>(expression);
      if(call->GetCallType() == ENUM_CALL) {
        base_type = expression->GetEvalType();
      }
      else if(call->GetMethod()) {
        base_type = call->GetMethod()->GetReturn();
      } 
      else if(call->GetLibraryMethod()) {
        base_type = call->GetLibraryMethod()->GetReturn();
      } 
      else {
        base_type = expression->GetBaseType();
      }
    } 
    else {
      base_type = expression->GetBaseType();
    }

    switch(base_type->GetType()) {
    case frontend::BYTE_TYPE:
    case frontend::CHAR_TYPE:
    case frontend::INT_TYPE:
      if(cast_type->GetType() == frontend::FLOAT_TYPE) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, I2F));
      }
      break;

    case frontend::FLOAT_TYPE:
      if(cast_type->GetType() != CLASS_TYPE && cast_type->GetType() != frontend::FLOAT_TYPE) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, F2I));
      }
      break;

    default:
      break;
    }
  }
  else if(expression->GetTypeOf()) {
    frontend::Type* type_of = expression->GetTypeOf();
#ifdef _DEBUG
    assert(type_of->GetType() == frontend::CLASS_TYPE);
#endif
    if(SearchProgramClasses(type_of->GetName())) {
      if(is_lib) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, LIB_OBJ_TYPE_OF, type_of->GetName()));
      }
      else {
        long id = SearchProgramClasses(type_of->GetName())->GetId();
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, OBJ_TYPE_OF, id));
      }
    }
    else {
      if(is_lib) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, LIB_OBJ_TYPE_OF, type_of->GetName()));
      }
      else {
        long id = parsed_program->GetLinker()->SearchClassLibraries(type_of->GetName(), parsed_program->GetLibUses())->GetId();
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression, cur_line_num, OBJ_TYPE_OF, id));
        expression->GetTypeOf()->SetResolved(true);
      }
    }
  }
}

/****************************
 * Translates variable
 ****************************/
void IntermediateEmitter::EmitVariable(Variable* variable)
{
  cur_line_num = variable->GetLineNumber();
  
  // emit subsequent method call parameters
  if(variable->GetMethodCall()) {
    MethodCall* tail = variable->GetMethodCall();
    while(tail->GetMethodCall()) {
      tail = tail->GetMethodCall();
    }
    
    // emit parameters for nested call
    MethodCall* temp = tail;
    while(temp && temp != variable->GetMethodCall()) {      
      EmitMethodCallParameters(temp);
      // update
      temp = static_cast<MethodCall*>(temp->GetPreviousExpression());
    }
    
    // emit parameters last call
    EmitMethodCallParameters(variable->GetMethodCall());
 
  }
  
  // self
  if(variable->GetEntry()->IsSelf()) {
    imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, variable, cur_line_num, LOAD_INST_MEM));
    return;
  }
  
  // pre-statement
  OperationAssignment* pre_stmt = variable->GetPreStatement();
  if(pre_stmt) {
    EmitAssignment(pre_stmt);
    variable->SetPreStatement(nullptr);
  }
  
  // indices
  ExpressionList* indices = variable->GetIndices();

  // memory context
  MemoryContext mem_context;
  if(variable->GetEntry()->IsLocal()) {
    mem_context = LOCL;
  } else if(variable->GetEntry()->IsStatic()) {
    mem_context = CLS;
  } else {
    mem_context = INST;
  }

  // array variable
  if(indices) {
    int dimension = (int)indices->GetExpressions().size();
    EmitIndices(indices);

    // load instance or class memory
    if(mem_context == INST) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, variable, cur_line_num, LOAD_INST_MEM));
    } else if(mem_context == CLS) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, variable, cur_line_num, LOAD_CLS_MEM));
    }

    switch(variable->GetBaseType()->GetType()) {
    case frontend::BYTE_TYPE:
    case frontend::BOOLEAN_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, variable, cur_line_num, LOAD_INT_VAR, variable->GetId(), mem_context));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, variable, cur_line_num, LOAD_BYTE_ARY_ELM, dimension, mem_context));
      break;
      
    case frontend::CHAR_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, variable, cur_line_num, LOAD_INT_VAR, variable->GetId(), mem_context));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, variable, cur_line_num, LOAD_CHAR_ARY_ELM, dimension, mem_context));
      break;
      
    case frontend::INT_TYPE:
    case frontend::CLASS_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, variable, cur_line_num, LOAD_INT_VAR, variable->GetId(), mem_context));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, variable, cur_line_num, LOAD_INT_ARY_ELM, dimension, mem_context));
      break;

    case frontend::FLOAT_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, variable, cur_line_num, LOAD_INT_VAR, variable->GetId(), mem_context));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, variable, cur_line_num, LOAD_FLOAT_ARY_ELM, dimension, mem_context));
      break;

    default:
      break;
    }
  }
  // scalar variable
  else {
    // load instance or class memory
    if(mem_context == INST) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, variable, cur_line_num, LOAD_INST_MEM));
    } else if(mem_context == CLS) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, variable, cur_line_num, LOAD_CLS_MEM));
    }

    switch(variable->GetBaseType()->GetType()) {
    case frontend::BOOLEAN_TYPE:
    case frontend::BYTE_TYPE:
    case frontend::CHAR_TYPE:
    case frontend::INT_TYPE:
    case frontend::CLASS_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, variable, cur_line_num, LOAD_INT_VAR, variable->GetId(), mem_context));
      break;

    case frontend::FLOAT_TYPE:
      if(variable->GetEntry()->GetType()->GetDimension() > 0) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, variable, cur_line_num, LOAD_INT_VAR, variable->GetId(), mem_context));
      } else {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, variable, cur_line_num, LOAD_FLOAT_VAR, variable->GetId(), mem_context));
      }
      break;
      
    case frontend::FUNC_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, variable, cur_line_num, LOAD_FUNC_VAR, variable->GetId(), mem_context));
      break;

    default:
      break;
    }
  }

  EmitCast(variable);
  
  // emit subsequent method calls
  if(variable->GetMethodCall()) {
    switch(variable->GetBaseType()->GetType()) {
    case frontend::BOOLEAN_TYPE:
    case frontend::BYTE_TYPE:
    case frontend::CHAR_TYPE:
    case frontend::INT_TYPE:
    case frontend::FLOAT_TYPE:
    case frontend::FUNC_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, variable, cur_line_num, LOAD_INST_MEM));
      break;

    default:
      EmitClassCast(variable);
      break;
    }    
    EmitMethodCallExpression(static_cast<MethodCall*>(variable->GetMethodCall()), true);

    // set cast type
    MethodCall* tail = static_cast<MethodCall*>(variable->GetMethodCall());
    while(tail->GetMethodCall()) {
      tail = tail->GetMethodCall();
    }
    variable->SetEvalType(tail->GetCastType() ? tail->GetCastType() : tail->GetEvalType(), false);
  }

  // set post statement
  OperationAssignment* post_stmt = variable->GetPostStatement();
  if(post_stmt) {
    post_statements.push(post_stmt);
    variable->SetPostStatement(nullptr);
  }
}

/****************************
 * Translates array indices
 ****************************/
void IntermediateEmitter::EmitIndices(ExpressionList* indices)
{
  EmitExpressions(indices);
}

/****************************
 * Translates an assignment
 * statement
 ****************************/
void IntermediateEmitter::EmitAssignment(Assignment* assignment)
{
  cur_line_num = assignment->GetLineNumber();

  // expression
  EmitExpression(assignment->GetExpression());

  // assignment
  Variable* variable = assignment->GetVariable();
  ExpressionList* indices = variable->GetIndices();
  MemoryContext mem_context;

  // memory context
  if(variable->GetEntry()->IsLocal()) {
    mem_context = LOCL;
  } 
  else if(variable->GetEntry()->IsStatic()) {
    mem_context = CLS;
  } 
  else {
    mem_context = INST;
  }

  // array variable
  if(indices) {
    // operation assignment
    EntryType eval_type = variable->GetEvalType()->GetType();      
    switch(assignment->GetStatementType()) {
    case ADD_ASSIGN_STMT:
      EmitOperatorVariable(variable, mem_context);
      if(eval_type == frontend::FLOAT_TYPE) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, ADD_FLOAT));
      } 
      else {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, ADD_INT));
      }
      break;
      
    case SUB_ASSIGN_STMT:
      EmitOperatorVariable(variable, mem_context);
      if(eval_type == frontend::FLOAT_TYPE) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, SUB_FLOAT));
      } 
      else {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, SUB_INT));
      }
      break;
      
    case MUL_ASSIGN_STMT:
      EmitOperatorVariable(variable, mem_context);
      if(eval_type == frontend::FLOAT_TYPE) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, MUL_FLOAT));
      } 
      else {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, MUL_INT));
      }      
      break;
      
    case DIV_ASSIGN_STMT:
      EmitOperatorVariable(variable, mem_context);
      if(eval_type == frontend::FLOAT_TYPE) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, DIV_FLOAT));
      } 
      else {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, DIV_INT));
      }
      break;

    default:
      break;
    }
    
    int dimension = (int)indices->GetExpressions().size();
    EmitIndices(indices);

    // load instance or class memory
    if(mem_context == INST) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, LOAD_INST_MEM));
    } else if(mem_context == CLS) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, LOAD_CLS_MEM));
    }

    switch(variable->GetBaseType()->GetType()) {
    case frontend::BYTE_TYPE:
    case frontend::BOOLEAN_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, LOAD_INT_VAR, variable->GetId(), mem_context));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, STOR_BYTE_ARY_ELM, dimension, mem_context));
      break;

    case frontend::CHAR_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, LOAD_INT_VAR, variable->GetId(), mem_context));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, STOR_CHAR_ARY_ELM, dimension, mem_context));
      break;
      
    case frontend::INT_TYPE:
    case frontend::CLASS_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, LOAD_INT_VAR, variable->GetId(), mem_context));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, STOR_INT_ARY_ELM, dimension, mem_context));
      break;

    case frontend::FLOAT_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, LOAD_INT_VAR, variable->GetId(), mem_context));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, STOR_FLOAT_ARY_ELM, dimension, mem_context));
      break;

    default:
      break;
    }
  }
  // scalar variable
  else {
    // load instance or class memory
    if(mem_context == INST) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, LOAD_INST_MEM));
    } 
    else if(mem_context == CLS) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, LOAD_CLS_MEM));
    }
    
    // operation assignment
    EntryType eval_type = variable->GetEvalType()->GetType();      
    switch(assignment->GetStatementType()) {
    case ADD_ASSIGN_STMT:
      EmitOperatorVariable(variable, mem_context);
      if(eval_type == frontend::FLOAT_TYPE) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, ADD_FLOAT));
      } 
      else {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, ADD_INT));
      }
      // load instance or class memory
      if(mem_context == INST) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, LOAD_INST_MEM));
      } 
      else if(mem_context == CLS) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, LOAD_CLS_MEM));
      }
      break;
      
    case SUB_ASSIGN_STMT:
      EmitOperatorVariable(variable, mem_context);
      if(eval_type == frontend::FLOAT_TYPE) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, SUB_FLOAT));
      } 
      else {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, SUB_INT));
      }
      // load instance or class memory
      if(mem_context == INST) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, LOAD_INST_MEM));
      } 
      else if(mem_context == CLS) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, LOAD_CLS_MEM));
      }
      break;
      
    case MUL_ASSIGN_STMT:
      EmitOperatorVariable(variable, mem_context);
      if(eval_type == frontend::FLOAT_TYPE) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, MUL_FLOAT));
      } 
      else {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, MUL_INT));
      }      
      // load instance or class memory
      if(mem_context == INST) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, LOAD_INST_MEM));
      } 
      else if(mem_context == CLS) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, LOAD_CLS_MEM));
      }
      break;
      
    case DIV_ASSIGN_STMT:
      EmitOperatorVariable(variable, mem_context);
      if(eval_type == frontend::FLOAT_TYPE) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, DIV_FLOAT));
      } 
      else {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, DIV_INT));
      }
      // load instance or class memory
      if(mem_context == INST) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, LOAD_INST_MEM));
      } 
      else if(mem_context == CLS) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, LOAD_CLS_MEM));
      }
      break;

    default:
      break;
    }

    switch(variable->GetBaseType()->GetType()) {
    case frontend::BOOLEAN_TYPE:
    case frontend::BYTE_TYPE:
    case frontend::CHAR_TYPE:
    case frontend::INT_TYPE:
    case frontend::CLASS_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, STOR_INT_VAR, variable->GetId(), mem_context));
      break;

    case frontend::FLOAT_TYPE:
      if(variable->GetEntry()->GetType()->GetDimension() > 0) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, STOR_INT_VAR, variable->GetId(), mem_context));
      } 
      else {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, STOR_FLOAT_VAR, variable->GetId(), mem_context));
      }
      break;

    case frontend::FUNC_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, STOR_FUNC_VAR, variable->GetId(), mem_context));
      break;

    default:
      break;
    }
  }

  new_char_str_count = 0;
}

/****************************
 * Translates string concatenation
 * statement
 ****************************/
void IntermediateEmitter::EmitStringConcat(OperationAssignment* assignment)
{
  EmitExpression(assignment->GetExpression());
  EmitVariable(assignment->GetVariable());

  Expression* expression = assignment->GetExpression();
  while(expression->GetMethodCall()) {
    expression = expression->GetMethodCall();
  }
  
  // append 'Char'  
  if(expression->GetEvalType()->GetType() == CHAR_TYPE) {
    LibraryMethod* string_append_method = string_cls->GetMethod(L"System.String:Append:c,");
#ifdef _DEBUG
    assert(string_append_method);
#endif
    if(is_lib) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, LIB_MTHD_CALL, 0, 
                                                                                 L"System.String",
                                                                                 string_append_method->GetName()));

    }
    else {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, MTHD_CALL, 
                                                                                 (INT_VALUE)string_cls->GetId(), 
                                                                                 string_append_method->GetId(), 1L));
    }
  }
  // append 'Byte'  
  else if(expression->GetEvalType()->GetType() == BYTE_TYPE) {
    LibraryMethod* string_append_method = string_cls->GetMethod(L"System.String:Append:b,");
#ifdef _DEBUG
    assert(string_append_method);
#endif
    if(is_lib) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, LIB_MTHD_CALL, 0, 
                                                                                 L"System.String",
                                                                                 string_append_method->GetName()));

    }
    else {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, MTHD_CALL, 
                                                                                 (INT_VALUE)string_cls->GetId(), 
                                                                                 string_append_method->GetId(), 1L));
    }
  }
  // append 'Int'  
  else if(expression->GetEvalType()->GetType() == frontend::INT_TYPE) {
    LibraryMethod* string_append_method = string_cls->GetMethod(L"System.String:Append:i,");
#ifdef _DEBUG
    assert(string_append_method);
#endif
    if(is_lib) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, LIB_MTHD_CALL, 0, 
                                                                                 L"System.String",
                                                                                 string_append_method->GetName()));

    }
    else {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, MTHD_CALL, 
                                                                                 (INT_VALUE)string_cls->GetId(), 
                                                                                 string_append_method->GetId(), 1L));
    }
  }
  // append 'Float'  
  else if(expression->GetEvalType()->GetType() == frontend::FLOAT_TYPE) {
    LibraryMethod* string_append_method = string_cls->GetMethod(L"System.String:Append:f,");
#ifdef _DEBUG
    assert(string_append_method);
#endif
    if(is_lib) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, LIB_MTHD_CALL, 0, 
                                                                                 L"System.String",
                                                                                 string_append_method->GetName()));

    }
    else {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, MTHD_CALL, 
                                                                                 (INT_VALUE)string_cls->GetId(), 
                                                                                 string_append_method->GetId(), 1L));
    }
  }
  // append 'Bool'  
  else if(expression->GetEvalType()->GetType() == BOOLEAN_TYPE) {
    LibraryMethod* string_append_method = string_cls->GetMethod(L"System.String:Append:l,");
#ifdef _DEBUG
    assert(string_append_method);
#endif
    if(is_lib) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, LIB_MTHD_CALL, 0, 
                                                                                 L"System.String",
                                                                                 string_append_method->GetName()));

    }
    else {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, MTHD_CALL, 
                                                                                 (INT_VALUE)string_cls->GetId(), 
                                                                                 string_append_method->GetId(), 1L));
    }
  }
  // append string
  else {
    is_str_array = true;
    
    LibraryMethod* string_append_method = string_cls->GetMethod(L"System.String:Append:o.System.String,");
#ifdef _DEBUG
    assert(string_append_method);
#endif
    if(is_lib) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, LIB_MTHD_CALL, 0, 
                                                                                 L"System.String",
                                                                                 string_append_method->GetName()));
      
    }
    else {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(assignment, cur_line_num, MTHD_CALL, 
                                                                                 (INT_VALUE)string_cls->GetId(), 
                                                                                 string_append_method->GetId(), 1L));
    }
    is_str_array = false;
  }
}

/****************************
 * Translates a declaration
 ****************************/
void IntermediateEmitter::EmitDeclaration(Declaration* declaration)
{
  Statement* statement = declaration->GetAssignment();
  if(statement) {
    EmitStatement(statement);
  }
}

/****************************
 * Translates a method call
 * parameters
 ****************************/
void IntermediateEmitter::EmitMethodCallParameters(MethodCall* method_call)
{
  cur_line_num = static_cast<Statement*>(method_call)->GetLineNumber();

  // new array
  if(method_call->GetCallType() == NEW_ARRAY_CALL) {
    std::vector<Expression*> expressions = method_call->GetCallingParameters()->GetExpressions();
    
    for(size_t i = 0; i < expressions.size(); ++i) {
      EmitExpression(expressions[i]);
      EmitClassCast(expressions[i]);
    }

    is_new_inst = false;
  }
  // enum call
  else if(method_call->GetCallType() == ENUM_CALL) {
    if(method_call->GetMethodCall() && method_call->GetMethodCall()->GetCallType() != ENUM_CALL) {
      method_call = method_call->GetMethodCall();
    }

    if(method_call->GetEnumItem()) {
      const INT64_VALUE value = method_call->GetEnumItem()->GetId();
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, value));
      if(method_call->GetCastType() && method_call->GetCastType()->GetType() == frontend::FLOAT_TYPE) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, I2F));
      }
    } 
    else {
      // '@self' or '@parent' reference
      if(method_call->GetVariable()) {
        EmitVariable(method_call->GetVariable());  
      }
      else if(method_call->GetLibraryEnumItem()) {
        const INT64_VALUE value = method_call->GetLibraryEnumItem()->GetId();
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, value));
      }
    }
    is_new_inst = false;
  }
  // instance
  else if(method_call->GetCallType() == NEW_INST_CALL) {
    // declarations
    std::vector<Expression*> expressions = method_call->GetCallingParameters()->GetExpressions();
    for(size_t i = 0; i < expressions.size(); ++i) {
      EmitExpression(expressions[i]);
      EmitClassCast(expressions[i]);
      new_char_str_count = 0;
    }

    // new object instance
    Method* method = method_call->GetMethod();
    if(method) {
      if(is_lib) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LIB_NEW_OBJ_INST, method->GetClass()->GetName()));
      } 
      else {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, NEW_OBJ_INST, (long)method->GetClass()->GetId()));
      }
    } 
    else {
      LibraryMethod* lib_method = method_call->GetLibraryMethod();

      if(is_lib) {
        const std::wstring &klass_name = lib_method->GetLibraryClass()->GetName();
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LIB_NEW_OBJ_INST, klass_name));
      } 
      else {
        long klass_id = lib_method->GetLibraryClass()->GetId();
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, NEW_OBJ_INST, klass_id));
      }
    }
    is_new_inst = true;
  }
  // method call
  else {
    // declarations
    if(method_call->GetCallingParameters()) {
      std::vector<Expression*> expressions = method_call->GetCallingParameters()->GetExpressions();
      for(size_t i = 0; i < expressions.size(); ++i) {
        EmitExpression(expressions[i]);
        EmitClassCast(expressions[i]);
        new_char_str_count = 0;
      }
    }
    is_new_inst = false;
  }
}

/****************************
 * Translates a method call
 ****************************/
void IntermediateEmitter::EmitMethodCall(MethodCall* method_call, bool is_nested)
{
  cur_line_num = static_cast<Statement*>(method_call)->GetLineNumber();
  
  // new array
  if(method_call->GetCallType() == NEW_ARRAY_CALL) {
    std::vector<Expression*> expressions = method_call->GetCallingParameters()->GetExpressions();
    // array copy constructor
    if(expressions.size() == 1 && (expressions[0]->GetExpressionType() == VAR_EXPR || expressions[0]->GetExpressionType() == STAT_ARY_EXPR) &&
       expressions[0]->GetEvalType() && expressions[0]->GetEvalType()->GetDimension()) {

      Type* type = method_call->GetArrayType();
      switch(type->GetType()) {
      case BYTE_TYPE: {
        LibraryClass* lib_class = parsed_program->GetLinker()->SearchClassLibraries(L"System.$Byte", parsed_program->GetLibUses());
        if(lib_class) {
          LibraryMethod* lib_mthd = lib_class->GetMethod(L"System.$Byte:Copy:b*,");
          if(lib_mthd) {
            imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_INST_MEM));
            imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, MTHD_CALL, lib_class->GetId(), lib_mthd->GetId(), lib_mthd->IsNative()));
          }
        }
      }
        break;

      case frontend::CHAR_TYPE: {
        LibraryClass* lib_class = parsed_program->GetLinker()->SearchClassLibraries(L"System.$Char", parsed_program->GetLibUses());
        if(lib_class) {
          LibraryMethod* lib_mthd = lib_class->GetMethod(L"System.$Char:Copy:c*,");
          if(lib_mthd) {
            imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_INST_MEM));
            imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, MTHD_CALL, lib_class->GetId(), lib_mthd->GetId(), lib_mthd->IsNative()));
          }
        }
      }
        break;

      case frontend::INT_TYPE: {
        LibraryClass* lib_class = parsed_program->GetLinker()->SearchClassLibraries(L"System.$Int", parsed_program->GetLibUses());
        if(lib_class) {
          LibraryMethod* lib_mthd = lib_class->GetMethod(L"System.$Int:Copy:i*,");
          if(lib_mthd) {
            imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_INST_MEM));
            imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, MTHD_CALL, lib_class->GetId(), lib_mthd->GetId(), lib_mthd->IsNative()));
          }
        }
      }
        break;

      case frontend::FLOAT_TYPE: {
        LibraryClass* lib_class = parsed_program->GetLinker()->SearchClassLibraries(L"System.$Float", parsed_program->GetLibUses());
        if(lib_class) {
          LibraryMethod* lib_mthd = lib_class->GetMethod(L"System.$Float:Copy:f*,");
          if(lib_mthd) {
            imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_INST_MEM));
            imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, MTHD_CALL, lib_class->GetId(), lib_mthd->GetId(), lib_mthd->IsNative()));
          }
        }
      }
        break;

      default:
        break;
      }
    }
    // new array instance
    else {
      switch(method_call->GetArrayType()->GetType()) {
      case frontend::BYTE_TYPE:
      case frontend::BOOLEAN_TYPE:
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, NEW_BYTE_ARY, (long)expressions.size()));
        break;

      case frontend::CHAR_TYPE:
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, NEW_CHAR_ARY, (long)expressions.size()));
        break;

      case frontend::CLASS_TYPE:
      case frontend::INT_TYPE:
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, NEW_INT_ARY, (long)expressions.size()));
        break;

      case frontend::FLOAT_TYPE:
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, NEW_FLOAT_ARY, (long)expressions.size()));
        break;

      default:
        break;
      }
    }
  } 
  else {
    // literal and variable method calls
    Variable* variable = method_call->GetVariable();
    SymbolEntry* entry = method_call->GetEntry();

    bool is_index_size = false;
    if(variable && variable->IsInternalVariable()) {
      entry = variable->GetEntry();
      is_index_size = true;
    }
    
    if(variable && method_call->GetCallType() == METHOD_CALL) {
      // emit variable
      EmitVariable(variable);            
      EmitClassCast(method_call);
    }
    else if(variable && method_call->GetTypeOf()) {
      EmitVariable(variable);
    }
    else if(entry) {
      // memory context
      MemoryContext mem_context;
      if(entry->IsLocal()) {
        mem_context = LOCL;
      } 
      else if(entry->IsStatic()) {
        mem_context = CLS;
      } 
      else {
        mem_context = INST;
      }
      
      if(entry->GetType()->GetDimension() > 0 && entry->GetType()->GetType() == CLASS_TYPE) {
        // load instance or class memory
        if(mem_context == INST) {
          imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_INST_MEM));
        } else if(mem_context == CLS) {
          imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_CLS_MEM));
        }
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_INT_VAR, entry->GetId(), mem_context));
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_INST_MEM));
      } 
      else if(!entry->IsSelf()) {
        switch(entry->GetType()->GetType()) {
        case frontend::BOOLEAN_TYPE:
        case frontend::BYTE_TYPE:
        case frontend::CHAR_TYPE:
        case frontend::INT_TYPE:
        case frontend::CLASS_TYPE:
          // load instance or class memory
          if(mem_context == INST) {
            imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_INST_MEM));
          } else if(mem_context == CLS) {
            imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_CLS_MEM));
          }
          imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_INT_VAR, entry->GetId(), mem_context));
          break;

        case frontend::FLOAT_TYPE:
          // load instance or class memory
          if(mem_context == INST) {
            imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_INST_MEM));
          } 
          else if(mem_context == CLS) {
            imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_CLS_MEM));
          }

          if(entry->GetType()->GetDimension() > 0) {
            imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_INT_VAR, entry->GetId(), mem_context));
          } 
          else {
            imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_FLOAT_VAR, entry->GetId(), mem_context));
          }
          break;
    
        case frontend::FUNC_TYPE:
          // load instance or class memory
          if(mem_context == INST) {
            imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_INST_MEM));
          } else if(mem_context == CLS) {
            imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_CLS_MEM));
          }
          imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_FUNC_VAR, entry->GetId(), mem_context));
          break;

        default:
          break;
        }
      } else {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_INST_MEM));
      }
    }
    
    // program method call
    if(method_call->GetMethod()) {
      Method* method = method_call->GetMethod();
      if(method_call->GetCallType() == PARENT_CALL ||
         (method->GetMethodType() != NEW_PUBLIC_METHOD &&
          method->GetMethodType() != NEW_PRIVATE_METHOD) ||
         current_method == method) {
        if(entry) {
          switch(entry->GetType()->GetType()) {
          case frontend::BOOLEAN_TYPE:
          case frontend::BYTE_TYPE:
          case frontend::CHAR_TYPE:
          case frontend::INT_TYPE:
          case frontend::FLOAT_TYPE:
          case frontend::FUNC_TYPE:
            imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_INST_MEM));
            break;

          case frontend::CLASS_TYPE:
            if(parsed_program->GetLinker()->SearchEnumLibraries(entry->GetType()->GetName(), parsed_program->GetLibUses()) || 
               SearchProgramEnums(entry->GetType()->GetName())) {
              imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_INST_MEM));
            }
            break;
            
          default:
            break;
          }
          // enum check
          if(entry->GetType()->GetType() == frontend::CLASS_TYPE && 
             SearchProgramEnums(entry->GetType()->GetName())) {
            imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_INST_MEM));
          }
        }
        else if(!is_nested && (!variable || variable->GetEntry()->GetType()->GetType() != CLASS_TYPE)) {
          imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_INST_MEM));
        }
        else if(method_call->IsEnumCall()) {
          imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_INST_MEM));
        }
      } 
      else if((current_method->GetMethodType() == NEW_PUBLIC_METHOD || current_method->GetMethodType() == NEW_PRIVATE_METHOD) && 
              (method->GetMethodType() == NEW_PUBLIC_METHOD || method->GetMethodType() == NEW_PRIVATE_METHOD) && !is_new_inst) {       
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_INST_MEM));
      }
    }
    // library method call
    else if(method_call->GetLibraryMethod()) {
      LibraryMethod* lib_method = method_call->GetLibraryMethod();
      if(lib_method->GetMethodType() != NEW_PUBLIC_METHOD &&
         lib_method->GetMethodType() != NEW_PRIVATE_METHOD) {
        if(entry) {
          switch(entry->GetType()->GetType()) {
          case frontend::BOOLEAN_TYPE:
          case frontend::BYTE_TYPE:
          case frontend::CHAR_TYPE:
          case frontend::INT_TYPE:
          case frontend::FLOAT_TYPE:
          case frontend::FUNC_TYPE:
            imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_INST_MEM));
            break;

          case frontend::CLASS_TYPE:
            if(parsed_program->GetLinker()->SearchEnumLibraries(entry->GetType()->GetName(), parsed_program->GetLibUses()) || 
               SearchProgramEnums(entry->GetType()->GetName()) || is_index_size) {
              imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_INST_MEM));
            }
            break;
            
          default:
            break;
          }
          // enum check
          if(entry->GetType()->GetType() == frontend::CLASS_TYPE && 
             parsed_program->GetLinker()->SearchEnumLibraries(entry->GetType()->GetName(), parsed_program->GetLibUses())) {
            imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_INST_MEM));
          }
        }
        else if(!is_nested && (!variable || variable->GetEntry()->GetType()->GetType() != CLASS_TYPE)) {
          imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_INST_MEM));
        }
        else if(method_call->IsEnumCall()) {
          imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_INST_MEM));
        }
        // note: localized hack for edge case
        else if(is_nested && lib_method->GetLibraryClass()->GetName() == BASE_ARRAY_CLASS_ID) {
          imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_INST_MEM));
        }
      } 
      else if((current_method->GetMethodType() == NEW_PUBLIC_METHOD || current_method->GetMethodType() == NEW_PRIVATE_METHOD) && 
              (lib_method->GetMethodType() == NEW_PUBLIC_METHOD || lib_method->GetMethodType() == NEW_PRIVATE_METHOD) && !is_new_inst) {        
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LOAD_INST_MEM));
      }
    }    
    
    // program method call
    if(method_call->GetMethod()) {
      Method* method = method_call->GetMethod();
      if(is_lib) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LIB_MTHD_CALL, method->IsNative(),
                                                                                   method->GetClass()->GetName(),
                                                                                   method->GetEncodedName()));
      } else {
        int klass_id = method->GetClass()->GetId();
        int method_id = method->GetId();
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, MTHD_CALL, klass_id, method_id, method->IsNative()));
      }
    }
    // library method call
    else if(method_call->GetLibraryMethod()) {
      LibraryMethod* lib_method = method_call->GetLibraryMethod();
      if(is_lib) {
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, LIB_MTHD_CALL, lib_method->IsNative(),
                                                                                   lib_method->GetLibraryClass()->GetName(),
                                                                                   lib_method->GetName()));
      } else {
        int klass_id = lib_method->GetLibraryClass()->GetId();
        int method_id = lib_method->GetId();
        imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, static_cast<Expression*>(method_call), cur_line_num, MTHD_CALL, klass_id, method_id, lib_method->IsNative()));
      }
    }
  }  
            
  new_char_str_count = 0;
  is_new_inst = false;
}

/****************************
 * Emits expressions
 ****************************/
void IntermediateEmitter::EmitExpressions(ExpressionList* declarations)
{
  std::vector<Expression*> expressions = declarations->GetExpressions();
  for(size_t i = 0; i < expressions.size(); ++i) {
    EmitExpression(expressions[i]);
  }
}

/****************************
 * Calculates the memory space
 * needed for a method or class
 ****************************/
int IntermediateEmitter::CalculateEntrySpace(SymbolTable* table, int &index, IntermediateDeclarations* declarations, bool is_static)
{
  if(table) {
    int var_space = 0;
    std::vector<SymbolEntry*> entries = table->GetEntries();
        
    for(size_t i = 0; i < entries.size(); ++i) {
      SymbolEntry* entry = entries[i];
      if(!entry->IsSelf() && entry->IsStatic() == is_static) {
        switch(entry->GetType()->GetType()) {
        case frontend::BOOLEAN_TYPE:
          if(entry->GetType()->GetDimension() > 0) {
#ifdef _DEBUG
            GetLogger() << L"\t" << index << L": INT_ARY_PARM: name=" << entry->GetName() 
      << L", dim=" << entry->GetType()->GetDimension() << std::endl;
#endif
            declarations->AddParameter(new IntermediateDeclaration(entry->GetName(), INT_ARY_PARM));
          } 
          else {
#ifdef _DEBUG
            GetLogger() << L"\t" << index << L": INT_PARM: name=" << entry->GetName() << std::endl;
#endif
            declarations->AddParameter(new IntermediateDeclaration(entry->GetName(), INT_PARM));
          }
          entry->SetId(index++);
          var_space++;
          break;

        case frontend::BYTE_TYPE:
          if(entry->GetType()->GetDimension() > 0) {
#ifdef _DEBUG
            GetLogger() << L"\t" << index << L": BYTE_ARY_PARM: name=" << entry->GetName() << std::endl;
#endif
            declarations->AddParameter(new IntermediateDeclaration(entry->GetName(), BYTE_ARY_PARM));
          } 
          else {
#ifdef _DEBUG
            GetLogger() << L"\t" << index << L": INT_PARM: name=" << entry->GetName() << std::endl;
#endif
            declarations->AddParameter(new IntermediateDeclaration(entry->GetName(), INT_PARM));
          }
          entry->SetId(index++);
          var_space++;
          break;

        case frontend::INT_TYPE:
          if(entry->GetType()->GetDimension() > 0) {
#ifdef _DEBUG
            GetLogger() << L"\t" << index << L": INT_ARY_PARM: name=" << entry->GetName()
      << L", dim=" << entry->GetType()->GetDimension() << std::endl;
#endif
            declarations->AddParameter(new IntermediateDeclaration(entry->GetName(), INT_ARY_PARM));
          } 
          else {
#ifdef _DEBUG
            GetLogger() << L"\t" << index << L": INT_PARM: name=" << entry->GetName() << std::endl;
#endif
            declarations->AddParameter(new IntermediateDeclaration(entry->GetName(), INT_PARM));
          }
          entry->SetId(index++);
          var_space++;
          break;

        case frontend::CHAR_TYPE:
          if(entry->GetType()->GetDimension() > 0) {
#ifdef _DEBUG
            GetLogger() << L"\t" << index << L": CHAR_ARY_PARM: name=" << entry->GetName() << std::endl;
#endif
            declarations->AddParameter(new IntermediateDeclaration(entry->GetName(), CHAR_ARY_PARM));
          } 
          else {
#ifdef _DEBUG
            GetLogger() << L"\t" << index << L": CHAR_PARM: name=" << entry->GetName() << std::endl;
#endif
            declarations->AddParameter(new IntermediateDeclaration(entry->GetName(), CHAR_PARM));
          }
          entry->SetId(index++);
          var_space++;
          break;

        case frontend::CLASS_TYPE:
          // object array
          if(entry->GetType()->GetDimension() > 0) {
            if(parsed_program->GetClass(entry->GetType()->GetName())) {
#ifdef _DEBUG
              GetLogger() << L"\t" << index << L": OBJ_ARY_PARM: name=" << entry->GetName() << std::endl;
#endif
              declarations->AddParameter(new IntermediateDeclaration(entry->GetName(), OBJ_ARY_PARM));
            } 
            else if(SearchProgramEnums(entry->GetType()->GetName())) {
#ifdef _DEBUG
              GetLogger() << L"\t" << index << L": INT_ARY_PARM: name=" << entry->GetName() << std::endl;
#endif
              declarations->AddParameter(new IntermediateDeclaration(entry->GetName(), INT_ARY_PARM));
            } 
            else if(SearchProgramEnums(current_class->GetName() + L"#" + entry->GetType()->GetName())) {
#ifdef _DEBUG
              GetLogger() << L"\t" << index << L": INT_ARY_PARM: name=" << entry->GetName() << std::endl;
#endif
              declarations->AddParameter(new IntermediateDeclaration(entry->GetName(), INT_PARM));
            }
            else if(parsed_program->GetLinker()->SearchEnumLibraries(entry->GetType()->GetName(), parsed_program->GetLibUses())) {
#ifdef _DEBUG
              GetLogger() << L"\t" << index << L": INT_ARY_PARM: name=" << entry->GetName() << std::endl;
#endif
              declarations->AddParameter(new IntermediateDeclaration(entry->GetName(), INT_ARY_PARM));
            } 
            else {
#ifdef _DEBUG
              GetLogger() << L"\t" << index << L": OBJ_ARY_PARM: name=" << entry->GetName() << std::endl;
#endif
              declarations->AddParameter(new IntermediateDeclaration(entry->GetName(), OBJ_ARY_PARM));
            }
          }
          // object
          else {
            if(SearchProgramClasses(entry->GetType()->GetName())) {
#ifdef _DEBUG
              GetLogger() << L"\t" << index << L": OBJ_PARM: name=" << entry->GetName() << std::endl;
#endif
              declarations->AddParameter(new IntermediateDeclaration(entry->GetName(), OBJ_PARM));
            } 
            else if(SearchProgramEnums(entry->GetType()->GetName())) {
#ifdef _DEBUG
              GetLogger() << L"\t" << index << L": INT_PARM: name=" << entry->GetName() << std::endl;
#endif
              declarations->AddParameter(new IntermediateDeclaration(entry->GetName(), INT_PARM));
            }
            else if(SearchProgramEnums(current_class->GetName() + L"#" + entry->GetType()->GetName())) {
#ifdef _DEBUG
              GetLogger() << L"\t" << index << L": INT_PARM: name=" << entry->GetName() << std::endl;
#endif
              declarations->AddParameter(new IntermediateDeclaration(entry->GetName(), INT_PARM));
            }
            else if(parsed_program->GetLinker()->SearchEnumLibraries(entry->GetType()->GetName(), parsed_program->GetLibUses())) {
#ifdef _DEBUG
              GetLogger() << L"\t" << index << L": INT_PARM: name=" << entry->GetName() << std::endl;
#endif
              declarations->AddParameter(new IntermediateDeclaration(entry->GetName(), INT_PARM));
            } 
            else {
#ifdef _DEBUG
              GetLogger() << L"\t" << index << L": OBJ_PARM: name=" << entry->GetName() << std::endl;
#endif
              declarations->AddParameter(new IntermediateDeclaration(entry->GetName(), OBJ_PARM));
            }
          }
          entry->SetId(index++);
          var_space++;
          break;

        case frontend::FLOAT_TYPE:
          if(entry->GetType()->GetDimension() > 0) {
#ifdef _DEBUG
            GetLogger() << L"\t" << index << L": FLOAT_ARY_PARM: name=" << entry->GetName() << std::endl;
#endif
            declarations->AddParameter(new IntermediateDeclaration(entry->GetName(), FLOAT_ARY_PARM));
            entry->SetId(index++);
            var_space++;
          } 
          else {
#ifdef _DEBUG
            GetLogger() << L"\t" << index << L": FLOAT_PARM: name=" << entry->GetName() << std::endl;
#endif
            declarations->AddParameter(new IntermediateDeclaration(entry->GetName(), FLOAT_PARM));
            entry->SetId(index++);
            var_space++;
          }
          break;
    
        case frontend::FUNC_TYPE:          
#ifdef _DEBUG
          GetLogger() << L"\t" << index << L": FUNC_PARM: name=" << entry->GetName() << std::endl;
#endif
          declarations->AddParameter(new IntermediateDeclaration(entry->GetName(), FUNC_PARM));
          entry->SetId(index);
          index += 2;
          var_space += 2;
          break;

        default:
          break;
        }
      }
    }
    
    return var_space * sizeof(INT64_VALUE);
  }

  return 0;
}

/****************************
 * Calculates the memory space
 * needed for a method or class
 ****************************/
int IntermediateEmitter::CalculateEntrySpace(IntermediateDeclarations* declarations, bool is_static)
{
  int size = 0;
  int index = 0;

  // class
  if(!current_method) {
    std::stack<Class*> parents;

    // setup dependency order
    Class* parent = current_class->GetParent();
    LibraryClass* lib_parent = current_class->GetLibraryParent();
    while(parent || lib_parent) {
      if(parent) {
        parents.push(parent);

        // next        
        lib_parent = parent->GetLibraryParent();
        parent = parent->GetParent();
      }
      else if(lib_parent) {
        break;
      }
    }
    
    // emit derived library dependencies
    if(lib_parent) {
      if(is_static) {
        size += lib_parent->GetClassSpace();
        // update entries
        std::vector<IntermediateDeclaration*> lib_cls_dclrs = lib_parent->GetClassEntries()->GetParameters();
        for(size_t i = 0; i < lib_cls_dclrs.size(); ++i) {
          declarations->AddParameter(lib_cls_dclrs[i]->Copy());
        }
      }
      else {
        size += lib_parent->GetInstanceSpace();
        // update entries
        std::vector<IntermediateDeclaration*> lib_inst_dclrs = lib_parent->GetInstanceEntries()->GetParameters();
        for(size_t i = 0; i < lib_inst_dclrs.size(); ++i) {
          declarations->AddParameter(lib_inst_dclrs[i]->Copy());
        }
      }
      index = size / sizeof(INT64_VALUE);
    }

    // emit source dependencies
    while(!parents.empty()) {
      parent = parents.top();
      parents.pop();

      SymbolTable* table = parsed_bundle->GetSymbolTableManager()->GetSymbolTable(parent->GetName());
      // parent may be defined in another file
      if(!table) {
        Class* prgm_cls = SearchProgramClasses(parent->GetName());
        if(prgm_cls) {
          table = prgm_cls->GetSymbolTable();
        }
      }
      size += CalculateEntrySpace(table, index, declarations, is_static);
    }
    
    // emit current class
    size += CalculateEntrySpace(current_table, index, declarations, is_static);
  }
  // method
  else {
    if(current_method->HasAndOr()) {
      size = index = 1;
    }
    size = CalculateEntrySpace(current_table, index, declarations, false);
  }

  return size;
}

void IntermediateEmitter::EmitClassCast(Expression* expression)
{
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
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression,cur_line_num, LIB_OBJ_INST_CAST, expression->GetToClass()->GetName()));
    }
    else {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression,cur_line_num, OBJ_INST_CAST, (long)expression->GetToClass()->GetId()));
    }
  }
  // class library cast
  else if(expression->GetToLibraryClass() && !(expression->GetExpressionType() == METHOD_CALL_EXPR &&
          static_cast<MethodCall*>(expression)->GetCallType() == NEW_ARRAY_CALL)) {
    if(is_lib) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression,cur_line_num, LIB_OBJ_INST_CAST, expression->GetToLibraryClass()->GetName()));
    }
    else {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, expression,cur_line_num, OBJ_INST_CAST, (long)expression->GetToLibraryClass()->GetId()));
    }
  }
}

frontend::Class* IntermediateEmitter::SearchProgramClasses(const std::wstring& klass_name)
{
  Class* klass = parsed_program->GetClass(klass_name);
  if(!klass) {
    klass = parsed_program->GetClass(parsed_bundle->GetName() + L"." + klass_name);
    if(!klass) {
      std::vector<std::wstring> uses = parsed_program->GetLibUses();
      for(size_t i = 0; !klass && i < uses.size(); ++i) {
        klass = parsed_program->GetClass(uses[i] + L"." + klass_name);
      }
    }
  }

  return klass;
}

frontend::Enum* IntermediateEmitter::SearchProgramEnums(const std::wstring& eenum_name)
{
  Enum* eenum = parsed_program->GetEnum(eenum_name);
  if(!eenum) {
    eenum = parsed_program->GetEnum(parsed_bundle->GetName() + L"." + eenum_name);
    if(!eenum) {
      std::vector<std::wstring> uses = parsed_program->GetLibUses();
      for(size_t i = 0; !eenum && i < uses.size(); ++i) {
        eenum = parsed_program->GetEnum(uses[i] + L"." + eenum_name);
      }
    }
  }

  return eenum;
}

void IntermediateEmitter::EmitOperatorVariable(Variable* variable, MemoryContext mem_context)
{
  // indices
  ExpressionList* indices = variable->GetIndices();

  // array variable
  if(indices) {
    int dimension = (int)indices->GetExpressions().size();
    EmitIndices(indices);

    // load instance or class memory
    if(mem_context == INST) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, variable,cur_line_num, LOAD_INST_MEM));
    }
    else if(mem_context == CLS) {
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, variable,cur_line_num, LOAD_CLS_MEM));
    }

    switch(variable->GetBaseType()->GetType()) {
    case frontend::BYTE_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, variable,cur_line_num, LOAD_INT_VAR, variable->GetId(), mem_context));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, variable,cur_line_num, LOAD_BYTE_ARY_ELM, dimension, mem_context));
      break;

    case frontend::CHAR_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, variable,cur_line_num, LOAD_INT_VAR, variable->GetId(), mem_context));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, variable,cur_line_num, LOAD_CHAR_ARY_ELM, dimension, mem_context));
      break;

    case frontend::INT_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, variable,cur_line_num, LOAD_INT_VAR, variable->GetId(), mem_context));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, variable,cur_line_num, LOAD_INT_ARY_ELM, dimension, mem_context));
      break;

    case frontend::FLOAT_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, variable,cur_line_num, LOAD_INT_VAR, variable->GetId(), mem_context));
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, variable,cur_line_num, LOAD_FLOAT_ARY_ELM, dimension, mem_context));
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
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, variable,cur_line_num, LOAD_INT_VAR, variable->GetId(), mem_context));
      break;

    case frontend::FLOAT_TYPE:
      imm_block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(current_statement, variable,cur_line_num, LOAD_FLOAT_VAR, variable->GetId(), mem_context));
      break;

    default:
      break;
    }
  }
}
