/***************************************************************************
 * VM stack machine.
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
#include <random>
#include <string.h>
#include <thread>

#ifdef _WIN32
#include "arch/memory.h"
#else
#include "arch/memory.h"
#endif

#ifdef _DEBUGGER
#include "../debugger/debugger.h"
#endif

#undef max

namespace Runtime {
#ifdef _DEBUGGER
  class Debugger;
#endif

#define FRAME_CACHE_SIZE 1024
#define CALL_STACK_SIZE 256
#define OP_STACK_SIZE 768

  // Forward declarations for dispatch
  enum class DispatchResult : int;
  struct DispatchContext;
  typedef DispatchResult (*OpcodeHandler)(DispatchContext& ctx);
  extern OpcodeHandler g_dispatch_table[];

  // holds the calling context for async
  // method calls
  struct ThreadHolder {
    StackMethod* called;
    size_t* self;
    size_t* param;
  };

  //
  // StackInterpreter
  //
  class StackInterpreter {
    // program
    static StackProgram* program;
    static std::set<StackInterpreter*> intpr_threads;
    static std::stack<StackFrame*> cached_frames;

#ifdef _WIN32
    static bool is_stdio_binary;
#endif

#ifdef _WIN32
    static CRITICAL_SECTION cached_frames_cs;
    static CRITICAL_SECTION intpr_threads_cs;
#else
    static pthread_mutex_t cached_frames_mutex;
    static pthread_mutex_t intpr_threads_mutex;
#endif

    // call stack and current frame pointer
    StackFrame** call_stack;
    long* call_stack_pos;
    
    StackFrame** stack_frame;
    StackFrameMonitor* stack_frame_monitor;

    // halt
    bool halt;

#ifdef _DEBUGGER
    Debugger* debugger;
#endif
    
    //
    // get stack frame
    //
    static StackFrame* GetStackFrame(StackMethod* method, size_t* instance);

    //
    // push call frame
    //
    inline void PushFrame(StackFrame* f) {
      if((*call_stack_pos) >= CALL_STACK_SIZE) {
        std::wcerr << L">>> call stack bounds have been exceeded! <<<" << std::endl;
        exit(1);
      }
      
      call_stack[(*call_stack_pos)++] = f;
    }

    //
    // pop call frame
    //
    inline StackFrame* PopFrame() {
      if((*call_stack_pos) <= 0) {
        std::wcerr << L">>> call stack bounds have been exceeded! <<<" << std::endl;
        exit(1);
      }
      
      return call_stack[--(*call_stack_pos)];
    }
    
    //
    // generates a stack dump if an error occurs
    //
    void StackErrorUnwind(StackMethod* method);
    
    //
    // is call stack empty?
    //
    inline bool StackEmpty() {
      return (*call_stack_pos) == 0;
    }

  public:
    // The following methods are public to allow access from dispatch handlers

    //
    // pops an integer from the calculation stack. this code
    // in normally inlined and there's a macro version available.
    //
    inline size_t PopInt(size_t* op_stack, size_t* stack_pos) {
#ifdef _DEBUG
      size_t v = op_stack[--(*stack_pos)];
      std::wcout << L"  [pop_i: stack_pos=" << (*stack_pos) << L"; value=" << v << L"("
            << (size_t*)v << L")]; frame=" << (*stack_frame) << L"; call_pos=" << (*call_stack_pos) << std::endl;
      return v;
#else
      return op_stack[--(*stack_pos)];
#endif
    }
    
    //
    // pushes an integer onto the calculation stack.  this code
    // in normally inlined and there's a macro version available.
    //
    inline void PushInt(const size_t v, size_t* op_stack, size_t* stack_pos) {
#ifdef _DEBUG
      std::wcout << L"  [push_i: stack_pos=" << (*stack_pos) << L"; value=" << v << L"("
            << (size_t*)v << L")]; frame=" << (*stack_frame) << L"; call_pos=" << (*call_stack_pos) << std::endl;
#endif
      op_stack[(*stack_pos)++] = v;
    }

    //
    // pushes an double onto the calculation stack.
    //
    inline void PushFloat(const FLOAT_VALUE v, size_t* op_stack, size_t* stack_pos) {
#ifdef _DEBUG
      std::wcout << L"  [push_f: stack_pos=" << (*stack_pos) << L"; value=" << v
            << L"]; frame=" << (*stack_frame) << L"; call_pos=" << (*call_stack_pos) << std::endl;
#endif
      *((FLOAT_VALUE*)(&op_stack[(*stack_pos)])) = v;
      (*stack_pos)++;
    }
  
    //
    // swaps two integers on the calculation stack
    //
    inline void SwapInt(size_t* op_stack, size_t* stack_pos) {
      const size_t v = op_stack[(*stack_pos) - 2];
      op_stack[(*stack_pos) - 2] = op_stack[(*stack_pos) - 1];
      op_stack[(*stack_pos) - 1] = v;
    }
    
    //
    // pops a double from the calculation stack
    //
    inline FLOAT_VALUE PopFloat(size_t* op_stack, size_t* stack_pos) {
      (*stack_pos)--;

#ifdef _DEBUG
      FLOAT_VALUE v = *((FLOAT_VALUE*)(&op_stack[(*stack_pos)]));
      std::wcout << L"  [pop_f: stack_pos=" << (*stack_pos) << L"; value=" << v
            << L"]; frame=" << (*stack_frame) << L"; call_pos=" << (*call_stack_pos) << std::endl;
      return v;
#endif

      return *((FLOAT_VALUE*)(&op_stack[(*stack_pos)]));
    }
    
    //
    // peeks at the integer on the top of the execution stack.
    //
    inline size_t TopInt(size_t* op_stack, size_t* stack_pos) {
#ifdef _DEBUG
      size_t v = op_stack[(*stack_pos) - 1];
      std::wcout << L"  [top_i: stack_pos=" << (*stack_pos) << L"; value=" << v << L"(" << (void*)v
            << L")]; frame=" << (*stack_frame) << L"; call_pos=" << (*call_stack_pos) << std::endl;
      return v;
#else
      return op_stack[(*stack_pos) - 1];
#endif
    }
    
    //
    // peeks at the double on the top of the execution stack.
    //
    inline FLOAT_VALUE TopFloat(size_t* op_stack, size_t* stack_pos) {
      const size_t index = (*stack_pos) - 1;
      
#ifdef _DEBUG
      FLOAT_VALUE v = *((FLOAT_VALUE*)(&op_stack[index]));
      std::wcout << L"  [top_f: stack_pos=" << (*stack_pos) << L"; value=" << v
            << L"]; frame=" << (*stack_frame) << L"; call_pos=" << (*call_stack_pos) << std::endl;
      return v;
#endif
      
      return *((FLOAT_VALUE*)(&op_stack[index]));
    }

    //
    // calculates an array offset
    //
    INT64_VALUE ArrayIndex(StackInstr* instr, size_t* array, const int64_t size, size_t* &op_stack, size_t* &stack_pos);
        
    //
    // creates a string object instance
    // 
    size_t* CreateStringObject(const std::wstring &value_str, size_t* &op_stack, size_t* &stack_pos);
    
    void StorLoclIntVar(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos);
    void StorClsInstIntVar(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos);
    void CopyLoclIntVar(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos);
    void CopyClsInstIntVar(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos);
    void LoadLoclIntVar(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos);
    void LoadClsInstIntVar(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos);

    void Str2Int(size_t* &op_stack, size_t* &stack_pos);
    void Str2Float(size_t* &op_stack, size_t* &stack_pos);
    void Int2Str(size_t* &op_stack, size_t* &stack_pos);
    void Float2Str(size_t*& op_stack, size_t*& stack_pos);
    void ByteChar2Int(size_t*& op_stack, size_t*& stack_pos);
    void ShlInt(size_t* &op_stack, size_t* &stack_pos);
    void ShrInt(size_t* &op_stack, size_t* &stack_pos);
    void AndInt(size_t* &op_stack, size_t* &stack_pos);
    void OrInt(size_t* &op_stack, size_t* &stack_pos);
    void AddInt(size_t* &op_stack, size_t* &stack_pos);
    void AddFloat(size_t* &op_stack, size_t* &stack_pos);
    void SubInt(size_t* &op_stack, size_t* &stack_pos);
    void SubFloat(size_t* &op_stack, size_t* &stack_pos);
    void MulInt(size_t* &op_stack, size_t* &stack_pos);
    void DivInt(size_t* &op_stack, size_t* &stack_pos);
    void MulFloat(size_t* &op_stack, size_t* &stack_pos);
    void DivFloat(size_t* &op_stack, size_t* &stack_pos);
    void ModInt(size_t* &op_stack, size_t* &stack_pos);
    void BitAndInt(size_t* &op_stack, size_t* &stack_pos);
    void BitOrInt(size_t* &op_stack, size_t* &stack_pos);
    void BitXorInt(size_t*& op_stack, size_t*& stack_pos);
    void BitNotInt(size_t*& op_stack, size_t*& stack_pos);
    void LesEqlInt(size_t* &op_stack, size_t* &stack_pos);
    void GtrEqlInt(size_t* &op_stack, size_t* &stack_pos);
    void LesEqlFloat(size_t* &op_stack, size_t* &stack_pos);
    void GtrEqlFloat(size_t* &op_stack, size_t* &stack_pos);
    void EqlInt(size_t* &op_stack, size_t* &stack_pos);
    void NeqlInt(size_t* &op_stack, size_t* &stack_pos);
    void LesInt(size_t* &op_stack, size_t* &stack_pos);
    void GtrInt(size_t* &op_stack, size_t* &stack_pos);
    void EqlFloat(size_t* &op_stack, size_t* &stack_pos);
    void NeqlFloat(size_t* &op_stack, size_t* &stack_pos);
    void LesFloat(size_t* &op_stack, size_t* &stack_pos);
    void GtrFloat(size_t* &op_stack, size_t* &stack_pos);
    void LoadArySize(size_t* &op_stack, size_t* &stack_pos);
    void CpyByteAry(size_t* &op_stack, size_t* &stack_pos);
    void CpyCharAry(size_t* &op_stack, size_t* &stack_pos);
    void CpyIntAry(size_t* &op_stack, size_t* &stack_pos);
    void CpyFloatAry(size_t* &op_stack, size_t* &stack_pos);
    void ZeroByteAry(size_t*& op_stack, size_t*& stack_pos);
    void ZeroCharAry(size_t*& op_stack, size_t*& stack_pos);
    void ZeroIntAry(size_t*& op_stack, size_t*& stack_pos);
    void ZeroFloatAry(size_t*& op_stack, size_t*& stack_pos);
    void ObjTypeOf(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos);
    void ObjInstCast(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos);
    void AsyncMthdCall(size_t* &op_stack, size_t* &stack_pos);
    void ThreadJoin(size_t* &op_stack, size_t* &stack_pos);
    void ThreadMutex(size_t* &op_stack, size_t* &stack_pos);
    void CriticalStart(size_t* &op_stack, size_t* &stack_pos);
    void CriticalEnd(size_t* &op_stack, size_t* &stack_pos);

    void ProcessNewArray(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos, bool is_float = false);
    void ProcessNewByteArray(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos);
    void ProcessNewCharArray(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos);
    void ProcessNewObjectInstance(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos);
    void ProcessNewFunctionInstance(StackInstr* instr, size_t*& op_stack, size_t*& stack_pos);
    void ProcessReturn(StackInstr** &instrs, long &ip);

    void ProcessMethodCall(StackInstr* instr, StackInstr** &instrs, long &ip, size_t* &op_stack, size_t* &stack_pos);
    void ProcessDynamicMethodCall(StackInstr* instr, StackInstr** &instrs, long &ip, size_t* &op_stack, size_t* &stack_pos);
    void ProcessJitMethodCall(StackMethod* called, size_t* instance, StackInstr** &instrs, long &ip, size_t* &op_stack, size_t* &stack_pos);
    void ProcessAsyncMethodCall(StackMethod* called, size_t* param);

    void ProcessInterpretedMethodCall(StackMethod* called, size_t* instance, StackInstr** &instrs, long &ip);
    void ProcessLoadIntArrayElement(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos);
    void ProcessStoreIntArrayElement(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos);
    void ProcessLoadFloatArrayElement(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos);
    void ProcessStoreFloatArrayElement(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos);
    void ProcessLoadByteArrayElement(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos);
    void ProcessStoreByteArrayElement(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos);
    void ProcessStoreCharArrayElement(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos);
    void ProcessLoadCharArrayElement(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos);
    void ProcessStoreFunctionVar(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos);
    void ProcessLoadFunctionVar(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos);
    void ProcessStoreFloat(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos);
    void ProcessLoadFloat(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos);
    void ProcessCopyFloat(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos);

    void SharedLibraryLoad(StackInstr* instr);
    void SharedLibraryUnload(StackInstr* instr);
    void SharedLibraryCall(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos);

    // Additional static accessors for dispatch handlers
    static StackProgram* GetProgram() { return program; }
    static void ReleaseStackFrame(StackFrame* frame);
    void StackErrorUnwind();
    bool ProcessTrap(size_t* &op_stack, size_t* &stack_pos);

    // initialize the runtime system
    static void Initialize(StackProgram* p, size_t m);

#ifdef _WIN32
    inline static void SetBinaryStdio(bool i) {
      is_stdio_binary = i;
    }

    inline static bool IsBinaryStdio() {
      return is_stdio_binary;
    }
#else 
    inline static bool IsBinaryStdio() {
      return false;
    }
#endif

    static void AddThread(StackInterpreter* i) {
#ifdef _WIN32
      EnterCriticalSection(&intpr_threads_cs);
#else
      pthread_mutex_lock(&intpr_threads_mutex);
#endif
      
      intpr_threads.insert(i);
      
#ifdef _WIN32
      LeaveCriticalSection(&intpr_threads_cs);
#else
      pthread_mutex_unlock(&intpr_threads_mutex);
#endif
    }

    static void RemoveThread(StackInterpreter* i) {
#ifdef _WIN32
      EnterCriticalSection(&intpr_threads_cs);
#else
      pthread_mutex_lock(&intpr_threads_mutex);
#endif
      
      intpr_threads.erase(i);
      
#ifdef _WIN32
      LeaveCriticalSection(&intpr_threads_cs);
#else
      pthread_mutex_unlock(&intpr_threads_mutex);
#endif
    }
    
    static void HaltAll() {
#ifdef _WIN32
      EnterCriticalSection(&intpr_threads_cs);
#else
      pthread_mutex_lock(&intpr_threads_mutex);
#endif
      
      std::set<StackInterpreter*>::iterator iter;
      for(iter = intpr_threads.begin(); iter != intpr_threads.end(); ++iter) {
        (*iter)->Halt();
      }

#ifdef _WIN32
      LeaveCriticalSection(&intpr_threads_cs);
#else
      pthread_mutex_unlock(&intpr_threads_mutex);
#endif
    }

    //
    void Halt() {
      halt = true;
    }
    
    // free static resources
    static void Clear() {
      while(!cached_frames.empty()) {
        StackFrame* frame = cached_frames.top();
        cached_frames.pop();
        free(frame->mem);
        delete frame;
      }
    }

#ifdef _WIN32
    static unsigned int WINAPI AsyncMethodCall(LPVOID arg);
#else
    static void* AsyncMethodCall(void* arg);
#endif

    StackInterpreter(StackFrame** c, long* cp) {
      // setup frame
      call_stack = c;
      call_stack_pos = cp;
      stack_frame_monitor = nullptr;
      stack_frame = new StackFrame*;
      halt = false;

      MemoryManager::AddPdaMethodRoot(stack_frame);
    }

    StackInterpreter() {
      // setup frame
      call_stack = new StackFrame*[CALL_STACK_SIZE];
      call_stack_pos = new long;
      *call_stack_pos = -1;
      halt = false;

      // register monitor
      stack_frame_monitor = new StackFrameMonitor;
      stack_frame_monitor->call_stack = call_stack;
      stack_frame_monitor->call_stack_pos = call_stack_pos;

      stack_frame = new StackFrame*;
      stack_frame_monitor->cur_frame = stack_frame;
      MemoryManager::AddPdaMethodRoot(stack_frame_monitor);
    }
  
    StackInterpreter(StackProgram* p, size_t m) {
      Initialize(p, m);
      
      // setup frame
      call_stack = new StackFrame*[CALL_STACK_SIZE];
      call_stack_pos = new long;
      *call_stack_pos = -1;
      stack_frame = new StackFrame*;
      halt = false;

      // register monitor
      stack_frame_monitor = new StackFrameMonitor;
      stack_frame_monitor->call_stack = call_stack;
      stack_frame_monitor->call_stack_pos = call_stack_pos;
      stack_frame_monitor->cur_frame = stack_frame;      
      MemoryManager::AddPdaMethodRoot(stack_frame_monitor);
    }

#ifdef _MODULE
    const std::wstringstream& GetOutputBuffer() {
      return program->output_buffer;
    }
#endif
  
#ifdef _DEBUGGER
    StackInterpreter(StackProgram* p, Debugger* d) {
      Initialize(p, 0);
      
      debugger = d;
      
      // setup frame
      call_stack = new StackFrame*[CALL_STACK_SIZE];
      call_stack_pos = new long;
      *call_stack_pos = -1;
      stack_frame = new StackFrame*;
      halt = false;

      // register monitor
      stack_frame_monitor = new StackFrameMonitor;
      stack_frame_monitor->call_stack = call_stack;
      stack_frame_monitor->call_stack_pos = call_stack_pos;
      stack_frame_monitor->cur_frame = stack_frame;
      MemoryManager::AddPdaMethodRoot(stack_frame_monitor);
    }
#endif
    
    ~StackInterpreter() {
      if(stack_frame_monitor) {
        MemoryManager::RemovePdaMethodRoot(stack_frame_monitor);

        delete[] call_stack;
        call_stack = nullptr;

        delete call_stack_pos;
        call_stack_pos = nullptr;

        delete stack_frame_monitor;
        stack_frame_monitor = nullptr;
      }
      else {
        MemoryManager::RemovePdaMethodRoot(stack_frame);
      }

      delete stack_frame;
      stack_frame = nullptr;
    }

    // execute method
    void Execute(size_t* op_stack, size_t* stack_pos, long i, StackMethod* method, size_t* instance, bool jit_called);
  };
}
