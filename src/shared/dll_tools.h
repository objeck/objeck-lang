/***************************************************************************
 * Shared library utilities
 *
 * Copyright (c) 2008-2011, Randy Hollines
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
 * - Neither the name of the StackVM Team nor the names of its
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

#ifndef __DLL_TOOLS_H__
#define __DLL_TOOLS_H__

#include <string>
#include <assert.h>
#include <string.h>

using namespace std;

#define ARRAY_HEADER_OFFSET 3

enum FunctionId {
  CLS_ID = 0,
  MTHD_ID
};

typedef void(*DLLTools_MethodCall_Ptr)(long*, long*, long*, int, int);

int DLLTools_GetArraySize(long* array) {
  if(array) {
    return array[0];
  }

  return 0;
}

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

long DLLTools_GetIntValue(long* array, int index) {
  if(array && index < array[0]) {
    array += ARRAY_HEADER_OFFSET;
    long* int_holder = (long*)array[index];
    return int_holder[0];
  }

  return 0;
}

void DLLTools_SetIntValue(long* array, int index, long value) {
  if(array && index < array[0]) {
    array += ARRAY_HEADER_OFFSET;
    long* int_holder = (long*)array[index];
    int_holder[0] = value;
  }
}

double DLLTools_GetFloatValue(long* array, int index) {
  if(array && index < array[0]) {
    array += ARRAY_HEADER_OFFSET;
    long* float_holder = (long*)array[index];
		
    double value;
    memcpy(&value, float_holder, sizeof(value));
    return value;
  }

  return 0.0;
} 

void DLLTools_SetFloatValue(long* array, int index, double value) {
  if(array && index < array[0]) {
    array += ARRAY_HEADER_OFFSET;
    long* float_holder = (long*)array[index];
    memcpy(float_holder, &value, sizeof(value));
  }
}

char* DLLTools_GetStringValue(long* array, int index) {
  if(array && index < array[0]) {
    array += ARRAY_HEADER_OFFSET;
    long* string_holder = (long*)array[index];
    long* char_array = (long*)string_holder[0];
    char* str = (char*)(char_array + 3);
    return str;
  }

  return NULL;
}

void DLLTools_CallMethod(DLLTools_MethodCall_Ptr callback, long* op_stack, 
		    long* stack_pos, long*, int cls_id, int mthd_id) {
  (*callback)(op_stack, stack_pos, NULL, cls_id, mthd_id);
#ifdef _DEBUG
  assert(*stack_pos == 0);
#endif
}

long DLLTools_CallMethodWithReturn(DLLTools_MethodCall_Ptr callback, long* op_stack, 
				   long* stack_pos, long*, int cls_id, int mthd_id) {
  (*callback)(op_stack, stack_pos, NULL, cls_id, mthd_id);
#ifdef _DEBUG
  assert(*stack_pos > 0);
#endif
  long rtrn_value = op_stack[--(*stack_pos)];
#ifdef _DEBUG
  assert(*stack_pos == 0);
#endif
  
  return rtrn_value;
}

void DLLTools_PushInt(long* op_stack, long *stack_pos, long value) {
  op_stack[(*stack_pos)++] = value;
}

void DLLTools_PushFloat(long* op_stack, long *stack_pos, double v) {
  memcpy(&op_stack[(*stack_pos)], &v, sizeof(double));
#ifdef _X64
  (*stack_pos)++;
#else
  (*stack_pos) += 2;
#endif
}

#endif
