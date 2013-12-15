/***************************************************************************
 * VM stack machine.
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

#ifndef __STACK_INTPR_H__
#define __STACK_INTPR_H__

#include "common.h"
#include <string.h>

#ifdef _WIN32
#include "os/windows/memory.h"
#else
#include "os/posix/memory.h"
#endif

#ifdef _DEBUGGER
#include "debugger/debugger.h"
#endif

using namespace std;

namespace Runtime {
#ifdef _DEBUGGER
  class Debugger;
#endif
  
#define CALL_STACK_SIZE 1024
#define CALC_STACK_SIZE 512
	
  // holds the calling context for async
  // method calls
  struct ThreadHolder {
    StackMethod* called;
    long* self;
    long* param;
  };
  
  class StackInterpreter {
    // program
    static StackProgram* program;
		static stack<StackFrame*> cached_frames;
#ifdef _WIN32
		static CRITICAL_SECTION cached_frames_cs;
#else
		static pthread_mutex_t cached_frames_mutex;
#endif

    // call stack and current frame pointer
    StackFrame** call_stack;
    long* call_stack_pos;
    StackFrame** frame;
    StackFrameMonitor* monitor;
    // halt
    bool halt;
#ifdef _DEBUGGER
    Debugger* debugger;
#endif
		
		//
		// get stack frame
		//
		static inline StackFrame* GetStackFrame(StackMethod* method, long* instance) {
#ifndef _SANITIZE
#ifdef _WIN32
			EnterCriticalSection(&cached_frames_cs);
#else
			pthread_mutex_lock(&cached_frames_mutex);
#endif
      if(cached_frames.empty()) {
				// allocate 256K frames
				for(int i = 0; i < CALL_STACK_SIZE; i++) {
					StackFrame* frame = new StackFrame();
					frame->mem = (long*)calloc(LOCAL_SIZE, sizeof(long));
					cached_frames.push(frame);
				}
      }
			StackFrame* frame = cached_frames.top();
			cached_frames.pop();
      
			frame->method = method;
			frame->mem[0] = (long)instance;
			frame->ip = -1;
			frame->jit_called = false;

#ifdef _DEBUG
			wcout << L"fetching frame=" << frame << endl;
#endif
      
#else      
      StackFrame* frame = new StackFrame;
			frame->method = method;
      frame->mem = (long*)calloc(LOCAL_SIZE, sizeof(long));
			frame->mem[0] = (long)instance;
			frame->ip = -1;
			frame->jit_called = false;
#endif
 
#ifdef _WIN32
			LeaveCriticalSection(&cached_frames_cs);
#else
			pthread_mutex_unlock(&cached_frames_mutex);
#endif
      return frame;
		}
		
		//
		// release stack frame
		//
		static inline void ReleaseStackFrame(StackFrame* frame) {
#ifdef _WIN32
      EnterCriticalSection(&cached_frames_cs);
#else
      pthread_mutex_lock(&cached_frames_mutex);
#endif
      
#ifndef _SANITIZE
       // cache up to 256k frames
       if(cached_frames.size() > CALL_STACK_SIZE * 256) {
         free(frame->mem);
         delete frame;
#ifdef _DEBUG
         wcout << L"releasing frame=" << frame << endl;
#endif
       }
       else {
         memset(frame->mem, 0, LOCAL_SIZE);
         cached_frames.push(frame);

#ifdef _DEBUG
         wcout << L"caching frame=" << frame << endl;
#endif
       }
       
#else 
       free(frame->mem);
       delete frame;

#endif
       
#ifdef _WIN32
      LeaveCriticalSection(&cached_frames_cs);
#else
      pthread_mutex_unlock(&cached_frames_mutex);
#endif
		}
		
    //
    // push call frame
    //
    inline void PushFrame(StackFrame* f) {
      if((*call_stack_pos) >= CALL_STACK_SIZE) {
        wcerr << L">>> call stack bounds have been exceeded! <<<" << endl;
        exit(1);
      }
      
      call_stack[(*call_stack_pos)++] = f;
    }

    //
    // pop call frame
    //
    inline StackFrame* PopFrame() {
      if((*call_stack_pos) <= 0) {
        wcerr << L">>> call stack bounds have been exceeded! <<<" << endl;
        exit(1);
      }
      
      return call_stack[--(*call_stack_pos)];
    }
    
    //
    // generates a stack dump if an error occurs
    //
    inline void StackErrorUnwind() {
      long pos = (*call_stack_pos);
#ifdef _DEBUGGER
      wcerr << L"Unwinding local stack (" << this << L"):" << endl;
      StackMethod* method =  (*frame)->method;
      if((*frame)->ip > 0 && pos > -1 && 
				 method->GetInstruction((*frame)->ip)->GetLineNumber() > 0) {
				wcerr << L"  method: pos=" << pos << L", file="
							<< (*frame)->method->GetClass()->GetFileName() << L", name='" 
							<< (*frame)->method->GetName() << L"', line=" 
							<< method->GetInstruction((*frame)->ip)->GetLineNumber() << endl;
      }
      if(pos != 0) {
				while(--pos) {
					StackMethod* method =  call_stack[pos]->method;
					if(call_stack[pos]->ip > 0 && pos > -1 && 
						 method->GetInstruction(call_stack[pos]->ip)->GetLineNumber() > 0) {
						wcerr << L"  method: pos=" << pos << L", file=" 
									<< call_stack[pos]->method->GetClass()->GetFileName() << L", name='"
									<< call_stack[pos]->method->GetName() << L"', line=" 
									<< method->GetInstruction(call_stack[pos]->ip)->GetLineNumber() << endl;
					}
				}
				pos = 0;
      }
      wcerr << L"  ..." << endl;
#else
      wcerr << L"Unwinding local stack (" << this << L"):" << endl;
      wcerr << L"  method: pos=" << pos << L", name=" 
						<< (*frame)->method->GetName() << endl;
      if(pos != 0) {
				while(--pos && pos > -1) {
					wcerr << L"  method: pos=" << pos << L", name="
								<< call_stack[pos]->method->GetName() << endl;
				}
      }
      wcerr << L"  ..." << endl;
#endif
    }
    
    //
    // generates a stack dump if an error occurs
    //
    inline void StackErrorUnwind(StackMethod* method) {
      long pos = (*call_stack_pos);
      wcerr << L"Unwinding local stack (" << this << L"):" << endl;
      wcerr << L"  method: pos=" << pos << L", name="
						<< method->GetName() << endl;
      while(--pos) {
				if(pos > - 1) {
					wcerr << L"  method: pos=" << pos << L", name="
								<< call_stack[pos]->method->GetName() << endl;
				}
      }
      wcerr << L"  ..." << endl;
    }
    
    //
    // is call stack empty?
    //
    inline bool StackEmpty() {
      return (*call_stack_pos) == 0;
    }

    //
    // pops an integer from the calculation stack.  this code
    // in normally inlined and there's a macro version available.
    //
    inline long PopInt(long* op_stack, long* stack_pos) {    
#ifdef _DEBUG
      long v = op_stack[--(*stack_pos)];
      wcout << L"  [pop_i: stack_pos=" << (*stack_pos) << L"; value=" << v << L"("
						<< (void*)v << L")]; frame=" << (*frame) << L"; call_pos=" << (*call_stack_pos) << endl;
      return v;
#else
      return op_stack[--(*stack_pos)];
#endif
    }
    
    //
    // pushes an integer onto the calculation stack.  this code
    // in normally inlined and there's a macro version available.
    //
    inline void PushInt(long v, long* op_stack, long* stack_pos) {
#ifdef _DEBUG
      wcout << L"  [push_i: stack_pos=" << (*stack_pos) << L"; value=" << v << L"("
						<< (void*)v << L")]; frame=" << (*frame) << L"; call_pos=" << (*call_stack_pos) << endl;
#endif
      op_stack[(*stack_pos)++] = v;
    }

    //
    // pushes an double onto the calculation stack.
    //
    inline void PushFloat(FLOAT_VALUE v, long* op_stack, long* stack_pos) {
#ifdef _DEBUG
      wcout << L"  [push_f: stack_pos=" << (*stack_pos) << L"; value=" << v
						<< L"]; frame=" << (*frame) << L"; call_pos=" << (*call_stack_pos) << endl;
#endif
      memcpy(&op_stack[(*stack_pos)], &v, sizeof(FLOAT_VALUE));

#ifdef _X64
      (*stack_pos)++;
#else
      (*stack_pos) += 2;
#endif
    }
  
    //
    // swaps two integers on the calculation stack
    //
    inline void SwapInt(long* op_stack, long* stack_pos) {
      long v = op_stack[(*stack_pos) - 2];
      op_stack[(*stack_pos) - 2] = op_stack[(*stack_pos) - 1];
      op_stack[(*stack_pos) - 1] = v;
    }
    
    //
    // pops a double from the calculation stack
    //
    inline FLOAT_VALUE PopFloat(long* op_stack, long* stack_pos) {
      FLOAT_VALUE v;

#ifdef _X64
      (*stack_pos)--;
#else
      (*stack_pos) -= 2;
#endif

      memcpy(&v, &op_stack[(*stack_pos)], sizeof(FLOAT_VALUE));
#ifdef _DEBUG
      wcout << L"  [pop_f: stack_pos=" << (*stack_pos) << L"; value=" << v
						<< L"]; frame=" << (*frame) << L"; call_pos=" << (*call_stack_pos) << endl;
#endif

      return v;
    }
    
    //
    // peeks at the integer on the top of the
    // execution stack.
    //
    inline long TopInt(long* op_stack, long* stack_pos) {
#ifdef _DEBUG
      long v = op_stack[(*stack_pos) - 1];
      wcout << L"  [top_i: stack_pos=" << (*stack_pos) << L"; value=" << v << L"(" << (void*)v
						<< L")]; frame=" << (*frame) << L"; call_pos=" << (*call_stack_pos) << endl;
      return v;
#else
      return op_stack[(*stack_pos) - 1];
#endif
    }
    
    //
    // peeks at the double on the top of the
    // execution stack.
    //
    inline FLOAT_VALUE TopFloat(long* op_stack, long* stack_pos) {
      FLOAT_VALUE v;

#ifdef _X64
      long index = (*stack_pos) - 1;
#else
      long index = (*stack_pos) - 2;
#endif

      memcpy(&v, &op_stack[index], sizeof(FLOAT_VALUE));
#ifdef _DEBUG
      wcout << L"  [top_f: stack_pos=" << (*stack_pos) << L"; value=" << v
						<< L"]; frame=" << (*frame) << L"; call_pos=" << (*call_stack_pos) << endl;
#endif

      return v;
    }

    //
    // calculates an array offset
    //
    inline long ArrayIndex(StackInstr* instr, long* array, const long size, long* &op_stack, long* &stack_pos) {
      // generate index
      long index = PopInt(op_stack, stack_pos);
      const long dim = instr->GetOperand();

      for(long i = 1; i < dim; i++) {
        index *= array[i];
        index += PopInt(op_stack, stack_pos);
      }

#ifdef _DEBUG
      wcout << L"  [raw index=" << index << L", raw size=" << size << L"]" << endl;
#endif

      // 64-bit bounds check
#ifdef _X64
      if(index < 0 || index >= size) {
        wcerr << L">>> Index out of bounds: " << index << L"," << size << L" <<<" << endl;
        StackErrorUnwind();
        exit(1);
      }
#else
      // 32-bit bounds check
      if(instr->GetType() == LOAD_FLOAT_ARY_ELM || instr->GetType() == STOR_FLOAT_ARY_ELM) {
        // float array
        index *= 2;
        if(index < 0 || index >= size * 2) {
          wcerr << L">>> Index out of bounds: " << index << L"," << (size * 2) << L" <<<" << endl;
          StackErrorUnwind();
          exit(1);
        }
      } 
      else {
        // interger array
        if(index < 0 || index >= size) {
          wcerr << L">>> Index out of bounds: " << index << L"," << size << L" <<<" << endl;
          StackErrorUnwind();
          exit(1);
        }
      }
#endif

      return index;
    }    
        
    //
    // creates a string object instance
    // 
    inline long* CreateStringObject(const wstring &value_str, long* &op_stack, long* &stack_pos) {
      // create character array
      const long char_array_size = value_str.size();
      const long char_array_dim = 1;
      long* char_array = (long*)MemoryManager::AllocateArray(char_array_size + 1 +
																														 ((char_array_dim + 2) *
																															sizeof(long)),
																														 CHAR_ARY_TYPE,
																														 op_stack, *stack_pos,
																														 false);
      char_array[0] = char_array_size + 1;
      char_array[1] = char_array_dim;
      char_array[2] = char_array_size;

      // copy wstring
      wchar_t* char_array_ptr = (wchar_t*)(char_array + 3);
      wcsncpy(char_array_ptr, value_str.c_str(), char_array_size);
      
      // create 'System.String' object instance
      long* str_obj = MemoryManager::AllocateObject(program->GetStringObjectId(),
																										(long*)op_stack, *stack_pos,
																										false);
      str_obj[0] = (long)char_array;
      str_obj[1] = char_array_size;
      str_obj[2] = char_array_size;
      
      return str_obj;
    }
    
    inline void ProcessNewArray(StackInstr* instr, long* &op_stack, long* &stack_pos, bool is_float = false);
    inline void ProcessNewByteArray(StackInstr* instr, long* &op_stack, long* &stack_pos);
    inline void ProcessNewCharArray(StackInstr* instr, long* &op_stack, long* &stack_pos);
    inline void ProcessNewObjectInstance(StackInstr* instr, long* &op_stack, long* &stack_pos);
    inline void ProcessReturn(StackInstr** &instrs, long &ip);

    inline void ProcessMethodCall(StackInstr* instr, StackInstr** &instrs, long &ip, long* &op_stack, long* &stack_pos);
    inline void ProcessDynamicMethodCall(StackInstr* instr, StackInstr** &instrs, long &ip, long* &op_stack, long* &stack_pos);
    inline void ProcessJitMethodCall(StackMethod* called, long* instance, StackInstr** &instrs, long &ip, long* &op_stack, long* &stack_pos);
    inline void ProcessAsyncMethodCall(StackMethod* called, long* param);

    inline void ProcessInterpretedMethodCall(StackMethod* called, long* instance, StackInstr** &instrs, long &ip);
    inline void ProcessLoadIntArrayElement(StackInstr* instr, long* &op_stack, long* &stack_pos);
    inline void ProcessStoreIntArrayElement(StackInstr* instr, long* &op_stack, long* &stack_pos);
    inline void ProcessLoadFloatArrayElement(StackInstr* instr, long* &op_stack, long* &stack_pos);
    inline void ProcessStoreFloatArrayElement(StackInstr* instr, long* &op_stack, long* &stack_pos);
    inline void ProcessLoadByteArrayElement(StackInstr* instr, long* &op_stack, long* &stack_pos);
    inline void ProcessStoreByteArrayElement(StackInstr* instr, long* &op_stack, long* &stack_pos);    
    inline void ProcessStoreCharArrayElement(StackInstr* instr, long* &op_stack, long* &stack_pos);
    inline void ProcessLoadCharArrayElement(StackInstr* instr, long* &op_stack, long* &stack_pos);
    inline void ProcessStoreFunction(StackInstr* instr, long* &op_stack, long* &stack_pos);
    inline void ProcessLoadFunction(StackInstr* instr, long* &op_stack, long* &stack_pos);
    inline void ProcessStoreFloat(StackInstr* instr, long* &op_stack, long* &stack_pos);
    inline void ProcessLoadFloat(StackInstr* instr, long* &op_stack, long* &stack_pos);
    inline void ProcessCopyFloat(StackInstr* instr, long* &op_stack, long* &stack_pos);
    inline void SerializeObject(long* &op_stack, long* &stack_pos);
    inline void DeserializeObject(long* &op_stack, long* &stack_pos);
    inline void ProcessDllLoad(StackInstr* instr);
    inline void ProcessDllUnload(StackInstr* instr);
    inline void ProcessDllCall(StackInstr* instr, long* &op_stack, long* &stack_pos);
    
  public:
    // initialize the runtime system
    static void Initialize(StackProgram* p);

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
    static uintptr_t WINAPI AsyncMethodCall(LPVOID arg);
#else
    static void* AsyncMethodCall(void* arg);
#endif

    StackInterpreter(StackFrame** c, long* cp) {
      // setup frame
      call_stack = c;
      call_stack_pos = cp;
      frame = new StackFrame*;
      monitor = NULL;
      
      MemoryManager::AddPdaMethodRoot(frame);
    }

    StackInterpreter() {
      // setup frame
      call_stack = new StackFrame*[CALL_STACK_SIZE];
      call_stack_pos = new long;
      frame = new StackFrame*;

      // register monitor
      monitor = new StackFrameMonitor;
      monitor->call_stack = call_stack;
      monitor->call_stack_pos = call_stack_pos;
      monitor->cur_frame = frame;
      MemoryManager::AddPdaMethodRoot(monitor);
    }
  
    StackInterpreter(StackProgram* p) {
      Initialize(p);
      
      // setup frame
      call_stack = new StackFrame*[CALL_STACK_SIZE];
      call_stack_pos = new long;
      frame = new StackFrame*;

      // register monitor
      monitor = new StackFrameMonitor;
      monitor->call_stack = call_stack;
      monitor->call_stack_pos = call_stack_pos;
      monitor->cur_frame = frame;      
      MemoryManager::AddPdaMethodRoot(monitor);
    }
  
#ifdef _DEBUGGER
    StackInterpreter(StackProgram* p, Debugger* d) {
      Initialize(p);
      
      debugger = d;
      
      // setup frame
      call_stack = new StackFrame*[CALL_STACK_SIZE];
      call_stack_pos = new long;
      frame = new StackFrame*;

      // register monitor
      monitor = new StackFrameMonitor;
      monitor->call_stack = call_stack;
      monitor->call_stack_pos = call_stack_pos;
      monitor->cur_frame = frame;
      MemoryManager::AddPdaMethodRoot(monitor);
    }
#endif
    
    ~StackInterpreter() {
      if(monitor) {
        MemoryManager::RemovePdaMethodRoot(monitor);

        delete[] call_stack;
        call_stack = NULL;

        delete call_stack_pos;
        call_stack_pos = NULL;

        delete monitor;
        monitor = NULL;
      }
      else {
        MemoryManager::RemovePdaMethodRoot(frame);
      }
      delete frame;
      frame = NULL;
    }

    // execute method
    void Execute(long* op_stack, long* stack_pos, long i, StackMethod* method, long* instance, bool jit_called);
  };
}
#endif
