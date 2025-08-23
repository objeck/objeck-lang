/***************************************************************************
 * Common JIT compiler functions
 *
 * Copyright (c) 2025 Randy Hollines
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list  of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in
 * the documentation and/or other materials provided with the distribution.
 * - Neither the name of the Objeck Team nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, RXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, RVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ***************************************************************************/

#include "jit_common.h"

StackProgram* JitCompiler::program;

void JitCompiler::Initialize(StackProgram* p)
{
  program = p;
}

JitCompiler::JitCompiler()
{

}

JitCompiler::~JitCompiler()
{

}

/**
 * JIT machine code callback
 */
void JitCompiler::JitStackCallback(const long instr_id, StackInstr* instr, const long cls_id,
                                   const long mthd_id, size_t* inst, size_t* op_stack, size_t* stack_pos,
                                   StackFrame** call_stack, long* call_stack_pos, const long ip)
{
#ifdef _DEBUG_JIT
  std::wcout << L"Stack Call: instr=" << instr_id
    << L", oper_1=" << instr->GetOperand() << L", oper_2=" << instr->GetOperand2()
    << L", oper_3=" << instr->GetOperand3() << L", self=" << inst << L"("
    << (size_t)inst << L"), std::stack=" << op_stack << L", stack_addr=" << stack_pos
    << L", stack_pos=" << (*stack_pos) << std::endl;
#endif
  switch(instr_id) {
  case MTHD_CALL:
  case DYN_MTHD_CALL: {
#ifdef _DEBUG_JIT
    StackMethod* called = program->GetClass(instr->GetOperand())->GetMethod(instr->GetOperand2());
    std::wcout << L"jit oper: MTHD_CALL: mthd=" << called->GetName() << std::endl;
#endif
    Runtime::StackInterpreter intpr(call_stack, call_stack_pos);
    intpr.Execute(op_stack, stack_pos, ip, program->GetClass(cls_id)->GetMethod(mthd_id), inst, true);
  }
    break;

  case LOAD_ARY_SIZE: {
    size_t* array = (size_t*)PopInt(op_stack, stack_pos);
    if(!array) {
      std::wcerr << L"Attempting to dereference a 'Nil' memory instance" << std::endl;
      std::wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << std::endl;
      exit(1);
    }
    PushInt(op_stack, stack_pos, array[2]);
  }
    break;

  case RAND_FLOAT:
    PushFloat(MemoryManager::GetRandomValue(), op_stack, stack_pos);
    break;

  case NEW_BYTE_ARY: {
    size_t indices[8];
    size_t value = PopInt(op_stack, stack_pos);
    size_t size = value;
    indices[0] = value;
    long dim = 1;
    for(long i = 1; i < instr->GetOperand(); ++i) {
      size_t value = PopInt(op_stack, stack_pos);
      size *= value;
      indices[dim++] = value;
    }

    // null terminated string workaround
    size++;
    size_t* mem = MemoryManager::AllocateArray((int64_t)(size + ((dim + 2) * sizeof(size_t))), BYTE_ARY_TYPE, op_stack, *stack_pos);
    mem[0] = size;
    mem[1] = dim;
    memcpy(mem + 2, indices, dim * sizeof(size_t));
    PushInt(op_stack, stack_pos, (size_t)mem);

#ifdef _DEBUG_JIT
    std::wcout << L"jit oper: NEW_BYTE_ARY: dim=" << dim << L"; size=" << size
      << L"; index=" << (*stack_pos) << L"; mem=" << mem << std::endl;
#endif
  }
    break;

  case NEW_CHAR_ARY: {
    size_t indices[8];
    size_t value = PopInt(op_stack, stack_pos);
    size_t size = value;
    indices[0] = value;
    long dim = 1;
    for(long i = 1; i < instr->GetOperand(); ++i) {
      size_t value = PopInt(op_stack, stack_pos);
      size *= value;
      indices[dim++] = value;
    }

    size++;
    size_t* mem = (size_t*)MemoryManager::AllocateArray((int64_t)(size + ((dim + 2) * sizeof(size_t))), CHAR_ARY_TYPE, op_stack, *stack_pos);
    mem[0] = size - 1;
    mem[1] = dim;
    memcpy(mem + 2, indices, dim * sizeof(size_t));
    PushInt(op_stack, stack_pos, (size_t)mem);

#ifdef _DEBUG_JIT
    std::wcout << L"jit oper: NEW_CHAR_ARY: dim=" << dim << L"; size=" << size
      << L"; index=" << (*stack_pos) << L"; mem=" << mem << std::endl;
#endif
  }
    break;

  case NEW_INT_ARY: {
    size_t indices[8];
    size_t value = PopInt(op_stack, stack_pos);
    size_t size = value;
    indices[0] = value;
    long dim = 1;
    for(long i = 1; i < instr->GetOperand(); ++i) {
      size_t value = PopInt(op_stack, stack_pos);
      size *= value;
      indices[dim++] = value;
    }

    size_t* mem = (size_t*)MemoryManager::AllocateArray((int64_t)(size + dim + 2), INT_TYPE, op_stack, *stack_pos);
#ifdef _DEBUG_JIT
    std::wcout << L"jit oper: NEW_INT_ARY: dim=" << dim << L"; size=" << size
      << L"; index=" << (*stack_pos) << L"; mem=" << mem << std::endl;
#endif
    mem[0] = size;
    mem[1] = dim;
    memcpy(mem + 2, indices, dim * sizeof(size_t));
    PushInt(op_stack, stack_pos, (size_t)mem);
  }
    break;

  case NEW_FLOAT_ARY: {
    size_t indices[8];
    size_t value = PopInt(op_stack, stack_pos);
    size_t size = value;
    indices[0] = value;
    long dim = 1;
    for(long i = 1; i < instr->GetOperand(); ++i) {
      size_t value = PopInt(op_stack, stack_pos);
      size *= value;
      indices[dim++] = value;
    }

    size_t* mem = (size_t*)MemoryManager::AllocateArray((int64_t)(size + dim + 2), FLOAT_TYPE, op_stack, *stack_pos);
    mem[0] = size;
    mem[1] = dim;

    memcpy(mem + 2, indices, dim * sizeof(size_t));
    PushInt(op_stack, stack_pos, (size_t)mem);
  }
    break;

  case NEW_OBJ_INST: {
#ifdef _DEBUG_JIT
    StackClass* klass = program->GetClass(instr->GetOperand());
    std::wcout << L"jit oper: NEW_OBJ_INST: class=" << klass->GetName() << std::endl;
#endif
    size_t* mem = MemoryManager::AllocateObject(instr->GetOperand(), op_stack, *stack_pos);
    PushInt(op_stack, stack_pos, (size_t)mem);
  }
    break;

  case I2S: {
    size_t* str_ptr = (size_t*)PopInt(op_stack, stack_pos);
    if(str_ptr) {
      wchar_t* str = (wchar_t*)(str_ptr + 3);
      const size_t base = PopInt(op_stack, stack_pos);
      const int64_t value = (int64_t)PopInt(op_stack, stack_pos);

      std::wstring conv;
      std::wstringstream formatter;

      const std::wstring int_format = program->GetProperty(L"int:string:format");
      if(!int_format.empty()) {
        if(int_format == L"dec") {
          formatter << std::dec;
        }
        else if(int_format == L"oct") {
          formatter << std::oct;
        }
        else if(int_format == L"hex") {
          formatter << std::hex;
        }

        formatter << value;
        conv = formatter.str();
      }
      else {
        switch(base) {
        case 8:
          formatter << std::oct;
          break;

        case 10:
          formatter << std::dec;
          break;

        case 16:
          formatter << std::hex << L"0x";
          break;
        }

        formatter << value;
        conv = formatter.str();
      }

      const size_t max = str_ptr[0];
#ifdef _WIN32
      wcsncpy_s(str, str_ptr[0] + 1, conv.c_str(), max);
#else
      wcsncpy(str, conv.c_str(), max);
#endif
    }
  }
    break;

  case F2S: {
    size_t* str_ptr = (size_t*)PopInt(op_stack, stack_pos);
    if(str_ptr) {
      wchar_t* str = (wchar_t*)(str_ptr + 3);
      const FLOAT_VALUE value = PopFloat(op_stack, stack_pos);

      std::wstringstream formatter;
      std::wstring conv;

      const std::wstring float_format = program->GetProperty(L"float:string:format");
      const std::wstring float_precision = program->GetProperty(L"float:string:precision");

      if(!float_format.empty() && !float_precision.empty()) {
        if(float_format == L"fixed") {
          formatter << std::fixed;
        }
        else if(float_format == L"scientific") {
          formatter << std::scientific;
        }
        else if(float_format == L"hex") {
          formatter << std::hexfloat;
        }
        formatter << std::setprecision(stoll(float_precision));

        formatter << value;
        conv = formatter.str();
      }
      else if(!float_format.empty()) {
        if(float_format == L"fixed") {
          formatter << std::fixed;
        }
        else if(float_format == L"scientific") {
          formatter << std::scientific;
        }
        else if(float_format == L"hex") {
          formatter << std::hexfloat;
        }

        formatter << value;
        conv = formatter.str();
      }
      else if(!float_precision.empty()) {
        formatter << std::setprecision(stoll(float_precision));

        formatter << value;
        conv = formatter.str();
      }
      else {
        conv = std::to_wstring(value);
      }

      const size_t max = conv.size() < 64 ? conv.size() : 64;
#ifdef _WIN32
      wcsncpy_s(str, str_ptr[0], conv.c_str(), max);
#else
      wcsncpy(str, conv.c_str(), max);
#endif
    }
  }
    break;

  case S2F: {
    size_t* str_ptr = (size_t*)PopInt(op_stack, stack_pos);
    if(str_ptr) {
      wchar_t* str = (wchar_t*)(str_ptr + 3);
      const FLOAT_VALUE value = std::stod(str);
      PushFloat(value, op_stack, stack_pos);
    }
    else {
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
      exit(1);
    }
  }
    break;

  case S2I: {
#ifdef _DEBUG_JIT
    std::wcout << L"stack oper: S2I; call_pos=" << (*call_stack_pos) << std::endl;
#endif

    size_t* str_ptr = (size_t*)PopInt(op_stack, stack_pos);
    const long base = (long)PopInt(op_stack, stack_pos);
    if(str_ptr) {
      wchar_t* str = (wchar_t*)(str_ptr + 3);
      try {
        if(wcslen(str) > 2) {
          switch(str[1]) {
            // binary
          case 'b':
            PushInt(op_stack, stack_pos, std::stoll(str + 2, nullptr, 2));
            return;

            // octal
          case 'o':
            PushInt(op_stack, stack_pos, std::stoll(str + 2, nullptr, 8));
            return;

            // hexadecimal
          case 'x':
          case 'X':
            PushInt(op_stack, stack_pos, std::stoll(str + 2, nullptr, 16));
            return;

          default:
      break;
          }
        }
        PushInt(op_stack, stack_pos, std::stoll(str, nullptr, base));
      }
      catch(std::invalid_argument& e) {
#ifdef _WIN32
        UNREFERENCED_PARAMETER(e);
#endif
        PushInt(op_stack, stack_pos, 0);
      }
    }
    else {
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
      exit(1);
    }
  }
    break;

  case OBJ_TYPE_OF: {
    size_t* mem = (size_t*)PopInt(op_stack, stack_pos);
    size_t* result = MemoryManager::ValidObjectCast(mem, instr->GetOperand(), program->GetHierarchy(), program->GetInterfaces());
    if(result) {
      PushInt(op_stack, stack_pos, 1);
    }
    else {
      PushInt(op_stack, stack_pos, 0);
    }
  }
    break;

  case OBJ_INST_CAST: {
    size_t* mem = (size_t*)PopInt(op_stack, stack_pos);
    long to_id = instr->GetOperand();
#ifdef _DEBUG_JIT
    std::wcout << L"jit oper: OBJ_INST_CAST: from=" << mem << L", to=" << to_id << std::endl;
#endif
    size_t result = (size_t)MemoryManager::ValidObjectCast(mem, to_id, program->GetHierarchy(), program->GetInterfaces());
    if(!result && mem) {
      StackClass* to_cls = MemoryManager::GetClass(mem);
      std::wcerr << L">>> Invalid object cast: '" << (to_cls ? to_cls->GetName() : L"?")
        << L"' to '" << program->GetClass(to_id)->GetName() << L"' <<<" << std::endl;
      std::wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << std::endl;
      exit(1);
    }
    PushInt(op_stack, stack_pos, result);
  }
    break;

    //----------- threads -----------

  case THREAD_JOIN: {
    size_t* instance = inst;
    if(!instance) {
      std::wcerr << L"Attempting to dereference a 'Nil' memory instance" << std::endl;
      std::wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << std::endl;
      exit(1);
    }

#ifdef _WIN32
    HANDLE vm_thread = (HANDLE)instance[0];
    if(WaitForSingleObject(vm_thread, INFINITE) != WAIT_OBJECT_0) {
      std::wcerr << L"Unable to join thread!" << std::endl;
      exit(-1);
    }
#else
    void* status;
    pthread_t vm_thread = (pthread_t)instance[0];
    if(pthread_join(vm_thread, &status)) {
      std::wcerr << L"Unable to join thread!" << std::endl;
      exit(-1);
    }
#endif      
  }
    break;

  case THREAD_SLEEP:
    std::this_thread::sleep_for(std::chrono::milliseconds(PopInt(op_stack, stack_pos)));
    break;

  case THREAD_MUTEX: {
    size_t* instance = inst;
    if(!instance) {
      std::wcerr << L"Attempting to dereference a 'Nil' memory instance" << std::endl;
      std::wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << std::endl;
      exit(1);
    }
#ifdef _WIN32      
    InitializeCriticalSection((CRITICAL_SECTION*)&instance[1]);
#else
    pthread_mutex_init((pthread_mutex_t*)&instance[1], nullptr);
#endif    
  }
    break;

  case CRITICAL_START: {
    size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
    if(!instance) {
      std::wcerr << L"Attempting to dereference a 'Nil' memory instance" << std::endl;
      std::wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << std::endl;
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
    size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
    if(!instance) {
      std::wcerr << L"Attempting to dereference a 'Nil' memory instance" << std::endl;
      std::wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << std::endl;
      exit(1);
    }
#ifdef _WIN32      
    LeaveCriticalSection((CRITICAL_SECTION*)&instance[1]);
#else
    pthread_mutex_unlock((pthread_mutex_t*)&instance[1]);
#endif      
  }
    break;

    // ---------------- memory copy ----------------
  case CPY_BYTE_ARY: {
    long length = (long)PopInt(op_stack, stack_pos);
    const long src_offset = (long)PopInt(op_stack, stack_pos);
    size_t* src_array = (size_t*)PopInt(op_stack, stack_pos);
    const long dest_offset = (long)PopInt(op_stack, stack_pos);
    size_t* dest_array = (size_t*)PopInt(op_stack, stack_pos);

    if(!src_array || !dest_array) {
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
      std::wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << std::endl;
      exit(1);
    }

    const long src_array_len = (long)src_array[2];
    const long dest_array_len = (long)dest_array[2];
    if(length > 0 && src_offset + length <= src_array_len && dest_offset + length <= dest_array_len) {
      char* src_array_ptr = (char*)(src_array + 3);
      char* dest_array_ptr = (char*)(dest_array + 3);
      if(src_array_ptr == dest_array_ptr) {
        memmove(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length);
      }
      else {
        memcpy(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length);
      }
      PushInt(op_stack, stack_pos, 1);
    }
    else {
      PushInt(op_stack, stack_pos, 0);
    }
  }
    break;

  case CPY_CHAR_ARY: {
    long length = (long)PopInt(op_stack, stack_pos);
    const long src_offset = (long)PopInt(op_stack, stack_pos);
    size_t* src_array = (size_t*)PopInt(op_stack, stack_pos);
    const long dest_offset = (long)PopInt(op_stack, stack_pos);
    size_t* dest_array = (size_t*)PopInt(op_stack, stack_pos);

    if(!src_array || !dest_array) {
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
      std::wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << std::endl;
      exit(1);
    }

    const long src_array_len = (long)src_array[2];
    const long dest_array_len = (long)dest_array[2];

    if(length > 0 && src_offset + length <= src_array_len && dest_offset + length <= dest_array_len) {
      const wchar_t* src_array_ptr = (wchar_t*)(src_array + 3);
      wchar_t* dest_array_ptr = (wchar_t*)(dest_array + 3);
      if(src_array_ptr == dest_array_ptr) {
        memmove(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length * sizeof(wchar_t));
      }
      else {
        memcpy(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length * sizeof(wchar_t));
      }
      PushInt(op_stack, stack_pos, 1);
    }
    else {
      PushInt(op_stack, stack_pos, 0);
    }
  }
    break;

  case CPY_INT_ARY: {
    long length = (long)PopInt(op_stack, stack_pos);
    const long src_offset = (long)PopInt(op_stack, stack_pos);
    size_t* src_array = (size_t*)PopInt(op_stack, stack_pos);
    const long dest_offset = (long)PopInt(op_stack, stack_pos);
    size_t* dest_array = (size_t*)PopInt(op_stack, stack_pos);

    if(!src_array || !dest_array) {
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
      std::wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << std::endl;
      exit(1);
    }

    const long src_array_len = (long)src_array[0];
    const long dest_array_len = (long)dest_array[0];
    if(length > 0 && src_offset + length <= src_array_len && dest_offset + length <= dest_array_len) {
      size_t* src_array_ptr = src_array + 3;
      size_t* dest_array_ptr = dest_array + 3;
      if(src_array_ptr == dest_array_ptr) {
        memmove(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length * sizeof(size_t));
      }
      else {
        memcpy(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length * sizeof(size_t));
      }
      PushInt(op_stack, stack_pos, 1);
    }
    else {
      PushInt(op_stack, stack_pos, 0);
    }
  }
    break;

  case CPY_FLOAT_ARY: {
    long length = (long)PopInt(op_stack, stack_pos);
    const long src_offset = (long)PopInt(op_stack, stack_pos);
    size_t* src_array = (size_t*)PopInt(op_stack, stack_pos);
    const long dest_offset = (long)PopInt(op_stack, stack_pos);
    size_t* dest_array = (size_t*)PopInt(op_stack, stack_pos);

    if(!src_array || !dest_array) {
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
      std::wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << std::endl;
      exit(1);
    }

    const long src_array_len = (long)src_array[0];
    const long dest_array_len = (long)dest_array[0];
    if(length > 0 && src_offset + length <= src_array_len && dest_offset + length <= dest_array_len) {
      size_t* src_array_ptr = src_array + 3;
      size_t* dest_array_ptr = dest_array + 3;
      if(src_array_ptr == dest_array_ptr) {
        memmove(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length * sizeof(FLOAT_VALUE));
      }
      else {
        memcpy(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length * sizeof(FLOAT_VALUE));
      }
      PushInt(op_stack, stack_pos, 1);
    }
    else {
      PushInt(op_stack, stack_pos, 0);
    }
  }
    break;

  case ZERO_BYTE_ARY: {
    size_t* array_ptr = (size_t*)PopInt(op_stack, stack_pos);
    const size_t array_len = array_ptr[0];
    char* buffer = (char*)(array_ptr + 3);
    memset(buffer, 0, array_len * sizeof(char));
  }
    break;

  case ZERO_CHAR_ARY: {
    size_t* array_ptr = (size_t*)PopInt(op_stack, stack_pos);
    const size_t array_len = array_ptr[0];
    wchar_t* buffer = (wchar_t*)(array_ptr + 3);
    memset(buffer, 0, array_len * sizeof(wchar_t));
  }
    break;

  case ZERO_INT_ARY: {
    size_t* array_ptr = (size_t*)PopInt(op_stack, stack_pos);
    const size_t array_len = array_ptr[0];
    size_t* buffer = (size_t*)(array_ptr + 3);
    memset(buffer, 0, array_len * sizeof(size_t));
  }
    break;

  case ZERO_FLOAT_ARY: {
    size_t* array_ptr = (size_t*)PopInt(op_stack, stack_pos);
    const size_t array_len = array_ptr[0];
    FLOAT_VALUE* buffer = (FLOAT_VALUE*)(array_ptr + 3);
    memset(buffer, 0, array_len * sizeof(FLOAT_VALUE));
  }
    break;

  case TRAP:
  case TRAP_RTRN:
    if(!TrapProcessor::ProcessTrap(program, inst, op_stack, stack_pos, nullptr)) {
      std::wcerr << L"  JIT compiled machine code..." << std::endl;
      exit(1);
    }
    break;

#ifdef _DEBUG_JIT
  default:
    std::wcerr << L"Unknown callback!" << std::endl;
    break;
#endif
  }
}

/**
 * Pop integer value
 */
size_t JitCompiler::PopInt(size_t* op_stack, size_t* stack_pos) {
  const size_t value = op_stack[--(*stack_pos)];
#ifdef _DEBUG_JIT
  std::wcout << L"\t[pop_i: value=" << (size_t*)value << L"(" << value << L")]" << L"; pos=" << (*stack_pos) << std::endl;
#endif

  return value;
}

/**
 * Push integer value
 */
void JitCompiler::PushInt(size_t* op_stack, size_t* stack_pos, size_t value) {
  op_stack[(*stack_pos)++] = value;
#ifdef _DEBUG_JIT
  std::wcout << L"\t[push_i: value=" << (size_t*)value << L"(" << value << L")]" << L"; pos=" << (*stack_pos) << std::endl;
#endif
}

/**
 * Pop FLOAT value
 */
FLOAT_VALUE JitCompiler::PopFloat(size_t* op_stack, size_t* stack_pos) {
  (*stack_pos)--;

#ifdef _DEBUG_JIT
  FLOAT_VALUE v = *((FLOAT_VALUE*)(&op_stack[(*stack_pos)]));
  std::wcout << L"  [pop_f: stack_pos=" << (*stack_pos) << L"; value=" << L"]; pos=" << (*stack_pos) << std::endl;
  return v;
#endif

  return *((FLOAT_VALUE*)(&op_stack[(*stack_pos)]));
}

/**
 * Push FLOAT value
 */
void JitCompiler::PushFloat(const FLOAT_VALUE v, size_t* op_stack, size_t* stack_pos) {
#ifdef _DEBUG_JIT
  std::wcout << L"  [push_f: stack_pos=" << (*stack_pos) << L"; value=" << v
    << L"]; call_pos=" << (*stack_pos) << std::endl;
#endif
  * ((FLOAT_VALUE*)(&op_stack[(*stack_pos)])) = v;
  (*stack_pos)++;
}