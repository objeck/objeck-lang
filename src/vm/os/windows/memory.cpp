/***************************************************************************
* VM memory manager. Implements a "mark and sweep" collection algorithm.
*
* Copyright (c) 2008-2013, Randy Hollines
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

StackProgram* MemoryManager::prgm;
unordered_map<long*, ClassMethodId*> MemoryManager::jit_roots;
set<StackFrame**> MemoryManager::pda_frames;
unordered_map<StackFrameMonitor*, StackFrameMonitor*> MemoryManager::pda_monitors;
stack<char*> MemoryManager::cache_pool_16;
stack<char*> MemoryManager::cache_pool_32;
stack<char*> MemoryManager::cache_pool_64;
stack<char*> MemoryManager::cache_pool_256;
stack<char*> MemoryManager::cache_pool_512;
vector<long*> MemoryManager::allocated_memory;
vector<long*> MemoryManager::marked_memory;

bool MemoryManager::initialized;
long MemoryManager::allocation_size;
long MemoryManager::mem_max_size;
long MemoryManager::uncollected_count;
long MemoryManager::collected_count;
CRITICAL_SECTION MemoryManager::jit_cs;
CRITICAL_SECTION MemoryManager::pda_frame_cs;
CRITICAL_SECTION MemoryManager::pda_monitor_cs;
CRITICAL_SECTION MemoryManager::allocated_cs;
CRITICAL_SECTION MemoryManager::marked_cs;
CRITICAL_SECTION MemoryManager::marked_sweep_cs;

void MemoryManager::Initialize(StackProgram* p)
{
  prgm = p;
  allocation_size = 0;
  mem_max_size = MEM_MAX;
  uncollected_count = 0;

  InitializeCriticalSection(&jit_cs);
  InitializeCriticalSection(&pda_frame_cs);
  InitializeCriticalSection(&pda_monitor_cs);
  InitializeCriticalSection(&allocated_cs);
  InitializeCriticalSection(&marked_cs);
  InitializeCriticalSection(&marked_sweep_cs);

  for(int i = 0; i < POOL_SIZE; i++) {
    cache_pool_16.push((char*)calloc(16, sizeof(char)));
  }

  for(int i = 0; i < POOL_SIZE; i++) {
    cache_pool_32.push((char*)calloc(32, sizeof(char)));
  }

  for(int i = 0; i < POOL_SIZE; i++) {
    cache_pool_64.push((char*)calloc(64, sizeof(char)));
  }

  for(int i = 0; i < POOL_SIZE; i++) {
    cache_pool_256.push((char*)calloc(256, sizeof(char)));
  }

  for(int i = 0; i < POOL_SIZE; i++) {
    cache_pool_512.push((char*)calloc(512, sizeof(char)));
  }

  initialized = true;
}

// if return true, trace memory otherwise do not
inline bool MemoryManager::MarkMemory(long* mem)
{
  if(mem) {
    // check if memory has been marked
    if(mem[MARKED_FLAG]) {
      return false;
    }

    // mark & add to list
#ifndef GC_SERIAL
    EnterCriticalSection(&marked_cs);
#endif
    mem[MARKED_FLAG] = 1L;
    marked_memory.push_back(mem);
#ifndef GC_SERIAL
    LeaveCriticalSection(&marked_cs);     
#endif

    return true;
  }

  return false;
}

// if return true, trace memory otherwise do not
inline bool MemoryManager::MarkValidMemory(long* mem)
{
  if(mem) {
#ifndef GC_SERIAL
    EnterCriticalSection(&allocated_cs);
#endif
    if(std::binary_search(allocated_memory.begin(), allocated_memory.end(), mem)) {
      // check if memory has been marked
      if(mem[MARKED_FLAG]) {
#ifndef GC_SERIAL
        LeaveCriticalSection(&allocated_cs);
#endif
        return false;
      }

      // mark & add to list
#ifndef GC_SERIAL
      EnterCriticalSection(&marked_cs);
#endif
      mem[-1] = 1L;
      marked_memory.push_back(mem);
#ifndef GC_SERIAL
      LeaveCriticalSection(&marked_cs);      
      LeaveCriticalSection(&allocated_cs);
#endif
      return true;
    } 
    else {
#ifndef GC_SERIAL
      LeaveCriticalSection(&allocated_cs);
#endif
      return false;
    }
  }

  return false;
}

void MemoryManager::AddPdaMethodRoot(StackFrame** frame)
{
  if(!initialized) {
    return;
  }

#ifdef _DEBUG
  wcout << L"adding PDA frame: addr=" << frame << endl;
#endif

#ifndef _GC_SERIAL
  EnterCriticalSection(&pda_frame_cs);
#endif
  pda_frames.insert(frame);
#ifndef _GC_SERIAL
  LeaveCriticalSection(&pda_frame_cs);
#endif
}

void MemoryManager::RemovePdaMethodRoot(StackFrame** frame)
{
#ifdef _DEBUG
  wcout << L"removing PDA frame: addr=" << frame << endl;
#endif
  
#ifndef _GC_SERIAL
  EnterCriticalSection(&pda_frame_cs);
#endif
  pda_frames.erase(frame);
#ifndef _GC_SERIAL
  LeaveCriticalSection(&pda_frame_cs);
#endif
}

void MemoryManager::AddPdaMethodRoot(StackFrameMonitor* monitor)
{
#ifdef _DEBUG
  wcout << L"adding PDA method: monitor=" << monitor << endl;
#endif

#ifndef GC_SERIAL
  EnterCriticalSection(&pda_monitor_cs);
#endif
  pda_monitors.insert(pair<StackFrameMonitor*, StackFrameMonitor*>(monitor, monitor));
#ifndef GC_SERIAL
  LeaveCriticalSection(&pda_monitor_cs);
#endif
}

void MemoryManager::RemovePdaMethodRoot(StackFrameMonitor* monitor)
{
  if(!initialized) {
    return;
  }

#ifdef _DEBUG
  wcout << L"removing PDA method: monitor=" << monitor << endl;
#endif

#ifndef GC_SERIAL
  EnterCriticalSection(&pda_monitor_cs);
#endif
  pda_monitors.erase(monitor);
#ifndef GC_SERIAL
  LeaveCriticalSection(&pda_monitor_cs);
#endif
}

void MemoryManager::AddJitMethodRoot(long cls_id, long mthd_id,
                                     long* self, long* mem, long offset)
{
#ifdef _DEBUG
  wcout << L"adding JIT root: class=" << cls_id << L", method=" << mthd_id << L", self=" << self
    << L"(" << (long)self << L"), mem=" << mem << L", offset=" << offset << endl;
#endif

  // zero out memory
  memset(mem, 0, offset);

  ClassMethodId* mthd_info = new ClassMethodId;
  mthd_info->self = self;
  mthd_info->mem = mem;
  mthd_info->cls_id = cls_id;
  mthd_info->mthd_id = mthd_id;

#ifndef GC_SERIAL
  EnterCriticalSection(&jit_cs);
#endif
  jit_roots.insert(pair<long*, ClassMethodId*>(mem, mthd_info));
#ifndef GC_SERIAL
  LeaveCriticalSection(&jit_cs);
#endif
}

void MemoryManager::RemoveJitMethodRoot(long* mem)
{
  ClassMethodId* id;
#ifndef GC_SERIAL
  EnterCriticalSection(&jit_cs);
#endif

  unordered_map<long*, ClassMethodId*>::iterator found = jit_roots.find(mem);
  if(found == jit_roots.end()) {
    wcerr << L"Unable to find JIT root!" << endl;
    exit(-1);
  }
  id = found->second;

#ifdef _DEBUG  
  wcout << L"removing JIT method: mem=" << id->mem << L", self=" 
    << id->self << L"(" << (long)id->self << L")" << endl;
#endif
  jit_roots.erase(found);

#ifndef GC_SERIAL
  LeaveCriticalSection(&jit_cs);
#endif

  delete id;;
  id = NULL;
}

long* MemoryManager::AllocateObject(const long obj_id, long* op_stack, long stack_pos, bool collect)
{
  StackClass* cls = prgm->GetClass(obj_id);
#ifdef _DEBUG
  assert(cls);
#endif

  long* mem = NULL;
  if(cls) {
    long size = cls->GetInstanceMemorySize();

    // collect memory
    if(collect && allocation_size + size > mem_max_size) {
      CollectAllMemory(op_stack, stack_pos);
    }

    // allocate memory
#ifdef _DEBUG
    bool is_cached = false;
#endif
    const long alloc_size = size * 2 + sizeof(long) * EXTRA_BUF_SIZE;
    if(cache_pool_512.size() > 0 && alloc_size <= 512 && alloc_size > 256) {
      mem = (long*)cache_pool_512.top();
      cache_pool_512.pop();
      mem[EXTRA_BUF_SIZE + CACHE_SIZE] = 512;
#ifdef _DEBUG
      is_cached = true;
#endif
    }
    else if(cache_pool_256.size() > 0 && alloc_size <= 256 && alloc_size > 64) {
      mem = (long*)cache_pool_256.top();
      cache_pool_256.pop();
      mem[EXTRA_BUF_SIZE + CACHE_SIZE] = 256;
#ifdef _DEBUG
      is_cached = true;
#endif
    }
    else if(cache_pool_64.size() > 0 && alloc_size <= 64 && alloc_size > 32) {
      mem = (long*)cache_pool_64.top();
      cache_pool_64.pop();
      mem[EXTRA_BUF_SIZE + CACHE_SIZE] = 64;
#ifdef _DEBUG
      is_cached = true;
#endif
    }
    else if(cache_pool_32.size() > 0 && alloc_size <= 32 && alloc_size > 16) {
      mem = (long*)cache_pool_32.top();
      cache_pool_32.pop();
      mem[EXTRA_BUF_SIZE + CACHE_SIZE] = 32;
#ifdef _DEBUG
      is_cached = true;
#endif
    }
    else if(cache_pool_16.size() > 0 && alloc_size <= 16) {
      mem = (long*)cache_pool_16.top();
      cache_pool_16.pop();
      mem[EXTRA_BUF_SIZE + CACHE_SIZE] = 16;
#ifdef _DEBUG
      is_cached = true;
#endif
    } 
    else {
      mem = (long*)calloc(alloc_size, sizeof(char));
      mem[EXTRA_BUF_SIZE + CACHE_SIZE] = -1;
    }
    mem[EXTRA_BUF_SIZE + TYPE] = NIL_TYPE;
    mem[EXTRA_BUF_SIZE + SIZE_OR_CLS] = (long)cls;
    mem += EXTRA_BUF_SIZE;

    // record
#ifndef GC_SERIAL
    EnterCriticalSection(&allocated_cs);
#endif
    allocation_size += size;
    allocated_memory.push_back(mem);
#ifndef GC_SERIAL
    LeaveCriticalSection(&allocated_cs);
#endif

#ifdef _DEBUG
    wcout << L"# allocating object: cached=" << (is_cached ? "true" : "false")  
      << ", addr=" << mem << L"(" << (long)mem << L"), size="
      << size << L" byte(s), used=" << allocation_size << L" byte(s) #" << endl;
#endif
  }

  return mem;
}

long* MemoryManager::AllocateArray(const long size, const MemoryType type,
                                   long* op_stack, long stack_pos, bool collect)
{
  long calc_size;
  long* mem;
  switch(type) {
  case BYTE_ARY_TYPE:
    calc_size = size * sizeof(char);
    break;

  case CHAR_ARY_TYPE:
    calc_size = size * sizeof(wchar_t);
    break;

  case INT_TYPE:
    calc_size = size * sizeof(long);
    break;

  case FLOAT_TYPE:
    calc_size = size * sizeof(FLOAT_VALUE);
    break;

  default:
    wcerr << L"internal error" << endl;
    exit(1);
    break;
  }

  // collect memory
  if(collect && allocation_size + calc_size > mem_max_size) {
    CollectAllMemory(op_stack, stack_pos);
  }

  // allocate memory
#ifdef _DEBUG
  bool is_cached = false;
#endif
  const long alloc_size = calc_size + sizeof(long) * EXTRA_BUF_SIZE;
  if(cache_pool_512.size() > 0 && alloc_size <= 512 && alloc_size > 256) {
    mem = (long*)cache_pool_512.top();
    cache_pool_512.pop();
    mem[EXTRA_BUF_SIZE + CACHE_SIZE] = 512;
#ifdef _DEBUG
    is_cached = true;
#endif
  }
  else if(cache_pool_256.size() > 0 && alloc_size <= 256 && alloc_size > 64) {
    mem = (long*)cache_pool_256.top();
    cache_pool_256.pop();
    mem[EXTRA_BUF_SIZE + CACHE_SIZE] = 256;
#ifdef _DEBUG
    is_cached = true;
#endif
  }
  else   if(cache_pool_64.size() > 0 && alloc_size <= 64 && alloc_size > 32) {
    mem = (long*)cache_pool_64.top();
    cache_pool_64.pop();
    mem[EXTRA_BUF_SIZE + CACHE_SIZE] = 64;
#ifdef _DEBUG
    is_cached = true;
#endif
  }
  else if(cache_pool_32.size() > 0 && alloc_size <= 32 && alloc_size > 16) {
    mem = (long*)cache_pool_32.top();
    cache_pool_32.pop();
    mem[EXTRA_BUF_SIZE + CACHE_SIZE] = 32;
#ifdef _DEBUG
    is_cached = true;
#endif
  }
  else if(cache_pool_16.size() > 0 && alloc_size <= 16) {
    mem = (long*)cache_pool_16.top();
    cache_pool_16.pop();
    mem[EXTRA_BUF_SIZE + CACHE_SIZE] = 16;
#ifdef _DEBUG
    is_cached = true;
#endif
  } 
  else {    
    mem = (long*)calloc(alloc_size, sizeof(char));
    mem[EXTRA_BUF_SIZE + CACHE_SIZE] = -1;
  }
  mem[EXTRA_BUF_SIZE + TYPE] = type;
  mem[EXTRA_BUF_SIZE + SIZE_OR_CLS] = calc_size;
  mem += EXTRA_BUF_SIZE;

#ifndef GC_SERIAL
  EnterCriticalSection(&allocated_cs);
#endif
  allocation_size += calc_size;
  allocated_memory.push_back(mem);
#ifndef GC_SERIAL
  LeaveCriticalSection(&allocated_cs);
#endif

#ifdef _DEBUG
  wcout << L"# allocating array: cached=" << (is_cached ? "true" : "false") 
    << ", addr=" << mem << L"(" << (long)mem << L"), size=" << calc_size
    << L" byte(s), used=" << allocation_size << L" byte(s) #" << endl;
#endif

  return mem;
}

long* MemoryManager::ValidObjectCast(long* mem, long to_id, int* cls_hierarchy, int** cls_interfaces)
{
  // invalid array cast  
  long id = GetObjectID(mem);
  if (id < 0) {
    return NULL;
  }

  // upcast
  int cls_id = id;
  while (cls_id != -1) {
    if (cls_id == to_id) {
      return mem;
    }
    // update
    cls_id = cls_hierarchy[cls_id];
  }

  // check interfaces
  cls_id = id;
  while (cls_id != -1) {
    int* interfaces = cls_interfaces[cls_id];
    if (interfaces) {
      int i = 0;
      int inf_id = interfaces[i];
      while (inf_id > -1) {
        if (inf_id == to_id) {
          return mem;
        }
        inf_id = interfaces[++i];
      }
    }
    // update
    cls_id = cls_hierarchy[cls_id];
  }

  return NULL;
}

void MemoryManager::CollectAllMemory(long* op_stack, long stack_pos)
{
#ifndef GC_SERIAL
  // only one thread at a time can invoke the gargabe collector
  if(!TryEnterCriticalSection(&marked_sweep_cs)) {
    return;
  }
#endif

  CollectionInfo* info = new CollectionInfo;
  info->op_stack = op_stack; 
  info->stack_pos = stack_pos;

#ifndef GC_SERIAL
  HANDLE collect_thread_id = (HANDLE)_beginthreadex(NULL, 0, CollectMemory, info, 0, NULL);
  if(!collect_thread_id) {
    wcerr << L"Unable to create garbage collection thread!" << endl;
    exit(-1);
  }
#else
  CollectMemory(info);
#endif

#ifndef GC_SERIAL
  if(WaitForSingleObject(collect_thread_id, INFINITE) != WAIT_OBJECT_0) {
    wcerr << L"Unable to join garbage collection threads!" << endl;
    exit(-1);
  }
  CloseHandle(collect_thread_id);
  LeaveCriticalSection(&marked_sweep_cs);
#endif
}

uintptr_t WINAPI MemoryManager::CollectMemory(void* arg)
{
  CollectionInfo* info = (CollectionInfo*)arg;

#ifndef _GC_SERIAL
  EnterCriticalSection(&allocated_cs);
#endif
  std::sort(allocated_memory.begin(), allocated_memory.end());
#ifndef _GC_SERIAL
  LeaveCriticalSection(&allocated_cs);
#endif

#ifdef _DEBUG
  long start = allocation_size;
  wcout << dec << endl << L"=========================================" << endl;
  wcout << L"Starting Garbage Collection; thread=" << GetCurrentThread() << endl;
  wcout << L"=========================================" << endl;
  wcout << L"## Marking memory ##" << endl;
#endif

#ifndef GC_SERIAL
  const int num_threads = 4;
  HANDLE thread_ids[num_threads];

  thread_ids[0] = (HANDLE)_beginthreadex(NULL, 0, CheckStatic, info, 0, NULL);
  if(!thread_ids[0]) {
    wcerr << L"Unable to create garbage collection thread!" << endl;
    exit(-1);
  }

  thread_ids[1] = (HANDLE)_beginthreadex(NULL, 0, CheckStack, info, 0, NULL);
  if(!thread_ids[1]) {
    wcerr << L"Unable to create garbage collection thread!" << endl;
    exit(-1);
  }

  thread_ids[2] = (HANDLE)_beginthreadex(NULL, 0, CheckPdaRoots, NULL, 0, NULL);
  if(!thread_ids[2]) {
    wcerr << L"Unable to create garbage collection thread!" << endl;
    exit(-1);
  }

  thread_ids[3] = (HANDLE)_beginthreadex(NULL, 0, CheckJitRoots, NULL, 0, NULL);
  if(!thread_ids[3]) {
    wcerr << L"Unable to create garbage collection thread!" << endl;
    exit(-1);
  }

  // join all of the mark threads
  if(WaitForMultipleObjects(num_threads, thread_ids, TRUE, INFINITE) != WAIT_OBJECT_0) {
    wcerr << L"Unable to join garbage collection threads!" << endl;
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
  wcout << L"## Sweeping memory ##" << endl;
#endif

  // sort and search
#ifndef GC_SERIAL
  EnterCriticalSection(&allocated_cs);
  EnterCriticalSection(&marked_cs);
#endif

#ifdef _DEBUG
  wcout << L"-----------------------------------------" << endl;
  wcout << L"Marked " << marked_memory.size() << L" items." << endl;
  wcout << L"-----------------------------------------" << endl;
#endif
  std::sort(marked_memory.begin(), marked_memory.end());
#ifndef GC_SERIAL

#endif
  vector<long*> live_memory;
  live_memory.reserve(allocated_memory.size());
  for(size_t i = 0; i < allocated_memory.size(); ++i) {
    long* mem = allocated_memory[i];

    // check dynamic memory
    bool found = false;
    if(std::binary_search(marked_memory.begin(), marked_memory.end(), mem)) {
      long* tmp = mem;
      tmp[MARKED_FLAG] = 0L;
      found = true;
    }

    // live
    if(found) {
      live_memory.push_back(mem);
    }
    // will be collected
    else {
      // object or array	
      long mem_size;
      if(mem[TYPE] == NIL_TYPE) {
        StackClass* cls = (StackClass*)mem[SIZE_OR_CLS];
#ifdef _DEBUG
        assert(cls);
#endif
        if(cls) {
          mem_size = cls->GetInstanceMemorySize();
        }
        else {
          mem_size = mem[SIZE_OR_CLS];
        }
      } 
      else {
        mem_size = mem[SIZE_OR_CLS];
      }

      // account for deallocated memory
      allocation_size -= mem_size;

      // cache or free memory
      long* tmp = mem - EXTRA_BUF_SIZE;
      switch(mem[CACHE_SIZE]) {
      case 512:	  
        if(cache_pool_512.size() < POOL_SIZE + 1) {
          memset(tmp, 0, 512);
          cache_pool_512.push((char*)tmp);
#ifdef _DEBUG
          wcout << L"# caching memory: addr=" << mem << L"(" << (long)mem
            << L"), size=" << mem_size << L" byte(s) #" << endl;
#endif
        }
        break;

      case 256:
        if(cache_pool_256.size() < POOL_SIZE + 1) {
          memset(tmp, 0, 256);
          cache_pool_256.push((char*)tmp);
#ifdef _DEBUG
          wcout << L"# caching memory: addr=" << mem << L"(" << (long)mem
            << L"), size=" << mem_size << L" byte(s) #" << endl;
#endif
        }
        break;

      case 64:
        if(cache_pool_64.size() < POOL_SIZE + 1) {
          memset(tmp, 0, 64);
          cache_pool_64.push((char*)tmp);
#ifdef _DEBUG
          wcout << L"# caching memory: addr=" << mem << L"(" << (long)mem
            << L"), size=" << mem_size << L" byte(s) #" << endl;
#endif
        }
        break;

      case 32:
        if(cache_pool_32.size() < POOL_SIZE + 1) {
          memset(tmp, 0, 32);
          cache_pool_32.push((char*)tmp);
#ifdef _DEBUG
          wcout << L"# caching memory: addr=" << mem << L"(" << (long)mem
            << L"), size=" << mem_size << L" byte(s) #" << endl;
#endif
        }
        break;

      case 16:
        if(cache_pool_16.size() < POOL_SIZE + 1) {
          memset(tmp, 0, 16);
          cache_pool_16.push((char*)tmp);
#ifdef _DEBUG
          wcout << L"# caching memory: addr=" << mem << L"(" << (long)mem
            << L"), size=" << mem_size << L" byte(s) #" << endl;
#endif
        }
        break;

      default:
        free(tmp);
        tmp = NULL;
#ifdef _DEBUG
        wcout << L"# freeing memory: addr=" << mem << L"(" << (long)mem
          << L"), size=" << mem_size << L" byte(s) #" << endl;
#endif
        break;
      }
    }
  }
  marked_memory.clear();
#ifndef GC_SERIAL
  LeaveCriticalSection(&marked_cs);
#endif  

  // did not collect memory; ajust constraints
  if(live_memory.size() == allocated_memory.size()) {
    if(uncollected_count < UNCOLLECTED_COUNT) {
      uncollected_count++;
    } else {
      mem_max_size <<= 2;
      uncollected_count = 0;
    }
  }
  // collected memory; ajust constraints
  else if(mem_max_size != MEM_MAX) {
    if(collected_count < COLLECTED_COUNT) {
      collected_count++;
    } else {
      mem_max_size >>= 1;
      collected_count = 0;
    }
  }

  // copy live memory to allocated memory
  allocated_memory = live_memory;
#ifndef GC_SERIAL
  LeaveCriticalSection(&allocated_cs);
#endif

#ifdef _DEBUG
  wcout << L"===============================================================" << endl;
  wcout << L"Finished Collection: collected=" << (start - allocation_size)
    << L" of " << start << L" byte(s) - " << showpoint << setprecision(3)
    << (((double)(start - allocation_size) / (double)start) * 100.0)
    << L"%" << endl;
  wcout << L"===============================================================" << endl;
#endif

  return 0;
}

size_t WINAPI MemoryManager::CheckStatic(void* arg)
{
  StackClass** clss = prgm->GetClasses();
  int cls_num = prgm->GetClassNumber();

  for(int i = 0; i < cls_num; i++) {
    StackClass* cls = clss[i];
    CheckMemory(cls->GetClassMemory(), cls->GetClassDeclarations(), 
      cls->GetNumberClassDeclarations(), 0);
  }

  return 0;
}

uintptr_t WINAPI MemoryManager::CheckStack(void* arg)
{
  CollectionInfo* info = (CollectionInfo*)arg;
#ifdef _DEBUG
  wcout << L"----- Marking Stack: stack: pos=" << info->stack_pos 
    << L"; thread=" << GetCurrentThread() << L" -----" << endl;
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
#ifndef GC_SERIAL
  EnterCriticalSection(&jit_cs);
#endif  

#ifdef _DEBUG
  wcout << L"---- Marking JIT method root(s): num=" << jit_roots.size() 
    << L"; thread=" << GetCurrentThread() << L" ------" << endl;
  wcout << L"memory types: " << endl;
#endif

  unordered_map<long*, ClassMethodId*>::iterator jit_iter;
  for(jit_iter = jit_roots.begin(); jit_iter != jit_roots.end(); ++jit_iter) {
    ClassMethodId* id = jit_iter->second;
    long* mem = id->mem;
    StackMethod* mthd = prgm->GetClass(id->cls_id)->GetMethod(id->mthd_id);
    const long dclrs_num = mthd->GetNumberDeclarations();

#ifdef _DEBUG
    wcout << L"\t===== JIT method: name=" << mthd->GetName() << L", id=" << id->cls_id << L"," 
      << id->mthd_id << L"; addr=" << mthd << L"; num=" << mthd->GetNumberDeclarations() 
      << L" =====" << endl;
#endif

    // check self
    CheckObject(id->self, true, 1);

    StackDclr** dclrs = mthd->GetDeclarations();
    for(int j = dclrs_num - 1; j > -1; j--) {
      // update address based upon type
      switch(dclrs[j]->type) {
      case FUNC_PARM:
#ifdef _DEBUG
        wcout << L"\t" << j << L": FUNC_PARM: value=" << (*mem) 
          << L"," << *(mem + 1) << endl;
#endif
        // update
        mem += 2;
        break;

      case INT_PARM:
#ifdef _DEBUG
        wcout << L"\t" << j << L": INT_PARM: value=" << (*mem) << endl;
#endif
        // update
        mem++;
        break;

      case FLOAT_PARM: {
#ifdef _DEBUG
        FLOAT_VALUE value;
        memcpy(&value, mem, sizeof(FLOAT_VALUE));
        wcout << L"\t" << j << L": FLOAT_PARM: value=" << value << endl;
#endif
        // update
        mem += 2;
                       }
                       break;

      case BYTE_ARY_PARM:
#ifdef _DEBUG
        wcout << L"\t" << j << L": BYTE_ARY_PARM: addr=" 
          << (long*)(*mem) << L"(" << (long)(*mem) 
          << L"), size=" << ((*mem) ? ((long*)(*mem))[SIZE_OR_CLS] : 0)
          << L" byte(s)" << endl;
#endif
        // mark data
        MarkMemory((long*)(*mem));
        // update
        mem++;
        break;

      case CHAR_ARY_PARM:
#ifdef _DEBUG
        wcout << L"\t" << j << L": CHAR_ARY_PARM: addr=" << (long*)(*mem) << L"(" << (long)(*mem) 
          << L"), size=" << ((*mem) ? ((long*)(*mem))[SIZE_OR_CLS] : 0)
          << L" byte(s)" << endl;
#endif
        // mark data
        MarkMemory((long*)(*mem));
        // update
        mem++;
        break;

      case INT_ARY_PARM:
#ifdef _DEBUG
        wcout << L"\t" << j << L": INT_ARY_PARM: addr=" << (long*)(*mem)
          << L"(" << (long)(*mem) << L"), size=" 
          << ((*mem) ? ((long*)(*mem))[SIZE_OR_CLS] : 0) 
          << L" byte(s)" << endl;
#endif
        // mark data
        MarkMemory((long*)(*mem));
        // update
        mem++;
        break;

      case FLOAT_ARY_PARM:
#ifdef _DEBUG
        wcout << L"\t" << j << L": FLOAT_ARY_PARM: addr=" << (long*)(*mem)
          << L"(" << (long)(*mem) << L"), size=" << L" byte(s)" 
          << ((*mem) ? ((long*)(*mem))[SIZE_OR_CLS] : 0) << endl;
#endif
        // mark data
        MarkMemory((long*)(*mem));
        // update
        mem++;
        break;

      case OBJ_PARM: {
#ifdef _DEBUG
        wcout << L"\t" << j << L": OBJ_PARM: addr=" << (long*)(*mem)
          << L"(" << (long)(*mem) << L"), id=";
        if(*mem) {
          StackClass* tmp = (StackClass*)((long*)(*mem))[SIZE_OR_CLS];
          wcout << L"'" << tmp->GetName() << L"'" << endl;
        }
        else {
          wcout << L"Unknown" << endl;
        }
#endif
        // check object
        CheckObject((long*)(*mem), true, 1);
        // update
        mem++;
                     }
                     break;

                     // TODO: test the code below
      case OBJ_ARY_PARM:
#ifdef _DEBUG
        wcout << L"\t" << j << L": OBJ_ARY_PARM: addr=" << (long*)(*mem) << L"("
          << (long)(*mem) << L"), size=" << ((*mem) ? ((long*)(*mem))[SIZE_OR_CLS] : 0) 
          << L" byte(s)" << endl;
#endif
        // mark data
        if(MarkValidMemory((long*)(*mem))) {
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

#ifndef GC_SERIAL
  LeaveCriticalSection(&jit_cs);  
#endif

  return 0;
}

uintptr_t WINAPI MemoryManager::CheckPdaRoots(void* arg)
{
#ifndef _GC_SERIAL
  EnterCriticalSection(&pda_frame_cs);
#endif

#ifdef _DEBUG
  wcout << L"----- PDA frames(s): num=" << pda_frames.size() 
        << L"; thread=" << GetCurrentThread()<< L" -----" << endl;
  wcout << L"memory types:" <<  endl;
#endif
  
  set<StackFrame**, StackFrame**>::iterator iter;
  for(iter = pda_frames.begin(); iter != pda_frames.end(); ++iter) {
    StackFrame** frame = *iter;
    StackMethod* mthd = (*frame)->method;
    long* mem = (*frame)->mem;
    
#ifdef _DEBUG
    wcout << L"\t===== PDA method: name=" << mthd->GetName() << L", addr="
          << mthd << L", num=" << mthd->GetNumberDeclarations() << L" =====" << endl;
#endif
    
    // mark self
    CheckObject((long*)(*mem), true, 1);
    
    if(mthd->HasAndOr()) {
      mem += 2;
    } 
    else {
      mem++;
    }
    
    // mark rest of memory
    CheckMemory(mem, mthd->GetDeclarations(), mthd->GetNumberDeclarations(), 0);
  }
#ifndef _GC_SERIAL
  LeaveCriticalSection(&pda_frame_cs);
#endif 
  
#ifndef GC_SERIAL
  EnterCriticalSection(&pda_monitor_cs);
#endif

#ifdef _DEBUG
  wcout << L"----- PDA method root(s): num=" << pda_monitors.size() 
    << L"; thread=" << GetCurrentThread()<< L" -----" << endl;
  wcout << L"memory types:" <<  endl;
#endif
  // look at pda methods
  unordered_map<StackFrameMonitor*, StackFrameMonitor*>::iterator pda_iter;
  for(pda_iter = pda_monitors.begin(); pda_iter != pda_monitors.end(); ++pda_iter) {
    // gather stack frames
    StackFrameMonitor* monitor = pda_iter->first;
    StackFrame** call_stack = monitor->call_stack;
    long call_stack_pos = *(monitor->call_stack_pos);
    StackFrame* cur_frame = *(monitor->cur_frame);

    // copy frames locally
    vector<StackFrame*> frames;
    frames.push_back(cur_frame);
    while(--call_stack_pos > -1) {
      frames.push_back(call_stack[call_stack_pos]);
    }

    for(size_t i = 0; i < frames.size(); ++i) {    
      StackMethod* mthd = frames[i]->method;
      long* mem = frames[i]->mem;

#ifdef _DEBUG
      wcout << L"\t===== PDA method: name=" << mthd->GetName() << L", addr="
        << mthd << L", num=" << mthd->GetNumberDeclarations() << L" =====" << endl;
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
  }
#ifndef GC_SERIAL
  LeaveCriticalSection(&pda_monitor_cs);
#endif

  return 0;
}

void MemoryManager::CheckMemory(long* mem, StackDclr** dclrs, const long dcls_size, long depth)
{
  // check method
  for(long i = 0; i < dcls_size; i++) {
#ifdef _DEBUG
    for(int j = 0; j < depth; j++) {
      wcout << L"\t";
    }
#endif

    // update address based upon type
    switch(dclrs[i]->type) {
    case FUNC_PARM:
#ifdef _DEBUG
      wcout << L"\t" << i << L": FUNC_PARM: value=" << (*mem) 
        << L"," << *(mem + 1)<< endl;
#endif
      // update
      mem += 2;
      break;

    case INT_PARM:
#ifdef _DEBUG
      wcout << L"\t" << i << L": INT_PARM: value=" << (*mem) << endl;
#endif
      // update
      mem++;
      break;

    case FLOAT_PARM: {
#ifdef _DEBUG
      FLOAT_VALUE value;
      memcpy(&value, mem, sizeof(FLOAT_VALUE));
      wcout << L"\t" << i << L": FLOAT_PARM: value=" << value << endl;
#endif
      // update
      mem += 2;
                     }
                     break;

    case BYTE_ARY_PARM:
#ifdef _DEBUG
      wcout << L"\t" << i << L": BYTE_ARY_PARM: addr=" << (long*)(*mem) << L"("
        << (long)(*mem) << L"), size=" << ((*mem) ? ((long*)(*mem))[SIZE_OR_CLS] : 0)
        << L" byte(s)" << endl;
#endif
      // mark data
      MarkMemory((long*)(*mem));
      // update
      mem++;
      break;

    case CHAR_ARY_PARM:
#ifdef _DEBUG
      wcout << L"\t" << i << L": CHAR_ARY_PARM: addr=" << (long*)(*mem) << L"("
        << (long)(*mem) << L"), size=" << ((*mem) ? ((long*)(*mem))[SIZE_OR_CLS] : 0) 
        << L" byte(s)" << endl;
#endif
      // mark data
      MarkMemory((long*)(*mem));
      // update
      mem++;
      break;

    case INT_ARY_PARM:
#ifdef _DEBUG
      wcout << L"\t" << i << L": INT_ARY_PARM: addr=" << (long*)(*mem) << L"("
        << (long)(*mem) << L"), size=" << ((*mem) ? ((long*)(*mem))[SIZE_OR_CLS] : 0) 
        << L" byte(s)" << endl;
#endif
      // mark data
      MarkMemory((long*)(*mem));
      // update
      mem++;
      break;

    case FLOAT_ARY_PARM:
#ifdef _DEBUG
      wcout << L"\t" << i << L": FLOAT_ARY_PARM: addr=" << (long*)(*mem) << L"("
        << (long)(*mem) << L"), size=" << ((*mem) ? ((long*)(*mem))[SIZE_OR_CLS] : 0) 
        << L" byte(s)" << endl;
#endif
      // mark data
      MarkMemory((long*)(*mem));
      // update
      mem++;
      break;

    case OBJ_PARM: {
#ifdef _DEBUG
      wcout << L"\t" << i << L": OBJ_PARM: addr=" << (long*)(*mem) << L"("
        << (long)(*mem) << L"), id=";
      if(*mem) {
        StackClass* tmp = (StackClass*)((long*)(*mem))[SIZE_OR_CLS];
        wcout << L"'" << tmp->GetName() << L"'" << endl;
      }
      else {
        wcout << L"Unknown" << endl;
      }
#endif
      // check object
      CheckObject((long*)(*mem), true, depth + 1);
      // update
      mem++;
                   }
                   break;

    case OBJ_ARY_PARM:
#ifdef _DEBUG
      wcout << L"\t" << i << L": OBJ_ARY_PARM: addr=" << (long*)(*mem) << L"("
        << (long)(*mem) << L"), size=" << ((*mem) ? ((long*)(*mem))[SIZE_OR_CLS] : 0) << L" byte(s)" << endl;
#endif
      // mark data
      if(MarkValidMemory((long*)(*mem))) {
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
    StackClass* cls;
    if(is_obj) {
      cls = GetClass(mem);
    }
    else {
      cls = GetClassMapping(mem);
    }

    if(cls) {
#ifdef _DEBUG
      for(int i = 0; i < depth; i++) {
        wcout << L"\t";
      }
      wcout << L"\t----- object: addr=" << mem << L"(" << (long)mem << L"), num="
        << cls->GetNumberInstanceDeclarations() << L" -----" << endl;
#endif

      // mark data
      if(MarkMemory(mem)) {
        CheckMemory(mem, cls->GetInstanceDeclarations(), cls->GetNumberInstanceDeclarations(), depth);
      }
    } 
    else {
      // NOTE: this happens when we are trying to mark unidentified memory
      // segments. these segments may be parts of that stack or temp for
      // register variables
#ifdef _DEBUG
      for(int i = 0; i < depth; i++) {
        wcout << L"\t";
      }
      wcout <<"$: addr/value=" << mem << endl;
      if(is_obj) {
        assert(cls);
      }
#endif
      // primitive or object array
      if(MarkValidMemory(mem)) {
        // ensure we're only checking int and obj arrays
        if(std::binary_search(allocated_memory.begin(), allocated_memory.end(), mem) && 
          (mem[TYPE] == NIL_TYPE || mem[TYPE] == INT_TYPE)) {
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
