/***************************************************************************
 * Shared library API header file
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

#ifndef __LIB_API_H__
#define __LIB_API_H__

#include "common.h"

// offset for Objeck arrays
#define ARRAY_HEADER_OFFSET 3

// function declaration for native C++ callbacks
typedef void(*APITools_MethodCall_Ptr) (size_t* op_stack, long* stack_pos, size_t* instance, const wchar_t* cls_name, const wchar_t* mthd_name);
typedef void(*APITools_MethodCallId_Ptr) (size_t* op_stack, long* stack_pos, size_t* instance, const int cls_id, const int mthd_id);
typedef size_t* (*APITools_AllocateObject_Ptr) (const wchar_t*, size_t* op_stack, long stack_pos, bool collect);
typedef size_t* (*APITools_AllocateArray_Ptr) (const long size, const instructions::MemoryType type, size_t* op_stack, long stack_pos, bool collect);

// context structure
struct VMContext {
  size_t* data_array;
  size_t* op_stack;
  long* stack_pos;
  APITools_AllocateArray_Ptr alloc_array;
  APITools_AllocateObject_Ptr alloc_obj;
  APITools_MethodCall_Ptr call_method_by_name;
  APITools_MethodCallId_Ptr call_method_by_id;
};

// function identifiers consist of two integer IDs
enum FunctionId {
  CLS_ID = 0,
  MTHD_ID
};

// gets number of parameters being passes
const long APITools_GetArgumentCount(VMContext& context) {
  if(context.data_array) {
    return (long)context.data_array[0];
  }

  return 0;
}

// gets an array element
long APITools_GetIntArrayElement(size_t* array, int index) {
  if(!array) {
    return 0;
  }

  const long src_array_len = (long)array[0];
  if(index < src_array_len) {
    size_t* src_array_ptr = array + 3;
    return (long)src_array_ptr[index];
  }

  return 0;
}

// sets an array element
void APITools_SetIntArrayElement(size_t* array, int index, long value) {
  if(!array) {
    return;
  }

  const long src_array_len = (long)array[0];
  if(index < src_array_len) {
    size_t* src_array_ptr = array + 3;
    src_array_ptr[index] = value;
  }
}

// gets an array element
double APITools_GetFloatArrayElement(size_t* array, int index) {
  if(!array) {
    return 0.0;
  }

  const long src_array_len = (long)array[0];
  if(index < src_array_len) {
    size_t* src_array_ptr = array + 3;
    
    double value;
    memcpy(&value, &src_array_ptr[index], sizeof(value));
    return value;
  }

  return 0.0;
}

// sets an array element
void APITools_SetFloatArrayElement(size_t* array, int index, double value) {
  if(!array) {
    return;
  }

  const long src_array_len = (long)array[0];
  if(index < src_array_len) {
    size_t* src_array_ptr = array + 3;
    memcpy(&src_array_ptr[index], &value, sizeof(value));
  }
}

// gets the root of an array
unsigned char* APITools_GetByteArray(size_t* array) {
  if(array) {
    return (unsigned char*)(array + 3);
  }

  return nullptr;
}

// gets the root of an array
wchar_t* APITools_GetCharArray(size_t * array) {
  if(array) {
    return (wchar_t*)(array + 3);
  }

  return nullptr;
}

// gets the root of an array
size_t* APITools_GetIntArray(size_t* array) {
  if(array) {
    return (size_t*)(array + 3);
  }

  return nullptr;
}

// gets the root of an array
double* APITools_GetFloatArray(size_t* array) {
  if(array) {
    return (double*)(array + 3);
  }

  return nullptr;
}

// gets size of array
long APITools_GetArraySize(size_t * array) {
  if(array) {
    return (long)array[0];
  }

  return -1;
}

// gets array from array holder
size_t* APITools_GetArray(size_t* array_holder) {
  if(array_holder) {
    return (size_t*)array_holder[0];
  }

  return nullptr;
}

// creates an array
size_t* APITools_MakeIntArray(VMContext & context, const long int_array_size) {
  // create character array
  const long int_array_dim = 1;
  size_t* int_array = (size_t*)context.alloc_array(int_array_size + int_array_dim + 2, instructions::INT_TYPE,
                                                   context.op_stack, *context.stack_pos, false);
  int_array[0] = int_array_size;
  int_array[1] = int_array_dim;
  int_array[2] = int_array_size;

  return int_array;
}

// creates an array
size_t* APITools_MakeFloatArray(VMContext & context, const long float_array_size) {
  // create character array
  const long float_array_dim = 1;
  size_t* float_array = (size_t*)context.alloc_array(float_array_size + float_array_dim + 2, instructions::FLOAT_TYPE,
                                                     context.op_stack, *context.stack_pos, false);
  float_array[0] = float_array_size;
  float_array[1] = float_array_dim;
  float_array[2] = float_array_size;

  return float_array;
}

// creates an array
size_t* APITools_MakeByteArray(VMContext & context, const long char_array_size) {
  // create character array
  const long char_array_dim = 1;
  size_t* char_array = (size_t*)context.alloc_array(char_array_size + 1 + ((char_array_dim + 2) * sizeof(size_t)), 
                                                    instructions::BYTE_ARY_TYPE, context.op_stack, *context.stack_pos, false);
  char_array[0] = char_array_size;
  char_array[1] = char_array_dim;
  char_array[2] = char_array_size;

  return char_array;
}

// creates an array
size_t * APITools_MakeCharArray(VMContext & context, const long char_array_size) {
  // create character array
  const long char_array_dim = 1;
  size_t* char_array = (size_t*)context.alloc_array(char_array_size + 1 + ((char_array_dim + 2) * sizeof(size_t)), 
                                                    instructions::CHAR_ARY_TYPE, context.op_stack, *context.stack_pos, false);
  char_array[0] = char_array_size;
  char_array[1] = char_array_dim;
  char_array[2] = char_array_size;

  return char_array;
}

// gets the requested function ID from an Object[]
long APITools_GetFunctionValue(VMContext & context, int index, FunctionId id) {
  size_t* data_array = context.data_array;
  if(data_array && index < (int)data_array[0]) {
    data_array += ARRAY_HEADER_OFFSET;
    size_t* int_holder = (size_t*)data_array[index];

    if(id == CLS_ID) {
      return (long)int_holder[0];
    }
    else {
      return (long)int_holder[1];
    }
  }

  return 0;
}

// sets the requested function ID from an Object[].  Please note, that 
// memory should be allocated for this element prior to array access.
void APITools_SetFunctionValue(VMContext & context, int index, FunctionId id, int value) {
  size_t* data_array = context.data_array;
  if(data_array && index < (int)data_array[0]) {
    data_array += ARRAY_HEADER_OFFSET;
    size_t* int_holder = (size_t*)data_array[index];

    if(id == CLS_ID) {
      int_holder[0] = value;
    }
    else {
      int_holder[1] = value;
    }
  }
}

// get the requested integer value from an Object[].
size_t APITools_GetIntValue(VMContext & context, int index) {
  size_t* data_array = context.data_array;
  if(data_array && index < (int)data_array[0]) {
    data_array += ARRAY_HEADER_OFFSET;
    size_t* int_holder = (size_t*)data_array[index];
#ifdef _DEBUG
    assert(int_holder);
#endif
    return int_holder[0];
  }

  return 0;
}

// get the requested integer address from an Object[].
size_t* APITools_GetIntAddress(VMContext & context, int index) {
  size_t* data_array = context.data_array;
  if(data_array && index < (int)data_array[0]) {
    data_array += ARRAY_HEADER_OFFSET;
    size_t* int_holder = (size_t*)data_array[index];
#ifdef _DEBUG
    assert(int_holder);
#endif
    return int_holder;
  }

  return nullptr;
}

// sets the requested function ID from an Object[].  Please note, that 
// memory should be allocated for this element prior to array access.
void APITools_SetIntValue(VMContext & context, int index, size_t value) {
  size_t* data_array = context.data_array;
  if(data_array && index < (int)data_array[0]) {
    data_array += ARRAY_HEADER_OFFSET;
    size_t* int_holder = (size_t*)data_array[index];
#ifdef _DEBUG
    assert(int_holder);
#endif
    int_holder[0] = value;
  }
}

// get the requested double value from an Object[].
double APITools_GetFloatValue(VMContext & context, int index) {
  size_t* data_array = context.data_array;
  if(data_array && index < (int)data_array[0]) {
    data_array += ARRAY_HEADER_OFFSET;
    size_t* float_holder = (size_t*)data_array[index];

#ifdef _DEBUG
    assert(float_holder);
#endif    
    double value;
    memcpy(&value, float_holder, sizeof(value));
    return value;
  }

  return 0.0;
}

// get the requested double address from an Object[].
size_t* APITools_GetFloatAddress(VMContext & context, int index) {
  size_t* data_array = context.data_array;
  if(data_array && index < (int)data_array[0]) {
    data_array += ARRAY_HEADER_OFFSET;
    size_t* float_holder = (size_t*)data_array[index];

#ifdef _DEBUG
    assert(float_holder);
#endif    
    return float_holder;
  }

  return nullptr;
}

// sets the requested float value for an Object[].  Please note, that 
// memory should be allocated for this element prior to array access.
void APITools_SetFloatValue(VMContext & context, int index, double value) {
  size_t* data_array = context.data_array;
  if(data_array && index < (int)data_array[0]) {
    data_array += ARRAY_HEADER_OFFSET;
    size_t* float_holder = (size_t*)data_array[index];

#ifdef _DEBUG
    assert(float_holder);
#endif
    memcpy(float_holder, &value, sizeof(value));
  }
}

// sets the requested Base object for an Object[].  Please note, that 
// memory should be allocated for this element prior to array access.
void APITools_SetObjectValue(VMContext & context, int index, size_t * obj) {
  size_t* data_array = context.data_array;
  if(data_array && index < (int)data_array[0]) {
    data_array += ARRAY_HEADER_OFFSET;
    data_array[index] = (size_t)obj;
  }
}

// creates object
size_t* APITools_CreateObject(VMContext& context, const std::wstring& cls_name) {
  return context.alloc_obj(cls_name.c_str(), context.op_stack, *context.stack_pos, false);
}

// creates a string object
size_t* APITools_CreateStringValue(VMContext & context, const std::wstring & value) {
  // create character array
  const long char_array_size = (long)value.size();
  const long char_array_dim = 1;
  size_t* char_array = (size_t*)context.alloc_array(char_array_size + 1 + ((char_array_dim + 2) * sizeof(size_t)),
                                                    CHAR_ARY_TYPE, context.op_stack, *context.stack_pos, false);
  char_array[0] = char_array_size;
  char_array[1] = char_array_dim;
  char_array[2] = char_array_size;

  // copy string
  wchar_t* char_array_ptr = (wchar_t*)(char_array + 3);
#ifdef _WIN32
  wcsncpy_s(char_array_ptr, char_array_size + 1, value.c_str(), value.size());
#else
  wcsncpy(char_array_ptr, value.c_str(), char_array_size);
#endif

  // create 'System.String' object instance
  size_t * str_obj = context.alloc_obj(L"System.String", context.op_stack, *context.stack_pos, false);
  str_obj[0] = (size_t)char_array;
  str_obj[1] = char_array_size;
  str_obj[2] = char_array_size;

  return str_obj;
}

// sets the requested String object for an Object[].  Please note, that 
// memory should be allocated for this element prior to array access.
void APITools_SetStringValue(VMContext & context, int index, const std::wstring & value) {
  APITools_SetObjectValue(context, index, APITools_CreateStringValue(context, value));
}

// get the requested string value from an Object[].
inline const wchar_t* APITools_GetStringValue(size_t* data_array, int index) {
  if(data_array && index < (int)data_array[0]) {
    data_array += ARRAY_HEADER_OFFSET;
    size_t* string_holder = (size_t*)data_array[index];
    if(string_holder) {
      size_t* char_array = (size_t*)string_holder[0];
      wchar_t* str = (wchar_t*)(char_array + 3);
      return str;
    }
  }

  return nullptr;
}

// get the requested string value from an Object[].
const wchar_t* APITools_GetStringValue(VMContext & context, int index) {
  return APITools_GetStringValue(context.data_array, index);
}

size_t* APITools_GetObjectValue(VMContext & context, int index) {
  size_t* data_array = context.data_array;
  if(data_array && index < (int)data_array[0]) {
    data_array += ARRAY_HEADER_OFFSET;
    size_t* object_holder = (size_t*)data_array[index];

    return object_holder;
  }

  return nullptr;
}


// invokes a runtime Objeck method
void APITools_CallMethod(VMContext & context, size_t * instance, const wchar_t* mthd_name) {
  const std::wstring qualified_method_name(mthd_name);
  size_t delim = qualified_method_name.find(':');
  if(delim != std::wstring::npos) {
    std::wstring cls_name = qualified_method_name.substr(0, delim);
    (*context.call_method_by_name)(context.op_stack, context.stack_pos, instance, cls_name.c_str(), mthd_name);

#ifdef _DEBUG
    assert(*context.stack_pos == 0);
#endif
  }
  else {
    std::cerr << L">>> DLL call: Invalid method name: '" << mthd_name << L"'" << std::endl;
    exit(1);
  }
}

// invokes a runtime Objeck method
void APITools_CallMethod(VMContext & context, size_t * instance, const int cls_id, const int mthd_id) {
  (*context.call_method_by_id)(context.op_stack, context.stack_pos, instance, cls_id, mthd_id);

#ifdef _DEBUG
  assert(*context.stack_pos == 0);
#endif
}

// pushes an integer value onto the runtime stack
void APITools_PushInt(VMContext & context, long value) {
  context.op_stack[(*context.stack_pos)++] = value;
}

// pushes an double value onto the runtime stack
void APITools_PushFloat(VMContext & context, double v) {
  memcpy(&context.op_stack[(*context.stack_pos)], &v, sizeof(double));
  (*context.stack_pos)++;
}

#endif
