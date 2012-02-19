/***************************************************************************
 * Vm memory manager. Implements a "mark and sweep" collection algorithm.
 *
 * Copyright (c) 2008-2012, Randy Hollines
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

#include "memory.h"
#include <iomanip>

MemoryManager* MemoryManager::instance;
StackProgram* MemoryManager::prgm;
list<ClassMethodId*> MemoryManager::jit_roots;
list<StackFrame*> MemoryManager::pda_roots;
map<long*, long> MemoryManager::allocated_memory;
set<long*> MemoryManager::allocated_int_obj_array;
map<long*, long> MemoryManager::static_memory;
vector<long*> MemoryManager::marked_memory;
long MemoryManager::allocation_size;
long MemoryManager::mem_max_size;
long MemoryManager::uncollected_count;
long MemoryManager::collected_count;
CRITICAL_SECTION MemoryManager::static_cs;
CRITICAL_SECTION MemoryManager::jit_cs;
CRITICAL_SECTION MemoryManager::pda_cs;
CRITICAL_SECTION MemoryManager::allocated_cs;
CRITICAL_SECTION MemoryManager::marked_cs;
CRITICAL_SECTION MemoryManager::marked_sweep_cs;

void MemoryManager::Initialize(StackProgram* p)
{
  prgm = p;
  allocation_size = 0;
  mem_max_size = MEM_MAX;
  uncollected_count = 0;

  InitializeCriticalSection(&static_cs);
  InitializeCriticalSection(&jit_cs);
  InitializeCriticalSection(&pda_cs);
  InitializeCriticalSection(&allocated_cs);
  InitializeCriticalSection(&marked_cs);
  InitializeCriticalSection(&marked_sweep_cs);
}

MemoryManager* MemoryManager::Instance()
{
  if(!instance) {
    instance = new MemoryManager;
  }

  return instance;
}

void MemoryManager::AddStaticMemory(long* mem)
{
#ifndef _GC_SERIAL
  EnterCriticalSection(&static_cs);
  EnterCriticalSection(&allocated_cs);
#endif
  
  // only add static references that don't exist
  map<long*, long>::iterator exists = static_memory.find(mem);
  if(exists == static_memory.end()) {
    // ensure that this is an object or array instance
    map<long*, long>::iterator result = allocated_memory.find(mem);
    if(result != allocated_memory.end()) {
#ifdef _DEBUG
      cout << "### adding static reference: " << mem << " ###" << endl;
#endif
      static_memory.insert(pair<long*, long>(mem, result->second));
    }
  }
  
#ifndef _GC_SERIAL
  LeaveCriticalSection(&static_cs);
  LeaveCriticalSection(&allocated_cs);
#endif
}

// if return true, trace memory otherwise do not
inline bool MemoryManager::MarkMemory(long* mem)
{
  if(mem) {
#ifndef _SERIAL
    EnterCriticalSection(&allocated_cs);
#endif
    map<long*, long>::iterator result = allocated_memory.find(mem);
    if(result != allocated_memory.end()) {
      // check if memory has been marked
      if(mem[-1]) {
#ifndef _SERIAL
	      LeaveCriticalSection(&allocated_cs);
#endif
        return false;
      }

      // mark & add to list
#ifndef _SERIAL
      EnterCriticalSection(&marked_cs);
#endif
      mem[-1] = 1L;
      marked_memory.push_back(mem);
#ifndef _SERIAL
      LeaveCriticalSection(&marked_cs);      
      LeaveCriticalSection(&allocated_cs);
#endif
      return true;
    } 
    else {
#ifndef _SERIAL
      LeaveCriticalSection(&allocated_cs);
#endif
      return false;
    }
  }
  
  return false;
}

void MemoryManager::AddPdaMethodRoot(StackFrame* frame)
{
#ifdef _DEBUG
  cout << "adding PDA method: frame=" << frame << ", self="
       << (long*)frame->GetMemory()[0] << "(" << frame->GetMemory()[0] << ")" << endl;
#endif

#ifndef _SERIAL
  EnterCriticalSection(&jit_cs);
#endif
  pda_roots.push_back(frame);
#ifndef _SERIAL
  LeaveCriticalSection(&jit_cs);
#endif
}

void MemoryManager::RemovePdaMethodRoot(StackFrame* frame)
{
#ifdef _DEBUG
  cout << "removing PDA method: frame=" << frame << ", self="
       << (long*)frame->GetMemory()[0] << "(" << frame->GetMemory()[0] << ")" << endl;
#endif

#ifndef _SERIAL
  EnterCriticalSection(&jit_cs);
#endif
  pda_roots.remove(frame);
#ifndef _SERIAL
  LeaveCriticalSection(&jit_cs);
#endif
}

void MemoryManager::AddJitMethodRoot(long cls_id, long mthd_id,
                                     long* self, long* mem, long offset)
{
#ifdef _DEBUG
  cout << "adding JIT root: class=" << cls_id << ", method=" << mthd_id << ", self=" << self
       << "(" << (long)self << "), mem=" << mem << ", offset=" << offset << endl;
#endif

  // zero out memory
  memset(mem, 0, offset);

  ClassMethodId* mthd_info = new ClassMethodId;
  mthd_info->self = self;
  mthd_info->mem = mem;
  mthd_info->cls_id = cls_id;
  mthd_info->mthd_id = mthd_id;

#ifndef _SERIAL
  EnterCriticalSection(&jit_cs);
#endif
  jit_roots.push_back(mthd_info);
#ifndef _SERIAL
  LeaveCriticalSection(&jit_cs);
#endif
}

void MemoryManager::RemoveJitMethodRoot(long* mem)
{
  // find
  ClassMethodId* found = NULL;
#ifndef _SERIAL
  EnterCriticalSection(&jit_cs);
#endif
  list<ClassMethodId*>::iterator jit_iter;
  for(jit_iter = jit_roots.begin(); !found && jit_iter != jit_roots.end(); ++jit_iter) {
    ClassMethodId* id = (*jit_iter);
    if(id->mem == mem) {
      found = id;
    }
  }
#ifndef _SERIAL
  LeaveCriticalSection(&jit_cs);
#endif

#ifdef _DEBUG
  assert(found);
  cout << "removing JIT method: mem=" << found->mem << ", self=" 
       << found->self << "(" << (long)found->self << ")" << endl;
#endif

#ifdef _DEBUG
  assert(found);
#endif
  
#ifndef _SERIAL
  EnterCriticalSection(&jit_cs);
#endif
  jit_roots.remove(found);
#ifndef _SERIAL
  LeaveCriticalSection(&jit_cs);
#endif
  
  delete found;
  found = NULL;
}

long* MemoryManager::AllocateObject(const long obj_id, long* op_stack, long stack_pos)
{
  StackClass* cls = prgm->GetClass(obj_id);
#ifdef _DEBUG
  assert(cls);
#endif

  long* mem = NULL;
  if(cls) {
    long size = cls->GetInstanceMemorySize();
#ifdef _X64
    // TODO: note: memory size is doubled because integers are assumed to be
    // 4-bytes.  This approach allocates more memory because doubles are also
    // doubled.  This will be refactored soon...
    size *= 2;
#endif

    // collect memory
    if(allocation_size + size > mem_max_size) {
      CollectAllMemory(op_stack, stack_pos);
    }
    // allocate memory
    mem = (long*)calloc(size * 2 + sizeof(long) * 2, sizeof(BYTE_VALUE));
    mem[0] = (long)cls;
    mem += 2;

    // record
#ifndef _SERIAL
    EnterCriticalSection(&allocated_cs);
#endif
    allocation_size += size;
    allocated_memory.insert(pair<long*, long>(mem, -obj_id));
#ifndef _SERIAL
    LeaveCriticalSection(&allocated_cs);
#endif
    
#ifdef _DEBUG
    cout << "# allocating object: addr=" << mem << "(" << (long)mem << "), size="
         << size << " byte(s), used=" << allocation_size << " byte(s) #" << endl;
#endif
  }

  return mem;
}

long* MemoryManager::AllocateArray(const long size, const MemoryType type,
                                   long* op_stack, long stack_pos)
{
  long calc_size;
  long* mem;
  switch(type) {
  case BYTE_ARY_TYPE:
    calc_size = size * sizeof(BYTE_VALUE);
    break;

  case INT_TYPE:
    calc_size = size * sizeof(long);
    break;

  case FLOAT_TYPE:
    calc_size = size * sizeof(FLOAT_VALUE);
    break;

  default:
    cerr << "internal error" << endl;
    exit(1);
    break;
  }
  // collect memory
  if(allocation_size + calc_size > mem_max_size) {
    CollectAllMemory(op_stack, stack_pos);
  }
  // allocate memory
  mem = (long*)calloc(calc_size + sizeof(long) * 2, sizeof(BYTE_VALUE));
  mem += 2;

#ifndef _SERIAL
  EnterCriticalSection(&allocated_cs);
#endif
  allocation_size += calc_size;
  allocated_memory.insert(pair<long*, long>(mem, calc_size));
  if(type == INT_TYPE) {
    allocated_int_obj_array.insert(mem);
  }
#ifndef _SERIAL
  LeaveCriticalSection(&allocated_cs);
#endif
  
#ifdef _DEBUG
  cout << "# allocating array: addr=" << mem << "(" << (long)mem << "), size=" << calc_size
       << " byte(s), used=" << allocation_size << " byte(s) #" << endl;
#endif

  return mem;
}

long* MemoryManager::ValidObjectCast(long* mem, long to_id, int* cls_hierarchy, int** cls_interfaces)
{
#ifndef _SERIAL
  EnterCriticalSection(&allocated_cs);
#endif
  
  long id;  
  map<long*, long>::iterator result = allocated_memory.find(mem);
  if(result != allocated_memory.end()) {
    id = -result->second;
  } 
  else {
#ifndef _SERIAL
    LeaveCriticalSection(&allocated_cs);
#endif
    return NULL;
  }
  
  // upcast
  int tmp_id = id;
  while(tmp_id != -1) {
    if(tmp_id == to_id) {
#ifndef _SERIAL
      LeaveCriticalSection(&allocated_cs);
#endif
      return mem;
    }
    // update
    tmp_id = cls_hierarchy[tmp_id];
  }

  // check interfaces
  tmp_id = id;
  int* interfaces = cls_interfaces[tmp_id];
  if(interfaces) {
    int i = 0;
    tmp_id = interfaces[i];
    while(tmp_id > -1) {
      if(tmp_id == to_id) {
#ifndef _GC_SERIAL
        LeaveCriticalSection(&allocated_cs);
#endif
        return mem;
      }
      tmp_id = interfaces[++i];
    }
  }
  
#ifndef _SERIAL
  LeaveCriticalSection(&allocated_cs);
#endif
  
  return NULL;
}

void MemoryManager::CollectAllMemory(long* op_stack, long stack_pos)
{
#ifndef _SERIAL
  // only one thread at a time can invoke the gargabe collector
  if(!TryEnterCriticalSection(&marked_sweep_cs)) {
    return;
  }
#endif
  
  CollectionInfo* info = new CollectionInfo;
  info->op_stack = op_stack; 
  info->stack_pos = stack_pos;
  
#ifndef _SERIAL
  HANDLE collect_thread_id = (HANDLE)_beginthreadex(NULL, 0, CollectMemory, info, 0, NULL);
  if(!collect_thread_id) {
    cerr << "Unable to create garbage collection thread!" << endl;
    exit(-1);
  }
#else
  CollectMemory(info);
#endif
  
#ifndef _SERIAL
  if(WaitForSingleObject(collect_thread_id, INFINITE) != WAIT_OBJECT_0) {
    cerr << "Unable to join garbage collection threads!" << endl;
    exit(-1);
  }
  CloseHandle(collect_thread_id);
  LeaveCriticalSection(&marked_sweep_cs);
#endif
}

uintptr_t WINAPI MemoryManager::CollectMemory(void* arg)
{
  CollectionInfo* info = (CollectionInfo*)arg;
  
#ifdef _DEBUG
  long start = allocation_size;
  cout << endl << "=========================================" << endl;
  cout << "Starting Garbage Collection; thread=" << GetCurrentThread() << endl;
  cout << "=========================================" << endl;
  cout << "## Marking memory ##" << endl;
#endif

#ifndef _SERIAL
  const int num_threads = 4;
  HANDLE thread_ids[num_threads];

  thread_ids[0] = (HANDLE)_beginthreadex(NULL, 0, CheckStatic, info, 0, NULL);
  if(!thread_ids[0]) {
    cerr << "Unable to create garbage collection thread!" << endl;
    exit(-1);
  }

  thread_ids[1] = (HANDLE)_beginthreadex(NULL, 0, CheckStack, info, 0, NULL);
  if(!thread_ids[1]) {
    cerr << "Unable to create garbage collection thread!" << endl;
    exit(-1);
  }
  
  thread_ids[2] = (HANDLE)_beginthreadex(NULL, 0, CheckPdaRoots, NULL, 0, NULL);
  if(!thread_ids[2]) {
    cerr << "Unable to create garbage collection thread!" << endl;
    exit(-1);
  }

  thread_ids[3] = (HANDLE)_beginthreadex(NULL, 0, CheckJitRoots, NULL, 0, NULL);
  if(!thread_ids[3]) {
    cerr << "Unable to create garbage collection thread!" << endl;
    exit(-1);
  }
  
  // join all of the mark threads
  if(WaitForMultipleObjects(num_threads, thread_ids, TRUE, INFINITE) != WAIT_OBJECT_0) {
    cerr << "Unable to join garbage collection threads!" << endl;
    exit(-1);
  }

  for(int i=0; i < num_threads; i++) {
    CloseHandle(thread_ids[i]);
  }
#else
  CheckStatic(NULL);
  CheckStack(info);
  CheckPdaRoots(NULL);
  CheckJitRoots(NULL);
#endif
  
  // sweep memory
#ifdef _DEBUG
  cout << "## Sweeping memory ##" << endl;
#endif
  vector<long*> erased_memory;
  
  // sort and search
#ifndef _SERIAL
  EnterCriticalSection(&allocated_cs);
  EnterCriticalSection(&marked_cs);
#endif
  
#ifdef _DEBUG
  cout << "-----------------------------------------" << endl;
  cout << "Marked " << marked_memory.size() << " items." << endl;
  cout << "-----------------------------------------" << endl;
#endif
  std::sort(marked_memory.begin(), marked_memory.end());
  map<long*, long>::iterator iter;

#ifndef _SERIAL
  
#endif
  for(iter = allocated_memory.begin(); iter != allocated_memory.end(); ++iter) {
    bool found = false;
    if(std::binary_search(marked_memory.begin(), marked_memory.end(), iter->first)) {
      long* tmp = iter->first;
      tmp[-1] = 0L;
      found = true;
    }

	// not found, will be collected
    if(!found) {
      // object or array	
      long mem_size;
      if(iter->second < 0) {
        StackClass* cls = prgm->GetClass(-iter->second);
#ifdef _DEBUG
        assert(cls);
#endif
        if(cls) {
          mem_size = cls->GetInstanceMemorySize();
        }
		else {
         mem_size = iter->second;
        }
      } 
      else {
        mem_size = iter->second;
      }

#ifdef _DEBUG
      cout << "# releasing memory: addr=" << iter->first << "(" << (long)iter->first
           << "), size=" << mem_size << " byte(s) #" << endl;
#endif

      // account for deallocated memory
      allocation_size -= mem_size;
      // erase memory
      long* tmp = iter->first;
      erased_memory.push_back(tmp);

      tmp -= 2;
      free(tmp);
      tmp = NULL;
    }
  }
  marked_memory.clear();
#ifndef _SERIAL
  LeaveCriticalSection(&marked_cs);
#endif  
  
  // did not collect memory; ajust constraints
  if(erased_memory.empty()) {
    if(uncollected_count < UNCOLLECTED_COUNT) {
      uncollected_count++;
    } else {
      mem_max_size *= 2;
      uncollected_count = 0;
    }
  }
  // collected memory; ajust constraints
  else if(mem_max_size != MEM_MAX) {
    if(collected_count < COLLECTED_COUNT) {
      collected_count++;
    } else {
      mem_max_size /= 2;
      collected_count = 0;
    }
  }
  
  // remove references from allocated pool
  for(size_t i = 0; i < erased_memory.size(); i++) {
    allocated_memory.erase(erased_memory[i]);
    allocated_int_obj_array.erase(erased_memory[i]);
  }
#ifndef _SERIAL
  LeaveCriticalSection(&allocated_cs);
#endif
  
#ifdef _DEBUG
  cout << "===============================================================" << endl;
  cout << "Finished Collection: collected=" << (start - allocation_size)
       << " of " << start << " byte(s) - " << showpoint << setprecision(3)
       << (((double)(start - allocation_size) / (double)start) * 100.0)
       << "%" << endl;
  cout << "===============================================================" << endl;
#endif
  
  return 0;
}

size_t WINAPI MemoryManager::CheckStatic(void* arg)
{
  // check static memory
#ifndef _GC_SERIAL
  EnterCriticalSection(&static_cs);
#endif
  map<long*, long>::iterator static_iter;
  for(static_iter = static_memory.begin(); static_iter != static_memory.end(); ++static_iter) {
    CheckObject(static_iter->first, false, 1);	
  }
#ifndef _GC_SERIAL
  LeaveCriticalSection(&static_cs);
#endif

  return 0;
}

uintptr_t WINAPI MemoryManager::CheckStack(void* arg)
{
  CollectionInfo* info = (CollectionInfo*)arg;
#ifdef _DEBUG
  cout << "----- Sweeping Stack: stack: pos=" << info->stack_pos 
       << "; thread=" << GetCurrentThread() << " -----" << endl;
#endif
  while(info->stack_pos > -1) {
    CheckObject((long*)info->op_stack[info->stack_pos--], false, 1);
  }
  delete info;
  info = NULL;
  
  return 0;
}

uintptr_t WINAPI MemoryManager::CheckJitRoots(void* arg)
{
#ifndef _SERIAL
  EnterCriticalSection(&jit_cs);
#endif  
  
#ifdef _DEBUG
  cout << "---- Sweeping JIT method root(s): num=" << jit_roots.size() 
       << "; thread=" << GetCurrentThread() << " ------" << endl;
  cout << "memory types: " << endl;
#endif
  
  list<ClassMethodId*>::iterator jit_iter;
  for(jit_iter = jit_roots.begin(); jit_iter != jit_roots.end(); jit_iter++) {
    ClassMethodId* id = (*jit_iter);
    long* mem = id->mem;
    StackMethod* mthd = prgm->GetClass(id->cls_id)->GetMethod(id->mthd_id);
    const long dclrs_num = mthd->GetNumberDeclarations();

#ifdef _DEBUG
    cout << "\t===== JIT method: name=" << mthd->GetName() << ", id=" << id->cls_id << "," 
	 << id->mthd_id << "; addr=" << mthd << "; num=" << mthd->GetNumberDeclarations() 
	 << " =====" << endl;
#endif

    // check self
    CheckObject(id->self, true, 1);

    StackDclr** dclrs = mthd->GetDeclarations();
    for(int j = dclrs_num - 1; j > -1; j--) {
#ifndef _SERIAL
      EnterCriticalSection(&allocated_cs);
#endif
      
#ifdef _DEBUG
      // get memory size
      long array_size = 0;
      map<long*, long>::iterator result = allocated_memory.find((long*)(*mem));
      if(result != allocated_memory.end()) {
        array_size = result->second;
      }
#endif
      
#ifndef _SERIAL
      LeaveCriticalSection(&allocated_cs);
#endif
      
      // update address based upon type
      switch(dclrs[j]->type) {
      case FUNC_PARM:
#ifdef _DEBUG
	cout << "\t" << j << ": FUNC_PARM: value=" << (*mem) 
	     << "," << *(mem + 1)<< endl;
#endif
	// update
	mem += 2;
	break;
	
      case INT_PARM:
#ifdef _DEBUG
        cout << "\t" << j << ": INT_PARM: value=" << (*mem) << endl;
#endif
        // update
        mem++;
        break;

      case FLOAT_PARM: {
#ifdef _DEBUG
        FLOAT_VALUE value;
        memcpy(&value, mem, sizeof(FLOAT_VALUE));
        cout << "\t" << j << ": FLOAT_PARM: value=" << value << endl;
#endif
        // update
        mem += 2;
      }
      break;

      case BYTE_ARY_PARM:
#ifdef _DEBUG
        cout << "\t" << j << ": BYTE_ARY_PARM: addr=" << (long*)(*mem)
             << "(" << (long)(*mem) << "), size=" << array_size << " byte(s)" << endl;
#endif
        // mark data
        MarkMemory((long*)(*mem));
        // update
        mem++;
        break;

      case INT_ARY_PARM:
#ifdef _DEBUG
        cout << "\t" << j << ": INT_ARY_PARM: addr=" << (long*)(*mem)
             << "(" << (long)(*mem) << "), size=" << array_size << " byte(s)" << endl;
#endif
        // mark data
        MarkMemory((long*)(*mem));
        // update
        mem++;
        break;

      case FLOAT_ARY_PARM:
#ifdef _DEBUG
        cout << "\t" << j << ": FLOAT_ARY_PARM: addr=" << (long*)(*mem)
             << "(" << (long)(*mem) << "), size=" << " byte(s)" << array_size << endl;
#endif
        // mark data
        MarkMemory((long*)(*mem));
        // update
        mem++;
        break;

      case OBJ_PARM:
#ifdef _DEBUG
        cout << "\t" << j << ": OBJ_PARM: addr=" << (long*)(*mem)
             << "(" << (long)(*mem) << "), id=" << array_size << endl;
#endif
        // check object
        CheckObject((long*)(*mem), true, 1);
        // update
        mem++;
        break;

        // TODO: test the code below
      case OBJ_ARY_PARM:
#ifdef _DEBUG
        cout << "\tOBJ_ARY_PARM: addr=" << (long*)(*mem) << "(" << (long)(*mem)
             << "), size=" << array_size << " byte(s)" << endl;
#endif
        // mark data
        if(MarkMemory((long*)(*mem))) {
          long* array = (long*)(*mem);
          const long size = array[0];
          const long dim = array[1];
          long* objects = (long*)(array + 2 + dim);
          for(long k = 0; k < size; k++) {
            CheckObject((long*)objects[k], true, 2);
          }
        }
        // update
        mem++;
        break;
		
		default:
			break;
      }
    }

    // NOTE: this marks temporary variables that are stored in JIT memory
    // during some method calls. there are 3 integer temp addresses
    for(int i = 0; i < 8; i++) {
      CheckObject((long*)mem[i], false, 1);
    }
  }
  
#ifndef _SERIAL
  LeaveCriticalSection(&jit_cs);  
#endif

  return 0;
}

uintptr_t WINAPI MemoryManager::CheckPdaRoots(void* arg)
{
#ifndef _SERIAL
  EnterCriticalSection(&jit_cs);
#endif
  
#ifdef _DEBUG
  cout << "----- PDA method root(s): num=" << pda_roots.size() 
       << "; thread=" << GetCurrentThread()<< " -----" << endl;
  cout << "memory types:" <<  endl;
#endif
  // look at pda methods
  list<StackFrame*>::iterator pda_iter;
  for(pda_iter = pda_roots.begin(); pda_iter != pda_roots.end(); ++pda_iter) {
    StackMethod* mthd = (*pda_iter)->GetMethod();
    long* mem = (*pda_iter)->GetMemory();

#ifdef _DEBUG
    cout << "\t===== PDA method: name=" << mthd->GetName() << ", addr="
         << mthd << ", num=" << mthd->GetNumberDeclarations() << " =====" << endl;
#endif

    // mark self
    CheckObject((long*)(*mem), true, 1);

    if(mthd->HasAndOr()) {
      mem += 2;
    } else {
      mem++;
    }

    // mark rest of memory
    CheckMemory(mem, mthd->GetDeclarations(), mthd->GetNumberDeclarations(), 0);
  }
#ifndef _SERIAL
  LeaveCriticalSection(&jit_cs);
#endif

  return 0;
}

void MemoryManager::CheckMemory(long* mem, StackDclr** dclrs, const long dcls_size, long depth)
{
  // check method
  for(long i = 0; i < dcls_size; i++) {
#ifndef _SERIAL
    EnterCriticalSection(&allocated_cs);
#endif
    
#ifdef _DEBUG
    // get memory size
    long array_size = 0;
    map<long*, long>::iterator result = allocated_memory.find((long*)(*mem));
    if(result != allocated_memory.end()) {
      array_size = result->second;
    }
#endif

#ifndef _SERIAL
    LeaveCriticalSection(&allocated_cs);
#endif
    
#ifdef _DEBUG
    for(int j = 0; j < depth; j++) {
      cout << "\t";
    }
#endif

    // update address based upon type
    switch(dclrs[i]->type) {
    case FUNC_PARM:
#ifdef _DEBUG
      cout << "\t" << i << ": FUNC_PARM: value=" << (*mem) 
	   << "," << *(mem + 1)<< endl;
#endif
      // update
      mem += 2;
      break;
      
    case INT_PARM:
#ifdef _DEBUG
      cout << "\t" << i << ": INT_PARM: value=" << (*mem) << endl;
#endif
      // update
      mem++;
      break;

    case FLOAT_PARM: {
#ifdef _DEBUG
      FLOAT_VALUE value;
      memcpy(&value, mem, sizeof(FLOAT_VALUE));
      cout << "\t" << i << ": FLOAT_PARM: value=" << value << endl;
#endif
      // update
      mem += 2;
    }
    break;

    case BYTE_ARY_PARM:
#ifdef _DEBUG
      cout << "\t" << i << ": BYTE_ARY_PARM: addr=" << (long*)(*mem) << "("
           << (long)(*mem) << "), size=" << array_size << " byte(s)" << endl;
#endif
      // mark data
      MarkMemory((long*)(*mem));
      // update
      mem++;
      break;

    case INT_ARY_PARM:
#ifdef _DEBUG
      cout << "\t" << i << ": INT_ARY_PARM: addr=" << (long*)(*mem) << "("
           << (long)(*mem) << "), size=" << array_size << " byte(s)" << endl;
#endif
      // mark data
      MarkMemory((long*)(*mem));
      // update
      mem++;
      break;

    case FLOAT_ARY_PARM:
#ifdef _DEBUG
      cout << "\t" << i << ": FLOAT_ARY_PARM: addr=" << (long*)(*mem) << "("
           << (long)(*mem) << "), size=" << array_size << " byte(s)" << endl;
#endif
      // mark data
      MarkMemory((long*)(*mem));
      // update
      mem++;
      break;

    case OBJ_PARM:
#ifdef _DEBUG
      cout << "\t" << i << ": OBJ_PARM: addr=" << (long*)(*mem) << "("
           << (long)(*mem) << "), id=" << array_size << endl;
#endif
      // check object
      CheckObject((long*)(*mem), true, depth + 1);
      // update
      mem++;
      break;

    case OBJ_ARY_PARM:
#ifdef _DEBUG
      cout << "\t" << i << ": OBJ_ARY_PARM: addr=" << (long*)(*mem) << "("
           << (long)(*mem) << "), size=" << array_size << " byte(s)" << endl;
#endif
      // mark data
      if(MarkMemory((long*)(*mem))) {
        long* array = (long*)(*mem);
        const long size = array[0];
        const long dim = array[1];
        long* objects = (long*)(array + 2 + dim);
        for(long k = 0; k < size; k++) {
          CheckObject((long*)objects[k], true, 2);
        }
      }
      // update
      mem++;
      break;
	  
	  default:
		break;
    }
  }
}

void MemoryManager::CheckObject(long* mem, bool is_obj, long depth)
{
  if(mem) {
    // TODO: optimize so this is not a double call.. see below
    StackClass* cls = GetClassMapping(mem);
    if(cls) {
#ifdef _DEBUG
      for(int i = 0; i < depth; i++) {
        cout << "\t";
      }
      cout << "\t----- object: addr=" << mem << "(" << (long)mem << "), num="
           << cls->GetNumberDeclarations() << " -----" << endl;
#endif

      // mark data
      if(MarkMemory(mem)) {
        CheckMemory(mem, cls->GetDeclarations(), cls->GetNumberDeclarations(), depth);
      }
    } 
    else {
      // NOTE: this happens when we are trying to mark unidentified memory
      // segments. these segments may be parts of that stack or temp for
      // register variables
#ifdef _DEBUG
      for(int i = 0; i < depth; i++) {
        cout << "\t";
      }
      cout <<"$: addr/value=" << mem << endl;
      if(is_obj) {
        assert(cls);
      }
#endif
      // primitive or object array
      if(MarkMemory(mem)) {
	// ensure we're only checking int and obj arrays
	set<long*>::iterator result = allocated_int_obj_array.find(mem);
      	if(result != allocated_int_obj_array.end()) {
	  long* array = mem;
	  const long size = array[0];
	  const long dim = array[1];
	  long* objects = (long*)(array + 2 + dim);
	  for(long k = 0; k < size; k++) {
	    CheckObject((long*)objects[k], false, 2);
	  }
	}
      }
    }
  }
}
