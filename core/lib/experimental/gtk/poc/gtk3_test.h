/***************************************************************************
 * GTK3 PoC support for Objeck
 *
 * Copyright (c) 2023, Randy Hollines
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
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

#ifndef __GTK3_TEST_H__
#define __GTK3_TEST_H__

#include <gtk/gtk.h>
#include "../../../../vm/lib_api.h"
#include "../../../../vm/interpreter.h"
#include "../../../../shared/sys.h"

#define EXEC_STACK_MAX 32

class MemoryAllocator {
  APITools_AllocateObject_Ptr allocate_object;
  APITools_MethodCallById_Ptr method_call_by_id;
  std::stack<std::pair<size_t*, long*>> op_stack_mem;
  std::stack<char*> raw_allocations;

public:
  MemoryAllocator(APITools_AllocateObject_Ptr ao, APITools_MethodCallById_Ptr mc) {
    allocate_object = ao;
    method_call_by_id = mc;

    for(size_t i = 0; i < EXEC_STACK_MAX; ++i) {
      size_t* op_stack = new size_t[OP_STACK_SIZE];
      long* stack_pos = new long;

      op_stack_mem.push(std::pair<size_t*, long*>(op_stack, stack_pos));
    }
  }

  ~MemoryAllocator() {
    allocate_object = nullptr;
    method_call_by_id = nullptr;

    while(!op_stack_mem.empty()) {
      std::pair<size_t*, long*> value = op_stack_mem.top();
      op_stack_mem.pop();

      size_t* op_stack = value.first;
      delete[] op_stack;
      op_stack = nullptr;

      long* stack_pos = value.second;
      delete stack_pos;
      stack_pos = nullptr;
    }

    while(!raw_allocations.empty()) {
      char* mem = raw_allocations.top();
      raw_allocations.pop();
      // delete
      delete mem;
      mem = nullptr;
    }
  }

  APITools_AllocateObject_Ptr GetAllocateObject() {
    return allocate_object;
  }

  APITools_MethodCallById_Ptr GetMethodCallById() {
    return method_call_by_id;
  }

  std::pair<size_t*, long*> GetOpStackMemory() {
    if(!op_stack_mem.empty()) {
      std::pair<size_t*, long*> mem_pair = op_stack_mem.top();
      op_stack_mem.pop();

      return mem_pair;
    }

    return std::pair<size_t*, long*>();
  }

  void ReleaseOpStackMemory(std::pair<size_t*, long*> mem_pair) {
    op_stack_mem.push(mem_pair);
  }

  void AddAllocation(char* mem) {
    raw_allocations.push(mem);
  }
};

#endif
