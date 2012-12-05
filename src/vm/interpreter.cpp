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
 * VM initialization
 ********************************/
void StackInterpreter::Initialize(StackProgram* p)
{
  program = p;
  
  const int line_max = 80;
  char buffer[line_max + 1];
  fstream config("config.prop", fstream::in);
  config.getline(buffer, line_max);
  if(config.good()) {
    while(strlen(buffer) > 0) {
      // readline ane parse
      string line(buffer);
      if(line.size() > 0 && line[0] != '#') {
	size_t offset = line.find_first_of('=');
	// set name/value pairs
	string name = line.substr(0, offset);      
	string value = line.substr(offset + 1);
	if(name.size() > 0 && value.size() > 0) {
	  program->SetProperty(name, value);
	}
      }
      // update
      config.getline(buffer, 80);
    }
  }
  config.close();
  
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
void StackInterpreter::Execute(long* op_stack, long* stack_pos, long i, StackMethod* method,
			       long* instance, bool jit_called)
{
  long right, left;
  double right_double, left_double;
  
#ifdef _TIMING
  clock_t start = clock();
#endif

  // inital setup
  call_stack_pos = 0;

  frame = new StackFrame(method, instance);
#ifdef _DEBUG
  cout << "creating frame=" << frame << endl;
#endif
  frame->SetJitCalled(jit_called);
  StackInstr** instrs = frame->GetMethod()->GetInstructions();
  long ip = i;

#ifdef _TIMING
  const string mthd_name = frame->GetMethod()->GetName();
#endif

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
  halt = false;
  do {
    StackInstr* instr = instrs[ip++];
    
#ifdef _DEBUGGER
    debugger->ProcessInstruction(instr, ip, call_stack, call_stack_pos, frame);
#endif
    
    switch(instr->GetType()) {
    case STOR_LOCL_INT_VAR: {
#ifdef _DEBUG
      cout << "stack oper: STOR_LOCL_INT_VAR; index=" << instr->GetOperand() << endl;
#endif
      long* mem = frame->GetMemory();
      mem[instr->GetOperand() + 1] = PopInt(op_stack, stack_pos);
    } 
      break;
      
    case STOR_CLS_INST_INT_VAR: {
#ifdef _DEBUG
      cout << "stack oper: STOR_CLS_INST_INT_VAR; index=" << instr->GetOperand() << endl;
#endif
      long* cls_inst_mem = (long*)PopInt(op_stack, stack_pos);
      if(!cls_inst_mem) {
	cerr << ">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
	StackErrorUnwind();
	exit(1);
      }
      long mem = PopInt(op_stack, stack_pos);
      cls_inst_mem[instr->GetOperand()] = mem;
    }    
      break;
      
    case STOR_FUNC_VAR:
      ProcessStoreFunction(instr, op_stack, stack_pos);
      break;

    case STOR_FLOAT_VAR:
      ProcessStoreFloat(instr, op_stack, stack_pos);
      break;
      
    case COPY_LOCL_INT_VAR: {
#ifdef _DEBUG
      cout << "stack oper: COPY_LOCL_INT_VAR; index=" << instr->GetOperand() << endl;
#endif
      long* mem = frame->GetMemory();
      mem[instr->GetOperand() + 1] = TopInt(op_stack, stack_pos);
    } 
      break;
      
    case COPY_CLS_INST_INT_VAR: {
#ifdef _DEBUG
      cout << "stack oper: COPY_CLS_INST_INT_VAR; index=" << instr->GetOperand() << endl;
#endif
      
      long* cls_inst_mem = (long*)PopInt(op_stack, stack_pos);
      if(!cls_inst_mem) {
	cerr << ">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
	StackErrorUnwind();
	exit(1);
      }
      cls_inst_mem[instr->GetOperand()] = TopInt(op_stack, stack_pos);
    }
      break;
      
    case COPY_FLOAT_VAR:
      ProcessCopyFloat(instr, op_stack, stack_pos);
      break;
      
    case LOAD_INT_LIT:
#ifdef _DEBUG
      cout << "stack oper: LOAD_INT_LIT; call_pos=" << call_stack_pos << endl;
#endif
      PushInt(instr->GetOperand(), op_stack, stack_pos);
      break;

    case SHL_INT: {
#ifdef _DEBUG
      cout << "stack oper: SHL_INT; call_pos=" << call_stack_pos << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);
      PushInt(right << left, op_stack, stack_pos);
    }
      break;
      
    case SHR_INT: {
#ifdef _DEBUG
      cout << "stack oper: SHR_INT; call_pos=" << call_stack_pos << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);
      PushInt(right >> left, op_stack, stack_pos);
    }
      break;

    case LOAD_FLOAT_LIT:
#ifdef _DEBUG
      cout << "stack oper: LOAD_FLOAT_LIT; call_pos=" << call_stack_pos << endl;
#endif
      PushFloat(instr->GetFloatOperand(), op_stack, stack_pos);
      break;

    case LOAD_LOCL_INT_VAR: {
#ifdef _DEBUG
      cout << "stack oper: LOAD_LOCL_INT_VAR; index=" << instr->GetOperand() << endl;
#endif
      long* mem = frame->GetMemory();
      PushInt(mem[instr->GetOperand() + 1], op_stack, stack_pos);
    } 
      break;
      
    case LOAD_CLS_INST_INT_VAR: {
#ifdef _DEBUG
      cout << "stack oper: LOAD_CLS_INST_INT_VAR; index=" << instr->GetOperand() << endl;
#endif      
      long* cls_inst_mem = (long*)PopInt(op_stack, stack_pos);
      if(!cls_inst_mem) {
	cerr << ">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
	StackErrorUnwind();
	exit(1);
      }
      PushInt(cls_inst_mem[instr->GetOperand()], op_stack, stack_pos);
    }
      break;
      
    case LOAD_FUNC_VAR:
      ProcessLoadFunction(instr, op_stack, stack_pos);
      break;

    case LOAD_FLOAT_VAR:
      ProcessLoadFloat(instr, op_stack, stack_pos);
      break;

    case AND_INT: {
#ifdef _DEBUG
      cout << "stack oper: AND; call_pos=" << call_stack_pos << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);
      PushInt(left && right, op_stack, stack_pos);
    }
      break;

    case OR_INT: {
#ifdef _DEBUG
      cout << "stack oper: OR; call_pos=" << call_stack_pos << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);
      PushInt(left || right, op_stack, stack_pos);
    }
      break;

    case ADD_INT:
#ifdef _DEBUG
      cout << "stack oper: ADD; call_pos=" << call_stack_pos << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);
      PushInt(right + left, op_stack, stack_pos);
      break;

    case ADD_FLOAT:
#ifdef _DEBUG
      cout << "stack oper: ADD; call_pos=" << call_stack_pos << endl;
#endif
      right_double = PopFloat(op_stack, stack_pos);
      left_double = PopFloat(op_stack, stack_pos);
      PushFloat(right_double + left_double, op_stack, stack_pos);
      break;

    case SUB_INT:
#ifdef _DEBUG
      cout << "stack oper: SUB; call_pos=" << call_stack_pos << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);
      PushInt(right - left, op_stack, stack_pos);
      break;

    case SUB_FLOAT:
#ifdef _DEBUG
      cout << "stack oper: SUB; call_pos=" << call_stack_pos << endl;
#endif
      right_double = PopFloat(op_stack, stack_pos);
      left_double = PopFloat(op_stack, stack_pos);
      PushFloat(right_double - left_double, op_stack, stack_pos);
      break;

    case MUL_INT:
#ifdef _DEBUG
      cout << "stack oper: MUL; call_pos=" << call_stack_pos << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);
      PushInt(right * left, op_stack, stack_pos);
      break;

    case DIV_INT:
#ifdef _DEBUG
      cout << "stack oper: DIV; call_pos=" << call_stack_pos << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);
      PushInt(right / left, op_stack, stack_pos);
      break;

    case MUL_FLOAT:
#ifdef _DEBUG
      cout << "stack oper: MUL; call_pos=" << call_stack_pos << endl;
#endif
      right_double = PopFloat(op_stack, stack_pos);
      left_double = PopFloat(op_stack, stack_pos);
      PushFloat(right_double * left_double, op_stack, stack_pos);
      break;

    case DIV_FLOAT:
#ifdef _DEBUG
      cout << "stack oper: DIV; call_pos=" << call_stack_pos << endl;
#endif
      right_double = PopFloat(op_stack, stack_pos);
      left_double = PopFloat(op_stack, stack_pos);
      PushFloat(right_double / left_double, op_stack, stack_pos);
      break;

    case MOD_INT:
#ifdef _DEBUG
      cout << "stack oper: MOD; call_pos=" << call_stack_pos << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);
      PushInt(right % left, op_stack, stack_pos);
      break;

    case BIT_AND_INT:
#ifdef _DEBUG
      cout << "stack oper: BIT_AND; call_pos=" << call_stack_pos << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);
      PushInt(right & left, op_stack, stack_pos);
      break;

    case BIT_OR_INT:
#ifdef _DEBUG
      cout << "stack oper: BIT_OR; call_pos=" << call_stack_pos << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);
      PushInt(right | left, op_stack, stack_pos);
      break;

    case BIT_XOR_INT:
#ifdef _DEBUG
      cout << "stack oper: BIT_XOR; call_pos=" << call_stack_pos << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);
      PushInt(right ^ left, op_stack, stack_pos);
      break;

    case LES_EQL_INT:
#ifdef _DEBUG
      cout << "stack oper: LES_EQL; call_pos=" << call_stack_pos << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);
      PushInt(right <= left, op_stack, stack_pos);
      break;

    case GTR_EQL_INT:
#ifdef _DEBUG
      cout << "stack oper: GTR_EQL; call_pos=" << call_stack_pos << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);
      PushInt(right >= left, op_stack, stack_pos);
      break;

    case LES_EQL_FLOAT:
#ifdef _DEBUG
      cout << "stack oper: LES_EQL; call_pos=" << call_stack_pos << endl;
#endif
      right_double = PopFloat(op_stack, stack_pos);
      left_double = PopFloat(op_stack, stack_pos);
      PushInt(right_double <= left_double, op_stack, stack_pos);
      break;

    case GTR_EQL_FLOAT:
#ifdef _DEBUG
      cout << "stack oper: GTR_EQL; call_pos=" << call_stack_pos << endl;
#endif
      right_double = PopFloat(op_stack, stack_pos);
      left_double = PopFloat(op_stack, stack_pos);
      PushInt(right_double >= left_double, op_stack, stack_pos);
      break;

    case EQL_INT:
#ifdef _DEBUG
      cout << "stack oper: EQL; call_pos=" << call_stack_pos << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);
      PushInt(right == left, op_stack, stack_pos);
      break;

    case NEQL_INT:
#ifdef _DEBUG
      cout << "stack oper: NEQL; call_pos=" << call_stack_pos << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);
      PushInt(right != left, op_stack, stack_pos);
      break;

    case LES_INT:
#ifdef _DEBUG
      cout << "stack oper: LES; call_pos=" << call_stack_pos << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);
      PushInt(right < left, op_stack, stack_pos);
      break;

    case GTR_INT:
#ifdef _DEBUG
      cout << "stack oper: GTR; call_pos=" << call_stack_pos << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);      
      PushInt(right > left, op_stack, stack_pos);
      break;

    case EQL_FLOAT:
#ifdef _DEBUG
      cout << "stack oper: EQL; call_pos=" << call_stack_pos << endl;
#endif
      right_double = PopFloat(op_stack, stack_pos);
      left_double = PopFloat(op_stack, stack_pos);
      PushInt(right_double == left_double, op_stack, stack_pos);
      break;

    case NEQL_FLOAT:
#ifdef _DEBUG
      cout << "stack oper: NEQL; call_pos=" << call_stack_pos << endl;
#endif
      right_double = PopFloat(op_stack, stack_pos);
      left_double = PopFloat(op_stack, stack_pos);
      PushInt(right_double != left_double, op_stack, stack_pos);
      break;

    case LES_FLOAT:
#ifdef _DEBUG
      cout << "stack oper: LES; call_pos=" << call_stack_pos << endl;
#endif
      right_double = PopFloat(op_stack, stack_pos);
      left_double = PopFloat(op_stack, stack_pos);
      PushInt(right_double < left_double, op_stack, stack_pos);
      break;

    case GTR_FLOAT:
#ifdef _DEBUG
      cout << "stack oper: GTR; call_pos=" << call_stack_pos << endl;
#endif
      right_double = PopFloat(op_stack, stack_pos);
      left_double = PopFloat(op_stack, stack_pos);
      PushInt(right_double > left_double, op_stack, stack_pos);
      break;

    case CPY_BYTE_ARY: {
      long length = PopInt(op_stack, stack_pos);
      const long src_offset = PopInt(op_stack, stack_pos);
      long* src_array = (long*)PopInt(op_stack, stack_pos);
      const long dest_offset = PopInt(op_stack, stack_pos);
      long* dest_array = (long*)PopInt(op_stack, stack_pos);      

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
        PushInt(1, op_stack, stack_pos);
      }
      else {
        PushInt(0, op_stack, stack_pos);
      }
    }
      break;

    case CPY_INT_ARY: {
      long length = PopInt(op_stack, stack_pos);
      const long src_offset = PopInt(op_stack, stack_pos);
      long* src_array = (long*)PopInt(op_stack, stack_pos);
      const long dest_offset = PopInt(op_stack, stack_pos);
      long* dest_array = (long*)PopInt(op_stack, stack_pos);      

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
        PushInt(1, op_stack, stack_pos);
      }
      else {
        PushInt(0, op_stack, stack_pos);
      }
    }
      break;

    case CPY_FLOAT_ARY: {
      long length = PopInt(op_stack, stack_pos);
      const long src_offset = PopInt(op_stack, stack_pos);
      long* src_array = (long*)PopInt(op_stack, stack_pos);
      const long dest_offset = PopInt(op_stack, stack_pos);
      long* dest_array = (long*)PopInt(op_stack, stack_pos);      

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
        PushInt(1, op_stack, stack_pos);
      }
      else {
        PushInt(0, op_stack, stack_pos);
      }
    }
      break;

      // Note: no supported via JIT -- *start*
    case CEIL_FLOAT:
      PushFloat(ceil(PopFloat(op_stack, stack_pos)), op_stack, stack_pos);
      break;

    case FLOR_FLOAT:
      PushFloat(floor(PopFloat(op_stack, stack_pos)), op_stack, stack_pos);
      break;

    case SIN_FLOAT:
      PushFloat(sin(PopFloat(op_stack, stack_pos)), op_stack, stack_pos);
      break;

    case COS_FLOAT:
      PushFloat(cos(PopFloat(op_stack, stack_pos)), op_stack, stack_pos);
      break;

    case TAN_FLOAT:
      PushFloat(tan(PopFloat(op_stack, stack_pos)), op_stack, stack_pos);
      break;

    case ASIN_FLOAT:
      PushFloat(asin(PopFloat(op_stack, stack_pos)), op_stack, stack_pos);
      break;

    case ACOS_FLOAT:
      PushFloat(acos(PopFloat(op_stack, stack_pos)), op_stack, stack_pos);
      break;

    case ATAN_FLOAT:
      PushFloat(atan(PopFloat(op_stack, stack_pos)), op_stack, stack_pos);
      break;

    case LOG_FLOAT:
      PushFloat(log(PopFloat(op_stack, stack_pos)), op_stack, stack_pos);
      break;

    case POW_FLOAT: {
      FLOAT_VALUE left = PopFloat(op_stack, stack_pos);
      FLOAT_VALUE right = PopFloat(op_stack, stack_pos);
      PushFloat(pow(right, left), op_stack, stack_pos);
    }
      break;

    case SQRT_FLOAT:
      PushFloat(sqrt(PopFloat(op_stack, stack_pos)), op_stack, stack_pos);
      break;

    case RAND_FLOAT: {
      FLOAT_VALUE value = (FLOAT_VALUE)rand();
      PushFloat(value / (FLOAT_VALUE)RAND_MAX, op_stack, stack_pos);
      break;
    }
      // Note: no supported via JIT -- *end*

    case I2F:
#ifdef _DEBUG
      cout << "stack oper: I2F; call_pos=" << call_stack_pos << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      PushFloat(right, op_stack, stack_pos);
      break;

    case F2I:
#ifdef _DEBUG
      cout << "stack oper: F2I; call_pos=" << call_stack_pos << endl;
#endif
      PushInt((long)PopFloat(op_stack, stack_pos), op_stack, stack_pos);
      break;

    case SWAP_INT:
#ifdef _DEBUG
      cout << "stack oper: SWAP_INT; call_pos=" << call_stack_pos << endl;
#endif
      SwapInt(op_stack, stack_pos);
      break;

    case POP_INT:
#ifdef _DEBUG
      cout << "stack oper: PopInt; call_pos=" << call_stack_pos << endl;
#endif
      PopInt(op_stack, stack_pos);
      break;

    case POP_FLOAT:
#ifdef _DEBUG
      cout << "stack oper: POP_FLOAT; call_pos=" << call_stack_pos << endl;
#endif
      PopFloat(op_stack, stack_pos);
      break;

    case OBJ_TYPE_OF: {
      long* mem = (long*)PopInt(op_stack, stack_pos);
      long* result = MemoryManager::Instance()->ValidObjectCast(mem, instr->GetOperand(),
								program->GetHierarchy(),
								program->GetInterfaces());
      if(result) {
        PushInt(1, op_stack, stack_pos);
      }
      else {
        PushInt(0, op_stack, stack_pos);
      }
    }
      break;

    case OBJ_INST_CAST: {
      long* mem = (long*)PopInt(op_stack, stack_pos);
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
      PushInt(result, op_stack, stack_pos);
    }
      break;

    case RTRN:
      ProcessReturn(instrs, ip);
      // return directly back to JIT code
      if(frame && frame->IsJitCalled()) {
        frame->SetJitCalled(false);
        return;
      }
      break;

    case DYN_MTHD_CALL:
      ProcessDynamicMethodCall(instr, instrs, ip, op_stack, stack_pos);
      // return directly back to JIT code
      if(frame->IsJitCalled()) {
        frame->SetJitCalled(false);
        return;
      }
      break;

    case MTHD_CALL:
      ProcessMethodCall(instr, instrs, ip, op_stack, stack_pos);
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
	  if(!impl_class) {
        cerr << ">>> Attempting to envoke a virtual method! <<<" << endl;
        StackErrorUnwind();
        exit(1);
      }
	  
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
      ProcessNewByteArray(instr, op_stack, stack_pos);
      break;

    case NEW_INT_ARY:
      ProcessNewArray(instr, op_stack, stack_pos);
      break;

    case NEW_FLOAT_ARY:
      ProcessNewArray(instr, op_stack, stack_pos, true);
      break;

    case NEW_OBJ_INST:
      ProcessNewObjectInstance(instr, op_stack, stack_pos);
      break;

    case STOR_BYTE_ARY_ELM:
      ProcessStoreByteArrayElement(instr, op_stack, stack_pos);
      break;

    case LOAD_BYTE_ARY_ELM:
      ProcessLoadByteArrayElement(instr, op_stack, stack_pos);
      break;

    case STOR_INT_ARY_ELM:
      ProcessStoreIntArrayElement(instr, op_stack, stack_pos);
      break;

    case LOAD_INT_ARY_ELM:
      ProcessLoadIntArrayElement(instr, op_stack, stack_pos);
      break;

    case STOR_FLOAT_ARY_ELM:
      ProcessStoreFloatArrayElement(instr, op_stack, stack_pos);
      break;

    case LOAD_FLOAT_ARY_ELM:
      ProcessLoadFloatArrayElement(instr, op_stack, stack_pos);
      break;

    case LOAD_CLS_MEM:
#ifdef _DEBUG
      cout << "stack oper: LOAD_CLS_MEM; call_pos=" << call_stack_pos << endl;
#endif
      PushInt((long)frame->GetMethod()->GetClass()->GetClassMemory(), op_stack, stack_pos);
      break;

    case LOAD_INST_MEM:
#ifdef _DEBUG
      cout << "stack oper: LOAD_INST_MEM; call_pos=" << call_stack_pos << endl;
#endif
      PushInt(frame->GetMemory()[0], op_stack, stack_pos);
      break;

    case TRAP:
    case TRAP_RTRN:
#ifdef _DEBUG
      cout << "stack oper: TRAP; call_pos=" << call_stack_pos << endl;
#endif
      ProcessTrap(instr, op_stack, stack_pos);
      break;

      // shared library support
    case DLL_LOAD:
      ProcessDllLoad(instr);
      break;

    case DLL_UNLOAD:
      ProcessDllUnload(instr);
      break;

    case DLL_FUNC_CALL:
      ProcessDllCall(instr, op_stack, stack_pos);
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
      right = PopInt(op_stack, stack_pos) * 1000;
      Sleep(right);
#else
      right = PopInt(op_stack, stack_pos);
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
      long* instance = (long*)PopInt(op_stack, stack_pos);
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
      long* instance = (long*)PopInt(op_stack, stack_pos);
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
      if(!instr->GetOperand3()) {
	if(instr->GetOperand2() < 0) {
	  ip = frame->GetMethod()->GetLabelIndex(instr->GetOperand()) + 1;
	  instr->SetOperand3(ip);
	} 
	else if(PopInt(op_stack, stack_pos) == instr->GetOperand2()) {
	  ip = frame->GetMethod()->GetLabelIndex(instr->GetOperand()) + 1;
	  instr->SetOperand3(ip);
	}
      }
      else {
	if(instr->GetOperand2() < 0) {
	  ip = instr->GetOperand3();
	} 
	else if(PopInt(op_stack, stack_pos) == instr->GetOperand2()) {
	  ip = instr->GetOperand3();
	}
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

#ifdef _TIMING
  clock_t end = clock();
  cout << "---------------------------" << endl;
  cout << "Dispatch method='" << mthd_name << "', time=" << (double)(end - start) / CLOCKS_PER_SEC << " second(s)." << endl;
#endif
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
    instance[3] = curr_time->tm_mday;          // day
    instance[4] = curr_time->tm_mon + 1;       // month
    instance[5] = curr_time->tm_year + 1900;   // year
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
void StackInterpreter::ProcessAddTime(TimeInterval t, long* &op_stack, long* &stack_pos)
{  
  long value = PopInt(op_stack, stack_pos);
  long* instance = (long*)PopInt(op_stack, stack_pos);

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
void StackInterpreter::ProcessSetTime1(long* &op_stack, long* &stack_pos) 
{
  // get time values
  long is_gmt = PopInt(op_stack, stack_pos);
  long year = PopInt(op_stack, stack_pos);
  long month = PopInt(op_stack, stack_pos);
  long day = PopInt(op_stack, stack_pos);
  long* instance = (long*)PopInt(op_stack, stack_pos);

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
void StackInterpreter::ProcessSetTime2(long* &op_stack, long* &stack_pos)
{
  // get time values
  long is_gmt = PopInt(op_stack, stack_pos);
  long secs = PopInt(op_stack, stack_pos);
  long mins = PopInt(op_stack, stack_pos);
  long hours = PopInt(op_stack, stack_pos);
  long year = PopInt(op_stack, stack_pos);
  long month = PopInt(op_stack, stack_pos);
  long day = PopInt(op_stack, stack_pos);
  long* instance = (long*)PopInt(op_stack, stack_pos);

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
void StackInterpreter::ProcessSetTime3(long* &op_stack, long* &stack_pos)
{
}

/********************************
 * Get platform string
 ********************************/
void StackInterpreter::ProcessPlatform(long* &op_stack, long* &stack_pos) 
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

  PushInt((long)str_obj, op_stack, stack_pos);
}

/********************************
 * Processes a load function
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessLoadFunction(StackInstr* instr, long* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  cout << "stack oper: LOAD_FUNC_VAR; index=" << instr->GetOperand()
       << "; local=" << ((instr->GetOperand2() == LOCL) ? "true" : "false") << endl;
#endif
  if(instr->GetOperand2() == LOCL) {
    long* mem = frame->GetMemory();
    PushInt(mem[instr->GetOperand() + 2], op_stack, stack_pos);
    PushInt(mem[instr->GetOperand() + 1], op_stack, stack_pos);
  } 
  else {
    long* cls_inst_mem = (long*)PopInt(op_stack, stack_pos);
    if(!cls_inst_mem) {
      cerr << ">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
      StackErrorUnwind();
      exit(1);
    }
    PushInt(cls_inst_mem[instr->GetOperand() + 1], op_stack, stack_pos);
    PushInt(cls_inst_mem[instr->GetOperand()], op_stack, stack_pos);
  }
}

/********************************
 * Processes a load float
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessLoadFloat(StackInstr* instr, long* &op_stack, long* &stack_pos)
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
    long* cls_inst_mem = (long*)PopInt(op_stack, stack_pos);
    if(!cls_inst_mem) {
      cerr << ">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
      StackErrorUnwind();
      exit(1);
    }
    memcpy(&value, &cls_inst_mem[instr->GetOperand()], sizeof(FLOAT_VALUE));
  }
  PushFloat(value, op_stack, stack_pos);
}

/********************************
 * Processes a store function
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessStoreFunction(StackInstr* instr, long* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  cout << "stack oper: STOR_FUNC_VAR; index=" << instr->GetOperand()
       << "; local=" << ((instr->GetOperand2() == LOCL) ? "true" : "false") << endl;
#endif
  if(instr->GetOperand2() == LOCL) {
    long* mem = frame->GetMemory();
    mem[instr->GetOperand() + 1] = PopInt(op_stack, stack_pos);
    mem[instr->GetOperand() + 2] = PopInt(op_stack, stack_pos);
  } 
  else {
    long* cls_inst_mem = (long*)PopInt(op_stack, stack_pos);
    if(!cls_inst_mem) {
      cerr << ">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
      StackErrorUnwind();
      exit(1);
    }
    cls_inst_mem[instr->GetOperand()] = PopInt(op_stack, stack_pos);
    cls_inst_mem[instr->GetOperand() + 1] = PopInt(op_stack, stack_pos);
  }
}

/********************************
 * Processes a store float
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessStoreFloat(StackInstr* instr, long* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  cout << "stack oper: STOR_FLOAT_VAR; index=" << instr->GetOperand()
       << "; local=" << ((instr->GetOperand2() == LOCL) ? "true" : "false") << endl;
#endif
  if(instr->GetOperand2() == LOCL) {
    FLOAT_VALUE value = PopFloat(op_stack, stack_pos);
    long* mem = frame->GetMemory();
    memcpy(&mem[instr->GetOperand() + 1], &value, sizeof(FLOAT_VALUE));
  } else {
    long* cls_inst_mem = (long*)PopInt(op_stack, stack_pos);
    if(!cls_inst_mem) {
      cerr << ">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
      StackErrorUnwind();
      exit(1);
    }
    FLOAT_VALUE value = PopFloat(op_stack, stack_pos);
    memcpy(&cls_inst_mem[instr->GetOperand()], &value, sizeof(FLOAT_VALUE));
  }
}

/********************************
 * Processes a copy float
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessCopyFloat(StackInstr* instr, long* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  cout << "stack oper: COPY_FLOAT_VAR; index=" << instr->GetOperand()
       << "; local=" << ((instr->GetOperand2() == LOCL) ? "true" : "false") << endl;
#endif
  if(instr->GetOperand2() == LOCL) {
    FLOAT_VALUE value = TopFloat(op_stack, stack_pos);
    long* mem = frame->GetMemory();
    memcpy(&mem[instr->GetOperand() + 1], &value, sizeof(FLOAT_VALUE));
  } else {
    long* cls_inst_mem = (long*)PopInt(op_stack, stack_pos);
    if(!cls_inst_mem) {
      cerr << ">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
      StackErrorUnwind();
      exit(1);
    }
    FLOAT_VALUE value = TopFloat(op_stack, stack_pos);
    memcpy(&cls_inst_mem[instr->GetOperand()], &value, sizeof(FLOAT_VALUE));
  }
}

/********************************
 * Processes a new object instance
 * request.
 ********************************/
void StackInterpreter::ProcessNewObjectInstance(StackInstr* instr, long* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  cout << "stack oper: NEW_OBJ_INST: id=" << instr->GetOperand() << endl;
#endif

  long inst_mem = (long)MemoryManager::AllocateObject(instr->GetOperand(),
						      op_stack, *stack_pos);
  PushInt(inst_mem, op_stack, stack_pos);
}

/********************************
 * Processes a new array instance
 * request.
 ********************************/
void StackInterpreter::ProcessNewArray(StackInstr* instr, long* &op_stack, long* &stack_pos, bool is_float)
{
#ifdef _DEBUG
  cout << "stack oper: NEW_INT_ARY/NEW_FLOAT_ARY; call_pos=" << call_stack_pos << endl;
#endif
  long indices[8];
  long value = PopInt(op_stack, stack_pos);
  long size = value;
  indices[0] = value;
  long dim = 1;
  for(long i = 1; i < instr->GetOperand(); i++) {
    long value = PopInt(op_stack, stack_pos);
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
  PushInt((long)mem, op_stack, stack_pos);
}

/********************************
 * Processes a new byte array instance
 * request.
 ********************************/
void StackInterpreter::ProcessNewByteArray(StackInstr* instr, long* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  cout << "stack oper: NEW_BYTE_ARY; call_pos=" << call_stack_pos << endl;
#endif
  long indices[8];
  long value = PopInt(op_stack, stack_pos);
  long size = value;
  indices[0] = value;
  long dim = 1;
  for(long i = 1; i < instr->GetOperand(); i++) {
    long value = PopInt(op_stack, stack_pos);
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
  PushInt((long)mem, op_stack, stack_pos);
}

/********************************
 * Processes a return instruction.
 * This instruction modifies the
 * call stack.
 ********************************/
void StackInterpreter::ProcessReturn(StackInstr** &instrs, long &ip)
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
  long* thread_op_stack = new long[CALC_STACK_SIZE];
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
  long* thread_op_stack = new long[CALC_STACK_SIZE];
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
void StackInterpreter::ProcessDynamicMethodCall(StackInstr* instr, StackInstr** &instrs, long &ip, long* &op_stack, long* &stack_pos)
{
  // save current method
  frame->SetIp(ip);
  PushFrame(frame);

  // pop instance
  long* instance = (long*)PopInt(op_stack, stack_pos);

  // make call
  long cls_id = PopInt(op_stack, stack_pos);
  long mthd_id = PopInt(op_stack, stack_pos);
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
    ProcessJitMethodCall(called, instance, instrs, ip, op_stack, stack_pos);
  }
  // execute interpreter
  else {
    ProcessInterpretedMethodCall(called, instance, instrs, ip);
  }
#else
  ProcessInterpretedMethodCall(called, instance, instrs, ip);
#endif
}

/********************************
 * Processes a synchronous method
 * call.
 ********************************/
void StackInterpreter::ProcessMethodCall(StackInstr* instr, StackInstr** &instrs, long &ip, long* &op_stack, long* &stack_pos)
{
  // save current method
  frame->SetIp(ip);
  PushFrame(frame);

  // pop instance
  long* instance = (long*)PopInt(op_stack, stack_pos);

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
    ProcessJitMethodCall(called, instance, instrs, ip, op_stack, stack_pos);
  }
  // execute interpreter
  else {
    ProcessInterpretedMethodCall(called, instance, instrs, ip);
  }
#else
  ProcessInterpretedMethodCall(called, instance, instrs, ip);
#endif
}

/********************************
 * Processes an interpreted
 * synchronous method call.
 ********************************/
void StackInterpreter::ProcessJitMethodCall(StackMethod* called, long* instance, StackInstr** &instrs, long &ip, long* &op_stack, long* &stack_pos)
{
#ifdef _DEBUGGER
  ProcessInterpretedMethodCall(called, instance, instrs, ip);
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
    // compile
#ifdef _X64
    JitCompilerIA64 jit_compiler;
#else
    JitCompilerIA32 jit_compiler;
#endif
    if(!jit_compiler.Compile(called)) {
      ProcessInterpretedMethodCall(called, instance, instrs, ip);
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
  }
#endif
}

/********************************
 * Processes an interpreted
 * synchronous method call.
 ********************************/
void StackInterpreter::ProcessInterpretedMethodCall(StackMethod* called, long* instance, StackInstr** &instrs, long &ip)
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
void StackInterpreter::ProcessLoadIntArrayElement(StackInstr* instr, long* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  cout << "stack oper: LOAD_INT_ARY_ELM; call_pos=" << call_stack_pos << endl;
#endif
  long* array = (long*)PopInt(op_stack, stack_pos);
  if(!array) {
    cerr << ">>> Atempting to dereference a 'Nil' memory element <<<" << endl;
    StackErrorUnwind();
    exit(1);
  }
  const long size = array[0];
  array += 2;
  long index = ArrayIndex(instr, array, size, op_stack, stack_pos);
  PushInt(array[index + instr->GetOperand()], op_stack, stack_pos);
}

/********************************
 * Processes a load store array
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessStoreIntArrayElement(StackInstr* instr, long* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  cout << "stack oper: STOR_INT_ARY_ELM; call_pos=" << call_stack_pos << endl;
#endif

  long* array = (long*)PopInt(op_stack, stack_pos);
  if(!array) {
    cerr << ">>> Atempting to dereference a 'Nil' memory element <<<" << endl;
    StackErrorUnwind();
    exit(1);
  }
  
  const long size = array[0];
  array += 2;
  long index = ArrayIndex(instr, array, size, op_stack, stack_pos);
  array[index + instr->GetOperand()] = PopInt(op_stack, stack_pos);
}

/********************************
 * Processes a load byte array
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessLoadByteArrayElement(StackInstr* instr, long* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  cout << "stack oper: LOAD_BYTE_ARY_ELM; call_pos=" << call_stack_pos << endl;
#endif
  long* array = (long*)PopInt(op_stack, stack_pos);
  if(!array) {
    cerr << ">>> Atempting to dereference a 'Nil' memory element <<<" << endl;
    StackErrorUnwind();
    exit(1);
  }
  const long size = array[0];
  array += 2;
  long index = ArrayIndex(instr, array, size, op_stack, stack_pos);
  array += instr->GetOperand();
  PushInt(((BYTE_VALUE*)array)[index], op_stack, stack_pos);
}

/********************************
 * Processes a store byte array
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessStoreByteArrayElement(StackInstr* instr, long* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  cout << "stack oper: STOR_BYTE_ARY_ELM; call_pos=" << call_stack_pos << endl;
#endif
  long* array = (long*)PopInt(op_stack, stack_pos);
  if(!array) {
    cerr << ">>> Atempting to dereference a 'Nil' memory element <<<" << endl;
    StackErrorUnwind();
    exit(1);
  }
  const long size = array[0];
  array += 2;
  long index = ArrayIndex(instr, array, size, op_stack, stack_pos);
  array += instr->GetOperand();
  ((BYTE_VALUE*)array)[index] = (BYTE_VALUE)PopInt(op_stack, stack_pos);
}

/********************************
 * Processes a load float array
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessLoadFloatArrayElement(StackInstr* instr, long* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  cout << "stack oper: LOAD_FLOAT_ARY_ELM; call_pos=" << call_stack_pos << endl;
#endif
  long* array = (long*)PopInt(op_stack, stack_pos);
  if(!array) {
    cerr << ">>> Atempting to dereference a 'Nil' memory element <<<" << endl;
    StackErrorUnwind();
    exit(1);
  }
  const long size = array[0];
  array += 2;
  long index = ArrayIndex(instr, array, size, op_stack, stack_pos);
  FLOAT_VALUE value;
  memcpy(&value, array + index + instr->GetOperand(), sizeof(FLOAT_VALUE));
  PushFloat(value, op_stack, stack_pos);
}

/********************************
 * Processes a store float array
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessStoreFloatArrayElement(StackInstr* instr, long* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  cout << "stack oper: STOR_FLOAT_ARY_ELM; call_pos=" << call_stack_pos << endl;
#endif
  long* array = (long*)PopInt(op_stack, stack_pos);
  if(!array) {
    cerr << ">>> Atempting to dereference a 'Nil' memory element <<<" << endl;
    StackErrorUnwind();
    exit(1);
  }
  const long size = array[0];
  array += 2;
  long index = ArrayIndex(instr, array, size, op_stack, stack_pos);
  FLOAT_VALUE value = PopFloat(op_stack, stack_pos);
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
void StackInterpreter::ProcessDllCall(StackInstr* instr, long* &op_stack, long* &stack_pos)
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
void StackInterpreter::ProcessTrap(StackInstr* instr, long* &op_stack, long* &stack_pos)
{
  switch(PopInt(op_stack, stack_pos)) {
  case LOAD_CLS_INST_ID: {
    long* obj = (long*)PopInt(op_stack, stack_pos);
    PushInt(MemoryManager::GetObjectID(obj), op_stack, stack_pos);
  }
    break;

  case LOAD_NEW_OBJ_INST: {
    long* array = (long*)PopInt(op_stack, stack_pos);
    if(array) {
      array = (long*)array[0];
      const char* name = (char*)(array + 3);

#ifdef _DEBUG
      cout << "stack oper: LOAD_NEW_OBJ_INST; call_pos=" << call_stack_pos 
	   << ", name='" << name << "'"  << endl;
#endif
      CreateNewObject(name, op_stack, stack_pos);
    }
    else {
      PushInt(0, op_stack, stack_pos);
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
    cls_obj[0] = (long)CreateStringObject(cls->GetName(), op_stack, stack_pos);
    frame->GetMemory()[1] = (long)cls_obj;
    CreateClassObject(cls, cls_obj, op_stack, stack_pos);
  }
    break;

  case LOAD_ARY_SIZE: {
    long* array = (long*)PopInt(op_stack, stack_pos);
    if(!array) {
      cerr << ">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
      StackErrorUnwind();
      exit(1);
    }
    PushInt(array[2], op_stack, stack_pos);
  }
    break;
    
  case LOAD_MULTI_ARY_SIZE: {
    long* array = (long*)PopInt(op_stack, stack_pos);
    if(!array) {
      cerr << ">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
      StackErrorUnwind();
      exit(1);
    }
    
    // allocate 'size' array and copy metadata
    long size = array[1];
    long dim = 1;
    long* mem = (long*)MemoryManager::AllocateArray(size + dim + 2, INT_TYPE,
						    op_stack, *stack_pos);
    int i, j;
    for(i = 0, j = size + 2; i < size; i++) {
      mem[i + 3] = array[--j];
    }
    mem[0] = size;
    mem[1] = dim;
    mem[2] = size;

    PushInt((long)mem, op_stack, stack_pos);
  }
    break;
    
  case CPY_CHAR_STR_ARY: {
    long index = PopInt(op_stack, stack_pos);
    BYTE_VALUE* value_str = program->GetCharStrings()[index];
    // copy array
    long* array = (long*)PopInt(op_stack, stack_pos);
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
    PushInt((long)array, op_stack, stack_pos);
  }
    break;

  case CPY_CHAR_STR_ARYS: {
    // copy array
    long* array = (long*)PopInt(op_stack, stack_pos);
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
      str[i] = PopInt(op_stack, stack_pos);
    }
#ifdef _DEBUG
    cout << "stack oper: CPY_CHAR_STR_ARYS" << endl;
#endif
    PushInt((long)array, op_stack, stack_pos);
  }
    break;

  case CPY_INT_STR_ARY: {
    long index = PopInt(op_stack, stack_pos);
    int* value_str = program->GetIntStrings()[index];
    // copy array
    long* array = (long*)PopInt(op_stack, stack_pos);
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
    PushInt((long)array, op_stack, stack_pos);
  }
    break;

  case CPY_FLOAT_STR_ARY: {
    long index = PopInt(op_stack, stack_pos);
    FLOAT_VALUE* value_str = program->GetFloatStrings()[index];
    // copy array
    long* array = (long*)PopInt(op_stack, stack_pos);
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
    PushInt((long)array, op_stack, stack_pos);
  }
    break;

    // ---------------- standard i/o ----------------
  case STD_OUT_BOOL:
#ifdef _DEBUG
    cout << "  STD_OUT_BOOL" << endl;
#endif
    cout << ((PopInt(op_stack, stack_pos) == 0) ? "false" : "true");
    break;
    
  case STD_OUT_BYTE:
#ifdef _DEBUG
    cout << "  STD_OUT_BYTE" << endl;
#endif
    cout << (unsigned char)PopInt(op_stack, stack_pos);
    break;

  case STD_OUT_CHAR:
#ifdef _DEBUG
    cout << "  STD_OUT_CHAR" << endl;
#endif
    cout << (char)PopInt(op_stack, stack_pos);
    break;

  case STD_OUT_INT:
#ifdef _DEBUG
    cout << "  STD_OUT_INT" << endl;
#endif
    cout << PopInt(op_stack, stack_pos);
    break;

  case STD_OUT_FLOAT:
#ifdef _DEBUG
    cout << "  STD_OUT_FLOAT" << endl;
#endif
    cout.precision(9);
    cout << PopFloat(op_stack, stack_pos);
    break;
    
  case STD_OUT_CHAR_ARY: {
    long* array = (long*)PopInt(op_stack, stack_pos);
    
#ifdef _DEBUG
    cout << "  STD_OUT_CHAR_ARY: addr=" << array << "(" << long(array) << ")" << endl;
#endif
    
    if(array) {
      char* str = (char*)(array + 3);
      cout << str;
    }
    else {
      cout << "Nil";
    }
  }
    break;

  case STD_OUT_BYTE_ARY: {
    long* array = (long*)PopInt(op_stack, stack_pos);
    const long num = PopInt(op_stack, stack_pos);
    const long offset = PopInt(op_stack, stack_pos);

#ifdef _DEBUG
    cout << "  STD_OUT_CHAR_ARY: addr=" << array << "(" << long(array) << ")" << endl;
#endif

    if(array && offset >= 0 && offset + num < array[0]) {
      char* buffer = (char*)(array + 3);
      cout.write(buffer + offset, num);
      PushInt(1, op_stack, stack_pos);
    } 
    else {
      cout << "Nil";
      PushInt(0, op_stack, stack_pos);
    }
  }
    break;

  case STD_IN_STRING: {
    long* array = (long*)PopInt(op_stack, stack_pos);
    if(array) {
      char* buffer = (char*)(array + 3);
      const long num = array[0];
      cin.getline(buffer, num);
    }
  }
    break;
    
    // ---------------- standard error i/o ----------------
  case STD_ERR_BOOL:
#ifdef _DEBUG
    cout << "  STD_ERR_BOOL" << endl;
#endif
    cerr << ((PopInt(op_stack, stack_pos) == 0) ? "false" : "true");
    break;
    
  case STD_ERR_BYTE:
#ifdef _DEBUG
    cout << "  STD_ERR_BYTE" << endl;
#endif
    cerr << (unsigned char)PopInt(op_stack, stack_pos);
    break;

  case STD_ERR_CHAR:
#ifdef _DEBUG
    cout << "  STD_ERR_CHAR" << endl;
#endif
    cerr << (char)PopInt(op_stack, stack_pos);
    break;

  case STD_ERR_INT:
#ifdef _DEBUG
    cout << "  STD_ERR_INT" << endl;
#endif
    cerr << PopInt(op_stack, stack_pos);
    break;

  case STD_ERR_FLOAT:
#ifdef _DEBUG
    cout << "  STD_ERR_FLOAT" << endl;
#endif
    cerr.precision(9);
    cerr << PopFloat(op_stack, stack_pos);
    break;
    
  case STD_ERR_CHAR_ARY: {
    long* array = (long*)PopInt(op_stack, stack_pos);
    
#ifdef _DEBUG
    cout << "  STD_ERR_CHAR_ARY: addr=" << array << "(" << long(array) << ")" << endl;
#endif
    
    if(array) {
      char* str = (char*)(array + 3);
      cerr << str;
    }
    else {
      cerr << "Nil";
    }
  }
    break;

  case STD_ERR_BYTE_ARY: {
    long* array = (long*)PopInt(op_stack, stack_pos);
    const long num = PopInt(op_stack, stack_pos);
    const long offset = PopInt(op_stack, stack_pos);

#ifdef _DEBUG
    cout << "  STD_ERR_CHAR_ARY: addr=" << array << "(" << long(array) << ")" << endl;
#endif

    if(array && offset >= 0 && offset + num < array[0]) {
      char* buffer = (char*)(array + 3);
      cerr.write(buffer + offset, num);
      PushInt(1, op_stack, stack_pos);
    } 
    else {
      cerr << "Nil";
      PushInt(0, op_stack, stack_pos);
    }
  }
    break;
    
    // ---------------- runtime ----------------
  case EXIT:
    exit(PopInt(op_stack, stack_pos));
    break;

  case GMT_TIME:
    ProcessCurrentTime(true);
    break;

  case SYS_TIME:
    ProcessCurrentTime(false);
    break;

  case DATE_TIME_SET_1:
    ProcessSetTime1(op_stack, stack_pos);
    break;

  case DATE_TIME_SET_2:
    ProcessSetTime2(op_stack, stack_pos);
    break;

    /* TODO
       case DATE_TIME_SET_3:
       ProcessSetTime3();
       break;
    */

  case DATE_TIME_ADD_DAYS:
    ProcessAddTime(DAY_TIME, op_stack, stack_pos);
    break;

  case DATE_TIME_ADD_HOURS:
    ProcessAddTime(HOUR_TIME, op_stack, stack_pos);
    break;

  case DATE_TIME_ADD_MINS:
    ProcessAddTime(MIN_TIME, op_stack, stack_pos);
    break;

  case DATE_TIME_ADD_SECS:
    ProcessAddTime(SEC_TIME, op_stack, stack_pos);
    break;

  case PLTFRM:
    ProcessPlatform(op_stack, stack_pos);
    break;

  case GET_SYS_PROP: {
    long* key_array = (long*)PopInt(op_stack, stack_pos);
    if(key_array) {    
      key_array = (long*)key_array[0];
      const char* key = (char*)(key_array + 3);
      long* value = CreateStringObject(program->GetProperty(key), op_stack, stack_pos);
      PushInt((long)value, op_stack, stack_pos);
    }
    else {
      long* value = CreateStringObject("", op_stack, stack_pos);
      PushInt((long)value, op_stack, stack_pos);
    }
  }
    break;
    
  case SET_SYS_PROP: {
    long* value_array = (long*)PopInt(op_stack, stack_pos);
    long* key_array = (long*)PopInt(op_stack, stack_pos);
    
    if(key_array && value_array) {    
      value_array = (long*)value_array[0];
      key_array = (long*)key_array[0];
      
      const char* key = (char*)(key_array + 3);
      const char* value = (char*)(value_array + 3);
      program->SetProperty(key, value);
    }
  }
    break;
    
    // ---------------- ip socket i/o ----------------
  case SOCK_TCP_HOST_NAME: {
    long* array = (long*)PopInt(op_stack, stack_pos);
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
      PushInt((long)array, op_stack, stack_pos);
    }
    else {
      PushInt(0, op_stack, stack_pos);
    }
  }
    break;

  case SOCK_TCP_CONNECT: {
    long port = PopInt(op_stack, stack_pos);
    long* array = (long*)PopInt(op_stack, stack_pos);
    long* instance = (long*)PopInt(op_stack, stack_pos);
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
    long port = PopInt(op_stack, stack_pos);
    long* instance = (long*)PopInt(op_stack, stack_pos);
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
    long backlog = PopInt(op_stack, stack_pos);
    long* instance = (long*)PopInt(op_stack, stack_pos);

    if(instance && (SOCKET)instance[0] >= 0) {
      SOCKET server = (SOCKET)instance[0];
#ifdef _DEBUG
      cout << "# socket listen: backlog=" << backlog << "'; instance=" << instance 
	   << "(" << (long)instance << ")" << "; addr=" << server << "(" 
	   << (long)server << ") #" << endl;
#endif
      if(IPSocket::Listen(server, backlog)) {
	PushInt(1, op_stack, stack_pos);    
      }
      else {
	PushInt(0, op_stack, stack_pos);
      }
    }
    else {
      PushInt(0, op_stack, stack_pos);
    }
  }
    break;   
    
  case SOCK_TCP_ACCEPT: {
    long* instance = (long*)PopInt(op_stack, stack_pos);
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
      sock_obj[1] = (long)CreateStringObject(client_address, op_stack, stack_pos);
      sock_obj[2] = client_port;

      PushInt((long)sock_obj, op_stack, stack_pos);
    }
  }
    break;

  case SOCK_TCP_CLOSE: {
    long* instance = (long*)PopInt(op_stack, stack_pos);
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
    long* array = (long*)PopInt(op_stack, stack_pos);
    long* instance = (long*)PopInt(op_stack, stack_pos);
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
    long* array = (long*)PopInt(op_stack, stack_pos);
    long* instance = (long*)PopInt(op_stack, stack_pos);
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
    SerializeInt(INT_PARM, op_stack, stack_pos);
    SerializeInt(frame->GetMemory()[1], op_stack, stack_pos);
    break;

  case SERL_FLOAT: {
#ifdef _DEBUG
    cout << "# serializing float #" << endl;
#endif
    SerializeInt(FLOAT_PARM, op_stack, stack_pos);
    FLOAT_VALUE value;
    memcpy(&value, &(frame->GetMemory()[1]), sizeof(value));
    SerializeFloat(value, op_stack, stack_pos);
  }
    break;

  case SERL_OBJ_INST:
    SerializeObject(op_stack, stack_pos);
    break;

  case SERL_BYTE_ARY:
    SerializeInt(BYTE_ARY_PARM, op_stack, stack_pos);
    SerializeArray((long*)frame->GetMemory()[1], BYTE_ARY_PARM, op_stack, stack_pos);
    break;

  case SERL_INT_ARY:
    SerializeInt(INT_ARY_PARM, op_stack, stack_pos);
    SerializeArray((long*)frame->GetMemory()[1], INT_ARY_PARM, op_stack, stack_pos);
    break;

  case SERL_FLOAT_ARY:
    SerializeInt(FLOAT_ARY_PARM, op_stack, stack_pos);
    SerializeArray((long*)frame->GetMemory()[1], FLOAT_ARY_PARM, op_stack, stack_pos);
    break;

  case DESERL_INT:
#ifdef _DEBUG
    cout << "# deserializing int #" << endl;
#endif
    if(INT_PARM == (ParamType)DeserializeInt()) {
      PushInt(DeserializeInt(), op_stack, stack_pos);
    }
    else {
      PushInt(0, op_stack, stack_pos);
    }
    break;

  case DESERL_FLOAT:
#ifdef _DEBUG
    cout << "# deserializing float #" << endl;
#endif
    if(FLOAT_PARM == (ParamType)DeserializeInt()) {
      PushFloat(DeserializeFloat(), op_stack, stack_pos);
    }
    else {
      PushFloat(0.0, op_stack, stack_pos);
    }
    break;

  case DESERL_OBJ_INST:
    DeserializeObject(op_stack, stack_pos);
    break;

  case DESERL_BYTE_ARY:
#ifdef _DEBUG
    cout << "# deserializing byte array #" << endl;
#endif
    if(BYTE_ARY_PARM == (ParamType)DeserializeInt()) {
      PushInt((long)DeserializeArray(BYTE_ARY_PARM, op_stack, stack_pos), op_stack, stack_pos);
    }
    else {
      PushInt(0, op_stack, stack_pos);
    }
    break;

  case DESERL_INT_ARY:
#ifdef _DEBUG
    cout << "# deserializing int array #" << endl;
#endif
    if(INT_ARY_PARM == (ParamType)DeserializeInt()) {
      PushInt((long)DeserializeArray(INT_ARY_PARM, op_stack, stack_pos), op_stack, stack_pos);
    }
    else {
      PushInt(0, op_stack, stack_pos);
    }
    break;

  case DESERL_FLOAT_ARY:
#ifdef _DEBUG
    cout << "# deserializing float array #" << endl;
#endif
    if(FLOAT_ARY_PARM == (ParamType)DeserializeInt()) {
      PushInt((long)DeserializeArray(FLOAT_ARY_PARM, op_stack, stack_pos), op_stack, stack_pos);
    }
    else {
      PushInt(0, op_stack, stack_pos);
    }
    break;

    // ---------------- file i/o ----------------
  case FILE_OPEN_READ: {
    long* array = (long*)PopInt(op_stack, stack_pos);
    long* instance = (long*)PopInt(op_stack, stack_pos);
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
    long* array = (long*)PopInt(op_stack, stack_pos);
    long* instance = (long*)PopInt(op_stack, stack_pos);
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
    long* array = (long*)PopInt(op_stack, stack_pos);
    long* instance = (long*)PopInt(op_stack, stack_pos);
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
    long* instance = (long*)PopInt(op_stack, stack_pos);
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
    long* instance = (long*)PopInt(op_stack, stack_pos);
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
    long* array = (long*)PopInt(op_stack, stack_pos);
    long* instance = (long*)PopInt(op_stack, stack_pos);
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
    long* array = (long*)PopInt(op_stack, stack_pos);
    long* instance = (long*)PopInt(op_stack, stack_pos);    
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
    long* instance = (long*)PopInt(op_stack, stack_pos);
    if(instance && (FILE*)instance[0]) {
      FILE* file = (FILE*)instance[0];
      rewind(file);
    }
  }
    break;

    // --- TRAP_RTRN --- //

    // ---------------- socket i/o ----------------
  case SOCK_TCP_IS_CONNECTED: {
    long* instance = (long*)PopInt(op_stack, stack_pos);
    if(instance && (SOCKET)instance[0] >= 0) {
      PushInt(1, op_stack, stack_pos);
    } 
    else {
      PushInt(0, op_stack, stack_pos);
    }
  }
    break;

  case SOCK_TCP_IN_BYTE: {
    long* instance = (long*)PopInt(op_stack, stack_pos);
    if(instance) {
      SOCKET sock = (SOCKET)instance[0];
      int status;
      PushInt(IPSocket::ReadByte(sock, status), op_stack, stack_pos);
    }
    else {
      PushInt(0, op_stack, stack_pos);
    }
  }
    break;

  case SOCK_TCP_IN_BYTE_ARY: {
    long* array = (long*)PopInt(op_stack, stack_pos);
    const long num = PopInt(op_stack, stack_pos);
    const long offset = PopInt(op_stack, stack_pos);
    long* instance = (long*)PopInt(op_stack, stack_pos);
    
    if(array && instance && (SOCKET)instance[0] >= 0 && offset + num < array[0]) {
      SOCKET sock = (SOCKET)instance[0];
      char* buffer = (char*)(array + 3);
      PushInt(IPSocket::ReadBytes(buffer + offset, num, sock), op_stack, stack_pos);
    }
    else {
      PushInt(-1, op_stack, stack_pos);
    }
  }
    break;

  case SOCK_TCP_OUT_BYTE: {
    long value = PopInt(op_stack, stack_pos);
    long* instance = (long*)PopInt(op_stack, stack_pos);
    if(instance) {
      SOCKET sock = (SOCKET)instance[0];
      IPSocket::WriteByte((char)value, sock);
      PushInt(1, op_stack, stack_pos);
    }
    else {
      PushInt(0, op_stack, stack_pos);
    }
  }
    break;

  case SOCK_TCP_OUT_BYTE_ARY: {
    long* array = (long*)PopInt(op_stack, stack_pos);
    const long num = PopInt(op_stack, stack_pos);
    const long offset = PopInt(op_stack, stack_pos);
    long* instance = (long*)PopInt(op_stack, stack_pos);
    
    if(array && instance && (SOCKET)instance[0] >= 0 && offset + num < array[0]) {
      SOCKET sock = (SOCKET)instance[0];
      char* buffer = (char*)(array + 3);
      PushInt(IPSocket::WriteBytes(buffer + offset, num, sock), op_stack, stack_pos);
    } 
    else {
      PushInt(-1, op_stack, stack_pos);
    }
  } 
    break;

    // -------------- file i/o -----------------
  case FILE_IN_BYTE: {
    long* instance = (long*)PopInt(op_stack, stack_pos);
    if((FILE*)instance[0]) {
      FILE* file = (FILE*)instance[0];
      if(fgetc(file) == EOF) {
        PushInt(0, op_stack, stack_pos);
      } 
      else {
        PushInt(1, op_stack, stack_pos);
      }
    } 
    else {
      PushInt(0, op_stack, stack_pos);
    }
  }
    break;

  case FILE_IN_BYTE_ARY: {
    long* array = (long*)PopInt(op_stack, stack_pos);
    const long num = PopInt(op_stack, stack_pos);
    const long offset = PopInt(op_stack, stack_pos);
    long* instance = (long*)PopInt(op_stack, stack_pos);
    
    if(array && instance && (FILE*)instance[0] && offset >= 0 && offset + num < array[0]) {
      FILE* file = (FILE*)instance[0];
      char* buffer = (char*)(array + 3);
      PushInt(fread(buffer + offset, 1, num, file), op_stack, stack_pos);     
    } 
    else {
      PushInt(-1, op_stack, stack_pos);
    }
  }
    break;

  case FILE_OUT_BYTE: {
    long value = PopInt(op_stack, stack_pos);
    long* instance = (long*)PopInt(op_stack, stack_pos);
    
    if(instance && (FILE*)instance[0]) {
      FILE* file = (FILE*)instance[0];
      if(fputc(value, file) != value) {
        PushInt(0, op_stack, stack_pos);
      } 
      else {
        PushInt(1, op_stack, stack_pos);
      }

    } 
    else {
      PushInt(0, op_stack, stack_pos);
    }
  }
    break;

  case FILE_OUT_BYTE_ARY: {
    long* array = (long*)PopInt(op_stack, stack_pos);
    const long num = PopInt(op_stack, stack_pos);
    const long offset = PopInt(op_stack, stack_pos);
    long* instance = (long*)PopInt(op_stack, stack_pos);
    
    if(array && instance && (FILE*)instance[0] && offset >=0 && offset + num < array[0]) {
      FILE* file = (FILE*)instance[0];
      char* buffer = (char*)(array + 3);
      PushInt(fwrite(buffer + offset, 1, num, file), op_stack, stack_pos);
    } 
    else {
      PushInt(-1, op_stack, stack_pos);
    }
  }
    break;

  case FILE_SEEK: {
    long pos = PopInt(op_stack, stack_pos);
    long* instance = (long*)PopInt(op_stack, stack_pos);
    
    if(instance && (FILE*)instance[0]) {
      FILE* file = (FILE*)instance[0];
      if(fseek(file, pos, SEEK_CUR) != 0) {
        PushInt(0, op_stack, stack_pos);
      } 
      else {
        PushInt(1, op_stack, stack_pos);
      }
    } 
    else {
      PushInt(0, op_stack, stack_pos);
    }
  }
    break;

  case FILE_EOF: {
    long* instance = (long*)PopInt(op_stack, stack_pos);
    if(instance && (FILE*)instance[0]) {
      FILE* file = (FILE*)instance[0];
      PushInt(feof(file) != 0, op_stack, stack_pos);
    } 
    else {
      PushInt(1, op_stack, stack_pos);
    }
  }
    break;

  case FILE_IS_OPEN: {
    long* instance = (long*)PopInt(op_stack, stack_pos);
    if(instance && (FILE*)instance[0]) {
      PushInt(1, op_stack, stack_pos);
    } 
    else {
      PushInt(0, op_stack, stack_pos);
    }
  }
    break;

  case FILE_EXISTS: {
    long* array = (long*)PopInt(op_stack, stack_pos);
    if(array) {
      array = (long*)array[0];
      const char* name = (char*)(array + 3);
      PushInt(File::FileExists(name), op_stack, stack_pos);
    }
    else {
      PushInt(0, op_stack, stack_pos);
    }
  }
    break;

  case FILE_SIZE: {
    long* array = (long*)PopInt(op_stack, stack_pos);
    if(array) {
      array = (long*)array[0];
      const char* name = (char*)(array + 3);
      PushInt(File::FileSize(name), op_stack, stack_pos);
    }
    else {
      PushInt(-1, op_stack, stack_pos);
    }
  }
    break;

  case FILE_DELETE: {
    long* array = (long*)PopInt(op_stack, stack_pos);
    if(array) {
      array = (long*)array[0];
      const char* name = (char*)(array + 3);

      if(remove(name) != 0) {
        PushInt(0, op_stack, stack_pos);
      } 
      else {
        PushInt(1, op_stack, stack_pos);
      }
    }
    else {
      PushInt(0, op_stack, stack_pos);
    }
  }
    break;

  case FILE_RENAME: {
    long* to = (long*)PopInt(op_stack, stack_pos);
    long* from = (long*)PopInt(op_stack, stack_pos);

    if(!to || !from) {
      PushInt(0, op_stack, stack_pos);
      return;
    }

    to = (long*)to[0];
    const char* to_name = (char*)(to + 3);

    from = (long*)from[0];
    const char* from_name = (char*)(from + 3);

    if(rename(from_name, to_name) != 0) {
      PushInt(0, op_stack, stack_pos);
    } else {
      PushInt(1, op_stack, stack_pos);
    }
  }
    break;
    
  case FILE_CREATE_TIME: {
    long is_gmt = PopInt(op_stack, stack_pos);
    long* array = (long*)PopInt(op_stack, stack_pos);
    if(array) {
      array = (long*)array[0];
      const char* name = (char*)(array + 3);

      time_t raw_time = File::FileCreatedTime(name);      
      if(raw_time > 0) {
	struct tm* curr_time;
	if(is_gmt) {
	  curr_time = gmtime(&raw_time);
	}
	else {
	  curr_time = localtime(&raw_time);
	}
	
	frame->GetMemory()[3] = curr_time->tm_mday;          // day
	frame->GetMemory()[4] = curr_time->tm_mon + 1;       // month
	frame->GetMemory()[5] = curr_time->tm_year + 1900;   // year
	frame->GetMemory()[6] = curr_time->tm_hour;          // hours
	frame->GetMemory()[7] = curr_time->tm_min;           // mins
	frame->GetMemory()[8] = curr_time->tm_sec;           // secs
      }
      else {
	PushInt(0, op_stack, stack_pos);
      }
    }
    else {
      PushInt(0, op_stack, stack_pos);
    }
  }
    break;
    
  case FILE_MODIFIED_TIME: {
    long is_gmt = PopInt(op_stack, stack_pos);
    long* array = (long*)PopInt(op_stack, stack_pos);
    if(array) {
      array = (long*)array[0];
      const char* name = (char*)(array + 3);
      
      time_t raw_time = File::FileModifiedTime(name);      
      if(raw_time > 0) {
	struct tm* curr_time;
	if(is_gmt) {
	  curr_time = gmtime(&raw_time);
	}
	else {
	  curr_time = localtime(&raw_time);
	}
	
	frame->GetMemory()[3] = curr_time->tm_mday;          // day
	frame->GetMemory()[4] = curr_time->tm_mon + 1;       // month
	frame->GetMemory()[5] = curr_time->tm_year + 1900;   // year
	frame->GetMemory()[6] = curr_time->tm_hour;          // hours
	frame->GetMemory()[7] = curr_time->tm_min;           // mins
	frame->GetMemory()[8] = curr_time->tm_sec;           // secs
      }
      else {
	PushInt(0, op_stack, stack_pos);
      }
    }
    else {
      PushInt(0, op_stack, stack_pos);
    }
  }
    break;
    
    //----------- directory functions -----------
  case DIR_CREATE: {
    long* array = (long*)PopInt(op_stack, stack_pos);
    if(array) {
      array = (long*)array[0];
      const char* name = (char*)(array + 3);      
      PushInt(File::MakeDir(name), op_stack, stack_pos);
    }
    else {
      PushInt(0, op_stack, stack_pos);
    }
  }
    break;

  case DIR_EXISTS: {
    long* array = (long*)PopInt(op_stack, stack_pos);
    if(array) {
      array = (long*)array[0];
      const char* name = (char*)(array + 3);      
      PushInt(File::IsDir(name), op_stack, stack_pos);
    }
    else {
      PushInt(0, op_stack, stack_pos);
    }
  }
    break;

  case DIR_LIST: {
    long* array = (long*)PopInt(op_stack, stack_pos);
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
        str_obj_array_ptr[i] = (long)CreateStringObject(files[i], op_stack, stack_pos);
      }

      PushInt((long)str_obj_array, op_stack, stack_pos);
    }
    else {
      PushInt(0, op_stack, stack_pos);
    }
  }
    break;
  }
}

/********************************
 * Serializes an object graph
 ********************************/
void StackInterpreter::SerializeObject(long* &op_stack, long* &stack_pos)
{
  long* obj = (long*)frame->GetMemory()[1];
  ObjectSerializer serializer(obj);
  vector<BYTE_VALUE> src_buffer = serializer.GetValues();
  const long src_buffer_size = src_buffer.size();
  long* inst = (long*)frame->GetMemory()[0];
  long* dest_buffer = (long*)inst[0];
  long dest_pos = inst[1];

  // expand buffer, if needed
  dest_buffer = ExpandSerialBuffer(src_buffer_size, dest_buffer, inst, op_stack, stack_pos);
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
void StackInterpreter::DeserializeObject(long* &op_stack, long* &stack_pos) {
  if(!DeserializeByte()) {
    PushInt(0, op_stack, stack_pos);    
  }
  else {
    long* inst = (long*)frame->GetMemory()[0];
    long* byte_array = (long*)inst[0];
    const long dest_pos = inst[1];
    const long byte_array_dim_size = byte_array[2];  
    const BYTE_VALUE* byte_array_ptr = ((BYTE_VALUE*)(byte_array + 3) + dest_pos);

    ObjectDeserializer deserializer(byte_array_ptr, byte_array_dim_size, op_stack, stack_pos);
    PushInt((long)deserializer.DeserializeObject(), op_stack, stack_pos);
    inst[1] = dest_pos + deserializer.GetOffset();
  }
}
