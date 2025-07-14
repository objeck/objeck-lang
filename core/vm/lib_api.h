/***************************************************************************
 * Objeck API routines for C++ shared library extensions
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

#pragma once

#include "common.h"

// pre-header offset for Objeck arrays
#define ARRAY_HEADER_OFFSET 3

// function declaration for native C++ callbacks
typedef void(*APITools_MethodCallByName_Ptr) (size_t* op_stack, long* stack_pos, size_t* instance, const wchar_t* cls_name, const wchar_t* mthd_name);
typedef void(*APITools_MethodCallById_Ptr) (size_t* op_stack, long* stack_pos, size_t* instance, const int cls_id, const int mthd_id);
typedef size_t* (*APITools_AllocateObject_Ptr) (const wchar_t*, size_t* op_stack, long stack_pos, bool collect);
typedef size_t* (*APITools_AllocateArray_Ptr) (const size_t size, const instructions::MemoryType type, size_t* op_stack, long stack_pos, bool collect);

//
// API calling context
//
struct VMContext {
  // calling date
  size_t* data_array;
  // stack references
  size_t* op_stack;
  long* stack_pos;
  // managed allocation routines
  APITools_AllocateArray_Ptr alloc_managed_array;  
  APITools_AllocateObject_Ptr alloc_managed_obj;
  // method call routines
  APITools_MethodCallByName_Ptr call_method_by_name;
  APITools_MethodCallById_Ptr call_method_by_id;
};

//
// Gets the number of parameters being passes
//
const size_t APITools_GetArgumentCount(VMContext& context) {
  if(context.data_array) {
    return context.data_array[0];
  }

  return 0;
}

//
// Gets an array reference from an Objeck array holder reference
//
size_t* APITools_GetArrayAddress(size_t* array_holder) {
  if(array_holder) {
    return (size_t*)array_holder[0];
  }

  return nullptr;
}

//
// Gets an array by index
//
size_t* APITools_GetArray(VMContext &context, size_t index) {
  size_t* data_array = context.data_array;
  if(data_array && index < data_array[0]) {
    data_array += ARRAY_HEADER_OFFSET;
    return (size_t*)data_array[index];
  }
  
  return nullptr;
}

//
// Gets an array 
//
size_t* APITools_GetArray(size_t* data_array) {
  if(data_array) {
    data_array += ARRAY_HEADER_OFFSET;
    return (size_t*)data_array;
  }

  return nullptr;
}

//
// Gets an array from an Objeck array holder reference
// (i.e. ByteArrayHolder, FloatArrayHolder, etc.)
//
size_t* APITools_SetArray(size_t* array_holder) {
  if(array_holder) {
    return (size_t*)array_holder[0];
  }

  return nullptr;
}

//
// Gets the size of Objeck array by reference
//
size_t APITools_GetArraySize(size_t* array) {
  if(array) {
    return array[0];
  }

  return 0;
}

//
// Creates a managed Int Objeck array instance
//
size_t* APITools_MakeIntArray(VMContext& context, const size_t int_array_size) {
  // create character array
  const size_t int_array_dim = 1;
  size_t* int_array = (size_t*)context.alloc_managed_array(int_array_size + int_array_dim + 2, instructions::INT_TYPE,
                                                           context.op_stack, *context.stack_pos, false);
  int_array[0] = int_array_size;
  int_array[1] = int_array_dim;
  int_array[2] = int_array_size;

  return int_array;
}

//
// Creates a managed Float Objeck array instance
//
size_t* APITools_MakeFloatArray(VMContext& context, const size_t float_array_size) {
  // create character array
  const long float_array_dim = 1;
  size_t* float_array = (size_t*)context.alloc_managed_array(float_array_size + float_array_dim + 2, instructions::FLOAT_TYPE,
                                                             context.op_stack, *context.stack_pos, false);
  float_array[0] = float_array_size;
  float_array[1] = float_array_dim;
  float_array[2] = float_array_size;

  return float_array;
}

//
// Creates a managed 2D Float Objeck matrix instance
//
size_t* APITools_MakeFloatMatrix(VMContext& context, const size_t rows, const size_t cols) {
  const long float_array_dim = 2;
  const size_t float_array_size = rows * cols;

  size_t* float_array = (size_t*)context.alloc_managed_array(float_array_size + float_array_dim + 2, instructions::FLOAT_TYPE,
    context.op_stack, *context.stack_pos, false);
  
  float_array[0] = float_array_size;
  float_array[1] = float_array_dim;
  float_array[2] = cols;
  float_array[3] = rows;

  return float_array;
}

//
// Creates a managed Char Objeck array instance
//
size_t* APITools_MakeCharArray(VMContext& context, const size_t char_array_size) {
  // create character array
  const long char_array_dim = 1;
  size_t* char_array = (size_t*)context.alloc_managed_array(char_array_size + 1 + ((char_array_dim + 2) * sizeof(size_t)),
                                                            instructions::CHAR_ARY_TYPE, context.op_stack, *context.stack_pos, false);
  char_array[0] = char_array_size;
  char_array[1] = char_array_dim;
  char_array[2] = char_array_size;

  return char_array;
}

//
// Creates a managed Byte Objeck array instance
//
size_t* APITools_MakeByteArray(VMContext& context, const size_t char_array_size) {
  // create character array
  const long char_array_dim = 1;
  size_t* char_array = (size_t*)context.alloc_managed_array(char_array_size + 1 + ((char_array_dim + 2) * sizeof(size_t)),
                                                            instructions::BYTE_ARY_TYPE, context.op_stack, *context.stack_pos, false);
  char_array[0] = char_array_size;
  char_array[1] = char_array_dim;
  char_array[2] = char_array_size;

  return char_array;
}

//
// Gets an array of integer elements
//
long APITools_GetIntArrayElement(size_t* data_array, size_t index) {
  if(!data_array) {
    return 0;
  }

  const size_t src_array_len = data_array[0];
  if(index < src_array_len) {
    data_array += ARRAY_HEADER_OFFSET;
    return (long)data_array[index];
  }

  return 0;
}

//
// Sets an integer array element
//
void APITools_SetIntArrayElement(size_t* data_array, size_t index, long value) {
  if(!data_array) {
    return;
  }

  const size_t src_array_len = data_array[0];
  if(index < src_array_len) {
    data_array += ARRAY_HEADER_OFFSET;
    data_array[index] = value;
  }
}

//
// Gets an character array element
//
wchar_t APITools_GetCharArrayElement(size_t* array, size_t index) {
  if(!array) {
    return 0;
  }

  const size_t src_array_len = array[0];
  if(index < src_array_len) {
    array += ARRAY_HEADER_OFFSET;
    wchar_t* value_array = (wchar_t*)array;
    return value_array[index];
  }

  return 0;
}


//
// Sets the Char array element value in a reference
//
void APITools_SetCharArrayElement(size_t* data_array, size_t index, wchar_t value) {
  if(!data_array) {
    return;
  }

  const size_t src_array_len = data_array[0];
  if(index < src_array_len) {
    data_array += ARRAY_HEADER_OFFSET;
    wchar_t* value_array = (wchar_t*)data_array;
#ifdef _DEBUG
    assert(value_array);
#endif
    value_array[index] = value;
  }
}


//
// Gets an byte array element
//
unsigned char APITools_GetByteArrayElement(size_t* array, size_t index) {
  if(!array) {
    return 0;
  }

  const size_t src_array_len = array[0];
  if(index < src_array_len) {
    array += ARRAY_HEADER_OFFSET;
    unsigned char* value_array = (unsigned char*)array;
    return value_array[index];
  }

  return 0;
}

//
// Sets the Byte array element value in a reference
//
void APITools_SetByteArrayElement(size_t* data_array, size_t index, unsigned char value) {
  if(!data_array) {
    return;
  }

  const size_t src_array_len = data_array[0];
  if(index < src_array_len) {
    data_array += ARRAY_HEADER_OFFSET;
    unsigned char* value_array = (unsigned char*)data_array;
#ifdef _DEBUG
    assert(value_array);
#endif
    value_array[index] = value;
  }
}

//
// Gets an float array element
//
double APITools_GetFloatArrayElement(size_t* array, size_t index) {
  if(!array) {
    return 0.0;
  }

  const size_t src_array_len = array[0];
  if(index < src_array_len) {
    array += ARRAY_HEADER_OFFSET;
    double* value_array = (double*)array;
    return value_array[index];
  }

  return 0.0;
}

//
// Sets the Float array element value in a reference
//
void APITools_SetFloatArrayElement(size_t* data_array, size_t index, double value) {
  if(!data_array) {
    return;
  }

  const size_t src_array_len = data_array[0];
  if(index < src_array_len) {
    data_array += ARRAY_HEADER_OFFSET;
    double* value_array = (double*)data_array;
#ifdef _DEBUG
    assert(value_array);
#endif
    value_array[index] = value;
  }
}

//
// Gets an indexed Bool value
//
bool APITools_GetBoolValue(VMContext& context, size_t index) {
  size_t* data_array = context.data_array;
  if(data_array && index < data_array[0]) {
    data_array += ARRAY_HEADER_OFFSET;
    const size_t* value_holder = (size_t*)data_array[index];
#ifdef _DEBUG
    assert(value_holder);
#endif
    return !(*value_holder) ? false : true;
  }

  return false;
}

//
// Gets an indexed Int value
//
size_t APITools_GetIntValue(VMContext &context, size_t index) {
  size_t* data_array = context.data_array;
  if(data_array && index < data_array[0]) {
    data_array += ARRAY_HEADER_OFFSET;
    const size_t* value_holder = (size_t*)data_array[index];
#ifdef _DEBUG
    assert(value_holder);
#endif
    return *value_holder;
  }

  return 0;
}

//
// Gets an indexed Float value
//
double APITools_GetFloatValue(VMContext& context, size_t index) {
  size_t* data_array = context.data_array;
  if(data_array && index < data_array[0]) {
    data_array += ARRAY_HEADER_OFFSET;
    const double* value_holder = (double*)data_array[index];

#ifdef _DEBUG
    assert(value_holder);
#endif    
    return *value_holder;
  }

  return 0.0;
}

//
// Gets an indexed Char value
//
wchar_t APITools_GetCharValue(VMContext& context, size_t index) {
  size_t* data_array = context.data_array;
  if(data_array && index < data_array[0]) {
    data_array += ARRAY_HEADER_OFFSET;
    const wchar_t* value_holder = (wchar_t*)data_array[index];

#ifdef _DEBUG
    assert(value_holder);
#endif    
    return *value_holder;
  }

  return 0;
}

//
// Gets an indexed Byte value
//
unsigned char APITools_GetByteValue(VMContext& context, size_t index) {
  size_t* data_array = context.data_array;
  if(data_array && index < data_array[0]) {
    data_array += ARRAY_HEADER_OFFSET;
    const unsigned char* value_holder = (unsigned char*)data_array[index];

#ifdef _DEBUG
    assert(value_holder);
#endif    
    return *value_holder;
  }

  return 0;
}

//
// Sets an indexed Bool value
//
void APITools_SetBoolValue(VMContext& context, int index, bool value) {
  size_t* data_array = context.data_array;
  if(data_array && index < (int)data_array[0]) {
    data_array += ARRAY_HEADER_OFFSET;
    size_t* value_holder = (size_t*)data_array[index];
#ifdef _DEBUG
    assert(value_holder);
#endif
    *value_holder = !value ? 0 : 1;
  }
}

//
// Sets an indexed Int value
//
void APITools_SetIntValue(VMContext &context, int index, size_t value) {
  size_t* data_array = context.data_array;
  if(data_array && index < (int)data_array[0]) {
    data_array += ARRAY_HEADER_OFFSET;
    size_t* value_holder = (size_t*)data_array[index];
#ifdef _DEBUG
    assert(value_holder);
#endif
    *value_holder = value;
  }
}

//
// Gets an indexed Char value
//
void APITools_SetCharValue(VMContext& context, size_t index, wchar_t value) {
  size_t* data_array = context.data_array;
  if(data_array && index < data_array[0]) {
    data_array += ARRAY_HEADER_OFFSET;
    wchar_t* value_holder = (wchar_t*)data_array[index];
#ifdef _DEBUG
    assert(value_holder);
#endif
    *value_holder = value;
  }
}

//
// Gets an indexed Byte value
//
void APITools_SetByteValue(VMContext& context, size_t index, unsigned char value) {
  size_t* data_array = context.data_array;
  if(data_array && index < data_array[0]) {
    data_array += ARRAY_HEADER_OFFSET;
    unsigned char* value_holder = (unsigned char*)data_array[index];
#ifdef _DEBUG
    assert(value_holder);
#endif
    *value_holder = value;
  }
}

//
// Sets an indexed Float value
//
void APITools_SetFloatValue(VMContext &context, int index, double value) {
  size_t* data_array = context.data_array;
  if(data_array && index < (int)data_array[0]) {
    data_array += ARRAY_HEADER_OFFSET;
    double* value_holder = (double*)data_array[index];
#ifdef _DEBUG
    assert(value_holder);
#endif
    *value_holder = value;
  }
}

//
// Gets an object reference value
//
size_t* APITools_GetObjectValue(VMContext& context, size_t index) {
  size_t* data_array = context.data_array;
  if(data_array && index < data_array[0]) {
    data_array += ARRAY_HEADER_OFFSET;
    size_t* object_holder = (size_t*)data_array[index];

    return object_holder;
  }

  return nullptr;
}

//
// Sets an object reference value
//
void APITools_SetObjectValue(VMContext &context, int index, size_t * obj) {
  size_t* data_array = context.data_array;
  if(data_array && index < (int)data_array[0]) {
    data_array += ARRAY_HEADER_OFFSET;
    data_array[index] = (size_t)obj;
  }
}

//
// Creates an object instance by name
//
size_t* APITools_CreateObject(VMContext& context, const std::wstring& cls_name) {
  return context.alloc_managed_obj(cls_name.c_str(), context.op_stack, *context.stack_pos, false);
}

//
// Creates a String value Objeck instance reference
//
size_t* APITools_CreateStringObject(VMContext& context, const std::wstring& value) {
  // create character array
  const size_t char_array_size = value.size();
  const size_t char_array_dim = 1;
  size_t* char_array = (size_t*)context.alloc_managed_array(char_array_size + 1 + ((char_array_dim + 2) * sizeof(size_t)),
                                                            CHAR_ARY_TYPE, context.op_stack, *context.stack_pos, false);
  char_array[0] = char_array_size;
  char_array[1] = char_array_dim;
  char_array[2] = char_array_size;

  // copy string
  wchar_t* char_array_ptr = (wchar_t*)(char_array + ARRAY_HEADER_OFFSET);
#ifdef _WIN32
  wcsncpy_s(char_array_ptr, char_array_size + 1, value.c_str(), value.size());
#else
  wcsncpy(char_array_ptr, value.c_str(), char_array_size);
#endif

  // create 'System.String' object instance
  size_t* str_obj = context.alloc_managed_obj(L"System.String", context.op_stack, *context.stack_pos, false);
  str_obj[0] = (size_t)char_array;
  str_obj[1] = char_array_size;
  str_obj[2] = char_array_size;

  return str_obj;
}

//
// Gets the C++ string value from an Objeck reference 
//
inline const wchar_t* APITools_GetStringValue(size_t* str_obj, size_t index) {
  if(str_obj && index < str_obj[0]) {
    str_obj += ARRAY_HEADER_OFFSET;
    size_t* string_holder = (size_t*)str_obj[index];
    if(string_holder) {
      size_t* char_array = (size_t*)string_holder[0];
      const wchar_t* str = (wchar_t*)(char_array + ARRAY_HEADER_OFFSET);
      return str;
    }
  }

  return nullptr;
}

//
// Gets the C++ string values from an Objeck string array reference (i.e. StringArrayHolder) by index
//
std::vector<std::wstring> APITools_GetStringsValues(VMContext& context, size_t index) {
  std::vector<std::wstring> strings_values;

  size_t* string_array_obj = APITools_GetObjectValue(context, index);
  if(string_array_obj && string_array_obj[0]) {
    string_array_obj = (size_t*)string_array_obj[0];
    const size_t string_array_size = string_array_obj[2];

    for(size_t i = 0; i < string_array_size; ++i) {
      const wchar_t* str_ptr = APITools_GetStringValue(string_array_obj, i);
      strings_values.push_back(str_ptr);
    }
  }

  return strings_values;
}


//
// Gets the C++ string value from an Objeck reference by index (i.e. String)
//
const wchar_t* APITools_GetStringValue(VMContext &context, size_t index) {
  return APITools_GetStringValue(context.data_array, index);
}

//
// Sets an Objeck string value instance reference by index
//
void APITools_SetStringValue(VMContext& context, int index, const std::wstring& value) {
  APITools_SetObjectValue(context, index, APITools_CreateStringObject(context, value));
}

//
// Calls an Objeck method by qualified method name
//
void APITools_CallMethod(VMContext &context, size_t * instance, const wchar_t* qualified_name) {
  const std::wstring qualified_method_name(qualified_name);
  size_t delim = qualified_method_name.find(':');
  if(delim != std::wstring::npos) {
    std::wstring cls_name = qualified_method_name.substr(0, delim);
    (*context.call_method_by_name)(context.op_stack, context.stack_pos, instance, cls_name.c_str(), qualified_name);

#ifdef _DEBUG
    assert(*context.stack_pos == 0);
#endif
  }
  else {
    std::wcerr << L">>> DLL_CALL: Invalid method name: '" << qualified_name << L"'" << std::endl;
    exit(1);
  }
}

//
// Calls an Objeck method by class and method ID
//
void APITools_CallMethod(VMContext &context, size_t * instance, const int cls_id, const int mthd_id) {
  (*context.call_method_by_id)(context.op_stack, context.stack_pos, instance, cls_id, mthd_id);

#ifdef _DEBUG
  assert(*context.stack_pos == 0);
#endif
}
