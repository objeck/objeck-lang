/***************************************************************************
 * VM stack machine.
 *
 * Copyright (c) 2008-2010 Randy Hollines
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

#include "interpreter.h"
#include "math.h"

#ifdef _X64
#include "jit/amd64/jit_intel_lp64.h"
#else
#include "jit/ia32/jit_intel_lp32.h"
#endif

#ifdef _WIN32
#include "os/windows/windows.h"
#else
#include "os/posix/posix.h"
#endif

using namespace Runtime;

StackProgram* StackInterpreter::program;

/********************************
 * JIT compiler thread
 ********************************/
#ifdef _WIN32
DWORD WINAPI StackInterpreter::CompileMethod(LPVOID arg)
{
  StackMethod* method = (StackMethod*)arg;
  Runtime::JitCompilerIA32 jit_compiler;
  if(!jit_compiler.Compile(method)) {
    exit(1);
  }

  return 0;
}
#else
void* StackInterpreter::CompileMethod(void* arg) 
{
  StackMethod* method = (StackMethod*)arg;
#ifdef _X64
  Runtime::JitCompilerIA64 jit_compiler;
#else
  Runtime::JitCompilerIA32 jit_compiler;
#endif
  jit_compiler.Compile(method);
  // clean up
  program->RemoveThread(pthread_self());
  pthread_exit(NULL);
}
#endif 

/********************************
 * VM initialization
 ********************************/
void StackInterpreter::Initialize(StackProgram* p)
{

#ifdef _X64
  JitCompilerIA64::Initialize(program);
#else
  JitCompilerIA32::Initialize(program);
#endif
  MemoryManager::Initialize(program);
}

/********************************
 * Main VM execution method
 ********************************/
void StackInterpreter::Execute(long* stack, long* pos, long i, StackMethod* method,
                               long* self, bool jit_called)
{
  // inital setup
  op_stack = stack;
  stack_pos = pos;
  call_stack_pos = 0;

  long* mem = method->NewMemory();
  mem[0] = (long)self;
  frame = new StackFrame(method, mem);
#ifdef _DEBUG
  cout << "creating frame=" << frame << endl;
#endif
  frame->SetJitCalled(jit_called);
  ip = i;

#ifdef _DEBUG
  cout << "\n---------- Executing Interpretered Code: method_id=" << frame->GetMethod()->GetId()
       << "; method_name='" << frame->GetMethod()->GetName() << "' ---------\n" << endl;
#endif

  // add frame
  if(!jit_called) {
    MemoryManager::Instance()->AddPdaMethodRoot(frame);
  }

  // execute
  Execute();
}

void StackInterpreter::Execute()
{
  // execute
  halt = false;
  while(!halt) {
    StackInstr* instr = frame->GetMethod()->GetInstruction(ip++);
    switch(instr->GetType()) {
    case STOR_INT_VAR:
      ProcessStoreInt(instr);
      break;

    case STOR_FLOAT_VAR:
      ProcessStoreFloat(instr);
      break;

    case COPY_INT_VAR:
      ProcessCopyInt(instr);
      break;

    case COPY_FLOAT_VAR:
      ProcessCopyFloat(instr);
      break;

    case LOAD_INT_LIT:
#ifdef _DEBUG
      cout << "stack oper: LOAD_INT_LIT; call_pos=" << call_stack_pos << endl;
#endif
      PushInt(instr->GetOperand());
      break;

    case SHL_INT: {
#ifdef _DEBUG
      cout << "stack oper: SHL_INT; call_pos=" << call_stack_pos << endl;
#endif
      long value = PopInt();
      PushInt(value << instr->GetOperand());
    }
    break;

    case SHR_INT: {
#ifdef _DEBUG
      cout << "stack oper: SHR_INT; call_pos=" << call_stack_pos << endl;
#endif
      long value = PopInt();
      PushInt(value >> instr->GetOperand());
    }
    break;

    case LOAD_FLOAT_LIT:
#ifdef _DEBUG
      cout << "stack oper: LOAD_FLOAT_LIT; call_pos=" << call_stack_pos << endl;
#endif
      PushFloat(instr->GetFloatOperand());
      break;

    case LOAD_INT_VAR:
      ProcessLoadInt(instr);
      break;

    case LOAD_FLOAT_VAR:
      ProcessLoadFloat(instr);
      break;

    case AND_INT: {
#ifdef _DEBUG
      cout << "stack oper: AND; call_pos=" << call_stack_pos << endl;
#endif
      long right = PopInt();
      long left = PopInt();
      PushInt(left && right);
    }
    break;

    case OR_INT: {
#ifdef _DEBUG
      cout << "stack oper: OR; call_pos=" << call_stack_pos << endl;
#endif
      long right = PopInt();
      long left = PopInt();
      PushInt(left || right);
    }
    break;

    case ADD_INT:
#ifdef _DEBUG
      cout << "stack oper: ADD; call_pos=" << call_stack_pos << endl;
#endif
      PushInt(PopInt() + PopInt());
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
      PushInt(PopInt() - PopInt());
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
      PushInt(PopInt() * PopInt());
      break;

    case DIV_INT:
#ifdef _DEBUG
      cout << "stack oper: DIV; call_pos=" << call_stack_pos << endl;
#endif
      PushInt(PopInt() / PopInt());
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
      PushInt(PopInt() % PopInt());
      break;

    case LES_EQL_INT:
#ifdef _DEBUG
      cout << "stack oper: LES_EQL; call_pos=" << call_stack_pos << endl;
#endif
      PushInt(PopInt() <= PopInt());
      break;

    case GTR_EQL_INT:
#ifdef _DEBUG
      cout << "stack oper: GTR_EQL; call_pos=" << call_stack_pos << endl;
#endif
      PushInt(PopInt() >= PopInt());
      break;

    case LES_EQL_FLOAT:
#ifdef _DEBUG
      cout << "stack oper: LES_EQL; call_pos=" << call_stack_pos << endl;
#endif
      PushInt(PopFloat() <= PopFloat());
      break;

    case GTR_EQL_FLOAT:
#ifdef _DEBUG
      cout << "stack oper: GTR_EQL; call_pos=" << call_stack_pos << endl;
#endif
      PushInt(PopFloat() >= PopFloat());
      break;

    case EQL_INT:
#ifdef _DEBUG
      cout << "stack oper: EQL; call_pos=" << call_stack_pos << endl;
#endif
      PushInt(PopInt() == PopInt());
      break;

    case NEQL_INT:
#ifdef _DEBUG
      cout << "stack oper: NEQL; call_pos=" << call_stack_pos << endl;
#endif
      PushInt(PopInt() != PopInt());
      break;

    case LES_INT:
#ifdef _DEBUG
      cout << "stack oper: LES; call_pos=" << call_stack_pos << endl;
#endif
      PushInt(PopInt() < PopInt());
      break;

    case GTR_INT:
#ifdef _DEBUG
      cout << "stack oper: GTR; call_pos=" << call_stack_pos << endl;
#endif
      PushInt(PopInt() > PopInt());
      break;

    case EQL_FLOAT:
#ifdef _DEBUG
      cout << "stack oper: EQL; call_pos=" << call_stack_pos << endl;
#endif
      PushInt(PopFloat() == PopFloat());
      break;

    case NEQL_FLOAT:
#ifdef _DEBUG
      cout << "stack oper: NEQL; call_pos=" << call_stack_pos << endl;
#endif
      PushInt(PopFloat() != PopFloat());
      break;

    case LES_FLOAT:
#ifdef _DEBUG
      cout << "stack oper: LES; call_pos=" << call_stack_pos << endl;
#endif
      PushInt(PopFloat() < PopFloat());
      break;

    case GTR_FLOAT:
#ifdef _DEBUG
      cout << "stack oper: GTR; call_pos=" << call_stack_pos << endl;
#endif
      PushInt(PopFloat() > PopFloat());
      break;

    case CEIL_FLOAT:
      PushFloat(ceil(PopFloat()));
      break;

    case FLOR_FLOAT:
      PushFloat(floor(PopFloat()));
      break;

    case I2F:
#ifdef _DEBUG
      cout << "stack oper: I2F; call_pos=" << call_stack_pos << endl;
#endif
      PushFloat(PopInt());
      break;

    case F2I:
#ifdef _DEBUG
      cout << "stack oper: F2I; call_pos=" << call_stack_pos << endl;
#endif
      PushInt((long)PopFloat());
      break;

    case POP_INT:
#ifdef _DEBUG
      cout << "stack oper: POP_INT; call_pos=" << call_stack_pos << endl;
#endif
      PopInt();
      break;

    case POP_FLOAT:
#ifdef _DEBUG
      cout << "stack oper: POP_FLOAT; call_pos=" << call_stack_pos << endl;
#endif
      PopFloat();
      break;

    case OBJ_INST_CAST: {
      long* mem = (long*)PopInt();
      long result = (long)MemoryManager::Instance()->ValidObjectCast(mem, instr->GetOperand(),
                    program->GetHierarchy());
      if(!result && mem) {
        StackClass* to_cls = MemoryManager::Instance()->GetClass((long*)mem);
        cerr << ">>> Invalid object cast: '" << (to_cls ? to_cls->GetName() : "?" )
             << "' to '" << program->GetClass(instr->GetOperand())->GetName() << "' <<<" << endl;
        StackErrorUnwind();
        exit(1);
      }
      PushInt(result);
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

    case MTHD_CALL:
      ProcessMethodCall(instr);
      // return directly back to JIT code
      if(frame->IsJitCalled()) {
        frame->SetJitCalled(false);
        return;
      }
      break;

    case ASYNC_MTHD_CALL:
      ProcessAsyncMethodCall(instr);
      // return directly back to JIT code
      if(frame->IsJitCalled()) {
        frame->SetJitCalled(false);
        return;
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
      PushInt((long)frame->GetMethod()->GetClass()->GetClassMemory());
      break;

    case LOAD_INST_MEM:
#ifdef _DEBUG
      cout << "stack oper: LOAD_INST_MEM; call_pos=" << call_stack_pos << endl;
#endif
      PushInt(frame->GetMemory()[0]);
      break;

    case TRAP:
    case TRAP_RTRN:
#ifdef _DEBUG
      cout << "stack oper: TRAP; call_pos=" << call_stack_pos << endl;
#endif
      ProcessTrap(instr);
      break;

    case THREAD_JOIN:
      // TODO: implement
      break;
      
    case THREAD_SLEEP:
      // TODO: implement
      break;

    case CRITICAL_START:
      // TODO: implement
      break;

    case CRITICAL_END:
      // TODO: implement
      break;
      
    case JMP:
#ifdef _DEBUG
      cout << "stack oper: JMP; call_pos=" << call_stack_pos << endl;
#endif
      if(instr->GetOperand2() < 0) {
        ip = frame->GetMethod()->GetLabelIndex(instr->GetOperand()) + 1;
      } else if(PopInt() == instr->GetOperand2()) {
        ip = frame->GetMethod()->GetLabelIndex(instr->GetOperand()) + 1;
      }
      break;
    }
  }
}

/********************************
 * Processes the current time
 ********************************/
void StackInterpreter::ProcessCurrentTime() {
  time_t raw_time;
  time (&raw_time);
  
  struct tm* local_time = localtime (&raw_time);
  long* time = (long*)frame->GetMemory()[0];
  time[0] = local_time->tm_mday; // day
  time[1] = local_time->tm_mon; // month
  time[2] = local_time->tm_year; // year
  time[3] = local_time->tm_hour; // hours
  time[4] = local_time->tm_min; // mins
  time[5] = local_time->tm_sec; // secs
  time[6] = local_time->tm_isdst > 0; // savings time
}

/********************************
 * Processes a load integer
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessLoadInt(StackInstr* instr)
{
#ifdef _DEBUG
  cout << "stack oper: LOAD_INT_VAR; index=" << instr->GetOperand()
       << "; local=" << ((instr->GetOperand2() == LOCL) ? "true" : "false") << endl;
#endif
  if(instr->GetOperand2() == LOCL) {
    long* mem = frame->GetMemory();
    PushInt(mem[instr->GetOperand() + 1]);
  } else {
    long* cls_inst_mem = (long*)PopInt();
    if(!cls_inst_mem) {
      cerr << "Atempting to dereference a 'Nil' memory instance" << endl;
      StackErrorUnwind();
      exit(1);
    }
    PushInt(cls_inst_mem[instr->GetOperand()]);
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
    long* cls_inst_mem = (long*)PopInt();
    if(!cls_inst_mem) {
      cerr << "Atempting to dereference a 'Nil' memory instance" << endl;
      StackErrorUnwind();
      exit(1);
    }
    memcpy(&value, &cls_inst_mem[instr->GetOperand()], sizeof(FLOAT_VALUE));
  }
  PushFloat(value);
}

/********************************
 * Processes a store integer
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessStoreInt(StackInstr* instr)
{
#ifdef _DEBUG
  cout << "stack oper: STOR_INT_VAR; index=" << instr->GetOperand()
       << "; local=" << ((instr->GetOperand2() == LOCL) ? "true" : "false") << endl;
#endif
  if(instr->GetOperand2() == LOCL) {
    long* mem = frame->GetMemory();
    mem[instr->GetOperand() + 1] = PopInt();
  } else {
    long* cls_inst_mem = (long*)PopInt();
    if(!cls_inst_mem) {
      cerr << "Atempting to dereference a 'Nil' memory instance" << endl;
      StackErrorUnwind();
      exit(1);
    }
    cls_inst_mem[instr->GetOperand()] = PopInt();
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
    long* cls_inst_mem = (long*)PopInt();
    if(!cls_inst_mem) {
      cerr << "Atempting to dereference a 'Nil' memory instance" << endl;
      StackErrorUnwind();
      exit(1);
    }
    FLOAT_VALUE value = PopFloat();
    memcpy(&cls_inst_mem[instr->GetOperand()], &value, sizeof(FLOAT_VALUE));
  }
}

/********************************
 * Processes a copy integer
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessCopyInt(StackInstr* instr)
{
#ifdef _DEBUG
  cout << "stack oper: COPY_INT_VAR; index=" << instr->GetOperand()
       << "; local=" << ((instr->GetOperand2() == LOCL) ? "true" : "false") << endl;
#endif
  if(instr->GetOperand2() == LOCL) {
    long* mem = frame->GetMemory();
    mem[instr->GetOperand() + 1] = TopInt();
  } else {
    long* cls_inst_mem = (long*)PopInt();
    if(!cls_inst_mem) {
      cerr << "Atempting to dereference a 'Nil' memory instance" << endl;
      StackErrorUnwind();
      exit(1);
    }
    cls_inst_mem[instr->GetOperand()] = TopInt();
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
    long* cls_inst_mem = (long*)PopInt();
    if(!cls_inst_mem) {
      cerr << "Atempting to dereference a 'Nil' memory instance" << endl;
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

  long inst_mem = (long)MemoryManager::Instance()->AllocateObject(instr->GetOperand(),
                  op_stack, *stack_pos);
  PushInt(inst_mem);
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
  long value = PopInt();
  long size = value;
  indices[0] = value;
  long dim = 1;
  for(long i = 1; i < instr->GetOperand(); i++) {
    long value = PopInt();
    size *= value;
    indices[dim++] = value;
  }
  // double for double
  if(is_float) {
    size *= 2;
  }
  // assumes that doubles are twice the size of integers
  long* mem = (long*)MemoryManager::Instance()->AllocateArray(size + dim + 2, INT_TYPE,
              op_stack, *stack_pos);
  if(is_float) {
    mem[0] = size / 2;
  } else {
    mem[0] = size;
  }
  mem[1] = dim;

  memcpy(mem + 2, indices, dim * sizeof(long));
  PushInt((long)mem);
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
  long value = PopInt();
  long size = value;
  indices[0] = value;
  long dim = 1;
  for(long i = 1; i < instr->GetOperand(); i++) {
    long value = PopInt();
    size *= value;
    indices[dim++] = value;
  }
  long* mem = (long*)MemoryManager::Instance()->AllocateArray(size + ((dim + 2) * sizeof(long)),
              BYTE_ARY_TYPE, op_stack, *stack_pos);
  mem[0] = size;
  mem[1] = dim;
  memcpy(mem + 2, indices, dim * sizeof(long));
  PushInt((long)mem);
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
    ip = frame->GetIp();
  } else {
    halt = true;
  }
}

/********************************
 * Processes an asynchronous method
 * call.
 ********************************/
void StackInterpreter::ProcessAsyncMethodCall(StackInstr* instr)
{
  cerr << "Unsupported operation: ProcessAsyncMethodCall!" << endl;
  exit(1);
  // cout << "??? " << frame->GetMethod()->GetClass()->GetId() << "," << instr->GetOperand2() << " ???" << endl;
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
  long instance = PopInt();

  // make call
  StackMethod* called = program->GetClass(instr->GetOperand())->GetMethod(instr->GetOperand2());
  // dynamically bind class for virutal method
  if(called->IsVirtual()) {
    StackClass* impl_class = MemoryManager::Instance()->GetClass((long*)instance);
    if(!impl_class) {
      cerr << "Attempting to envoke a virtual class!" << endl;
      StackErrorUnwind();
      exit(1);
    }
    const string& qualified_method_name = called->GetName();
    const string& method_name = impl_class->GetName() +
                                qualified_method_name.substr(qualified_method_name.find(':'));

#ifdef _DEBUG
    cout << "=== Binding virtual method call: from: '" << called->GetName()
         << "'; to: '" << method_name << "' ===" << endl;
#endif
    called = program->GetClass(impl_class->GetId())->GetMethod(method_name);
  }

  // execute JIT call
  if(instr->GetOperand3()) {
    ProcessJitMethodCall(called, instance);
  }
  // execute interpreter
  else {
    ProcessInterpretedMethodCall(called, instance);
  }
}

/********************************
 * Processes an interpreted
 * synchronous method call.
 ********************************/
void StackInterpreter::ProcessJitMethodCall(StackMethod* called, long instance)
{
  // #ifdef _X64
  // ProcessInterpretedMethodCall(called, instance);
  // #else

  // TODO: don't try and re-compile code that doesn't compile the first time
  // execute method if it's been compiled
  if(called->GetNativeCode()) {
    Runtime::JitExecutorIA32 jit_executor;
    jit_executor.Execute(called, (long*)instance, op_stack, stack_pos);
    // restore previous state
    frame = PopFrame();
    ip = frame->GetIp();
  } 
  else {
#ifdef _JIT_SERIAL
    // compile
#ifdef _X64
    Runtime::JitCompilerIA64 jit_compiler;
#else
    Runtime::JitCompilerIA32 jit_compiler;
#endif
    jit_compiler.Compile(called);      
    // execute
    Runtime::JitExecutorIA32 jit_executor;
    jit_executor.Execute(called, (long*)instance, op_stack, stack_pos);
    // restore previous state
    frame = PopFrame();
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
      HANDLE thread_id = CreateThread(NULL, 0, CompileMethod, called, 0, NULL);
      if(!thread_id) {
	      cerr << "Unable to create thread to compile method!" << endl;
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
	cerr << "Unable to create thread to compile method!" << endl;
	exit(-1);
      }
      program->AddThread(jit_thread);
      pthread_attr_destroy(&attrs);
      
      // execute code in parallel
      ProcessInterpretedMethodCall(called, instance);
    }
#endif
#endif
  }
  // #endif
}

/********************************
 * Processes an interpreted
 * synchronous method call.
 ********************************/
void StackInterpreter::ProcessInterpretedMethodCall(StackMethod* called, long instance)
{
#ifdef _DEBUG
  cout << "=== MTHD_CALL: id=" << called->GetClass()->GetId() << ","
       << called->GetId() << "; name='" << called->GetName() << "' ===" << endl;
#endif
  long* mem = called->NewMemory();
  mem[0] = instance;
  ip = 0;
  frame = new StackFrame(called, mem);
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
  long* array = (long*)PopInt();
  const long size = array[0];
  array += 2;
  long index = ArrayIndex(instr, array, size);
  PushInt(array[index + instr->GetOperand()]);
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
  long* array = (long*)PopInt();
  const long size = array[0];
  array += 2;
  long index = ArrayIndex(instr, array, size);
  array[index + instr->GetOperand()] = PopInt();
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
  long* array = (long*)PopInt();
  const long size = array[0];
  array += 2;
  long index = ArrayIndex(instr, array, size);
  array += instr->GetOperand();
  PushInt(((BYTE_VALUE*)array)[index]);
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
  long* array = (long*)PopInt();
  const long size = array[0];
  array += 2;
  long index = ArrayIndex(instr, array, size);
  array += instr->GetOperand();
  ((BYTE_VALUE*)array)[index] = PopInt();
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
  long* array = (long*)PopInt();
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
  long* array = (long*)PopInt();
  const long size = array[0];
  array += 2;
  long index = ArrayIndex(instr, array,size);
  FLOAT_VALUE value = PopFloat();
  memcpy(array + index + instr->GetOperand(), &value, sizeof(FLOAT_VALUE));
}

/********************************
 * Processes trap instruction
 ********************************/
void StackInterpreter::ProcessTrap(StackInstr* instr)
{
  switch(PopInt()) {
  case LOAD_CLS_INST_ID:
    PushInt(MemoryManager::Instance()->GetObjectID((long*)PopInt()));
    break;

  case LOAD_ARY_SIZE: {
    long* array = (long*)PopInt();
    PushInt(array[2]);
  }
  break;

  case CPY_STR_ARY: {
    long index = PopInt();
    BYTE_VALUE* value_str = program->GetCharStrings()[index];
    // copy array
    long* array = (long*)PopInt();
    const long size = array[0];
    BYTE_VALUE* str = (BYTE_VALUE*)(array + 3);
    for(long i = 0; value_str[i] != '\0' && i < size; i++) {
      str[i] = value_str[i];
    }
#ifdef _DEBUG
    cout << "stack oper: CPY_STR_ARY" << endl;
    /*
    cout << "stack oper: CPY_STR_ARY: from='" << value_str << "', to='"
         << str << "', size=" << size << endl;
    */
#endif
    PushInt((long)array);
  }
  break;

  // ---------------- standard i/o ----------------
  case STD_OUT_BOOL:
    cout << ((PopInt() == 0) ? "false" : "true");
    break;

  case STD_OUT_BYTE:
    cout << (void*)PopInt();
    break;

  case STD_OUT_CHAR:
    cout << (char)PopInt();
    break;

  case STD_OUT_INT:
    cout << PopInt();
    break;

  case STD_OUT_FLOAT:
    cout << PopFloat();
    break;

  case STD_OUT_CHAR_ARY: {
    long* array = (long*)PopInt();
    BYTE_VALUE* str = (BYTE_VALUE*)(array + 3);
    cout << str;
  }
  break;

  case STD_IN_STRING: {
    long* array = (long*)PopInt();
    char* buffer = (char*)(array + 3);
    const long num = array[0];
    cin.getline(buffer, num);
  }
  break;
  
  // ---------------- system time ----------------
  case SYS_TIME:
    ProcessCurrentTime();
    break;
    
    // ---------------- ip socket i/o ----------------
  case SOCK_IP_CONNECT: {
    long* array = (long*)PopInt();
    array = (long*)array[0];
    long* instance = (long*)PopInt();
    long port = PopInt();
    const char* addr = (char*)(array + 3);
    SOCKET sock = IPSocket::Open(addr, port);
#ifdef _DEBUG
    cout << "# socket connect: addr='" << addr << ":" << port << "'; instance=" << instance << "(" << (long)instance << ")" <<
         "; addr=" << sock << "(" << (long)sock << ") #" << endl;
#endif
    instance[0] = (long)sock;
  }
    break;

  case SOCK_IP_IS_CONNECTED: {
    long* instance = (long*)PopInt();
    SOCKET sock = (SOCKET)instance[0];
    
    if(sock) {
      PushInt(1);
    } 
    else {
      PushInt(0);
    }
  }
    break;
    
  case SOCK_IP_CLOSE: {
    long* instance = (long*)PopInt();
    SOCKET sock = (SOCKET)instance[0];

#ifdef _DEBUG
    cout << "# socket close: addr=" << sock << "(" << (long)sock << ") #" << endl;
#endif
    if(sock) {
      instance[0] = NULL;
      IPSocket::Close(sock);
    }
  }
    break;
    
  case SOCK_IP_IN_BYTE:
    break;

  case SOCK_IP_IN_BYTE_ARY:
    break;

  case SOCK_IP_OUT_BYTE:
    break;

  case SOCK_IP_OUT_BYTE_ARY:
    break;

    // ---------------- file i/o ----------------
  case FILE_OPEN_READ: {
    long* array = (long*)PopInt();
    array = (long*)array[0];
    long* instance = (long*)PopInt();
    const char* name = (char*)(array + 3);
    FILE* file = File::FileOpen(name, "r");
#ifdef _DEBUG
    cout << "# file open: name='" << name << "'; instance=" << instance << "(" << (long)instance << ")" <<
         "; addr=" << file << "(" << (long)file << ") #" << endl;
#endif
    instance[0] = (long)file;
  }
  break;

  case FILE_OPEN_WRITE: {
    long* array = (long*)PopInt();
    array = (long*)array[0];
    long* instance = (long*)PopInt();
    const char* name = (char*)(array + 3);
    FILE* file = File::FileOpen(name, "a");
#ifdef _DEBUG
    cout << "# file open: name='" << name << "'; instance=" << instance << "(" << (long)instance << ")" <<
         "; addr=" << file << "(" << (long)file << ") #" << endl;
#endif
    instance[0] = (long)file;
  }
  break;

  case FILE_OPEN_READ_WRITE: {
    long* array = (long*)PopInt();
    array = (long*)array[0];
    long* instance = (long*)PopInt();
    const char* name = (char*)(array + 3);
    FILE* file = File::FileOpen(name, "a+");
#ifdef _DEBUG
    cout << "# file open: name='" << name << "'; instance=" << instance << "(" << (long)instance << ")"
         << "; addr=" << file << "(" << (long)file << ") #" << endl;
#endif
    instance[0] = (long)file;
  }
  break;

  case FILE_CLOSE: {
    long* instance = (long*)PopInt();
    FILE* file = (FILE*)instance[0];

#ifdef _DEBUG
    cout << "# file close: addr=" << file << "(" << (long)file << ") #" << endl;
#endif
    if(file) {
      instance[0] = NULL;
      fclose(file);
    }
  }
  break;

  case FILE_IN_STRING: {
    long* array = (long*)PopInt();
    long* instance = (long*)PopInt();
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
  break;

  case FILE_OUT_STRING: {
    long* array = (long*)PopInt();
    long* instance = (long*)PopInt();
    FILE* file = (FILE*)instance[0];
    const char* name = (char*)(array + 3);

    if(file) {
      fputs(name, file);
    }
  }
  break;

  case FILE_REWIND: {
    long* instance = (long*)PopInt();
    FILE* file = (FILE*)instance[0];

    if(file) {
      rewind(file);
    }
  }
  break;

  // --- TRAP_RTRN --- //

  case FILE_IN_BYTE: {
    long* instance = (long*)PopInt();
    FILE* file = (FILE*)instance[0];

    if(file) {
      if(fgetc(file) == EOF) {
        PushInt(0);
      } else {
        PushInt(1);
      }
    } else {
      PushInt(0);
    }
  }
  break;


  case FILE_IN_BYTE_ARY: {
    long* array = (long*)PopInt();
    const long num = PopInt();
    const long offset = PopInt();
    long* instance = (long*)PopInt();
    FILE* file = (FILE*)instance[0];

    if(file && offset + num < array[0]) {
      char* buffer = (char*)(array + 3);
      if(fread(buffer + offset, 1, num, (FILE*)instance[0]) != num) {
        PushInt(0);
      } else {
        PushInt(1);
      }
    } else {
      PushInt(0);
    }
  }
  break;

  case FILE_OUT_BYTE: {
    long value = PopInt();
    long* instance = (long*)PopInt();
    FILE* file = (FILE*)instance[0];

    if(file) {
      if(fputc(value, file) != value) {
        PushInt(0);
      } else {
        PushInt(1);
      }

    } else {
      PushInt(0);
    }
  }
  break;

  case FILE_OUT_BYTE_ARY: {
    long* array = (long*)PopInt();
    const long num = PopInt();
    const long offset = PopInt();
    long* instance = (long*)PopInt();
    FILE* file = (FILE*)instance[0];

    if(file && offset + num < array[0]) {
      char* buffer = (char*)(array + 3);
      if(fwrite(buffer + offset, 1, num, (FILE*)instance[0]) != num) {
        PushInt(0);
      } else {
        PushInt(1);
      }
    } else {
      PushInt(0);
    }
  }
  break;

  case FILE_SEEK: {
    long pos = PopInt();
    long* instance = (long*)PopInt();
    FILE* file = (FILE*)instance[0];

    if(file) {
      if(fseek(file, pos, SEEK_CUR) != 0) {
        PushInt(0);
      } else {
        PushInt(1);
      }
    } else {
      PushInt(0);
    }
  }
  break;

  case FILE_EOF: {
    long* instance = (long*)PopInt();
    FILE* file = (FILE*)instance[0];

    if(file) {
      PushInt(feof(file) != 0);
    } else {
      PushInt(1);
    }
  }
  break;

  case FILE_IS_OPEN: {
    long* instance = (long*)PopInt();
    FILE* file = (FILE*)instance[0];

    if(file) {
      PushInt(1);
    } 
    else {
      PushInt(0);
    }
  }
  break;

  case FILE_EXISTS: {
    long* array = (long*)PopInt();
    array = (long*)array[0];
    const char* name = (char*)(array + 3);

    PushInt(File::FileExists(name));
  }
  break;

  case FILE_SIZE: {
    long* array = (long*)PopInt();
    array = (long*)array[0];
    const char* name = (char*)(array + 3);

    PushInt(File::FileSize(name));

  }
  break;

  case FILE_DELETE: {
    long* array = (long*)PopInt();
    array = (long*)array[0];
    const char* name = (char*)(array + 3);

    if(remove(name) != 0) {
      PushInt(0);
    } else {
      PushInt(1);
    }
  }
  break;

  case FILE_RENAME: {
    long* to = (long*)PopInt();
    to = (long*)to[0];
    const char* to_name = (char*)(to + 3);

    long* from = (long*)PopInt();
    from = (long*)from[0];
    const char* from_name = (char*)(from + 3);

    if(rename(from_name, to_name) != 0) {
      PushInt(0);
    } else {
      PushInt(1);
    }
  }
  break;

  //----------- directory functions -----------
  case DIR_CREATE: {
    long* array = (long*)PopInt();
    array = (long*)array[0];
    const char* name = (char*)(array + 3);

    PushInt(File::MakeDir(name));
  }
  break;

  case DIR_EXISTS: {
    long* array = (long*)PopInt();
    array = (long*)array[0];
    const char* name = (char*)(array + 3);

    PushInt(File::IsDir(name));
  }
  break;

  case DIR_LIST: {
    long* array = (long*)PopInt();
    array = (long*)array[0];
    char* name = (char*)(array + 3);

    vector<string> files = File::ListDir(name);

    // create 'System.String' object array
    long str_obj_array_size = files.size();
    const long str_obj_array_dim = 1;
    long* str_obj_array = (long*)MemoryManager::Instance()->AllocateArray(str_obj_array_size +
                          str_obj_array_dim + 2,
                          INT_TYPE, op_stack,
                          *stack_pos);
    str_obj_array[0] = str_obj_array_size;
    str_obj_array[1] = str_obj_array_dim;
    str_obj_array[2] = str_obj_array_size;
    long* str_obj_array_ptr = str_obj_array + 3;

    // create and assign 'System.String' instances to array
    for(unsigned long i = 0; i < files.size(); i++) {
      // get value string
      string &value_str = files[i];

      // create character array
      const long char_array_size = value_str.size();
      const long char_array_dim = 1;
      long* char_array = (long*)MemoryManager::Instance()->AllocateArray(char_array_size + 1 +
                         ((char_array_dim + 2) *
                          sizeof(long)),
                         BYTE_ARY_TYPE,
                         op_stack, *stack_pos);
      char_array[0] = char_array_size;
      char_array[1] = char_array_dim;
      char_array[2] = char_array_size;

      // copy string
      char* char_array_ptr = (char*)(char_array + 3);

      strcpy(char_array_ptr, value_str.c_str());
//			strcpy_s(char_array_ptr,  value_str.size(), value_str.c_str());

      // create 'System.String' object instance
      long* str_obj = MemoryManager::Instance()->AllocateObject(program->GetStringClassId(),
                      (long*)op_stack, *stack_pos);
      str_obj[0] = (long)char_array;
      str_obj[1] = char_array_size;

      // add to object array
      str_obj_array_ptr[i] = (long)str_obj;
    }

    PushInt((long)str_obj_array);
  }
  break;
  }
}
