/***************************************************************************
 * VM stack machine.
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

#include "interpreter.h"
#include "lib_api.h"

#ifdef _X64
#include "jit/amd64/jit_amd_lp64.h"
#else
#include "jit/ia32/jit_intel_lp32.h"
#endif

#ifdef _WIN32
#include "os/windows/windows.h"
#else
#include "os/posix/posix.h"
#endif

#ifdef _DEBUGGER
#include "debugger/debugger.h"
#endif

#include <math.h>

using namespace Runtime;

StackProgram* StackInterpreter::program;

/********************************
 * JIT compiler thread
 ********************************/
#ifdef _WIN32
uintptr_t WINAPI StackInterpreter::CompileMethod(LPVOID arg)
{
  StackMethod* method = (StackMethod*)arg;
  JitCompilerIA32 jit_compiler;
  jit_compiler.Compile(method);

  return 0;
}
#else
void* StackInterpreter::CompileMethod(void* arg) 
{
  StackMethod* method = (StackMethod*)arg;
#ifdef _X64
  JitCompilerIA64 jit_compiler;
#else
  JitCompilerIA32 jit_compiler;
#endif
  jit_compiler.Compile(method);

  // clean up
  program->RemoveThread(pthread_self());
  return NULL;
}
#endif

/********************************
 * VM initialization
 ********************************/
void StackInterpreter::Initialize(StackProgram* p)
{
  program = p;

#ifdef _WIN32
  StackMethod::InitVirtualEntry();
#endif 

#ifdef _X64
  JitCompilerIA64::Initialize(program);
#else
  JitCompilerIA32::Initialize(program);
#endif
  MemoryManager::Initialize(program);
}

/********************************
 * Main VM execution method. This
 * funciton is used by callbacks 
 * from native code for the C API
 ********************************/
void StackInterpreter::Execute(long* stack, long* pos, long i, StackMethod* method,
			       long* instance, bool jit_called)
{
  // inital setup
  op_stack = stack;
  stack_pos = pos;
  call_stack_pos = 0;

  frame = new StackFrame(method, instance);
#ifdef _DEBUG
  cout << "creating frame=" << frame << endl;
#endif
  frame->SetJitCalled(jit_called);
  instrs = frame->GetMethod()->GetInstructions();
  ip = i;

#ifdef _DEBUG
  cout << "\n---------- Executing Interpretered Code: id=" 
       << ((frame->GetMethod()->GetClass()) ? frame->GetMethod()->GetClass()->GetId() : -1) << ","
       << frame->GetMethod()->GetId() << "; method_name='" << frame->GetMethod()->GetName() 
       << "' ---------\n" << endl;
#endif

  // add frame
  if(!jit_called) {
    MemoryManager::Instance()->AddPdaMethodRoot(frame);
  }

  // execute
  Execute();
}

/********************************
 * Main execution loop.
 ********************************/
void StackInterpreter::Execute()
{
  long right, left;
  
  // execute
  halt = false;
  do {
    StackInstr* instr = instrs[ip++];
    
#ifdef _DEBUGGER
    debugger->ProcessInstruction(instr, ip, call_stack, call_stack_pos, frame);
#endif
    
    switch(instr->GetType()) {
    case STOR_INT_VAR: {
#ifdef _DEBUG
      cout << "stack oper: STOR_INT_VAR; index=" << instr->GetOperand()
	   << "; local=" << ((instr->GetOperand2() == LOCL) ? "true" : "false") << endl;
#endif
      if(instr->GetOperand2() == LOCL) {
	long* mem = frame->GetMemory();
	mem[instr->GetOperand() + 1] = POP_INT();
      } 
      else {
	long* cls_inst_mem = (long*)POP_INT();
	if(!cls_inst_mem) {
	  cerr << ">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
	  StackErrorUnwind();
	  exit(1);
	}
	long mem = POP_INT();
	cls_inst_mem[instr->GetOperand()] = mem;
      }
    }
      break;

    case STOR_FUNC_VAR:
      ProcessStoreFunction(instr);
      break;

    case STOR_FLOAT_VAR:
      ProcessStoreFloat(instr);
      break;

    case COPY_INT_VAR: {
#ifdef _DEBUG
      cout << "stack oper: COPY_INT_VAR; index=" << instr->GetOperand()
	   << "; local=" << ((instr->GetOperand2() == LOCL) ? "true" : "false") << endl;
#endif
      if(instr->GetOperand2() == LOCL) {
	long* mem = frame->GetMemory();
	mem[instr->GetOperand() + 1] = TOP_INT();
      } else {
	long* cls_inst_mem = (long*)POP_INT();
	if(!cls_inst_mem) {
	  cerr << ">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
	  StackErrorUnwind();
	  exit(1);
	}
	cls_inst_mem[instr->GetOperand()] = TOP_INT();
      }
    }
      break;

    case COPY_FLOAT_VAR:
      ProcessCopyFloat(instr);
      break;

    case LOAD_INT_LIT:
#ifdef _DEBUG
      cout << "stack oper: LOAD_INT_LIT; call_pos=" << call_stack_pos << endl;
#endif
      PUSH_INT(instr->GetOperand());
      break;

    case SHL_INT: {
#ifdef _DEBUG
      cout << "stack oper: SHL_INT; call_pos=" << call_stack_pos << endl;
#endif
      right = POP_INT();
      left = POP_INT();
      PUSH_INT(right << left);
    }
      break;
      
    case SHR_INT: {
#ifdef _DEBUG
      cout << "stack oper: SHR_INT; call_pos=" << call_stack_pos << endl;
#endif
      right = POP_INT();
      left = POP_INT();
      PUSH_INT(right >> left);
    }
      break;

    case LOAD_FLOAT_LIT:
#ifdef _DEBUG
      cout << "stack oper: LOAD_FLOAT_LIT; call_pos=" << call_stack_pos << endl;
#endif
      PushFloat(instr->GetFloatOperand());
      break;

    case LOAD_INT_VAR: {
#ifdef _DEBUG
      cout << "stack oper: LOAD_INT_VAR; index=" << instr->GetOperand()
	   << "; local=" << ((instr->GetOperand2() == LOCL) ? "true" : "false") << endl;
#endif
      if(instr->GetOperand2() == LOCL) {
	long* mem = frame->GetMemory();
	PUSH_INT(mem[instr->GetOperand() + 1]);
      } 
      else {
	long* cls_inst_mem = (long*)POP_INT();
	if(!cls_inst_mem) {
	  cerr << ">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
	  StackErrorUnwind();
	  exit(1);
	}
	PUSH_INT(cls_inst_mem[instr->GetOperand()]);
      }
    }
      break;

    case LOAD_FUNC_VAR:
      ProcessLoadFunction(instr);
      break;

    case LOAD_FLOAT_VAR:
      ProcessLoadFloat(instr);
      break;

    case AND_INT: {
#ifdef _DEBUG
      cout << "stack oper: AND; call_pos=" << call_stack_pos << endl;
#endif
      right = POP_INT();
      left = POP_INT();
      PUSH_INT(left && right);
    }
      break;

    case OR_INT: {
#ifdef _DEBUG
      cout << "stack oper: OR; call_pos=" << call_stack_pos << endl;
#endif
      right = POP_INT();
      left = POP_INT();
      PUSH_INT(left || right);
    }
      break;

    case ADD_INT:
#ifdef _DEBUG
      cout << "stack oper: ADD; call_pos=" << call_stack_pos << endl;
#endif
      
      right = POP_INT();
      left = POP_INT();
      PUSH_INT(right + left);
      break;

    case ADD_FLOAT:
#ifdef _DEBUG
      cout << "stack oper: ADD; call_pos=" << call_stack_pos << endl;
#endif
      PushFloat(PopFloat() + PopFloat());
      break;

    case SUB_INT:
#ifdef _DEBUG
      cout << "stack oper: SUB; call_pos=" << call_stack_pos << endl;
#endif
      right = POP_INT();
      left = POP_INT();
      PUSH_INT(right - left);
      break;

    case SUB_FLOAT:
#ifdef _DEBUG
      cout << "stack oper: SUB; call_pos=" << call_stack_pos << endl;
#endif
      PushFloat(PopFloat() - PopFloat());
      break;

    case MUL_INT:
#ifdef _DEBUG
      cout << "stack oper: MUL; call_pos=" << call_stack_pos << endl;
#endif
      right = POP_INT();
      left = POP_INT();
      PUSH_INT(right * left);
      break;

    case DIV_INT:
#ifdef _DEBUG
      cout << "stack oper: DIV; call_pos=" << call_stack_pos << endl;
#endif
      right = POP_INT();
      left = POP_INT();
      PUSH_INT(right / left);
      break;

    case MUL_FLOAT:
#ifdef _DEBUG
      cout << "stack oper: MUL; call_pos=" << call_stack_pos << endl;
#endif
      PushFloat(PopFloat() * PopFloat());
      break;

    case DIV_FLOAT:
#ifdef _DEBUG
      cout << "stack oper: DIV; call_pos=" << call_stack_pos << endl;
#endif
      PushFloat(PopFloat() / PopFloat());
      break;

    case MOD_INT:
#ifdef _DEBUG
      cout << "stack oper: MOD; call_pos=" << call_stack_pos << endl;
#endif
      right = POP_INT();
      left = POP_INT();
      PUSH_INT(right % left);
      break;

    case BIT_AND_INT:
#ifdef _DEBUG
      cout << "stack oper: BIT_AND; call_pos=" << call_stack_pos << endl;
#endif
      right = POP_INT();
      left = POP_INT();
      PUSH_INT(right & left);
      break;

    case BIT_OR_INT:
#ifdef _DEBUG
      cout << "stack oper: BIT_OR; call_pos=" << call_stack_pos << endl;
#endif
      right = POP_INT();
      left = POP_INT();
      PUSH_INT(right | left);
      break;

    case BIT_XOR_INT:
#ifdef _DEBUG
      cout << "stack oper: BIT_XOR; call_pos=" << call_stack_pos << endl;
#endif
      right = POP_INT();
      left = POP_INT();
      PUSH_INT(right ^ left);
      break;

    case LES_EQL_INT:
#ifdef _DEBUG
      cout << "stack oper: LES_EQL; call_pos=" << call_stack_pos << endl;
#endif
      right = POP_INT();
      left = POP_INT();
      PUSH_INT(right <= left);
      break;

    case GTR_EQL_INT:
#ifdef _DEBUG
      cout << "stack oper: GTR_EQL; call_pos=" << call_stack_pos << endl;
#endif
      right = POP_INT();
      left = POP_INT();
      PUSH_INT(right >= left);
      break;

    case LES_EQL_FLOAT:
#ifdef _DEBUG
      cout << "stack oper: LES_EQL; call_pos=" << call_stack_pos << endl;
#endif
      right = POP_INT();
      left = POP_INT();
      PUSH_INT(right <= left);
      break;

    case GTR_EQL_FLOAT:
#ifdef _DEBUG
      cout << "stack oper: GTR_EQL; call_pos=" << call_stack_pos << endl;
#endif
      right = POP_INT();
      left = POP_INT();
      PUSH_INT(right >= left);
      break;

    case EQL_INT:
#ifdef _DEBUG
      cout << "stack oper: EQL; call_pos=" << call_stack_pos << endl;
#endif
      right = POP_INT();
      left = POP_INT();
      PUSH_INT(right == left);
      break;

    case NEQL_INT:
#ifdef _DEBUG
      cout << "stack oper: NEQL; call_pos=" << call_stack_pos << endl;
#endif
      right = POP_INT();
      left = POP_INT();
      PUSH_INT(right != left);
      break;

    case LES_INT:
#ifdef _DEBUG
      cout << "stack oper: LES; call_pos=" << call_stack_pos << endl;
#endif
      right = POP_INT();
      left = POP_INT();
      PUSH_INT(right < left);
      break;

    case GTR_INT:
#ifdef _DEBUG
      cout << "stack oper: GTR; call_pos=" << call_stack_pos << endl;
#endif
      right = POP_INT();
      left = POP_INT();      
      PUSH_INT(right > left);
      break;

    case EQL_FLOAT:
#ifdef _DEBUG
      cout << "stack oper: EQL; call_pos=" << call_stack_pos << endl;
#endif
      PUSH_INT(PopFloat() == PopFloat());
      break;

    case NEQL_FLOAT:
#ifdef _DEBUG
      cout << "stack oper: NEQL; call_pos=" << call_stack_pos << endl;
#endif
      PUSH_INT(PopFloat() != PopFloat());
      break;

    case LES_FLOAT:
#ifdef _DEBUG
      cout << "stack oper: LES; call_pos=" << call_stack_pos << endl;
#endif
      PUSH_INT(PopFloat() < PopFloat());
      break;

    case GTR_FLOAT:
#ifdef _DEBUG
      cout << "stack oper: GTR; call_pos=" << call_stack_pos << endl;
#endif
      PUSH_INT(PopFloat() > PopFloat());
      break;

    case CPY_BYTE_ARY: {
      long length = POP_INT();
      const long src_offset = POP_INT();
      long* src_array = (long*)POP_INT();
      const long dest_offset = POP_INT();
      long* dest_array = (long*)POP_INT();      


      if(!src_array || !dest_array) {
        cerr << ">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
        StackErrorUnwind();
        exit(1);
      }

      const long src_array_len = src_array[2];
      const long dest_array_len = dest_array[2];

      if(length > 0 && src_offset + length <= src_array_len && dest_offset + length <= dest_array_len) {
        char* src_array_ptr = (char*)(src_array + 3);
        char* dest_array_ptr = (char*)(dest_array + 3);
        memcpy(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length);
        PUSH_INT(1);
      }
      else {
        PUSH_INT(0);
      }
    }
      break;

    case CPY_INT_ARY: {
      long length = POP_INT();
      const long src_offset = POP_INT();
      long* src_array = (long*)POP_INT();
      const long dest_offset = POP_INT();
      long* dest_array = (long*)POP_INT();      

      if(!src_array || !dest_array) {
        cerr << ">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
        StackErrorUnwind();
        exit(1);
      }

      const long src_array_len = src_array[0];
      const long dest_array_len = dest_array[0];
      if(length > 0 && src_offset + length <= src_array_len && dest_offset + length <= dest_array_len) {
        long* src_array_ptr = src_array + 3;
        long* dest_array_ptr = dest_array + 3;
        memcpy(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length * sizeof(long));
        PUSH_INT(1);
      }
      else {
        PUSH_INT(0);
      }
    }
      break;

    case CPY_FLOAT_ARY: {
      long length = POP_INT();
      const long src_offset = POP_INT();
      long* src_array = (long*)POP_INT();
      const long dest_offset = POP_INT();
      long* dest_array = (long*)POP_INT();      

      if(!src_array || !dest_array) {
        cerr << ">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
        StackErrorUnwind();
        exit(1);
      }

      const long src_array_len = src_array[0];
      const long dest_array_len = dest_array[0];
      if(length > 0 && src_offset + length <= src_array_len && dest_offset + length <= dest_array_len) {
        long* src_array_ptr = src_array + 3;
        long* dest_array_ptr = dest_array + 3;
        memcpy(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length * sizeof(FLOAT_VALUE));
        PUSH_INT(1);
      }
      else {
        PUSH_INT(0);
      }
    }
      break;

      // Note: no supported via JIT -- *start*
    case CEIL_FLOAT:
      PushFloat(ceil(PopFloat()));
      break;

    case FLOR_FLOAT:
      PushFloat(floor(PopFloat()));
      break;

    case SIN_FLOAT:
      PushFloat(sin(PopFloat()));
      break;

    case COS_FLOAT:
      PushFloat(cos(PopFloat()));
      break;

    case TAN_FLOAT:
      PushFloat(tan(PopFloat()));
      break;

    case ASIN_FLOAT:
      PushFloat(asin(PopFloat()));
      break;

    case ACOS_FLOAT:
      PushFloat(acos(PopFloat()));
      break;

    case ATAN_FLOAT:
      PushFloat(atan(PopFloat()));
      break;

    case LOG_FLOAT:
      PushFloat(log(PopFloat()));
      break;

    case POW_FLOAT: {
      FLOAT_VALUE left = PopFloat();
      FLOAT_VALUE right = PopFloat();
      PushFloat(pow(right, left));
    }
      break;

    case SQRT_FLOAT:
      PushFloat(sqrt(PopFloat()));
      break;

    case RAND_FLOAT: {
      FLOAT_VALUE value = (FLOAT_VALUE)rand();
      PushFloat(value / (FLOAT_VALUE)RAND_MAX);
      break;
    }
      // Note: no supported via JIT -- *end*

    case I2F:
#ifdef _DEBUG
      cout << "stack oper: I2F; call_pos=" << call_stack_pos << endl;
#endif
      right = POP_INT();
      PushFloat(right);
      break;

    case F2I:
#ifdef _DEBUG
      cout << "stack oper: F2I; call_pos=" << call_stack_pos << endl;
#endif
      PUSH_INT((long)PopFloat());
      break;

    case SWAP_INT:
#ifdef _DEBUG
      cout << "stack oper: SWAP_INT; call_pos=" << call_stack_pos << endl;
#endif
      SwapInt();
      break;

    case POP_INT:
#ifdef _DEBUG
      cout << "stack oper: POP_INT; call_pos=" << call_stack_pos << endl;
#endif
      POP_INT();
      break;

    case POP_FLOAT:
#ifdef _DEBUG
      cout << "stack oper: POP_FLOAT; call_pos=" << call_stack_pos << endl;
#endif
      PopFloat();
      break;

    case OBJ_TYPE_OF: {
      long* mem = (long*)POP_INT();
      long* result = MemoryManager::Instance()->ValidObjectCast(mem, instr->GetOperand(),
								program->GetHierarchy(),
								program->GetInterfaces());
      if(result) {
        PUSH_INT(1);
      }
      else {
        PUSH_INT(0);
      }
    }
      break;

    case OBJ_INST_CAST: {
      long* mem = (long*)POP_INT();
      long result = (long)MemoryManager::Instance()->ValidObjectCast(mem, instr->GetOperand(),
								     program->GetHierarchy(),
								     program->GetInterfaces());
#ifdef _DEBUG
      cout << "stack oper: OBJ_INST_CAST: from=" << mem << ", to=" << instr->GetOperand() << endl; 
#endif
      if(!result && mem) {
        StackClass* to_cls = MemoryManager::GetClass((long*)mem);
        cerr << ">>> Invalid object cast: '" << (to_cls ? to_cls->GetName() : "?" )
	     << "' to '" << program->GetClass(instr->GetOperand())->GetName() << "' <<<" << endl;
        StackErrorUnwind();
        exit(1);
      }
      PUSH_INT(result);
    }
      break;

    case RTRN:
      ProcessReturn();
      // return directly back to JIT code
      if(frame && frame->IsJitCalled()) {
        frame->SetJitCalled(false);
        return;
      }
      break;

    case DYN_MTHD_CALL:
      ProcessDynamicMethodCall(instr);
      // return directly back to JIT code
      if(frame->IsJitCalled()) {
        frame->SetJitCalled(false);
        return;
      }
      break;

    case MTHD_CALL:
      ProcessMethodCall(instr);
      // return directly back to JIT code
      if(frame->IsJitCalled()) {
        frame->SetJitCalled(false);
        return;
      }
      break;

    case ASYNC_MTHD_CALL: {
      long* instance = (long*)frame->GetMemory()[0];
      long* param = (long*)frame->GetMemory()[1];

      StackClass* impl_class = MemoryManager::GetClass(instance);
#ifdef _DEBUG
      assert(impl_class);
#endif
      const string& mthd_name = impl_class->GetName() + ":Run:o.System.Base,";
      StackMethod* called = impl_class->GetMethod(mthd_name);
#ifdef _DEBUG
      assert(called);
      cout << "=== ASYNC_MTHD_CALL: id=" << called->GetClass()->GetId() << ","
	   << called->GetId() << "; name='" << called->GetName() 
	   << "'; param=" << param << " ===" << endl;
#endif

      // create and execute the new thread
      // make sure that calls to the model are synced.  Are find method synced?
      ProcessAsyncMethodCall(called, param);
    }
      break;

    case NEW_BYTE_ARY:
      ProcessNewByteArray(instr);
      break;

    case NEW_INT_ARY:
      ProcessNewArray(instr);
      break;

    case NEW_FLOAT_ARY:
      ProcessNewArray(instr, true);
      break;

    case NEW_OBJ_INST:
      ProcessNewObjectInstance(instr);
      break;

    case STOR_BYTE_ARY_ELM:
      ProcessStoreByteArrayElement(instr);
      break;

    case LOAD_BYTE_ARY_ELM:
      ProcessLoadByteArrayElement(instr);
      break;

    case STOR_INT_ARY_ELM:
      ProcessStoreIntArrayElement(instr);
      break;

    case LOAD_INT_ARY_ELM:
      ProcessLoadIntArrayElement(instr);
      break;

    case STOR_FLOAT_ARY_ELM:
      ProcessStoreFloatArrayElement(instr);
      break;

    case LOAD_FLOAT_ARY_ELM:
      ProcessLoadFloatArrayElement(instr);
      break;

    case LOAD_CLS_MEM:
#ifdef _DEBUG
      cout << "stack oper: LOAD_CLS_MEM; call_pos=" << call_stack_pos << endl;
#endif
      PUSH_INT((long)frame->GetMethod()->GetClass()->GetClassMemory());
      break;

    case LOAD_INST_MEM:
#ifdef _DEBUG
      cout << "stack oper: LOAD_INST_MEM; call_pos=" << call_stack_pos << endl;
#endif
      PUSH_INT(frame->GetMemory()[0]);
      break;

    case TRAP:
    case TRAP_RTRN:
#ifdef _DEBUG
      cout << "stack oper: TRAP; call_pos=" << call_stack_pos << endl;
#endif
      ProcessTrap(instr);
      break;

      // shared library support
    case DLL_LOAD:
      ProcessDllLoad(instr);
      break;

    case DLL_UNLOAD:
      ProcessDllUnload(instr);
      break;

    case DLL_FUNC_CALL:
      ProcessDllCall(instr);
      break;

      //
      // Start: Thread support
      // 

    case THREAD_JOIN: {
#ifdef _DEBUG
      cout << "stack oper: THREAD_JOIN; call_pos=" << call_stack_pos << endl;
#endif
      long* instance = (long*)frame->GetMemory()[0];
      if(!instance) {
        cerr << ">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
        StackErrorUnwind();
        exit(1);
      }

#ifdef _WIN32
      HANDLE vm_thread = (HANDLE)instance[0];
      if(WaitForSingleObject(vm_thread, INFINITE) != WAIT_OBJECT_0) {
        cerr << ">>> Unable to join thread! <<<" << endl;
        exit(1);
      }
#else
      void* status;
      pthread_t vm_thread = (pthread_t)instance[0];      
      if(pthread_join(vm_thread, &status)) {
        cerr << ">>> Unable to join thread! <<<" << endl;
        exit(1);
      }
#endif
    }
      break;

    case THREAD_SLEEP:
#ifdef _DEBUG
      cout << "stack oper: THREAD_SLEEP; call_pos=" << call_stack_pos << endl;
#endif

#ifdef _WIN32
      right = POP_INT() * 1000;
      Sleep(right);
#else
      right = POP_INT();
      sleep(right);
#endif
      break;

    case THREAD_MUTEX: {
#ifdef _DEBUG
      cout << "stack oper: THREAD_MUTEX; call_pos=" << call_stack_pos << endl;
#endif
      long* instance = (long*)frame->GetMemory()[0];
      if(!instance) {
        cerr << ">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
        StackErrorUnwind();
        exit(1);
      }
#ifdef _WIN32
      InitializeCriticalSection((CRITICAL_SECTION*)&instance[1]);
#else
      pthread_mutex_init((pthread_mutex_t*)&instance[1], NULL);
#endif
    }
      break;

    case CRITICAL_START: {
#ifdef _DEBUG
      cout << "stack oper: CRITICAL_START; call_pos=" << call_stack_pos << endl;
#endif
      long* instance = (long*)POP_INT();
      if(!instance) {
        cerr << ">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
        StackErrorUnwind();
        exit(1);
      }
#ifdef _WIN32
      EnterCriticalSection((CRITICAL_SECTION*)&instance[1]);
#else
      pthread_mutex_lock((pthread_mutex_t*)&instance[1]);
#endif
    }
      break;

    case CRITICAL_END: {
#ifdef _DEBUG
      cout << "stack oper: CRITICAL_END; call_pos=" << call_stack_pos << endl;
#endif
      long* instance = (long*)POP_INT();
      if(!instance) {
        cerr << ">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
        StackErrorUnwind();
        exit(1);
      }
#ifdef _WIN32
      LeaveCriticalSection((CRITICAL_SECTION*)&instance[1]);
#else
      pthread_mutex_unlock((pthread_mutex_t*)&instance[1]);
#endif
    }
      break;

      //
      // End: Thread support
      // 

    case JMP:
#ifdef _DEBUG
      cout << "stack oper: JMP; call_pos=" << call_stack_pos << endl;
#endif
      if(instr->GetOperand2() < 0) {
        ip = frame->GetMethod()->GetLabelIndex(instr->GetOperand()) + 1;
      } else if(POP_INT() == instr->GetOperand2()) {
        ip = frame->GetMethod()->GetLabelIndex(instr->GetOperand()) + 1;
      }
      break;

      // note: just for debugger
    case END_STMTS:
      break;

    default:
      break;
    }
  }
  while(!halt);
}

/********************************
 * Creates a Date object with
 * current time
 ********************************/
void StackInterpreter::ProcessCurrentTime(bool is_gmt) 
{
  time_t raw_time;
  time(&raw_time);  

  struct tm* curr_time;
  if(is_gmt) {
    curr_time = gmtime(&raw_time);
  }
  else {
    curr_time = localtime(&raw_time);
  }

  long* instance = (long*)frame->GetMemory()[0];
  if(instance) {
    instance[0] = curr_time->tm_mday;          // day
    instance[1] = curr_time->tm_mon + 1;       // month
    instance[2] = curr_time->tm_year + 1900;   // year
    instance[3] = curr_time->tm_hour;          // hours
    instance[4] = curr_time->tm_min;           // mins
    instance[5] = curr_time->tm_sec;           // secs
    instance[6] = curr_time->tm_isdst > 0;     // savings time
    instance[7] = curr_time->tm_wday;          // day of week
    instance[8] = is_gmt;                      // is GMT
  }
}

/********************************
 * Date/time calculations
 ********************************/
void StackInterpreter::ProcessAddTime(TimeInterval t)
{  
  long value = POP_INT();
  long* instance = (long*)POP_INT();

  if(instance) {
    // calculate change in seconds
    long offset;
    switch(t) {
    case DAY_TIME:
      offset = 86400 * value;
      break;

    case HOUR_TIME:
      offset = 3600 * value;
      break;

    case MIN_TIME:
      offset = 60 * value;
      break;

    default:
      offset = value;
      break;
    }

    // create time structure
    struct tm set_time;
    set_time.tm_mday = instance[0];          // day
    set_time.tm_mon = instance[1] - 1;       // month
    set_time.tm_year = instance[2] - 1900;   // year
    set_time.tm_hour = instance[3];          // hours
    set_time.tm_min = instance[4];           // mins
    set_time.tm_sec = instance[5];           // secs
    set_time.tm_isdst = instance[6] > 0;     // savings time

    // calculate difference
    time_t raw_time = mktime(&set_time);
    raw_time += offset;  

    struct tm* curr_time;
    if(instance[8]) {
      curr_time = gmtime(&raw_time);
    }
    else {
      curr_time = localtime(&raw_time);
    }

    // set instance values
    instance[0] = curr_time->tm_mday;          // day
    instance[1] = curr_time->tm_mon + 1;       // month
    instance[2] = curr_time->tm_year + 1900;   // year
    instance[3] = curr_time->tm_hour;          // hours
    instance[4] = curr_time->tm_min;           // mins
    instance[5] = curr_time->tm_sec;           // secs
    instance[6] = curr_time->tm_isdst > 0;     // savings time
    instance[7] = curr_time->tm_wday;          // day of week
  }
}

/********************************
 * Creates a Date object with 
 * specified time
 ********************************/
void StackInterpreter::ProcessSetTime1() 
{
  // get time values
  long is_gmt = POP_INT();
  long year = POP_INT();
  long month = POP_INT();
  long day = POP_INT();
  long* instance = (long*)POP_INT();

  if(instance) {
    // get current time
    time_t raw_time;
    time(&raw_time);  
    struct tm* curr_time;
    if(is_gmt) {
      curr_time = gmtime(&raw_time);
    }
    else {
      curr_time = localtime(&raw_time);
    }

    // update time
    curr_time->tm_year = year - 1900;
    curr_time->tm_mon = month - 1;
    curr_time->tm_mday = day;
    mktime(curr_time);

    // set instance values
    instance[0] = curr_time->tm_mday;          // day
    instance[1] = curr_time->tm_mon + 1;       // month
    instance[2] = curr_time->tm_year + 1900;   // year
    instance[3] = curr_time->tm_hour;          // hours
    instance[4] = curr_time->tm_min;           // mins
    instance[5] = curr_time->tm_sec;           // secs
    instance[6] = curr_time->tm_isdst > 0;     // savings time
    instance[7] = curr_time->tm_wday;          // day of week
    instance[8] = is_gmt;                      // is GMT
  }
}

/********************************
 * Sets a time instance
 ********************************/
void StackInterpreter::ProcessSetTime2() 
{
  // get time values
  long is_gmt = POP_INT();
  long secs = POP_INT();
  long mins = POP_INT();
  long hours = POP_INT();
  long year = POP_INT();
  long month = POP_INT();
  long day = POP_INT();
  long* instance = (long*)POP_INT();

  if(instance) {
    // get current time
    time_t raw_time;
    time(&raw_time);  
    struct tm* curr_time;
    if(is_gmt) {
      curr_time = gmtime(&raw_time);
    }
    else {
      curr_time = localtime(&raw_time);
    }

    // update time
    curr_time->tm_year = year - 1900;
    curr_time->tm_mon = month - 1;
    curr_time->tm_mday = day;
    curr_time->tm_hour = hours;
    curr_time->tm_min = mins;
    curr_time->tm_sec = secs;  
    mktime(curr_time);

    // set instance values
    instance[0] = curr_time->tm_mday;          // day
    instance[1] = curr_time->tm_mon + 1;       // month
    instance[2] = curr_time->tm_year + 1900;   // year
    instance[3] = curr_time->tm_hour;          // hours
    instance[4] = curr_time->tm_min;           // mins
    instance[5] = curr_time->tm_sec;           // secs
    instance[6] = curr_time->tm_isdst > 0;     // savings time
    instance[7] = curr_time->tm_wday;          // day of week
    instance[8] = is_gmt;                      // is GMT
  }
}

/********************************
 * Set a time instance
 ********************************/
void StackInterpreter::ProcessSetTime3() 
{
}

/********************************
 * Get platform string
 ********************************/
void StackInterpreter::ProcessPlatform() 
{
  string value_str = System::GetPlatform();

  // create character array
  const long char_array_size = value_str.size();
  const long char_array_dim = 1;
  long* char_array = (long*)MemoryManager::AllocateArray(char_array_size + 1 +
							 ((char_array_dim + 2) *
							  sizeof(long)),
							 BYTE_ARY_TYPE,
							 op_stack, *stack_pos, false);
  char_array[0] = char_array_size + 1;
  char_array[1] = char_array_dim;
  char_array[2] = char_array_size;

  // copy string
  char* char_array_ptr = (char*)(char_array + 3);
  strcpy(char_array_ptr, value_str.c_str());

  // create 'System.String' object instance
  long* str_obj = MemoryManager::AllocateObject(program->GetStringObjectId(),
						(long*)op_stack, *stack_pos, false);
  str_obj[0] = (long)char_array;
  str_obj[1] = char_array_size;

  PUSH_INT((long)str_obj);
}

/********************************
 * Processes a load function
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessLoadFunction(StackInstr* instr)
{
#ifdef _DEBUG
  cout << "stack oper: LOAD_FUNC_VAR; index=" << instr->GetOperand()
       << "; local=" << ((instr->GetOperand2() == LOCL) ? "true" : "false") << endl;
#endif
  if(instr->GetOperand2() == LOCL) {
    long* mem = frame->GetMemory();
    PUSH_INT(mem[instr->GetOperand() + 2]);
    PUSH_INT(mem[instr->GetOperand() + 1]);
  } 
  else {
    long* cls_inst_mem = (long*)POP_INT();
    if(!cls_inst_mem) {
      cerr << ">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
      StackErrorUnwind();
      exit(1);
    }
    PUSH_INT(cls_inst_mem[instr->GetOperand() + 1]);
    PUSH_INT(cls_inst_mem[instr->GetOperand()]);
  }
}

/********************************
 * Processes a load float
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessLoadFloat(StackInstr* instr)
{
#ifdef _DEBUG
  cout << "stack oper: LOAD_FLOAT_VAR; index=" << instr->GetOperand()
       << "; local=" << ((instr->GetOperand2() == LOCL) ? "true" : "false") << endl;
#endif
  FLOAT_VALUE value;
  if(instr->GetOperand2() == LOCL) {
    long* mem = frame->GetMemory();
    memcpy(&value, &mem[instr->GetOperand() + 1], sizeof(FLOAT_VALUE));
  } else {
    long* cls_inst_mem = (long*)POP_INT();
    if(!cls_inst_mem) {
      cerr << ">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
      StackErrorUnwind();
      exit(1);
    }
    memcpy(&value, &cls_inst_mem[instr->GetOperand()], sizeof(FLOAT_VALUE));
  }
  PushFloat(value);
}

/********************************
 * Processes a store function
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessStoreFunction(StackInstr* instr)
{
#ifdef _DEBUG
  cout << "stack oper: STOR_FUNC_VAR; index=" << instr->GetOperand()
       << "; local=" << ((instr->GetOperand2() == LOCL) ? "true" : "false") << endl;
#endif
  if(instr->GetOperand2() == LOCL) {
    long* mem = frame->GetMemory();
    mem[instr->GetOperand() + 1] = POP_INT();
    mem[instr->GetOperand() + 2] = POP_INT();
  } 
  else {
    long* cls_inst_mem = (long*)POP_INT();
    if(!cls_inst_mem) {
      cerr << ">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
      StackErrorUnwind();
      exit(1);
    }
    cls_inst_mem[instr->GetOperand()] = POP_INT();
    cls_inst_mem[instr->GetOperand() + 1] = POP_INT();
  }
}

/********************************
 * Processes a store float
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessStoreFloat(StackInstr* instr)
{
#ifdef _DEBUG
  cout << "stack oper: STOR_FLOAT_VAR; index=" << instr->GetOperand()
       << "; local=" << ((instr->GetOperand2() == LOCL) ? "true" : "false") << endl;
#endif
  if(instr->GetOperand2() == LOCL) {
    FLOAT_VALUE value = PopFloat();
    long* mem = frame->GetMemory();
    memcpy(&mem[instr->GetOperand() + 1], &value, sizeof(FLOAT_VALUE));
  } else {
    long* cls_inst_mem = (long*)POP_INT();
    if(!cls_inst_mem) {
      cerr << ">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
      StackErrorUnwind();
      exit(1);
    }
    FLOAT_VALUE value = PopFloat();
    memcpy(&cls_inst_mem[instr->GetOperand()], &value, sizeof(FLOAT_VALUE));
  }
}

/********************************
 * Processes a copy float
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessCopyFloat(StackInstr* instr)
{
#ifdef _DEBUG
  cout << "stack oper: COPY_FLOAT_VAR; index=" << instr->GetOperand()
       << "; local=" << ((instr->GetOperand2() == LOCL) ? "true" : "false") << endl;
#endif
  if(instr->GetOperand2() == LOCL) {
    FLOAT_VALUE value = TopFloat();
    long* mem = frame->GetMemory();
    memcpy(&mem[instr->GetOperand() + 1], &value, sizeof(FLOAT_VALUE));
  } else {
    long* cls_inst_mem = (long*)POP_INT();
    if(!cls_inst_mem) {
      cerr << ">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
      StackErrorUnwind();
      exit(1);
    }
    FLOAT_VALUE value = TopFloat();
    memcpy(&cls_inst_mem[instr->GetOperand()], &value, sizeof(FLOAT_VALUE));
  }
}

/********************************
 * Processes a new object instance
 * request.
 ********************************/
void StackInterpreter::ProcessNewObjectInstance(StackInstr* instr)
{
#ifdef _DEBUG
  cout << "stack oper: NEW_OBJ_INST: id=" << instr->GetOperand() << endl;
#endif

  long inst_mem = (long)MemoryManager::AllocateObject(instr->GetOperand(),
						      op_stack, *stack_pos);
  PUSH_INT(inst_mem);
}

/********************************
 * Processes a new array instance
 * request.
 ********************************/
void StackInterpreter::ProcessNewArray(StackInstr* instr, bool is_float)
{
#ifdef _DEBUG
  cout << "stack oper: NEW_INT_ARY/NEW_FLOAT_ARY; call_pos=" << call_stack_pos << endl;
#endif
  long indices[8];
  long value = POP_INT();
  long size = value;
  indices[0] = value;
  long dim = 1;
  for(long i = 1; i < instr->GetOperand(); i++) {
    long value = POP_INT();
    size *= value;
    indices[dim++] = value;
  }

  long* mem;  
#ifdef _X64
  mem = (long*)MemoryManager::AllocateArray(size + dim + 2, INT_TYPE,
					    op_stack, *stack_pos);
#else
  if(is_float) {
    // doubles are twice the size of integers for 32-bit target
    mem = (long*)MemoryManager::AllocateArray(size * 2 + dim + 2, INT_TYPE,
					      op_stack, *stack_pos);
  }
  else {
    mem = (long*)MemoryManager::AllocateArray(size + dim + 2, INT_TYPE,
					      op_stack, *stack_pos);
  }
#endif

  mem[0] = size;
  mem[1] = dim;

  memcpy(mem + 2, indices, dim * sizeof(long));
  PUSH_INT((long)mem);
}

/********************************
 * Processes a new byte array instance
 * request.
 ********************************/
void StackInterpreter::ProcessNewByteArray(StackInstr* instr)
{
#ifdef _DEBUG
  cout << "stack oper: NEW_BYTE_ARY; call_pos=" << call_stack_pos << endl;
#endif
  long indices[8];
  long value = POP_INT();
  long size = value;
  indices[0] = value;
  long dim = 1;
  for(long i = 1; i < instr->GetOperand(); i++) {
    long value = POP_INT();
    size *= value;
    indices[dim++] = value;
  }
  // NULL terminated string workaround
  size++;
  long* mem = (long*)MemoryManager::AllocateArray(size + ((dim + 2) * sizeof(long)),
						  BYTE_ARY_TYPE, op_stack, *stack_pos);
  mem[0] = size;
  mem[1] = dim;
  memcpy(mem + 2, indices, dim * sizeof(long));
  PUSH_INT((long)mem);
}

/********************************
 * Processes a return instruction.
 * This instruction modifies the
 * call stack.
 ********************************/
void StackInterpreter::ProcessReturn()
{
#ifdef _DEBUG
  cout << "stack oper: RTRN; call_pos=" << call_stack_pos << endl;
#endif

  // unregister old frame
  MemoryManager::Instance()->RemovePdaMethodRoot(frame);
#ifdef _DEBUG
  cout << "removing frame=" << frame << endl;
#endif

  delete frame;
  frame = NULL;

  // restore previous frame
  if(!StackEmpty()) {
    frame = PopFrame();
    instrs = frame->GetMethod()->GetInstructions();
    ip = frame->GetIp();
  } 
  else {
    halt = true;
  }
}

/********************************
 * Processes a asynchronous
 * method call.
 ********************************/
void StackInterpreter::ProcessAsyncMethodCall(StackMethod* called, long* param)
{
  ThreadHolder* holder = new ThreadHolder;
  holder->called = called;
  holder->param = param;

#ifdef _WIN32
  HANDLE vm_thread = (HANDLE)_beginthreadex(NULL, 0, AsyncMethodCall, holder, 0, NULL);
  if(!vm_thread) {
    cerr << ">>> Unable to create garbage collection thread! <<<" << endl;
    exit(-1);
  }
#else
  pthread_attr_t attrs;
  pthread_attr_init(&attrs);
  pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_JOINABLE);

  // execute thread
  pthread_t vm_thread;
  if(pthread_create(&vm_thread, &attrs, AsyncMethodCall, (void*)holder)) {
    cerr << ">>> Unable to create runtime thread! <<<" << endl;
    exit(-1);
  }
#endif  
  // assign thread ID
  long* instance = (long*)frame->GetMemory()[0];
  if(!instance) {
    cerr << ">>> Unable to create runtime thread! <<<" << endl;
    exit(-1);
  }

  instance[0] = (long)vm_thread;
#ifdef _DEBUG
  cout << "*** New Thread ID: " << vm_thread  << ": " << instance << " ***" << endl;
#endif
  program->AddThread(vm_thread);
}

#ifdef _WIN32
//
// windows thread callback
//
uintptr_t WINAPI StackInterpreter::AsyncMethodCall(LPVOID arg)
{
  ThreadHolder* holder = (ThreadHolder*)arg;

  // execute
  long* thread_op_stack = new long[STACK_SIZE];
  long* thread_stack_pos = new long;
  (*thread_stack_pos) = 0;

  // set parameter
  thread_op_stack[(*thread_stack_pos)++] = (long)holder->param;

  HANDLE vm_thread;
  DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
		  &vm_thread, 0, TRUE, DUPLICATE_SAME_ACCESS);

#ifdef _DEBUG
  cout << "# Starting thread=" << vm_thread << " #" << endl;
#endif  

  Runtime::StackInterpreter intpr;
  intpr.Execute(thread_op_stack, thread_stack_pos, 0, holder->called, NULL, false);

#ifdef _DEBUG
  cout << "# final stack: pos=" << (*thread_stack_pos) << ", thread=" << vm_thread << " #" << endl;
#endif

  // clean up
  delete[] thread_op_stack;
  thread_op_stack = NULL;

  delete thread_stack_pos;
  thread_stack_pos = NULL;

  delete holder;
  holder = NULL;

  program->RemoveThread(vm_thread);

  return 0;
}
#else
//
// posix thread callback
//
void* StackInterpreter::AsyncMethodCall(void* arg)
{
  ThreadHolder* holder = (ThreadHolder*)arg;

  // execute
  long* thread_op_stack = new long[STACK_SIZE];
  long* thread_stack_pos = new long;
  (*thread_stack_pos) = 0;

  // set parameter
  thread_op_stack[(*thread_stack_pos)++] = (long)holder->param;

#ifdef _DEBUG
  cout << "# Starting thread=" << pthread_self() << " #" << endl;
#endif  

  Runtime::StackInterpreter intpr;
  intpr.Execute(thread_op_stack, thread_stack_pos, 0, holder->called, NULL, false);

#ifdef _DEBUG
  cout << "# final stack: pos=" << (*thread_stack_pos) << ", thread=" << pthread_self() << " #" << endl;
#endif

  // clean up
  delete[] thread_op_stack;
  thread_op_stack = NULL;

  delete thread_stack_pos;
  thread_stack_pos = NULL;

  delete holder;
  holder = NULL;

  program->RemoveThread(pthread_self());

  return NULL;
}
#endif

/********************************
 * Processes a synchronous
 * dynamic method call.
 ********************************/
void StackInterpreter::ProcessDynamicMethodCall(StackInstr* instr)
{
  // save current method
  frame->SetIp(ip);
  PushFrame(frame);

  // pop instance
  long* instance = (long*)POP_INT();

  // make call
  long cls_id = POP_INT();
  long mthd_id = POP_INT();
#ifdef _DEBUG
  cout << "stack oper: DYN_MTHD_CALL; cls_mtd_id=" << cls_id << "," << mthd_id << endl;
#endif
  StackMethod* called = program->GetClass(cls_id)->GetMethod(mthd_id);
#ifdef _DEBUG
  cout << "=== Binding funcion call: to: '" << called->GetName() << "' ===" << endl;
#endif

#ifndef _NO_JIT
  // execute JIT call
  if(instr->GetOperand3()) {
    ProcessJitMethodCall(called, instance);
  }
  // execute interpreter
  else {
    ProcessInterpretedMethodCall(called, instance);
  }
#else
  ProcessInterpretedMethodCall(called, instance);
#endif
}

/********************************
 * Processes a synchronous method
 * call.
 ********************************/
void StackInterpreter::ProcessMethodCall(StackInstr* instr)
{
  // save current method
  frame->SetIp(ip);
  PushFrame(frame);

  // pop instance
  long* instance = (long*)POP_INT();

  // make call
  StackMethod* called = program->GetClass(instr->GetOperand())->GetMethod(instr->GetOperand2());
  // dynamically bind class for virutal method
  if(called->IsVirtual()) {
    StackClass* impl_class = MemoryManager::GetClass((long*)instance);
    if(!impl_class) {
      cerr << ">>> Attempting to envoke a virtual method! <<<" << endl;
      StackErrorUnwind();
      exit(1);
    }

#ifdef _DEBUG
    cout << "=== Binding virtual method call: from: '" << called->GetName();
#endif

    // binding method
    const string& qualified_method_name = called->GetName();
    const string& method_ending = qualified_method_name.substr(qualified_method_name.find(':'));
    string method_name = impl_class->GetName() + method_ending;

    // check method cache
    called = StackMethod::GetVirtualEntry(method_name);
    if(!called) {
      called = impl_class->GetMethod(method_name);
      while(!called) {
        impl_class = program->GetClass(impl_class->GetParentId());
        method_name = impl_class->GetName() + method_ending;
        called = program->GetClass(impl_class->GetId())->GetMethod(method_name);
      }
      // add cache entry
      StackMethod::AddVirtualEntry(method_name, called);
    }

#ifdef _DEBUG
    cout << "'; to: '" << method_name << "' ===" << endl;
#endif
  }

#ifndef _NO_JIT
  // execute JIT call
  if(instr->GetOperand3()) {
    ProcessJitMethodCall(called, instance);
  }
  // execute interpreter
  else {
    ProcessInterpretedMethodCall(called, instance);
  }
#else
  ProcessInterpretedMethodCall(called, instance);
#endif
}

/********************************
 * Processes an interpreted
 * synchronous method call.
 ********************************/
void StackInterpreter::ProcessJitMethodCall(StackMethod* called, long* instance)
{
#ifdef _DEBUGGER
  ProcessInterpretedMethodCall(called, instance);
#else
  // TODO: don't try and re-compile code that doesn't compile the first time
  // execute method if it's been compiled
  if(called->GetNativeCode()) {
    JitExecutorIA32 jit_executor;
    long status = jit_executor.Execute(called, (long*)instance, op_stack, stack_pos);
    if(status < 0) {
      switch(status) {
      case -1:
        cerr << ">>> Atempting to dereference a 'Nil' memory instance in native JIT code <<<" << endl;
        break;

      case -2:
      case -3:
        cerr << ">>> Index out of bounds in native JIT code! <<<" << endl;
        break;
      }
      StackErrorUnwind(called);
      exit(1);
    }
    // restore previous state
    frame = PopFrame();
    instrs = frame->GetMethod()->GetInstructions();
    ip = frame->GetIp();
  } 
  else {
#ifdef _JIT_SERIAL
    // compile
#ifdef _X64
    JitCompilerIA64 jit_compiler;
#else
    JitCompilerIA32 jit_compiler;
#endif
    if(!jit_compiler.Compile(called)) {
      ProcessInterpretedMethodCall(called, instance);
#ifdef _DEBUG
      cerr << "### Unable to compile: " << called->GetName() << " ###" << endl;
#endif
      return;
    }
    // execute
    JitExecutorIA32 jit_executor;
    long status = jit_executor.Execute(called, (long*)instance, op_stack, stack_pos);
    if(status < 0) {
      switch(status) {
      case -1:
        cerr << ">>> Atempting to dereference a 'Nil' memory instance in native JIT code <<<" << endl;
        break;

      case -2:
      case -3:
        cerr << ">>> Index out of bounds in native JIT code! <<<" << endl;
        break;
      }
      StackErrorUnwind(called);
      exit(1);
    }
    // restore previous state
    frame = PopFrame();
    instrs = frame->GetMethod()->GetInstructions();
    ip = frame->GetIp();
#else
#ifdef _WIN32
    // 
    // Windows: lock this section while we compile...
    //
    if(!TryEnterCriticalSection(&called->jit_cs)) {
      ProcessInterpretedMethodCall(called, instance);
    }
    else { 
      HANDLE thread_id = (HANDLE)_beginthreadex(NULL, 0, CompileMethod, called, 0, NULL);
      if(!thread_id) {
        cerr << ">>> Unable to create thread to compile method! <<<" << endl;
        exit(-1);
      }
      program->AddThread(thread_id);
      ProcessInterpretedMethodCall(called, instance);
    }
#else
    // 
    // Linux and OS X: lock this section while we compile...
    //
    if(pthread_mutex_trylock(&called->jit_mutex)) {
      ProcessInterpretedMethodCall(called, instance);
    }
    else {     
      pthread_attr_t attrs;
      pthread_attr_init(&attrs);
      pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_JOINABLE);

      pthread_t jit_thread;
      if(pthread_create(&jit_thread, &attrs, CompileMethod, (void*)called)) {
        cerr << ">>> Unable to create thread to compile method! <<<" << endl;
        exit(-1);
      }
      program->AddThread(jit_thread);
      pthread_attr_destroy(&attrs);
      //      pthread_detach(jit_thread);

      // execute code in parallel
      ProcessInterpretedMethodCall(called, instance);
    }
#endif
#endif
  }
#endif
}

/********************************
 * Processes an interpreted
 * synchronous method call.
 ********************************/
void StackInterpreter::ProcessInterpretedMethodCall(StackMethod* called, long* instance)
{
#ifdef _DEBUG
  cout << "=== MTHD_CALL: id=" << called->GetClass()->GetId() << ","
       << called->GetId() << "; name='" << called->GetName() << "' ===" << endl;
#endif
  frame = new StackFrame(called, instance);
  instrs = frame->GetMethod()->GetInstructions();
  ip = 0;
#ifdef _DEBUG
  cout << "creating frame=" << frame << endl;
#endif
  MemoryManager::Instance()->AddPdaMethodRoot(frame);
}

/********************************
 * Processes a load integer array
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessLoadIntArrayElement(StackInstr* instr)
{
#ifdef _DEBUG
  cout << "stack oper: LOAD_INT_ARY_ELM; call_pos=" << call_stack_pos << endl;
#endif
  long* array = (long*)POP_INT();
  if(!array) {
    cerr << ">>> Atempting to dereference a 'Nil' memory element <<<" << endl;
    StackErrorUnwind();
    exit(1);
  }
  const long size = array[0];
  array += 2;
  long index = ArrayIndex(instr, array, size);
  PUSH_INT(array[index + instr->GetOperand()]);
}

/********************************
 * Processes a load store array
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessStoreIntArrayElement(StackInstr* instr)
{
#ifdef _DEBUG
  cout << "stack oper: STOR_INT_ARY_ELM; call_pos=" << call_stack_pos << endl;
#endif
  long* array = (long*)POP_INT();

  const long size = array[0];
  array += 2;
  long index = ArrayIndex(instr, array, size);
  array[index + instr->GetOperand()] = POP_INT();
}

/********************************
 * Processes a load byte array
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessLoadByteArrayElement(StackInstr* instr)
{
#ifdef _DEBUG
  cout << "stack oper: LOAD_BYTE_ARY_ELM; call_pos=" << call_stack_pos << endl;
#endif
  long* array = (long*)POP_INT();
  if(!array) {
    cerr << ">>> Atempting to dereference a 'Nil' memory element <<<" << endl;
    StackErrorUnwind();
    exit(1);
  }
  const long size = array[0];
  array += 2;
  long index = ArrayIndex(instr, array, size);
  array += instr->GetOperand();
  PUSH_INT(((BYTE_VALUE*)array)[index]);
}

/********************************
 * Processes a store byte array
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessStoreByteArrayElement(StackInstr* instr)
{
#ifdef _DEBUG
  cout << "stack oper: STOR_BYTE_ARY_ELM; call_pos=" << call_stack_pos << endl;
#endif
  long* array = (long*)POP_INT();
  if(!array) {
    cerr << ">>> Atempting to dereference a 'Nil' memory element <<<" << endl;
    StackErrorUnwind();
    exit(1);
  }
  const long size = array[0];
  array += 2;
  long index = ArrayIndex(instr, array, size);
  array += instr->GetOperand();
  ((BYTE_VALUE*)array)[index] = (BYTE_VALUE)POP_INT();
}

/********************************
 * Processes a load float array
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessLoadFloatArrayElement(StackInstr* instr)
{
#ifdef _DEBUG
  cout << "stack oper: LOAD_FLOAT_ARY_ELM; call_pos=" << call_stack_pos << endl;
#endif
  long* array = (long*)POP_INT();
  if(!array) {
    cerr << ">>> Atempting to dereference a 'Nil' memory element <<<" << endl;
    StackErrorUnwind();
    exit(1);
  }
  const long size = array[0];
  array += 2;
  long index = ArrayIndex(instr, array, size);
  FLOAT_VALUE value;
  memcpy(&value, array + index + instr->GetOperand(), sizeof(FLOAT_VALUE));
  PushFloat(value);
}

/********************************
 * Processes a store float array
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessStoreFloatArrayElement(StackInstr* instr)
{
#ifdef _DEBUG
  cout << "stack oper: STOR_FLOAT_ARY_ELM; call_pos=" << call_stack_pos << endl;
#endif
  long* array = (long*)POP_INT();
  if(!array) {
    cerr << ">>> Atempting to dereference a 'Nil' memory element <<<" << endl;
    StackErrorUnwind();
    exit(1);
  }
  const long size = array[0];
  array += 2;
  long index = ArrayIndex(instr, array, size);
  FLOAT_VALUE value = PopFloat();
  memcpy(array + index + instr->GetOperand(), &value, sizeof(FLOAT_VALUE));
}

/********************************
 * Shared library operations
 ********************************/

typedef void (*ext_load_def)();
void StackInterpreter::ProcessDllLoad(StackInstr* instr)
{
#ifdef _DEBUG
  cout << "stack oper: shared library_LOAD; call_pos=" << call_stack_pos << endl;
#endif
  long* instance = (long*)frame->GetMemory()[0];
  if(!instance) {
    cerr << ">>> Unable to load shared library! <<<" << endl;
    exit(1);
  }

  long* str_obj = (long*)instance[0];
  if(!str_obj && !(long*)str_obj[0]) {
    cerr << ">>> Name of runtime shared library was not specified! <<<" << endl;
    exit(1);
  }

  long* array = (long*)str_obj[0];
  const char* str = (char*)(array + 3);
  string dll_string(str);
  if(dll_string.size() == 0) {
    cerr << ">>> Name of runtime shared library was not specified! <<<" << endl;
    exit(1);
  }

#ifdef _MINGW
  dll_string += ".so";
#elif _WIN32
  dll_string += ".dll";  
#elif _OSX
  dll_string += ".dylib";
#else
  dll_string += ".so";
#endif 

  // load shared library
#ifdef _WIN32
  // Load shared library file
  HINSTANCE dll_handle = LoadLibrary(dll_string.c_str());
  if(!dll_handle) {
    cerr << ">>> Runtime error loading shared library: " << dll_string.c_str() << " <<<" << endl;
    exit(1);
  }
  instance[1] = (long)dll_handle;

  // call load function
  ext_load_def ext_load = (ext_load_def)GetProcAddress(dll_handle, "load_lib");
  if(!ext_load) {
    cerr << ">>> Runtime error calling function: load_lib <<<" << endl;
    FreeLibrary(dll_handle);
    exit(1);
  }
  (*ext_load)();
#else
  void* dll_handle = dlopen(dll_string.c_str(), RTLD_LAZY);
  if(!dll_handle) {
    cerr << ">>> Runtime error loading shared library: " << dlerror() << " <<<" << endl;
    exit(1);
  }
  instance[1] = (long)dll_handle;

  // call load function
  ext_load_def ext_load = (ext_load_def)dlsym(dll_handle, "load_lib");
  char* error;
  if((error = dlerror()) != NULL)  {
    cerr << ">>> Runtime error calling function: " << error << " <<<" << endl;
    exit(1);
  }
  // call function
  (*ext_load)();
#endif
}

typedef void (*ext_unload_def)();
void StackInterpreter::ProcessDllUnload(StackInstr* instr)
{
#ifdef _DEBUG
  cout << "stack oper: shared library_UNLOAD; call_pos=" << call_stack_pos << endl;
#endif
  long* instance = (long*)frame->GetMemory()[0];
  // unload shared library
#ifdef _WIN32
  HINSTANCE dll_handle = (HINSTANCE)instance[1];
  if(dll_handle) {
    // call unload function  
    ext_load_def ext_unload = (ext_load_def)GetProcAddress(dll_handle, "unload_lib");
    if(!ext_unload) {
      cerr << ">>> Runtime error calling function: unload_lib <<<" << endl;
      FreeLibrary(dll_handle);
      exit(1);
    }
    (*ext_unload)();
    // free handle
    FreeLibrary(dll_handle);
  }
#else
  void* dll_handle = (void*)instance[1];
  if(dll_handle) {
    // call unload function
    ext_unload_def ext_unload = (ext_unload_def)dlsym(dll_handle, "unload_lib");
    char* error;
    if((error = dlerror()) != NULL)  {
      cerr << ">>> Runtime error calling function: " << error << " <<<" << endl;
      exit(1);
    }
    // call function
    (*ext_unload)();
    // unload lib
    dlclose(dll_handle);
  }
#endif
}

typedef void (*lib_func_def) (VMContext& callbacks);
void StackInterpreter::ProcessDllCall(StackInstr* instr)
{
#ifdef _DEBUG
  cout << "stack oper: shared library_FUNC_CALL; call_pos=" << call_stack_pos << endl;
#endif 
  long* instance = (long*)frame->GetMemory()[0];
  long* str_obj = (long*)frame->GetMemory()[1];
  long* array = (long*)str_obj[0];
  if(!array) {
    cerr << ">>> Runtime error calling function <<<" << endl;
    exit(1);
  }

  const char* str = (char*)(array + 3);
  long* args = (long*)frame->GetMemory()[2];
  lib_func_def ext_func;

#ifdef _WIN32
  HINSTANCE dll_handle = (HINSTANCE)instance[1];
  if(dll_handle) {
    // get function pointer
    ext_func = (lib_func_def)GetProcAddress(dll_handle, str);
    if(!ext_func) {
      cerr << ">>> Runtime error calling function: " << str << " <<<" << endl;
      FreeLibrary(dll_handle);
      exit(1);
    }
    // call function
    VMContext context;
    context.data_array = args;
    context.op_stack = op_stack;
    context.stack_pos = stack_pos;
    context.call_method_by_name = APITools_MethodCall;
    context.call_method_by_id = APITools_MethodCallId;
    context.alloc_array = MemoryManager::AllocateArray;
    context.alloc_obj = MemoryManager::AllocateObject;
    (*ext_func)(context);
  }
#else
  // load function
  void* dll_handle = (void*)instance[1];
  if(dll_handle) {
    ext_func = (lib_func_def)dlsym(dll_handle, str);
    char* error;
    if((error = dlerror()) != NULL)  {
      cerr << ">>> Runtime error calling function: " << error << " <<<" << endl;
      exit(1);
    }
    // call function
    VMContext context;
    context.data_array = args;
    context.op_stack = op_stack;
    context.stack_pos = stack_pos;
    context.call_method_by_name = APITools_MethodCall;
    context.call_method_by_id = APITools_MethodCallId;
    context.alloc_array = MemoryManager::AllocateArray;
    context.alloc_obj = MemoryManager::AllocateObject;
    (*ext_func)(context);
  }  
#endif
}

/********************************
 * Processes trap instruction
 ********************************/
void StackInterpreter::ProcessTrap(StackInstr* instr)
{
  switch(POP_INT()) {
  case LOAD_CLS_INST_ID: {
    long* obj = (long*)POP_INT();
    PUSH_INT(MemoryManager::GetObjectID(obj));
  }
    break;

  case LOAD_NEW_OBJ_INST: {
    long* array = (long*)POP_INT();
    if(array) {
      array = (long*)array[0];
      const char* name = (char*)(array + 3);

#ifdef _DEBUG
      cout << "stack oper: LOAD_NEW_OBJ_INST; call_pos=" << call_stack_pos 
	   << ", name='" << name << "'"  << endl;
#endif
      CreateNewObject(name);
    }
    else {
      PUSH_INT(0);
    }    
  }
    break;

  case LOAD_CLS_BY_INST: {
#ifdef _DEBUG
    cout << "stack oper: LOAD_CLS_BY_INST; call_pos=" << call_stack_pos << endl;
#endif

    long* inst = (long*)frame->GetMemory()[0];
    StackClass* cls = MemoryManager::GetClass(inst);
    if(!cls) {
      cerr << ">>> Internal error: looking up class instance " << inst << " <<<" << endl;
      StackErrorUnwind();
      exit(1);
    }
    /// set name and create 'Class' instance
    long* cls_obj = MemoryManager::AllocateObject(program->GetClassObjectId(),
						  (long*)op_stack, *stack_pos, false);
    cls_obj[0] = (long)CreateStringObject(cls->GetName());
    frame->GetMemory()[1] = (long)cls_obj;
    CreateClassObject(cls, cls_obj);
  }
    break;

  case LOAD_ARY_SIZE: {
    long* array = (long*)POP_INT();
    if(!array) {
      cerr << ">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
      StackErrorUnwind();
      exit(1);
    }
    PUSH_INT(array[2]);
  }
    break;

  case CPY_CHAR_STR_ARY: {
    long index = POP_INT();
    BYTE_VALUE* value_str = program->GetCharStrings()[index];
    // copy array
    long* array = (long*)POP_INT();
    if(!array) {
      cerr << ">>> Atempting to dereference a 'Nil' memory element <<<" << endl;
      StackErrorUnwind();
      exit(1);
    }
    const long size = array[2];
    BYTE_VALUE* str = (BYTE_VALUE*)(array + 3);
    memcpy(str, value_str, size);
#ifdef _DEBUG
    cout << "stack oper: CPY_CHAR_STR_ARY: index=" << index << ", string='" << value_str << "'" << endl;
#endif
    PUSH_INT((long)array);
  }
    break;

  case CPY_CHAR_STR_ARYS: {
    // copy array
    long* array = (long*)POP_INT();
    if(!array) {
      cerr << ">>> Atempting to dereference a 'Nil' memory element <<<" << endl;
      StackErrorUnwind();
      exit(1);
    }
    const long size = array[0];
    const long dim = array[1];
    // copy elements
    long* str = (long*)(array + dim + 2);
    for(long i = 0; i < size; i++) {
      str[i] = POP_INT();
    }
#ifdef _DEBUG
    cout << "stack oper: CPY_CHAR_STR_ARYS" << endl;
#endif
    PUSH_INT((long)array);
  }
    break;

  case CPY_INT_STR_ARY: {
    long index = POP_INT();
    int* value_str = program->GetIntStrings()[index];
    // copy array
    long* array = (long*)POP_INT();
    if(!array) {
      cerr << ">>> Atempting to dereference a 'Nil' memory element <<<" << endl;
      StackErrorUnwind();
      exit(1);
    }
    const long size = array[0];
    const long dim = array[1];    
    long* str = (long*)(array + dim + 2);
    for(long i = 0; i < size; i++) {
      str[i] = value_str[i];
    }
#ifdef _DEBUG
    cout << "stack oper: CPY_INT_STR_ARY" << endl;
#endif
    PUSH_INT((long)array);
  }
    break;

  case CPY_FLOAT_STR_ARY: {
    long index = POP_INT();
    FLOAT_VALUE* value_str = program->GetFloatStrings()[index];
    // copy array
    long* array = (long*)POP_INT();
    if(!array) {
      cerr << ">>> Atempting to dereference a 'Nil' memory element <<<" << endl;
      StackErrorUnwind();
      exit(1);
    }
    const long size = array[0];
    const long dim = array[1];    
    FLOAT_VALUE* str = (FLOAT_VALUE*)(array + dim + 2);
    for(long i = 0; i < size; i++) {
      str[i] = value_str[i];
    }

#ifdef _DEBUG
    cout << "stack oper: CPY_FLOAT_STR_ARY" << endl;
#endif
    PUSH_INT((long)array);
  }
    break;

    // ---------------- standard i/o ----------------
  case STD_OUT_BOOL:
    cout << ((POP_INT() == 0) ? "false" : "true");
    break;

  case STD_OUT_BYTE:
    cout << (unsigned char)POP_INT();
    break;

  case STD_OUT_CHAR:
    cout << (char)POP_INT();
    break;

  case STD_OUT_INT:
    cout << POP_INT();
    break;

  case STD_OUT_FLOAT:
    cout << PopFloat();
    break;

  case STD_OUT_CHAR_ARY: {
    long* array = (long*)POP_INT();
    if(array) {
      char* str = (char*)(array + 3);
      cout << str;
    }
  }
    break;

  case STD_OUT_BYTE_ARY: {
    long* array = (long*)POP_INT();
    const long num = POP_INT();
    const long offset = POP_INT();

    if(array && offset >= 0 && offset + num < array[0]) {
      char* buffer = (char*)(array + 3);
      cout.write(buffer + offset, num);
      PUSH_INT(1);
    } 
    else {
      PUSH_INT(0);
    }
  }
    break;

  case STD_IN_STRING: {
    long* array = (long*)POP_INT();
    if(array) {
      char* buffer = (char*)(array + 3);
      const long num = array[0];
      cin.getline(buffer, num);
    }
  }
    break;

    // ---------------- runtime ----------------
  case EXIT:
    exit(POP_INT());
    break;

  case GMT_TIME:
    ProcessCurrentTime(true);
    break;

  case SYS_TIME:
    ProcessCurrentTime(false);
    break;

  case DATE_TIME_SET_1:
    ProcessSetTime1();
    break;

  case DATE_TIME_SET_2:
    ProcessSetTime2();
    break;

    /* TODO
       case DATE_TIME_SET_3:
       ProcessSetTime3();
       break;
    */

  case DATE_TIME_ADD_DAYS:
    ProcessAddTime(DAY_TIME);
    break;

  case DATE_TIME_ADD_HOURS:
    ProcessAddTime(HOUR_TIME);
    break;

  case DATE_TIME_ADD_MINS:
    ProcessAddTime(MIN_TIME);
    break;

  case DATE_TIME_ADD_SECS:
    ProcessAddTime(SEC_TIME);
    break;

  case PLTFRM:
    ProcessPlatform();
    break;

    // ---------------- ip socket i/o ----------------
  case SOCK_TCP_HOST_NAME: {
    long* array = (long*)POP_INT();
    if(array) {    
      const long size = array[0];
      BYTE_VALUE* str = (BYTE_VALUE*)(array + 3);

      // get host name
      char value_str[SMALL_BUFFER_MAX + 1];    
      if(!gethostname(value_str, SMALL_BUFFER_MAX)) {
        // copy name    
        for(long i = 0; value_str[i] != '\0' && i < size; i++) {
          str[i] = value_str[i];
        }
      }
      else {
        str[0] = '\0';
      }
#ifdef _DEBUG
      cout << "stack oper: SOCK_TCP_HOST_NAME: host='" << value_str << "'" << endl;
#endif
      PUSH_INT((long)array);
    }
    else {
      PUSH_INT(0);
    }
  }
    break;

  case SOCK_TCP_CONNECT: {
    long port = POP_INT();
    long* array = (long*)POP_INT();
    long* instance = (long*)POP_INT();
    if(array && instance) {
      array = (long*)array[0];
      const char* addr = (char*)(array + 3);
      SOCKET sock = IPSocket::Open(addr, port);
#ifdef _DEBUG
      cout << "# socket connect: addr='" << addr << ":" << port << "'; instance=" 
	   << instance << "(" << (long)instance << ")" << "; addr=" << sock << "(" 
	   << (long)sock << ") #" << endl;
#endif
      instance[0] = (long)sock;
    }
  }
    break;  

  case SOCK_TCP_BIND: {
    long port = POP_INT();
    long* instance = (long*)POP_INT();
    if(instance) {
      SOCKET server = IPSocket::Bind(port);
#ifdef _DEBUG
      cout << "# socket bind: port=" << port << "; instance=" << instance << "(" 
	   << (long)instance << ")" << "; addr=" << server << "(" << (long)server 
	   << ") #" << endl;
#endif
      instance[0] = (long)server;
    }
  }
    break;  

  case SOCK_TCP_LISTEN: {
    long backlog = POP_INT();
    long* instance = (long*)POP_INT();

    if(instance && (SOCKET)instance[0] >= 0) {
      SOCKET server = (SOCKET)instance[0];
#ifdef _DEBUG
      cout << "# socket listen: backlog=" << backlog << "'; instance=" << instance 
	   << "(" << (long)instance << ")" << "; addr=" << server << "(" 
	   << (long)server << ") #" << endl;
#endif
      if(IPSocket::Listen(server, backlog)) {
	PUSH_INT(1);      
      }
      else {
	PUSH_INT(0);
      }
    }
    else {
      PUSH_INT(0);
    }
  }
    break;   
    
  case SOCK_TCP_ACCEPT: {
    long* instance = (long*)POP_INT();
    if(instance && (SOCKET)instance[0] >= 0) {
      SOCKET server = (SOCKET)instance[0];
      char client_address[SMALL_BUFFER_MAX + 1];
      int client_port;
      SOCKET client = IPSocket::Accept(server, client_address, client_port);
#ifdef _DEBUG
      cout << "# socket accept: instance=" << instance << "(" << (long)instance << ")" << "; ip=" 
	   << client_address << "; port=" << client_port << "; addr=" << server << "(" 
	   << (long)server << ") #" << endl;
#endif

      long* sock_obj = MemoryManager::AllocateObject(program->GetSocketObjectId(),
						     (long*)op_stack, *stack_pos, false);
      sock_obj[0] = client;
      sock_obj[1] = (long)CreateStringObject(client_address);
      sock_obj[2] = client_port;

      PUSH_INT((long)sock_obj);
    }
  }
    break;

  case SOCK_TCP_CLOSE: {
    long* instance = (long*)POP_INT();
    if(instance && (SOCKET)instance[0] >= 0) {
      SOCKET sock = (SOCKET)instance[0];

#ifdef _DEBUG
      cout << "# socket close: addr=" << sock << "(" << (long)sock << ") #" << endl;
#endif
    
      instance[0] = 0;
      IPSocket::Close(sock);
    }
                    
  }
    break;

  case SOCK_TCP_OUT_STRING: {
    long* array = (long*)POP_INT();
    long* instance = (long*)POP_INT();
    if(array && instance) {
      SOCKET sock = (SOCKET)instance[0];
      char* data = (char*)(array + 3);      
      if(sock >= 0) {
        IPSocket::WriteBytes(data, strlen(data), sock);
      }
    }
  }
    break;

  case SOCK_TCP_IN_STRING: {
    long* array = (long*)POP_INT();
    long* instance = (long*)POP_INT();
    if(array && instance) {
      char* buffer = (char*)(array + 3);
      const long num = array[0] - 1;
      SOCKET sock = (SOCKET)instance[0];

      int status;
      if(sock >= 0) {
	int index = 0;
	BYTE_VALUE value;
	bool end_line = false;
	do {
	  value = IPSocket::ReadByte(sock, status);
	  if(value != '\r' && value != '\n' && index < num && status > 0) {
	    buffer[index++] = value;
	  }
	  else {
	    end_line = true;
	  }
	}
	while(!end_line);

	// assume LF
	if(value == '\r') {
	  IPSocket::ReadByte(sock, status);
	}
      }
    }
  }
    break;

    // ---------------- serialization ----------------
  case SERL_INT:
#ifdef _DEBUG
    cout << "# serializing int #" << endl;
#endif
    SerializeInt(INT_PARM);
    SerializeInt(frame->GetMemory()[1]);
    break;

  case SERL_FLOAT: {
#ifdef _DEBUG
    cout << "# serializing float #" << endl;
#endif
    SerializeInt(FLOAT_PARM);
    FLOAT_VALUE value;
    memcpy(&value, &(frame->GetMemory()[1]), sizeof(value));
    SerializeFloat(value);
  }
    break;

  case SERL_OBJ_INST:
    SerializeObject();
    break;

  case SERL_BYTE_ARY:
    SerializeInt(BYTE_ARY_PARM);
    SerializeArray((long*)frame->GetMemory()[1], BYTE_ARY_PARM);
    break;

  case SERL_INT_ARY:
    SerializeInt(INT_ARY_PARM);
    SerializeArray((long*)frame->GetMemory()[1], INT_ARY_PARM);
    break;

  case SERL_FLOAT_ARY:
    SerializeInt(FLOAT_ARY_PARM);
    SerializeArray((long*)frame->GetMemory()[1], FLOAT_ARY_PARM);
    break;

  case DESERL_INT:
#ifdef _DEBUG
    cout << "# deserializing int #" << endl;
#endif
    if(INT_PARM == (ParamType)DeserializeInt()) {
      PUSH_INT(DeserializeInt());
    }
    else {
      PUSH_INT(0);
    }
    break;

  case DESERL_FLOAT:
#ifdef _DEBUG
    cout << "# deserializing float #" << endl;
#endif
    if(FLOAT_PARM == (ParamType)DeserializeInt()) {
      PushFloat(DeserializeFloat());
    }
    else {
      PushFloat(0.0);
    }
    break;

  case DESERL_OBJ_INST:
    DeserializeObject();
    break;

  case DESERL_BYTE_ARY:
#ifdef _DEBUG
    cout << "# deserializing byte array #" << endl;
#endif
    if(BYTE_ARY_PARM == (ParamType)DeserializeInt()) {
      PUSH_INT((long)DeserializeArray(BYTE_ARY_PARM));
    }
    else {
      PUSH_INT(0);
    }
    break;

  case DESERL_INT_ARY:
#ifdef _DEBUG
    cout << "# deserializing int array #" << endl;
#endif
    if(INT_ARY_PARM == (ParamType)DeserializeInt()) {
      PUSH_INT((long)DeserializeArray(INT_ARY_PARM));
    }
    else {
      PUSH_INT(0);
    }
    break;

  case DESERL_FLOAT_ARY:
#ifdef _DEBUG
    cout << "# deserializing float array #" << endl;
#endif
    if(FLOAT_ARY_PARM == (ParamType)DeserializeInt()) {
      PUSH_INT((long)DeserializeArray(FLOAT_ARY_PARM));
    }
    else {
      PUSH_INT(0);
    }
    break;

    // ---------------- file i/o ----------------
  case FILE_OPEN_READ: {
    long* array = (long*)POP_INT();
    long* instance = (long*)POP_INT();
    if(array && instance) {
      array = (long*)array[0];
      const char* name = (char*)(array + 3);
      FILE* file = File::FileOpen(name, "rb");
#ifdef _DEBUG
      cout << "# file open: name='" << name << "'; instance=" << instance << "(" 
	   << (long)instance << ")" << "; addr=" << file << "(" << (long)file 
	   << ") #" << endl;
#endif
      instance[0] = (long)file;
    }
  }
    break;

  case FILE_OPEN_WRITE: {
    long* array = (long*)POP_INT();
    long* instance = (long*)POP_INT();
    if(array && instance) {
      array = (long*)array[0];
      const char* name = (char*)(array + 3);
      FILE* file = File::FileOpen(name, "wb");
#ifdef _DEBUG
      cout << "# file open: name='" << name << "'; instance=" << instance << "(" 
	   << (long)instance << ")" << "; addr=" << file << "(" << (long)file 
	   << ") #" << endl;
#endif
      instance[0] = (long)file;
    }
  }
    break;

  case FILE_OPEN_READ_WRITE: {
    long* array = (long*)POP_INT();
    long* instance = (long*)POP_INT();
    if(array && instance) {
      array = (long*)array[0];
      const char* name = (char*)(array + 3);
      FILE* file = File::FileOpen(name, "w+b");
#ifdef _DEBUG
      cout << "# file open: name='" << name << "'; instance=" << instance << "(" 
	   << (long)instance << ")" << "; addr=" << file << "(" << (long)file 
	   << ") #" << endl;
#endif
      instance[0] = (long)file;
    }
  }
    break;

  case FILE_CLOSE: {
    long* instance = (long*)POP_INT();
    if(instance && (FILE*)instance[0]) {
      FILE* file = (FILE*)instance[0];
#ifdef _DEBUG
      cout << "# file close: addr=" << file << "(" << (long)file << ") #" << endl;
#endif
      instance[0] = 0;
      fclose(file);
    }
  }
    break;

  case FILE_FLUSH: {
    long* instance = (long*)POP_INT();
    if(instance && (FILE*)instance[0]) {
      FILE* file = (FILE*)instance[0];

#ifdef _DEBUG
      cout << "# file close: addr=" << file << "(" << (long)file << ") #" << endl;
#endif
      instance[0] = 0;
      fflush(file);
    }
  }
    break;

  case FILE_IN_STRING: {
    long* array = (long*)POP_INT();
    long* instance = (long*)POP_INT();
    if(array && instance) {
      char* buffer = (char*)(array + 3);
      const long num = array[0] - 1;
      FILE* file = (FILE*)instance[0];

      if(file && fgets(buffer, num, file)) {
        long end_index = strlen(buffer) - 1;
        if(end_index >= 0) {
          if(buffer[end_index] == '\n') {
            buffer[end_index] = '\0';
          }
        }
        else {
          buffer[0] = '\0';
        }
      }
    }
  }
    break;

  case FILE_OUT_STRING: {
    long* array = (long*)POP_INT();
    long* instance = (long*)POP_INT();    
    if(array && instance) {
      FILE* file = (FILE*)instance[0];
      const char* data = (char*)(array + 3);      
      if(file) {
        fputs(data, file);
      }
    }
  }
    break;

  case FILE_REWIND: {
    long* instance = (long*)POP_INT();
    if(instance && (FILE*)instance[0]) {
      FILE* file = (FILE*)instance[0];
      rewind(file);
    }
  }
    break;

    // --- TRAP_RTRN --- //

    // ---------------- socket i/o ----------------
  case SOCK_TCP_IS_CONNECTED: {
    long* instance = (long*)POP_INT();
    if(instance && (SOCKET)instance[0] >= 0) {
      PUSH_INT(1);
    } 
    else {
      PUSH_INT(0);
    }
  }
    break;

  case SOCK_TCP_IN_BYTE: {
    long* instance = (long*)POP_INT();
    if(instance) {
      SOCKET sock = (SOCKET)instance[0];
      int status;
      PUSH_INT(IPSocket::ReadByte(sock, status));
    }
    else {
      PUSH_INT(0);
    }
  }
    break;

  case SOCK_TCP_IN_BYTE_ARY: {
    long* array = (long*)POP_INT();
    const long num = POP_INT();
    const long offset = POP_INT();
    long* instance = (long*)POP_INT();
    
    if(array && instance && (SOCKET)instance[0] >= 0 && offset + num < array[0]) {
      SOCKET sock = (SOCKET)instance[0];
      char* buffer = (char*)(array + 3);
      PUSH_INT(IPSocket::ReadBytes(buffer + offset, num, sock));
    }
    else {
      PUSH_INT(-1);
    }
  }
    break;

  case SOCK_TCP_OUT_BYTE: {
    long value = POP_INT();
    long* instance = (long*)POP_INT();
    if(instance) {
      SOCKET sock = (SOCKET)instance[0];
      IPSocket::WriteByte((char)value, sock);
      PUSH_INT(1);
    }
    else {
      PUSH_INT(0);
    }
  }
    break;

  case SOCK_TCP_OUT_BYTE_ARY: {
    long* array = (long*)POP_INT();
    const long num = POP_INT();
    const long offset = POP_INT();
    long* instance = (long*)POP_INT();
    
    if(array && instance && (SOCKET)instance[0] >= 0 && offset + num < array[0]) {
      SOCKET sock = (SOCKET)instance[0];
      char* buffer = (char*)(array + 3);
      PUSH_INT(IPSocket::WriteBytes(buffer + offset, num, sock));
    } 
    else {
      PUSH_INT(-1);
    }
  } 
    break;

    // -------------- file i/o -----------------
  case FILE_IN_BYTE: {
    long* instance = (long*)POP_INT();
    if((FILE*)instance[0]) {
      FILE* file = (FILE*)instance[0];
      if(fgetc(file) == EOF) {
        PUSH_INT(0);
      } 
      else {
        PUSH_INT(1);
      }
    } 
    else {
      PUSH_INT(0);
    }
  }
    break;

  case FILE_IN_BYTE_ARY: {
    long* array = (long*)POP_INT();
    const long num = POP_INT();
    const long offset = POP_INT();
    long* instance = (long*)POP_INT();
    
    if(array && instance && (FILE*)instance[0] && offset >= 0 && offset + num < array[0]) {
      FILE* file = (FILE*)instance[0];
      char* buffer = (char*)(array + 3);
      PUSH_INT(fread(buffer + offset, 1, num, file));        
    } 
    else {
      PUSH_INT(-1);
    }
  }
    break;

  case FILE_OUT_BYTE: {
    long value = POP_INT();
    long* instance = (long*)POP_INT();
    
    if(instance && (FILE*)instance[0]) {
      FILE* file = (FILE*)instance[0];
      if(fputc(value, file) != value) {
        PUSH_INT(0);
      } 
      else {
        PUSH_INT(1);
      }

    } 
    else {
      PUSH_INT(0);
    }
  }
    break;

  case FILE_OUT_BYTE_ARY: {
    long* array = (long*)POP_INT();
    const long num = POP_INT();
    const long offset = POP_INT();
    long* instance = (long*)POP_INT();
    
    if(array && instance && (FILE*)instance[0] && offset >=0 && offset + num < array[0]) {
      FILE* file = (FILE*)instance[0];
      char* buffer = (char*)(array + 3);
      PUSH_INT(fwrite(buffer + offset, 1, num, file));
    } 
    else {
      PUSH_INT(-1);
    }
  }
    break;

  case FILE_SEEK: {
    long pos = POP_INT();
    long* instance = (long*)POP_INT();
    
    if(instance && (FILE*)instance[0]) {
      FILE* file = (FILE*)instance[0];
      if(fseek(file, pos, SEEK_CUR) != 0) {
        PUSH_INT(0);
      } 
      else {
        PUSH_INT(1);
      }
    } 
    else {
      PUSH_INT(0);
    }
  }
    break;

  case FILE_EOF: {
    long* instance = (long*)POP_INT();
    if(instance && (FILE*)instance[0]) {
      FILE* file = (FILE*)instance[0];
      PUSH_INT(feof(file) != 0);
    } 
    else {
      PUSH_INT(1);
    }
  }
    break;

  case FILE_IS_OPEN: {
    long* instance = (long*)POP_INT();
    if(instance && (FILE*)instance[0]) {
      PUSH_INT(1);
    } 
    else {
      PUSH_INT(0);
    }
  }
    break;

  case FILE_EXISTS: {
    long* array = (long*)POP_INT();
    if(array) {
      array = (long*)array[0];
      const char* name = (char*)(array + 3);
      PUSH_INT(File::FileExists(name));
    }
    else {
      PUSH_INT(0);
    }
  }
    break;

  case FILE_SIZE: {
    long* array = (long*)POP_INT();
    if(array) {
      array = (long*)array[0];
      const char* name = (char*)(array + 3);
      PUSH_INT(File::FileSize(name));
    }
    else {
      PUSH_INT(-1);
    }
  }
    break;

  case FILE_DELETE: {
    long* array = (long*)POP_INT();
    if(array) {
      array = (long*)array[0];
      const char* name = (char*)(array + 3);

      if(remove(name) != 0) {
        PUSH_INT(0);
      } 
      else {
        PUSH_INT(1);
      }
    }
    else {
      PUSH_INT(0);
    }
  }
    break;

  case FILE_RENAME: {
    long* to = (long*)POP_INT();
    long* from = (long*)POP_INT();

    if(!to || !from) {
      PUSH_INT(0);
      return;
    }

    to = (long*)to[0];
    const char* to_name = (char*)(to + 3);

    from = (long*)from[0];
    const char* from_name = (char*)(from + 3);

    if(rename(from_name, to_name) != 0) {
      PUSH_INT(0);
    } else {
      PUSH_INT(1);
    }
  }
    break;

    //----------- directory functions -----------
  case DIR_CREATE: {
    long* array = (long*)POP_INT();
    if(array) {
      array = (long*)array[0];
      const char* name = (char*)(array + 3);      
      PUSH_INT(File::MakeDir(name));
    }
    else {
      PUSH_INT(0);
    }
  }
    break;

  case DIR_EXISTS: {
    long* array = (long*)POP_INT();
    if(array) {
      array = (long*)array[0];
      const char* name = (char*)(array + 3);      
      PUSH_INT(File::IsDir(name));
    }
    else {
      PUSH_INT(0);
    }
  }
    break;

  case DIR_LIST: {
    long* array = (long*)POP_INT();
    array = (long*)array[0];
    if(array) {
      char* name = (char*)(array + 3);

      vector<string> files = File::ListDir(name);

      // create 'System.String' object array
      const long str_obj_array_size = files.size();
      const long str_obj_array_dim = 1;
      long* str_obj_array = (long*)MemoryManager::AllocateArray(str_obj_array_size +
								str_obj_array_dim + 2,
								INT_TYPE, op_stack,
								*stack_pos, false);
      str_obj_array[0] = str_obj_array_size;
      str_obj_array[1] = str_obj_array_dim;
      str_obj_array[2] = str_obj_array_size;
      long* str_obj_array_ptr = str_obj_array + 3;

      // create and assign 'System.String' instances to array
      for(size_t i = 0; i < files.size(); i++) {
        str_obj_array_ptr[i] = (long)CreateStringObject(files[i]);
      }

      PUSH_INT((long)str_obj_array);
    }
    else {
      PUSH_INT(0);
    }
  }
    break;
  }
}

/********************************
 * Serializes an object graph
 ********************************/
void StackInterpreter::SerializeObject() {
  long* obj = (long*)frame->GetMemory()[1];
  ObjectSerializer serializer(obj);
  vector<BYTE_VALUE> src_buffer = serializer.GetValues();
  const long src_buffer_size = src_buffer.size();
  long* inst = (long*)frame->GetMemory()[0];
  long* dest_buffer = (long*)inst[0];
  long dest_pos = inst[1];

  // expand buffer, if needed
  dest_buffer = ExpandSerialBuffer(src_buffer_size, dest_buffer, inst);
  inst[0] = (long)dest_buffer;

  // copy content
  char* dest_buffer_ptr = ((char*)(dest_buffer + 3) + dest_pos);
  for(int i = 0; i < src_buffer_size; i++, dest_pos++) {
    dest_buffer_ptr[i] = src_buffer[i];
  }
  inst[1] = dest_pos;
}

/********************************
 * Deserializes an object graph
 ********************************/
void StackInterpreter::DeserializeObject() {
  if(!DeserializeByte()) {
    PUSH_INT(0);    
  }
  else {
    long* inst = (long*)frame->GetMemory()[0];
    long* byte_array = (long*)inst[0];
    const long dest_pos = inst[1];
    const long byte_array_dim_size = byte_array[2];  
    const BYTE_VALUE* byte_array_ptr = ((BYTE_VALUE*)(byte_array + 3) + dest_pos);

    ObjectDeserializer deserializer(byte_array_ptr, byte_array_dim_size, op_stack, stack_pos);
    PUSH_INT((long)deserializer.DeserializeObject());
    inst[1] = dest_pos + deserializer.GetOffset();
  }
}
