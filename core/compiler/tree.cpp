/***************************************************************************
 * Language parse tree.
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

#include "tree.h"

using namespace frontend;

/****************************
 * Expression class
 ****************************/
void Expression::SetMethodCall(MethodCall* call)
{
  if(call) {
    method_call = call;
    call->SetPreviousExpression(this);
  }
}

/****************************
 * TreeFactory class
 ****************************/
TreeFactory* TreeFactory::instance;

TreeFactory* TreeFactory::Instance()
{
  if(!instance) {
    instance = new TreeFactory;
  }

  return instance;
}

/****************************
 * Creates a default copy of
 * an entry
 ****************************/
SymbolEntry* SymbolEntry::Copy() 
{
  return TreeFactory::Instance()->MakeSymbolEntry(file_name, line_num,
						  name, type, is_static,
						  is_local, is_self);
}

/****************************
 * Sets the id for all
 * references to a variable.
 ****************************/
void SymbolEntry::SetId(int i)
{
  if(id < 0) {
    id = i;
    for(size_t j = 0; j < variables.size(); ++j) {
      variables[j]->SetId(i);
    }
  }
}

/****************************
 * Encodes a method parameter
 ****************************/
wstring Method::EncodeType(Type* type, Class* klass, ParsedProgram* program, Linker* linker)
{
  wstring name;
  if(type) {
    // type
    switch(type->GetType()) {
    case BOOLEAN_TYPE:
      name = L'l';
      break;

    case BYTE_TYPE:
      name = L'b';
      break;

    case INT_TYPE:
      name = L'i';
      break;

    case FLOAT_TYPE:
      name = L'f';
      break;

    case CHAR_TYPE:
      name = L'c';
      break;

    case NIL_TYPE:
      name = L'n';
      break;

    case VAR_TYPE:
      name = L'v';
      break;

    case CLASS_TYPE: {
      name = L"o.";
      
      // program class check
      const wstring type_klass_name = type->GetClassName();
      Class* prgm_klass = program->GetClass(type_klass_name);
      if(prgm_klass) {
        name += prgm_klass->GetName();
      }
      else {
        // full path resolution
        vector<wstring> uses = program->GetUses();
        for(size_t i = 0; !prgm_klass && i < uses.size(); ++i) {
          prgm_klass = program->GetClass(uses[i] + L"." + type_klass_name);
        }
        
        // program enum check
        if(prgm_klass) {
          name += prgm_klass->GetName();
        }
        else {
          // full path resolution
          Enum* prgm_enum = program->GetEnum(type_klass_name);
          vector<wstring> uses = program->GetUses();
          for(size_t i = 0; !prgm_enum && i < uses.size(); ++i) {
            prgm_enum = program->GetEnum(uses[i] + L"." + type_klass_name);
          }
          
          if(prgm_enum) {
            name += prgm_enum->GetName();
          }
          else {
            const wstring type_klass_name_ext = klass->GetName() + L"#" + type_klass_name;
            prgm_enum = program->GetEnum(type_klass_name_ext);
            if(prgm_enum) {
              name += type_klass_name_ext;
            }
          }
        }
      }
      
      // search libaraires      
      if(name == L"o.") {
        LibraryClass* lib_klass = linker->SearchClassLibraries(type_klass_name, program->GetUses());
        if(lib_klass) {
          name += lib_klass->GetName();
        } 
        else {
          LibraryEnum* lib_enum = linker->SearchEnumLibraries(type_klass_name, program->GetUses());
          if(lib_enum) {
            name += lib_enum->GetName();
          }
          else {
            const wstring type_klass_name_ext = klass->GetName() + L"#" + type_klass_name;
            lib_enum = linker->SearchEnumLibraries(type_klass_name_ext, program->GetUses());
            if(lib_enum) {
              name += type_klass_name_ext;
            }
          }
        }
      }      
    }
      break;
      
    case FUNC_TYPE:  {
      name = L"m.";
      if(type->GetClassName().size() == 0) {
        name += EncodeFunctionType(type->GetFunctionParameters(), type->GetFunctionReturn(), klass, program, linker);
      }
      else {
        name += type->GetClassName();
      }
    }
      break;
    }
    // dimension
    for(int i = 0; i < type->GetDimension(); ++i) {
      name += L'*';
    }
  }

  return name;
}

/****************************
 * Validates a static array
 ****************************/
void StaticArray::Validate(StaticArray* array) {
  vector<Expression*> static_array = array->GetElements()->GetExpressions();
  for(size_t i = 0; i < static_array.size(); ++i) { 
    if(static_array[i]) {
      if(static_array[i]->GetExpressionType() == STAT_ARY_EXPR) {
	if(i == 0) {
	  dim++;
	}
	Validate(static_cast<StaticArray*>(static_array[i]));
      }
      else {
	// check lengths
	if(cur_width == -1) {
	  cur_width = (int)static_array.size();
	}
	if(cur_width != (int)static_array.size()) {
	  matching_lengths = false;
	}      
	// check types
	if(cur_type == VAR_EXPR) {
	  cur_type = static_array[i]->GetExpressionType();
	}
	else if(cur_type != static_array[i]->GetExpressionType()) {
	  matching_types = false;
	}
      }
    }
  }
}

/****************************
 * Get all static elements
 ****************************/
ExpressionList* StaticArray::GetAllElements() {
  if(!all_elements) {
    all_elements = TreeFactory::Instance()->MakeExpressionList();  
    GetAllElements(this, all_elements);
    
    // change row/column order    
    if(dim == 2) {
      ExpressionList* temp = TreeFactory::Instance()->MakeExpressionList();
      vector<Expression*> elements = all_elements->GetExpressions();
      // update indices
      for(int i = 0; i < cur_width; ++i) {
	for(int j = 0; j < cur_height; ++j) {
	  const int index = j * cur_width + i;
	  temp->AddExpression(elements[index]);
	}
      }      
      all_elements = temp;
    }
  }
  
  return all_elements;
}

/****************************
 * Get the size for 
 * multidimensional array
 ****************************/
vector<int> StaticArray::GetSizes() {
  if(!sizes.size()) {
    int count = 0;
    GetSizes(this, count);
    
    sizes.push_back(cur_width);
    if(count) {
      sizes.push_back(count);
    }
  }
  
  return sizes;
}

Variable* Variable::Copy() {
  Variable* v = TreeFactory::Instance()->MakeVariable(file_name, line_num, name);
  v->indices = indices;
  return v;
}

Declaration* Declaration::Copy() {
  if(assignment) {
    return TreeFactory::Instance()->MakeDeclaration(file_name, line_num, entry->Copy(), child, assignment);
  }

  return TreeFactory::Instance()->MakeDeclaration(file_name, line_num, entry->Copy(), child);
}
