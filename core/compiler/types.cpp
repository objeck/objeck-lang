/***************************************************************************
 * Defines internal language types.
 *
 * Copyright (c) 2008-2021, Randy Hollines
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, hare permitted provided that the following conditions are met:
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

#include "types.h"

using namespace frontend;

/****************************
 * TypeFactory class
 ****************************/
TypeFactory* TypeFactory::instance;

TypeFactory* TypeFactory::Instance()
{
  if(!instance) {
    instance = new TypeFactory;
  }

  return instance;
}

/****************************
 * Basic data type classes.
 ****************************/
Type* Type::CharStringType()
{
  return TypeFactory::Instance()->MakeType(CLASS_TYPE, L"System.String");
}

/********************************
 * Routines for parsing library
 * encode strings
 ********************************/
vector<frontend::Type*> TypeParser::ParseParameters(const wstring& param_str)
{
  vector<frontend::Type*> types;

  size_t index = 0;
  while(index < param_str.size()) {
    frontend::Type* type = nullptr;
    int dimension = 0;
    switch(param_str[index]) {
    case 'l':
      type = frontend::TypeFactory::Instance()->MakeType(frontend::BOOLEAN_TYPE);
      index++;
      break;

    case 'b':
      type = frontend::TypeFactory::Instance()->MakeType(frontend::BYTE_TYPE);
      index++;
      break;

    case 'i':
      type = frontend::TypeFactory::Instance()->MakeType(frontend::INT_TYPE);
      index++;
      break;

    case 'f':
      type = frontend::TypeFactory::Instance()->MakeType(frontend::FLOAT_TYPE);
      index++;
      break;

    case 'c':
      type = frontend::TypeFactory::Instance()->MakeType(frontend::CHAR_TYPE);
      index++;
      break;

    case 'n':
      type = frontend::TypeFactory::Instance()->MakeType(frontend::NIL_TYPE);
      index++;
      break;

    case 'm': {
      size_t start = index;

      const wstring prefix = L"m.(";
      int nested_count = 1;
      size_t found = param_str.find(prefix);
      while(found != wstring::npos) {
        nested_count++;
        found = param_str.find(prefix, found + prefix.size());
      }

      while(nested_count--) {
        while(index < param_str.size() && param_str[index] != L'~') {
          index++;
        }
        if(param_str[index] == L'~') {
          index++;
        }
      }

      while(index < param_str.size() && param_str[index] != L',') {
        index++;
      }

      const wstring name = param_str.substr(start, index - start - 1);
      type = frontend::TypeFactory::Instance()->MakeType(frontend::FUNC_TYPE, name);
      ParseFunctionalType(type);
    }
      break;

    case 'o': {
      index += 2;
      size_t start = index;
      while(index < param_str.size() && param_str[index] != L'*' && param_str[index] != L',' && param_str[index] != L'|') {
        index++;
      }
      size_t end = index;
      const wstring& cls_name = param_str.substr(start, end - start);
      type = frontend::TypeFactory::Instance()->MakeType(frontend::CLASS_TYPE, cls_name);
    }
      break;
    }

    // set generics
    if(index < param_str.size() && param_str[index] == L'|') {
      vector<frontend::Type*> generic_types;
      do {
        index++;
        size_t start = index;
        while(index < param_str.size() && param_str[index] != L'*' && param_str[index] != L',' && param_str[index] != L'|') {
          index++;
        }
        size_t end = index;

        const wstring generic_name = param_str.substr(start, end - start);
        generic_types.push_back(frontend::TypeFactory::Instance()->MakeType(frontend::CLASS_TYPE, generic_name));
      } 
      while(index < param_str.size() && param_str[index] == L'|');

      if(type) {
        type->SetGenerics(generic_types);
      }
    }

    // set dimension
    while(index < param_str.size() && param_str[index] == L'*') {
      dimension++;
      index++;
    }

    if(type) {
      type->SetDimension(dimension);
    }

    types.push_back(type);

#ifdef _DEBUG
    assert(index >= param_str.size() || param_str[index] == L',');
#endif
    index++;
  }

  return types;
}

frontend::Type* TypeParser::ParseType(const wstring& type_name)
{
  frontend::Type* type = nullptr;

  size_t index = 0;
  switch(type_name[index]) {
  case L'l':
    type = frontend::TypeFactory::Instance()->MakeType(frontend::BOOLEAN_TYPE);
    index++;
    break;

  case L'b':
    type = frontend::TypeFactory::Instance()->MakeType(frontend::BYTE_TYPE);
    index++;
    break;

  case L'i':
    type = frontend::TypeFactory::Instance()->MakeType(frontend::INT_TYPE);
    index++;
    break;

  case L'f':
    type = frontend::TypeFactory::Instance()->MakeType(frontend::FLOAT_TYPE);
    index++;
    break;

  case L'c':
    type = frontend::TypeFactory::Instance()->MakeType(frontend::CHAR_TYPE);
    index++;
    break;

  case L'n':
    type = frontend::TypeFactory::Instance()->MakeType(frontend::NIL_TYPE);
    index++;
    break;

  case L'm': {
    size_t start = index;

    int nested_count = 1;
    const wstring prefix = L"m.(";
    size_t found = type_name.find(prefix);
    while(found != wstring::npos) {
      nested_count++;
      found = type_name.find(prefix, found + prefix.size());
    }

    while(nested_count--) {
      while(index < type_name.size() && type_name[index] != L'~') {
        index++;
      }
      if(type_name[index] == L'~') {
        index++;
      }
    }

    while(index < type_name.size() && type_name[index] != L',') {
      index++;
    }

    const wstring name = type_name.substr(start, index - start);
    type = frontend::TypeFactory::Instance()->MakeType(frontend::FUNC_TYPE, name);
    ParseFunctionalType(type);
  }
    break;

  case L'o':
    index = 2;
    while(index < type_name.size() && type_name[index] != L'*' && type_name[index] != L'|') {
      index++;
    }
    type = frontend::TypeFactory::Instance()->MakeType(frontend::CLASS_TYPE, type_name.substr(2, index - 2));
    break;
  }

  // set generics
  if(index < type_name.size() && type_name[index] == L'|') {
    vector<frontend::Type*> generic_types;
    do {
      index++;
      size_t start = index;
      while(index < type_name.size() && type_name[index] != L'*' && type_name[index] != L',' && type_name[index] != L'|') {
        index++;
      }
      size_t end = index;

      const wstring generic_name = type_name.substr(start, end - start);
      generic_types.push_back(frontend::TypeFactory::Instance()->MakeType(frontend::CLASS_TYPE, generic_name));
    } while(index < type_name.size() && type_name[index] == L'|');
    type->SetGenerics(generic_types);
  }

  // set dimension
  int dimension = 0;
  while(index < type_name.size() && type_name[index] == L'*') {
    dimension++;
    index++;
  }
  type->SetDimension(dimension);

  return type;
}

void TypeParser::ParseFunctionalType(frontend::Type* func_type)
{
  const wstring func_name = func_type->GetName();

  // parse parameters
  size_t start = func_name.rfind(L'(');
  size_t middle = func_name.find(L')');

  if(start != wstring::npos && middle != wstring::npos) {
    start++;
    const wstring params_str = func_name.substr(start, middle - start);
    vector<frontend::Type*> func_params = ParseParameters(params_str);
    func_type->SetFunctionParameters(func_params);

    // parse return
    size_t end = func_name.find(L',', middle);
    if(end == wstring::npos) {
      end = func_name.size();
    }

    middle += 2;
    const wstring rtrn_str = func_name.substr(middle, end - middle);
    frontend::Type* func_rtrn = ParseType(rtrn_str);
    func_type->SetFunctionReturn(func_rtrn);
  }
}

bool frontend::EndsWith(wstring const& str, wstring const& ending)
{
  if(str.length() >= ending.length()) {
    return str.compare(str.length() - ending.length(), ending.length(), ending) == 0;
  }

  return false;
}
