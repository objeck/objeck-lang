/***************************************************************************
 * Shared library API header file
 *
 * Copyright (c) 2011, Randy Hollines
 * All rights reserved.
 *
 * Redistribution and use in sohurce and binary forms, with or without
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

#ifdef _WIN32
#include "os/windows/memory.h"
#else
#include "os/posix/memory.h"
#endif

// offset for Objeck arrays
#define ARRAY_HEADER_OFFSET 3

// function declaration for native C callbacks
typedef void(*DLLTools_MethodCall_Ptr) (long* op_stack, long *stack_pos, long *instance, 
					const char* cls_id, const char* mthd_id);
typedef long*(*DLLTools_AllocateObject_Ptr) (const long obj_id, long* op_stack, long stack_pos);
typedef long*(*DLLTools_AllocateArray_Ptr) (const long size, const MemoryType type, 
					    long* op_stack, long stack_pos);

struct Callbacks {
  DLLTools_MethodCall_Ptr method_call;
  DLLTools_AllocateObject_Ptr alloc_obj;
  DLLTools_AllocateArray_Ptr alloc_array;
};

// function identifiers consist of two integer IDs
enum FunctionId {
  CLS_ID = 0,
  MTHD_ID
};

// gets the size of an Object[] array
int DLLTools_GetArraySize(long* array) {
  if(array) {
    return array[0];
  }

  return 0;
}

// gets the requested function ID from an Object[]
int DLLTools_GetFunctionValue(long* array, int index, FunctionId id) {
  if(array && index < array[0]) {
    array += ARRAY_HEADER_OFFSET;
    long* int_holder = (long*)array[index];
    
    if(id == CLS_ID) {
      return int_holder[0];
    }
    else {
      return int_holder[1];
    }
  }
  
  return 0;
}

// sets the requested function ID from an Object[].  Please note, that 
// memory should be allocated for this element prior to array access.
void DLLTools_SetFunctionValue(long* array, int index, 
			       FunctionId id, int value) {
  if(array && index < array[0]) {
    array += ARRAY_HEADER_OFFSET;
    long* int_holder = (long*)array[index];
    
    if(id == CLS_ID) {
      int_holder[0] = value;
    }
    else {
      int_holder[1] = value;
    }
  }
}

// get the requested integer value from an Object[].
long DLLTools_GetIntValue(long* array, int index) {
  if(array && index < array[0]) {
    array += ARRAY_HEADER_OFFSET;
    long* int_holder = (long*)array[index];
#ifdef _DEBUG
    assert(int_holder);
#endif
    return int_holder[0];
  }

  return 0;
}

// sets the requested function ID from an Object[].  Please note, that 
// memory should be allocated for this element prior to array access.
void DLLTools_SetIntValue(long* array, int index, long value) {
  if(array && index < array[0]) {
    array += ARRAY_HEADER_OFFSET;
    long* int_holder = (long*)array[index];
#ifdef _DEBUG
    assert(int_holder);
#endif
    int_holder[0] = value;
  }
}

// get the requested double value from an Object[].
double DLLTools_GetFloatValue(long* array, int index) {
  if(array && index < array[0]) {
    array += ARRAY_HEADER_OFFSET;
    long* float_holder = (long*)array[index];

#ifdef _DEBUG
    assert(float_holder);
#endif		
    double value;
    memcpy(&value, float_holder, sizeof(value));
    return value;
  }

  return 0.0;
} 

// sets the requested float value for an Object[].  Please note, that 
// memory should be allocated for this element prior to array access.
void DLLTools_SetFloatValue(long* array, int index, double value) {
  if(array && index < array[0]) {
    array += ARRAY_HEADER_OFFSET;
    long* float_holder = (long*)array[index];

#ifdef _DEBUG
    assert(float_holder);
#endif
    memcpy(float_holder, &value, sizeof(value));
  }
}

// get the requested string value from an Object[].
char* DLLTools_GetStringValue(long* array, int index) {
  if(array && index < array[0]) {
    array += ARRAY_HEADER_OFFSET;
    long* string_holder = (long*)array[index];

#ifdef _DEBUG
    assert(string_holder);
#endif
    long* char_array = (long*)string_holder[0];
    char* str = (char*)(char_array + 3);
    return str;
  }

  return NULL;
}

// invokes a runtime Objeck method
void DLLTools_CallMethod(DLLTools_MethodCall_Ptr callback, long* op_stack, 
			 long* stack_pos, long* inst, const char* mthd_id) {
  string qualified_method_name(mthd_id);
  size_t delim = qualified_method_name.find(':');
  if(delim != string::npos) {
    string cls_name = qualified_method_name.substr(0, delim);
    (*callback)(op_stack, stack_pos, inst, cls_name.c_str(), mthd_id);
    
#ifdef _DEBUG
    assert(*stack_pos == 0);
#endif
  }
  else {
    cerr << ">>> DLL call: Invalid method name: '" << mthd_id << "'" << endl;
    exit(1);
  }
}

// invokes a runtime Objeck method that returns a value, which may be a point to memory
long DLLTools_CallMethodWithReturn(DLLTools_MethodCall_Ptr callback, long* op_stack, 
				   long* stack_pos, long* inst, const char* mthd_id) {
  string qualified_method_name(mthd_id);
  size_t delim = qualified_method_name.find(':');
  if(delim != string::npos) {
    string cls_name = qualified_method_name.substr(0, delim);
    (*callback)(op_stack, stack_pos, inst, cls_name.c_str(), mthd_id);
    
#ifdef _DEBUG
    assert(*stack_pos > 0);
#endif
    long rtrn_value = op_stack[--(*stack_pos)];
#ifdef _DEBUG
    assert(*stack_pos == 0);
#endif
    
    return rtrn_value;
  }
  else {
    cerr << ">>> DLL call: Invalid method name: '" << mthd_id << "'" << endl;
    exit(1);
  }
}

// pushes an integer value onto the runtime stack
void DLLTools_PushInt(long* op_stack, long *stack_pos, long value) {
  op_stack[(*stack_pos)++] = value;
}

// pushes an double value onto the runtime stack
void DLLTools_PushFloat(long* op_stack, long *stack_pos, double v) {
  memcpy(&op_stack[(*stack_pos)], &v, sizeof(double));
#ifdef _X64
  (*stack_pos)++;
#else
  (*stack_pos) += 2;
#endif
}

#endif
