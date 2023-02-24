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

