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

#include "interpreter.h"
#include "lib_api.h"

#ifndef _NO_JIT
#ifdef _X64
#include "jit/amd64/jit_amd_lp64.h"
#else
#include "jit/ia32/jit_intel_lp32.h"
#endif
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
stack<StackFrame*> StackInterpreter::cached_frames;
#ifdef _WIN32
	CRITICAL_SECTION StackInterpreter::cached_frames_cs;
#else
  pthread_mutex_t StackInterpreter::cached_frames_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

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
      wstring line = BytesToUnicode(buffer);
      if(line.size() > 0 && line[0] != L'#') {
        size_t offset = line.find_first_of(L'=');
        // set name/value pairs
        wstring name = line.substr(0, offset);      
        wstring value = line.substr(offset + 1);
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
	InitializeCriticalSection(&cached_frames_cs);
#endif

#ifndef _SANITIZE
  // allocate 256K frames
	for(int i = 0; i < CALL_STACK_SIZE * 16; i++) {
		StackFrame* frame = new StackFrame();
		frame->mem = (long*)calloc(LOCAL_SIZE, sizeof(long));
		cached_frames.push(frame);
	}
#endif
  
#ifdef _WIN32
  StackMethod::InitVirtualEntry();
#endif 

#ifndef _NO_JIT
#ifdef _X64
  JitCompilerIA64::Initialize(program);
#else
  JitCompilerIA32::Initialize(program);
#endif
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
  if(monitor) {
    (*call_stack_pos) = 0;
  }
  (*frame) = GetStackFrame(method, instance);
	
#ifdef _DEBUG
  wcout << L"creating frame=" << (*frame) << endl;
#endif
  (*frame)->jit_called = jit_called;
  StackInstr** instrs = (*frame)->method->GetInstructions();
  long ip = i;

#ifdef _TIMING
  const wstring mthd_name = (*frame)->method->GetName();
#endif

#ifdef _DEBUG
  wcout << L"\n---------- Executing Interpretered Code: id=" 
				<< (((*frame)->method->GetClass()) ? (*frame)->method->GetClass()->GetId() : -1) << ","
				<< (*frame)->method->GetId() << "; method_name='" << (*frame)->method->GetName() 
				<< "' ---------\n" << endl;
#endif

  // execute
  halt = false;
  do {
    StackInstr* instr = instrs[ip++];
    
#ifdef _DEBUGGER
    debugger->ProcessInstruction(instr, ip, call_stack, (*call_stack_pos), (*frame));
#endif
    
    switch(instr->GetType()) {
    case STOR_LOCL_INT_VAR: {
#ifdef _DEBUG
      wcout << L"stack oper: STOR_LOCL_INT_VAR; index=" << instr->GetOperand() << endl;
#endif
      long* mem = (*frame)->mem;
      mem[instr->GetOperand() + 1] = PopInt(op_stack, stack_pos);
    } 
      break;
      
    case STOR_CLS_INST_INT_VAR: {
#ifdef _DEBUG
      wcout << L"stack oper: STOR_CLS_INST_INT_VAR; index=" << instr->GetOperand() << endl;
#endif
      long* cls_inst_mem = (long*)PopInt(op_stack, stack_pos);
      if(!cls_inst_mem) {
				wcerr << L">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
				StackErrorUnwind();
#ifdef _DEBUGGER
        halt = true;
				return;
#else
				exit(1);
#endif
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
      wcout << L"stack oper: COPY_LOCL_INT_VAR; index=" << instr->GetOperand() << endl;
#endif
      long* mem = (*frame)->mem;
      mem[instr->GetOperand() + 1] = TopInt(op_stack, stack_pos);
    } 
      break;
      
    case COPY_CLS_INST_INT_VAR: {
#ifdef _DEBUG
      wcout << L"stack oper: COPY_CLS_INST_INT_VAR; index=" << instr->GetOperand() << endl;
#endif
      
      long* cls_inst_mem = (long*)PopInt(op_stack, stack_pos);
      if(!cls_inst_mem) {
				wcerr << L">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
				StackErrorUnwind();
#ifdef _DEBUGGER
        halt = true;
				return;
#else
				exit(1);
#endif
      }
      cls_inst_mem[instr->GetOperand()] = TopInt(op_stack, stack_pos);
    }
      break;
      
    case COPY_FLOAT_VAR:
      ProcessCopyFloat(instr, op_stack, stack_pos);
      break;
    
    case LOAD_CHAR_LIT:
    case LOAD_INT_LIT:
    
#ifdef _DEBUG
      wcout << L"stack oper: LOAD_INT_LIT; call_pos=" << (*call_stack_pos) << endl;
#endif
      PushInt(instr->GetOperand(), op_stack, stack_pos);
      break;

    case SHL_INT: {
#ifdef _DEBUG
      wcout << L"stack oper: SHL_INT; call_pos=" << (*call_stack_pos) << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);
      PushInt(right << left, op_stack, stack_pos);
    }
      break;
      
    case SHR_INT: {
#ifdef _DEBUG
      wcout << L"stack oper: SHR_INT; call_pos=" << (*call_stack_pos) << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);
      PushInt(right >> left, op_stack, stack_pos);
    }
      break;

    case LOAD_FLOAT_LIT:
#ifdef _DEBUG
      wcout << L"stack oper: LOAD_FLOAT_LIT; call_pos=" << (*call_stack_pos) << endl;
#endif
      PushFloat(instr->GetFloatOperand(), op_stack, stack_pos);
      break;

    case LOAD_LOCL_INT_VAR: {
#ifdef _DEBUG
      wcout << L"stack oper: LOAD_LOCL_INT_VAR; index=" << instr->GetOperand() << endl;
#endif
      long* mem = (*frame)->mem;
      PushInt(mem[instr->GetOperand() + 1], op_stack, stack_pos);
    } 
      break;
      
    case LOAD_CLS_INST_INT_VAR: {
#ifdef _DEBUG
      wcout << L"stack oper: LOAD_CLS_INST_INT_VAR; index=" << instr->GetOperand() << endl;
#endif      
      long* cls_inst_mem = (long*)PopInt(op_stack, stack_pos);
      if(!cls_inst_mem) {
				wcerr << L">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
				StackErrorUnwind();
#ifdef _DEBUGGER
        halt = true;
				return;
#else
				exit(1);
#endif
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
      wcout << L"stack oper: AND; call_pos=" << (*call_stack_pos) << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);
      PushInt(left && right, op_stack, stack_pos);
    }
      break;

    case OR_INT: {
#ifdef _DEBUG
      wcout << L"stack oper: OR; call_pos=" << (*call_stack_pos) << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);
      PushInt(left || right, op_stack, stack_pos);
    }
      break;

    case ADD_INT:
#ifdef _DEBUG
      wcout << L"stack oper: ADD; call_pos=" << (*call_stack_pos) << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);
      PushInt(right + left, op_stack, stack_pos);
      break;

    case ADD_FLOAT:
#ifdef _DEBUG
      wcout << L"stack oper: ADD; call_pos=" << (*call_stack_pos) << endl;
#endif
      right_double = PopFloat(op_stack, stack_pos);
      left_double = PopFloat(op_stack, stack_pos);
      PushFloat(right_double + left_double, op_stack, stack_pos);
      break;

    case SUB_INT:
#ifdef _DEBUG
      wcout << L"stack oper: SUB; call_pos=" << (*call_stack_pos) << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);
      PushInt(right - left, op_stack, stack_pos);
      break;

    case SUB_FLOAT:
#ifdef _DEBUG
      wcout << L"stack oper: SUB; call_pos=" << (*call_stack_pos) << endl;
#endif
      right_double = PopFloat(op_stack, stack_pos);
      left_double = PopFloat(op_stack, stack_pos);
      PushFloat(right_double - left_double, op_stack, stack_pos);
      break;

    case MUL_INT:
#ifdef _DEBUG
      wcout << L"stack oper: MUL; call_pos=" << (*call_stack_pos) << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);
      PushInt(right * left, op_stack, stack_pos);
      break;

    case DIV_INT:
#ifdef _DEBUG
      wcout << L"stack oper: DIV; call_pos=" << (*call_stack_pos) << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);
      PushInt(right / left, op_stack, stack_pos);
      break;

    case MUL_FLOAT:
#ifdef _DEBUG
      wcout << L"stack oper: MUL; call_pos=" << (*call_stack_pos) << endl;
#endif
      right_double = PopFloat(op_stack, stack_pos);
      left_double = PopFloat(op_stack, stack_pos);
      PushFloat(right_double * left_double, op_stack, stack_pos);
      break;

    case DIV_FLOAT:
#ifdef _DEBUG
      wcout << L"stack oper: DIV; call_pos=" << (*call_stack_pos) << endl;
#endif
      right_double = PopFloat(op_stack, stack_pos);
      left_double = PopFloat(op_stack, stack_pos);
      PushFloat(right_double / left_double, op_stack, stack_pos);
      break;

    case MOD_INT:
#ifdef _DEBUG
      wcout << L"stack oper: MOD; call_pos=" << (*call_stack_pos) << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);
      PushInt(right % left, op_stack, stack_pos);
      break;

    case BIT_AND_INT:
#ifdef _DEBUG
      wcout << L"stack oper: BIT_AND; call_pos=" << (*call_stack_pos) << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);
      PushInt(right & left, op_stack, stack_pos);
      break;

    case BIT_OR_INT:
#ifdef _DEBUG
      wcout << L"stack oper: BIT_OR; call_pos=" << (*call_stack_pos) << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);
      PushInt(right | left, op_stack, stack_pos);
      break;

    case BIT_XOR_INT:
#ifdef _DEBUG
      wcout << L"stack oper: BIT_XOR; call_pos=" << (*call_stack_pos) << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);
      PushInt(right ^ left, op_stack, stack_pos);
      break;

    case LES_EQL_INT:
#ifdef _DEBUG
      wcout << L"stack oper: LES_EQL; call_pos=" << (*call_stack_pos) << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);
      PushInt(right <= left, op_stack, stack_pos);
      break;

    case GTR_EQL_INT:
#ifdef _DEBUG
      wcout << L"stack oper: GTR_EQL; call_pos=" << (*call_stack_pos) << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);
      PushInt(right >= left, op_stack, stack_pos);
      break;

    case LES_EQL_FLOAT:
#ifdef _DEBUG
      wcout << L"stack oper: LES_EQL; call_pos=" << (*call_stack_pos) << endl;
#endif
      right_double = PopFloat(op_stack, stack_pos);
      left_double = PopFloat(op_stack, stack_pos);
      PushInt(right_double <= left_double, op_stack, stack_pos);
      break;

    case GTR_EQL_FLOAT:
#ifdef _DEBUG
      wcout << L"stack oper: GTR_EQL; call_pos=" << (*call_stack_pos) << endl;
#endif
      right_double = PopFloat(op_stack, stack_pos);
      left_double = PopFloat(op_stack, stack_pos);
      PushInt(right_double >= left_double, op_stack, stack_pos);
      break;

    case EQL_INT:
#ifdef _DEBUG
      wcout << L"stack oper: EQL; call_pos=" << (*call_stack_pos) << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);
      PushInt(right == left, op_stack, stack_pos);
      break;

    case NEQL_INT:
#ifdef _DEBUG
      wcout << L"stack oper: NEQL; call_pos=" << (*call_stack_pos) << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);
      PushInt(right != left, op_stack, stack_pos);
      break;

    case LES_INT:
#ifdef _DEBUG
      wcout << L"stack oper: LES; call_pos=" << (*call_stack_pos) << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);
      PushInt(right < left, op_stack, stack_pos);
      break;

    case GTR_INT:
#ifdef _DEBUG
      wcout << L"stack oper: GTR; call_pos=" << (*call_stack_pos) << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      left = PopInt(op_stack, stack_pos);      
      PushInt(right > left, op_stack, stack_pos);
      break;

    case EQL_FLOAT:
#ifdef _DEBUG
      wcout << L"stack oper: EQL; call_pos=" << (*call_stack_pos) << endl;
#endif
      right_double = PopFloat(op_stack, stack_pos);
      left_double = PopFloat(op_stack, stack_pos);
      PushInt(right_double == left_double, op_stack, stack_pos);
      break;

    case NEQL_FLOAT:
#ifdef _DEBUG
      wcout << L"stack oper: NEQL; call_pos=" << (*call_stack_pos) << endl;
#endif
      right_double = PopFloat(op_stack, stack_pos);
      left_double = PopFloat(op_stack, stack_pos);
      PushInt(right_double != left_double, op_stack, stack_pos);
      break;

    case LES_FLOAT:
#ifdef _DEBUG
      wcout << L"stack oper: LES; call_pos=" << (*call_stack_pos) << endl;
#endif
      right_double = PopFloat(op_stack, stack_pos);
      left_double = PopFloat(op_stack, stack_pos);
      PushInt(right_double < left_double, op_stack, stack_pos);
      break;

    case GTR_FLOAT:
#ifdef _DEBUG
      wcout << L"stack oper: GTR_FLOAT; call_pos=" << (*call_stack_pos) << endl;
#endif
      right_double = PopFloat(op_stack, stack_pos);
      left_double = PopFloat(op_stack, stack_pos);
      PushInt(right_double > left_double, op_stack, stack_pos);
      break;
      
    case LOAD_ARY_SIZE: {
#ifdef _DEBUG
      wcout << L"stack oper: LOAD_ARY_SIZE; call_pos=" << (*call_stack_pos) << endl;
#endif
      long* array = (long*)PopInt(op_stack, stack_pos);
      if(!array) {
				wcerr << L">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
        StackErrorUnwind();
#ifdef _DEBUGGER
        halt = true;
				return;
#else
				exit(1);
#endif
      }
      PushInt(array[2], op_stack, stack_pos);
    }
      break;

    case CPY_BYTE_ARY: {
#ifdef _DEBUG
      wcout << L"stack oper: CPY_BYTE_ARY; call_pos=" << (*call_stack_pos) << endl;
#endif
      long length = PopInt(op_stack, stack_pos);
      const long src_offset = PopInt(op_stack, stack_pos);
      long* src_array = (long*)PopInt(op_stack, stack_pos);
      const long dest_offset = PopInt(op_stack, stack_pos);
      long* dest_array = (long*)PopInt(op_stack, stack_pos);      

      if(!src_array || !dest_array) {
        wcerr << L">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
        StackErrorUnwind();
#ifdef _DEBUGGER
        halt = true;
				return;
#else
				exit(1);
#endif
      }

      const long src_array_len = src_array[2];
      const long dest_array_len = dest_array[2];

      if(length > 0 && src_offset + length <= src_array_len && dest_offset + length <= dest_array_len) {
        const char* src_array_ptr = (char*)(src_array + 3);
        char* dest_array_ptr = (char*)(dest_array + 3);
        memcpy(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length);
        PushInt(1, op_stack, stack_pos);
      }
      else {
        PushInt(0, op_stack, stack_pos);
      }
    }
      break;

    case CPY_CHAR_ARY: {
#ifdef _DEBUG
      wcout << L"stack oper: CPY_CHAR_ARY; call_pos=" << (*call_stack_pos) << endl;
#endif
      long length = PopInt(op_stack, stack_pos);
      const long src_offset = PopInt(op_stack, stack_pos);
      long* src_array = (long*)PopInt(op_stack, stack_pos);
      const long dest_offset = PopInt(op_stack, stack_pos);
      long* dest_array = (long*)PopInt(op_stack, stack_pos);      

      if(!src_array || !dest_array) {
        wcerr << L">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
        StackErrorUnwind();
#ifdef _DEBUGGER
        halt = true;
				return;
#else
				exit(1);
#endif
      }

      const long src_array_len = src_array[2];
      const long dest_array_len = dest_array[2];

      if(length > 0 && src_offset + length <= src_array_len && dest_offset + length <= dest_array_len) {
        wchar_t* src_array_ptr = (wchar_t*)(src_array + 3);
        wchar_t* dest_array_ptr = (wchar_t*)(dest_array + 3);
        memcpy(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length * sizeof(wchar_t));
        PushInt(1, op_stack, stack_pos);
      }
      else {
        PushInt(0, op_stack, stack_pos);
      }
    }
      break;
      
    case CPY_INT_ARY: {
#ifdef _DEBUG
      wcout << L"stack oper: CPY_INT_ARY; call_pos=" << (*call_stack_pos) << endl;
#endif
      long length = PopInt(op_stack, stack_pos);
      const long src_offset = PopInt(op_stack, stack_pos);
      long* src_array = (long*)PopInt(op_stack, stack_pos);
      const long dest_offset = PopInt(op_stack, stack_pos);
      long* dest_array = (long*)PopInt(op_stack, stack_pos);      

      if(!src_array || !dest_array) {
        wcerr << L">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
        StackErrorUnwind();
#ifdef _DEBUGGER
        halt = true;
				return;
#else
				exit(1);
#endif
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
#ifdef _DEBUG
      wcout << L"stack oper: CPY_FLOAT_ARY; call_pos=" << (*call_stack_pos) << endl;
#endif
      long length = PopInt(op_stack, stack_pos);
      const long src_offset = PopInt(op_stack, stack_pos);
      long* src_array = (long*)PopInt(op_stack, stack_pos);
      const long dest_offset = PopInt(op_stack, stack_pos);
      long* dest_array = (long*)PopInt(op_stack, stack_pos);      

      if(!src_array || !dest_array) {
        wcerr << L">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
        StackErrorUnwind();
#ifdef _DEBUGGER
        halt = true;
				return;
#else
				exit(1);
#endif
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
      wcout << L"stack oper: I2F; call_pos=" << (*call_stack_pos) << endl;
#endif
      right = PopInt(op_stack, stack_pos);
      PushFloat(right, op_stack, stack_pos);
      break;

    case F2I:
#ifdef _DEBUG
      wcout << L"stack oper: F2I; call_pos=" << (*call_stack_pos) << endl;
#endif
      PushInt((long)PopFloat(op_stack, stack_pos), op_stack, stack_pos);
      break;

    case SWAP_INT:
#ifdef _DEBUG
      wcout << L"stack oper: SWAP_INT; call_pos=" << (*call_stack_pos) << endl;
#endif
      SwapInt(op_stack, stack_pos);
      break;

    case POP_INT:
#ifdef _DEBUG
      wcout << L"stack oper: PopInt; call_pos=" << (*call_stack_pos) << endl;
#endif
      PopInt(op_stack, stack_pos);
      break;

    case POP_FLOAT:
#ifdef _DEBUG
      wcout << L"stack oper: POP_FLOAT; call_pos=" << (*call_stack_pos) << endl;
#endif
      PopFloat(op_stack, stack_pos);
      break;

    case OBJ_TYPE_OF: {
      long* mem = (long*)PopInt(op_stack, stack_pos);
      long* result = MemoryManager::ValidObjectCast(mem, instr->GetOperand(),
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
      long result = (long)MemoryManager::ValidObjectCast(mem, instr->GetOperand(),
																												 program->GetHierarchy(),
																												 program->GetInterfaces());
#ifdef _DEBUG
      wcout << L"stack oper: OBJ_INST_CAST: from=" << mem << ", to=" << instr->GetOperand() << endl; 
#endif
      if(!result && mem) {
        StackClass* to_cls = MemoryManager::GetClass((long*)mem);
        wcerr << L">>> Invalid object cast: '" << (to_cls ? to_cls->GetName() : L"?")
							<< "' to '" << program->GetClass(instr->GetOperand())->GetName() << "' <<<" << endl;
        StackErrorUnwind();
#ifdef _DEBUGGER
        halt = true;
				return;
#else
				exit(1);
#endif
      }
      PushInt(result, op_stack, stack_pos);
    }
      break;

    case RTRN:
      ProcessReturn(instrs, ip);
      // return directly back to JIT code
      if((*frame) && (*frame)->jit_called) {
        (*frame)->jit_called = false;
        ReleaseStackFrame(*frame);
        return;
      }
      break;

    case DYN_MTHD_CALL:
      ProcessDynamicMethodCall(instr, instrs, ip, op_stack, stack_pos);
      // return directly back to JIT code
      if((*frame)->jit_called) {
        (*frame)->jit_called = false;
        ReleaseStackFrame(*frame);
        return;
      }
      break;

    case MTHD_CALL:
      ProcessMethodCall(instr, instrs, ip, op_stack, stack_pos);
      // return directly back to JIT code
      if((*frame)->jit_called) {
        (*frame)->jit_called = false;
        ReleaseStackFrame(*frame);
        return;
      }
      break;

    case ASYNC_MTHD_CALL: {
      long* instance = (long*)(*frame)->mem[0];
      long* param = (long*)(*frame)->mem[1];

      StackClass* impl_class = MemoryManager::GetClass(instance);
      if(!impl_class) {
        wcerr << L">>> Invalid instance reference! ref=" << instance << " << " << endl;
        StackErrorUnwind();
#ifdef _DEBUGGER
        halt = true;
				return;
#else
				exit(1);
#endif
      }
	  
      const wstring& mthd_name = impl_class->GetName() + L":Run:o.System.Base,";
      StackMethod* called = impl_class->GetMethod(mthd_name);
#ifdef _DEBUG
      assert(called);
      wcout << L"=== ASYNC_MTHD_CALL: id=" << called->GetClass()->GetId() << ","
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

    case NEW_CHAR_ARY:
      ProcessNewCharArray(instr, op_stack, stack_pos);
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

    case STOR_CHAR_ARY_ELM:
      ProcessStoreCharArrayElement(instr, op_stack, stack_pos);
      break;
      
    case LOAD_BYTE_ARY_ELM:
      ProcessLoadByteArrayElement(instr, op_stack, stack_pos);
      break;
      
    case LOAD_CHAR_ARY_ELM:
      ProcessLoadCharArrayElement(instr, op_stack, stack_pos);
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
      wcout << L"stack oper: LOAD_CLS_MEM; call_pos=" << (*call_stack_pos) << endl;
#endif
      PushInt((long)(*frame)->method->GetClass()->GetClassMemory(), op_stack, stack_pos);
      break;

    case LOAD_INST_MEM:
#ifdef _DEBUG
      wcout << L"stack oper: LOAD_INST_MEM; call_pos=" << (*call_stack_pos) << endl;
#endif
      PushInt((*frame)->mem[0], op_stack, stack_pos);
      break;

    case TRAP:
    case TRAP_RTRN:
#ifdef _DEBUG
      wcout << L"stack oper: TRAP; call_pos=" << (*call_stack_pos) << endl;
#endif
      if(!TrapProcessor::ProcessTrap(program, (long*)(*frame)->mem[0], op_stack, stack_pos, (*frame))) {
				StackErrorUnwind();
#ifdef _DEBUGGER
        halt = true;
				return;
#else
				exit(1);
#endif
      }
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
      wcout << L"stack oper: THREAD_JOIN; call_pos=" << (*call_stack_pos) << endl;
#endif
      long* instance = (long*)(*frame)->mem[0];
      if(!instance) {
        wcerr << L">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
        StackErrorUnwind();
#ifdef _DEBUGGER
        halt = true;
				return;
#else
				exit(1);
#endif
      }

#ifdef _WIN32
      HANDLE vm_thread = (HANDLE)instance[0];
      if(WaitForSingleObject(vm_thread, INFINITE) != WAIT_OBJECT_0) {
        wcerr << L">>> Unable to join thread! <<<" << endl;
#ifdef _DEBUGGER
				return;
#else
				exit(1);
#endif
      }
#else
      void* status;
      pthread_t vm_thread = (pthread_t)instance[0];      
      if(pthread_join(vm_thread, &status)) {
        wcerr << L">>> Unable to join thread! <<<" << endl;
#ifdef _DEBUGGER
				return;
#else
				exit(1);
#endif
      }
#endif
    }
      break;

    case THREAD_SLEEP:
#ifdef _DEBUG
      wcout << L"stack oper: THREAD_SLEEP; call_pos=" << (*call_stack_pos) << endl;
#endif

#ifdef _WIN32
      right = PopInt(op_stack, stack_pos);
      Sleep(right);
#else
      right = PopInt(op_stack, stack_pos);
      usleep(right * 1000);
#endif
      break;

    case THREAD_MUTEX: {
#ifdef _DEBUG
      wcout << L"stack oper: THREAD_MUTEX; call_pos=" << (*call_stack_pos) << endl;
#endif
      long* instance = (long*)(*frame)->mem[0];
      if(!instance) {
        wcerr << L">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
        StackErrorUnwind();
#ifdef _DEBUGGER
        halt = true;
				return;
#else
				exit(1);
#endif
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
      wcout << L"stack oper: CRITICAL_START; call_pos=" << (*call_stack_pos) << endl;
#endif
      long* instance = (long*)PopInt(op_stack, stack_pos);
      if(!instance) {
        wcerr << L">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
        StackErrorUnwind();
#ifdef _DEBUGGER
        halt = true;
				return;
#else
				exit(1);
#endif        
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
      wcout << L"stack oper: CRITICAL_END; call_pos=" << (*call_stack_pos) << endl;
#endif
      long* instance = (long*)PopInt(op_stack, stack_pos);
      if(!instance) {
        wcerr << L">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
        StackErrorUnwind();
#ifdef _DEBUGGER
        halt = true;
				return;
#else
				exit(1);
#endif
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
      wcout << L"stack oper: JMP; call_pos=" << (*call_stack_pos) << endl;
#endif
      if(!instr->GetOperand3()) {
				if(instr->GetOperand2() < 0) {
					ip = (*frame)->method->GetLabelIndex(instr->GetOperand()) + 1;
					instr->SetOperand3(ip);
				} 
				else if(PopInt(op_stack, stack_pos) == instr->GetOperand2()) {
					ip = (*frame)->method->GetLabelIndex(instr->GetOperand()) + 1;
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
  wcout << L"---------------------------" << endl;
  wcout << L"Dispatch method='" << mthd_name << "', time=" << (double)(end - start) / CLOCKS_PER_SEC << " second(s)." << endl;
#endif
}

/********************************
 * Processes a load function
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessLoadFunction(StackInstr* instr, long* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  wcout << L"stack oper: LOAD_FUNC_VAR; index=" << instr->GetOperand()
				<< "; local=" << ((instr->GetOperand2() == LOCL) ? "true" : "false") << endl;
#endif
  if(instr->GetOperand2() == LOCL) {
    long* mem = (*frame)->mem;
    PushInt(mem[instr->GetOperand() + 2], op_stack, stack_pos);
    PushInt(mem[instr->GetOperand() + 1], op_stack, stack_pos);
  } 
  else {
    long* cls_inst_mem = (long*)PopInt(op_stack, stack_pos);
    if(!cls_inst_mem) {
      wcerr << L">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
      StackErrorUnwind();
#ifdef _DEBUGGER
      halt = true;
      return;
#else
      exit(1);
#endif
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
  wcout << L"stack oper: LOAD_FLOAT_VAR; index=" << instr->GetOperand()
				<< "; local=" << ((instr->GetOperand2() == LOCL) ? "true" : "false") << endl;
#endif
  FLOAT_VALUE value;
  if(instr->GetOperand2() == LOCL) {
    long* mem = (*frame)->mem;
    memcpy(&value, &mem[instr->GetOperand() + 1], sizeof(FLOAT_VALUE));
  } else {
    long* cls_inst_mem = (long*)PopInt(op_stack, stack_pos);
    if(!cls_inst_mem) {
      wcerr << L">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
      StackErrorUnwind();
#ifdef _DEBUGGER
      halt = true;
      return;
#else
      exit(1);
#endif
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
  wcout << L"stack oper: STOR_FUNC_VAR; index=" << instr->GetOperand()
				<< "; local=" << ((instr->GetOperand2() == LOCL) ? "true" : "false") << endl;
#endif
  if(instr->GetOperand2() == LOCL) {
    long* mem = (*frame)->mem;
    mem[instr->GetOperand() + 1] = PopInt(op_stack, stack_pos);
    mem[instr->GetOperand() + 2] = PopInt(op_stack, stack_pos);
  } 
  else {
    long* cls_inst_mem = (long*)PopInt(op_stack, stack_pos);
    if(!cls_inst_mem) {
      wcerr << L">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
      StackErrorUnwind();
#ifdef _DEBUGGER
      halt = true;
      return;
#else
      exit(1);
#endif
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
  wcout << L"stack oper: STOR_FLOAT_VAR; index=" << instr->GetOperand()
				<< "; local=" << ((instr->GetOperand2() == LOCL) ? "true" : "false") << endl;
#endif
  if(instr->GetOperand2() == LOCL) {
    FLOAT_VALUE value = PopFloat(op_stack, stack_pos);
    long* mem = (*frame)->mem;
    memcpy(&mem[instr->GetOperand() + 1], &value, sizeof(FLOAT_VALUE));
  } else {
    long* cls_inst_mem = (long*)PopInt(op_stack, stack_pos);
    if(!cls_inst_mem) {
      wcerr << L">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
      StackErrorUnwind();
#ifdef _DEBUGGER
      halt = true;
      return;
#else
      exit(1);
#endif
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
  wcout << L"stack oper: COPY_FLOAT_VAR; index=" << instr->GetOperand()
				<< "; local=" << ((instr->GetOperand2() == LOCL) ? "true" : "false") << endl;
#endif
  if(instr->GetOperand2() == LOCL) {
    FLOAT_VALUE value = TopFloat(op_stack, stack_pos);
    long* mem = (*frame)->mem;
    memcpy(&mem[instr->GetOperand() + 1], &value, sizeof(FLOAT_VALUE));
  } else {
    long* cls_inst_mem = (long*)PopInt(op_stack, stack_pos);
    if(!cls_inst_mem) {
      wcerr << L">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
      StackErrorUnwind();
#ifdef _DEBUGGER
      halt = true;
      return;
#else
      exit(1);
#endif
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
  wcout << L"stack oper: NEW_OBJ_INST: id=" << instr->GetOperand() << endl;
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
  wcout << L"stack oper: NEW_INT_ARY/NEW_FLOAT_ARY; call_pos=" << (*call_stack_pos) << endl;
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
  wcout << L"stack oper: NEW_BYTE_ARY; call_pos=" << (*call_stack_pos) << endl;
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
  // NULL terminated string 
  size++;
  long* mem = (long*)MemoryManager::AllocateArray(size + ((dim + 2) * sizeof(long)),
																									BYTE_ARY_TYPE, op_stack, *stack_pos);
  mem[0] = size - 1;
  mem[1] = dim;
  memcpy(mem + 2, indices, dim * sizeof(long));
  PushInt((long)mem, op_stack, stack_pos);
}

/********************************
 * Processes a new char array instance
 * request.
 ********************************/
void StackInterpreter::ProcessNewCharArray(StackInstr* instr, long* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  wcout << L"stack oper: NEW_CHAR_ARY; call_pos=" << (*call_stack_pos) << endl;
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
  // NULL terminated string 
  size++;
  long* mem = (long*)MemoryManager::AllocateArray(size + ((dim + 2) * sizeof(long)),
																									CHAR_ARY_TYPE, op_stack, *stack_pos);
  mem[0] = size - 1;
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
  wcout << L"stack oper: RTRN; call_pos=" << (*call_stack_pos) << endl;
#endif

  // unregister old frame
#ifdef _DEBUG
  wcout << L"removing frame=" << (*frame) << endl;
#endif
	
	ReleaseStackFrame(*frame);
	
  // restore previous frame
  if(!StackEmpty()) {
    (*frame) = PopFrame();
    instrs = (*frame)->method->GetInstructions();
    ip = (*frame)->ip;
  } 
  else {
    (*frame) = NULL;
    halt = true;
  }
}

/********************************
 * Processes a asynchronous
 * method call.
 ********************************/
void StackInterpreter::ProcessAsyncMethodCall(StackMethod* called, long* param)
{
  long* instance = (long*)(*frame)->mem[0];
  ThreadHolder* holder = new ThreadHolder;
  holder->called = called;
  holder->self = instance;
  holder->param = param;

#ifdef _WIN32
  HANDLE vm_thread = (HANDLE)_beginthreadex(NULL, 0, AsyncMethodCall, holder, 0, NULL);
  if(!vm_thread) {
    wcerr << L">>> Internal error: Unable to create garbage collection thread! <<<" << endl;
    exit(-1);
  }
#else
  pthread_attr_t attrs;
  pthread_attr_init(&attrs);
  pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_JOINABLE);

  // execute thread
  pthread_t vm_thread;
  if(pthread_create(&vm_thread, &attrs, AsyncMethodCall, (void*)holder)) {
    wcerr << L">>> Internal error: Internal error: Unable to create runtime thread! <<<" << endl;
    exit(-1);
  }
#endif  
  
  // assign thread ID
  if(!instance) {
    wcerr << L">>> Internal error: Unable to create runtime thread! <<<" << endl;
    exit(-1);
  }

  instance[0] = (long)vm_thread;
#ifdef _DEBUG
  wcout << L"*** New Thread ID: " << vm_thread  << ": " << instance << " ***" << endl;
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
  wcout << L"# Starting thread=" << vm_thread << " #" << endl;
#endif  

  Runtime::StackInterpreter intpr;
  intpr.Execute(thread_op_stack, thread_stack_pos, 0, holder->called, holder->self, false);

#ifdef _DEBUG
  wcout << L"# final stack: pos=" << (*thread_stack_pos) << ", thread=" << vm_thread << " #" << endl;
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
  wcout << L"# Starting thread=" << pthread_self() << " #" << endl;
#endif  

  Runtime::StackInterpreter intpr;
  intpr.Execute(thread_op_stack, thread_stack_pos, 0, holder->called, holder->self, false);

#ifdef _DEBUG
  wcout << L"# final stack: pos=" << (*thread_stack_pos) << ", thread=" << pthread_self() << " #" << endl;
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
  (*frame)->ip = ip;
  PushFrame((*frame));

  // pop instance
  long* instance = (long*)PopInt(op_stack, stack_pos);

  // make call
  long cls_id = PopInt(op_stack, stack_pos);
  long mthd_id = PopInt(op_stack, stack_pos);
#ifdef _DEBUG
  wcout << L"stack oper: DYN_MTHD_CALL; cls_mtd_id=" << cls_id << "," << mthd_id << endl;
#endif
  StackMethod* called = program->GetClass(cls_id)->GetMethod(mthd_id);
#ifdef _DEBUG
  wcout << L"=== Binding function call: to: '" << called->GetName() << "' ===" << endl;
#endif

#ifndef _NO_JIT
  // execute JIT call
  if(instr->GetOperand3()) {
    ProcessJitMethodCall(called, instance, instrs, ip, op_stack, stack_pos);
  }
  // execute interpreter
  else {
    (*frame) = GetStackFrame(called, instance);		
    instrs = (*frame)->method->GetInstructions();
    ip = 0;
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
  (*frame)->ip = ip;
  PushFrame((*frame));

  // pop instance
  long* instance = (long*)PopInt(op_stack, stack_pos);

  // make call
  StackMethod* called = program->GetClass(instr->GetOperand())->GetMethod(instr->GetOperand2());
  // dynamically bind class for virutal method
  if(called->IsVirtual()) {
    StackClass* impl_class = MemoryManager::GetClass((long*)instance);
    if(!impl_class) {
      wcerr << L">>> Invalid instance reference! ref=" << instance << " << " << endl;
      StackErrorUnwind();
#ifdef _DEBUGGER
      halt = true;
      return;
#else
      exit(1);
#endif
    }
    
#ifdef _DEBUG
    wcout << L"=== Binding virtual method call: from: '" << called->GetName();
#endif

    // binding method
    const wstring& qualified_method_name = called->GetName();
    const wstring& method_ending = qualified_method_name.substr(qualified_method_name.find(L':'));
    wstring method_name = impl_class->GetName() + method_ending;

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
    wcout << L"'; to: '" << method_name << "' ===" << endl;
#endif
  }

#ifndef _NO_JIT
  // execute JIT call
  if(instr->GetOperand3()) {
    ProcessJitMethodCall(called, instance, instrs, ip, op_stack, stack_pos);
  }
  // execute interpreter
  else {
    (*frame) = GetStackFrame(called, instance);
    instrs = (*frame)->method->GetInstructions();
    ip = 0;
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
  if(called->GetNativeCode()) {
    JitExecutorIA32 jit_executor;
    long status = jit_executor.Execute(called, (long*)instance, op_stack, stack_pos, call_stack, call_stack_pos);
    if(status < 0) {
      switch(status) {
      case -1:
        wcerr << L">>> Atempting to dereference a 'Nil' memory instance in native JIT code <<<" << endl;
        break;
      case -2:
      case -3:
        wcerr << L">>> Index out of bounds in native JIT code! <<<" << endl;
        break;
      }
      StackErrorUnwind(called);
#ifdef _DEBUGGER
      halt = true;
      return;
#else
      exit(1);
#endif
    }
    // restore previous state
    (*frame) = PopFrame();
    instrs = (*frame)->method->GetInstructions();
    ip = (*frame)->ip;
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
      wcerr << L"### Unable to compile: " << called->GetName() << " ###" << endl;
#endif
      return;
    }
    // execute
    JitExecutorIA32 jit_executor;
    long status = jit_executor.Execute(called, (long*)instance, op_stack, stack_pos, call_stack, call_stack_pos);
    if(status < 0) {
      switch(status) {
      case -1:
        wcerr << L">>> Atempting to dereference a 'Nil' memory instance in native JIT code <<<" << endl;
        break;

      case -2:
      case -3:
        wcerr << L">>> Index out of bounds in native JIT code! <<<" << endl;
        break;
      }
      StackErrorUnwind(called);
#ifdef _DEBUGGER
      halt = true;
      return;
#else
      exit(1);
#endif
    }
    // restore previous state
    (*frame) = PopFrame();
    instrs = (*frame)->method->GetInstructions();
    ip = (*frame)->ip;
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
  wcout << L"=== MTHD_CALL: id=" << called->GetClass()->GetId() << ","
				<< called->GetId() << "; name='" << called->GetName() << "' ===" << endl;
#endif	
	(*frame) = GetStackFrame(called, instance);
  instrs = (*frame)->method->GetInstructions();
  ip = 0;
#ifdef _DEBUG
  wcout << L"creating frame=" << (*frame) << endl;
#endif
}

/********************************
 * Processes a load integer array
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessLoadIntArrayElement(StackInstr* instr, long* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  wcout << L"stack oper: LOAD_INT_ARY_ELM; call_pos=" << (*call_stack_pos) << endl;
#endif
  long* array = (long*)PopInt(op_stack, stack_pos);
  if(!array) {
    wcerr << L">>> Atempting to dereference a 'Nil' memory element <<<" << endl;
    StackErrorUnwind();
#ifdef _DEBUGGER
    halt = true;
    exit(1);
#else
    return;
#endif
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
  wcout << L"stack oper: STOR_INT_ARY_ELM; call_pos=" << (*call_stack_pos) << endl;
#endif

  long* array = (long*)PopInt(op_stack, stack_pos);
  if(!array) {
    wcerr << L">>> Atempting to dereference a 'Nil' memory element <<<" << endl;
    StackErrorUnwind();
#ifdef _DEBUGGER
    halt = true;
    exit(1);
#else
    return;
#endif
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
  wcout << L"stack oper: LOAD_BYTE_ARY_ELM; call_pos=" << (*call_stack_pos) << endl;
#endif
  long* array = (long*)PopInt(op_stack, stack_pos);
  if(!array) {
    wcerr << L">>> Atempting to dereference a 'Nil' memory element <<<" << endl;
    StackErrorUnwind();
#ifdef _DEBUGGER
    halt = true;
    exit(1);
#else
    return;
#endif
  }
  const long size = array[0];
  array += 2;
  long index = ArrayIndex(instr, array, size, op_stack, stack_pos);
  array += instr->GetOperand();
  PushInt(((char*)array)[index], op_stack, stack_pos);
}

/********************************
 * Processes a load char array
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessLoadCharArrayElement(StackInstr* instr, long* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  wcout << L"stack oper: LOAD_CHAR_ARY_ELM; call_pos=" << (*call_stack_pos) << endl;
#endif
  long* array = (long*)PopInt(op_stack, stack_pos);
  if(!array) {
    wcerr << L">>> Atempting to dereference a 'Nil' memory element <<<" << endl;
    StackErrorUnwind();
#ifdef _DEBUGGER
    halt = true;
    exit(1);
#else
    return;
#endif
  }
  const long size = array[0];
  array += 2;
  long index = ArrayIndex(instr, array, size, op_stack, stack_pos);
  array += instr->GetOperand();
  PushInt(((wchar_t*)array)[index], op_stack, stack_pos);
}

/********************************
 * Processes a store byte array
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessStoreByteArrayElement(StackInstr* instr, long* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  wcout << L"stack oper: STOR_BYTE_ARY_ELM; call_pos=" << (*call_stack_pos) << endl;
#endif
  long* array = (long*)PopInt(op_stack, stack_pos);
  if(!array) {
    wcerr << L">>> Atempting to dereference a 'Nil' memory element <<<" << endl;
    StackErrorUnwind();
#ifdef _DEBUGGER
    halt = true;
    exit(1);
#else
    return;
#endif
  }
  const long size = array[0];
  array += 2;
  long index = ArrayIndex(instr, array, size, op_stack, stack_pos);
  array += instr->GetOperand();
  ((char*)array)[index] = (char)PopInt(op_stack, stack_pos);
}

/********************************
 * Processes a store char array
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessStoreCharArrayElement(StackInstr* instr, long* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  wcout << L"stack oper: STOR_CHAR_ARY_ELM; call_pos=" << (*call_stack_pos) << endl;
#endif
  long* array = (long*)PopInt(op_stack, stack_pos);
  if(!array) {
    wcerr << L">>> Atempting to dereference a 'Nil' memory element <<<" << endl;
    StackErrorUnwind();
#ifdef _DEBUGGER
    halt = true;
    exit(1);
#else
    return;
#endif
  }
  const long size = array[0];
  array += 2;
  long index = ArrayIndex(instr, array, size, op_stack, stack_pos);
  array += instr->GetOperand();
  ((wchar_t*)array)[index] = (wchar_t)PopInt(op_stack, stack_pos);
}

/********************************
 * Processes a load float array
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessLoadFloatArrayElement(StackInstr* instr, long* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  wcout << L"stack oper: LOAD_FLOAT_ARY_ELM; call_pos=" << (*call_stack_pos) << endl;
#endif
  long* array = (long*)PopInt(op_stack, stack_pos);
  if(!array) {
    wcerr << L">>> Atempting to dereference a 'Nil' memory element <<<" << endl;
    StackErrorUnwind();
#ifdef _DEBUGGER
    halt = true;
    exit(1);
#else
    return;
#endif
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
  wcout << L"stack oper: STOR_FLOAT_ARY_ELM; call_pos=" << (*call_stack_pos) << endl;
#endif
  long* array = (long*)PopInt(op_stack, stack_pos);
  if(!array) {
    wcerr << L">>> Atempting to dereference a 'Nil' memory element <<<" << endl;
    StackErrorUnwind();
#ifdef _DEBUGGER
    halt = true;
    exit(1);
#else
    return;
#endif
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
  wcout << L"stack oper: shared library_LOAD; call_pos=" << (*call_stack_pos) << endl;
#endif
  long* instance = (long*)(*frame)->mem[0];
  if(!instance) {
    wcerr << L">>> Unable to load shared library! <<<" << endl;
#ifdef _DEBUGGER
    exit(1);
#else
    return;
#endif
  }

  long* str_obj = (long*)instance[0];
  if(!str_obj || !(long*)str_obj[0]) {
    wcerr << L">>> Name of runtime shared library was not specified! <<<" << endl;
#ifdef _DEBUGGER
    exit(1);
#else
    return;
#endif
  }

  long* array = (long*)str_obj[0];
  wstring str((wchar_t*)(array + 3));
  string dll_string(str.begin(), str.end());
  if(dll_string.size() == 0) {
    wcerr << L">>> Name of runtime shared library was not specified! <<<" << endl;
#ifdef _DEBUGGER
    exit(1);
#else
    return;
#endif
  }

#ifdef _WIN32
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
    wcerr << L">>> Runtime error loading shared library: " << dll_string.c_str() << " <<<" << endl;
#ifdef _DEBUGGER
    exit(1);
#else
    return;
#endif
  }
  instance[1] = (long)dll_handle;

  // call load function
  ext_load_def ext_load = (ext_load_def)GetProcAddress(dll_handle, "load_lib");
  if(!ext_load) {
    wcerr << L">>> Runtime error calling function: load_lib <<<" << endl;
    FreeLibrary(dll_handle);
#ifdef _DEBUGGER
    exit(1);
#else
    return;
#endif
  }
  (*ext_load)();
#else
  void* dll_handle = dlopen(dll_string.c_str(), RTLD_LAZY);
  if(!dll_handle) {
    wcerr << L">>> Runtime error loading shared library: " << dlerror() << " <<<" << endl;
#ifdef _DEBUGGER
    exit(1);
#else
    return;
#endif
  }
  instance[1] = (long)dll_handle;

  // call load function
  ext_load_def ext_load = (ext_load_def)dlsym(dll_handle, "load_lib");
  char* error;
  if((error = dlerror()) != NULL)  {
    wcerr << L">>> Runtime error calling function: " << error << " <<<" << endl;
#ifdef _DEBUGGER
    exit(1);
#else
    return;
#endif
  }
  // call function
  (*ext_load)();
#endif
}

typedef void (*ext_unload_def)();
void StackInterpreter::ProcessDllUnload(StackInstr* instr)
{
#ifdef _DEBUG
  wcout << L"stack oper: shared library_UNLOAD; call_pos=" << (*call_stack_pos) << endl;
#endif
  long* instance = (long*)(*frame)->mem[0];
  // unload shared library
#ifdef _WIN32
  HINSTANCE dll_handle = (HINSTANCE)instance[1];
  if(dll_handle) {
    // call unload function  
    ext_load_def ext_unload = (ext_load_def)GetProcAddress(dll_handle, "unload_lib");
    if(!ext_unload) {
      wcerr << L">>> Runtime error calling function: unload_lib <<<" << endl;
      FreeLibrary(dll_handle);
#ifdef _DEBUGGER
      return;
#else
      exit(1);
#endif
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
      wcerr << L">>> Runtime error calling function: " << error << " <<<" << endl;
#ifdef _DEBUGGER
      return;
#else
      exit(1);
#endif
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
  wcout << L"stack oper: shared library_FUNC_CALL; call_pos=" << (*call_stack_pos) << endl;
#endif 
  long* instance = (long*)(*frame)->mem[0];
  long* str_obj = (long*)(*frame)->mem[1];
  long* array = (long*)str_obj[0];
  if(!array) {
    wcerr << L">>> Runtime error calling function <<<" << endl;
#ifdef _DEBUGGER
    exit(1);
#else
    return;
#endif
  }

  const wstring wstr((wchar_t*)(array + 3));
  long* args = (long*)(*frame)->mem[2];
  lib_func_def ext_func;

#ifdef _WIN32
  HINSTANCE dll_handle = (HINSTANCE)instance[1];
  if(dll_handle) {
    // get function pointer
    const string str(wstr.begin(), wstr.end());
    ext_func = (lib_func_def)GetProcAddress(dll_handle, str.c_str());
    if(!ext_func) {
      wcerr << L">>> Runtime error calling function: " << wstr << " <<<" << endl;
      FreeLibrary(dll_handle);
#ifdef _DEBUGGER
      return;
#else
      exit(1);
#endif
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
    const string str(wstr.begin(), wstr.end());
    ext_func = (lib_func_def)dlsym(dll_handle, str.c_str());
    char* error;
    if((error = dlerror()) != NULL)  {
      wcerr << L">>> Runtime error calling function: " << error << " <<<" << endl;
#ifdef _DEBUGGER
      return;
#else
      exit(1);
#endif
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

