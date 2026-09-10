/***************************************************************************
 * JIT compiler for 64-bit AMD64 architectures (Windows, Linux and macOS).
 *
 * Copyright (c) 2026 Randy Hollines
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

#include "jit_amd_lp64.h"
#include <cstddef>
#include <unordered_map>
#include <algorithm>

// defined below, next to Compile(); PlanPinRegions reports pinned loops through it
static bool JitReportEnabled();
#include <string>
#include <mutex>

using namespace Runtime;

PageManager* JitAmd64::page_manager;

void JitAmd64::Initialize(StackProgram* p) {
  JitCompiler::Initialize(p);
  page_manager = new PageManager;
}

void JitAmd64::Prolog() {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [<prolog>]" << std::endl;
#endif

  // local_space was sized and aligned by Compile(): the frame is set up
  // twice, once per entry (EmitBridgePrologue, EmitNativePrologue), and both
  // must reserve the same amount
  unsigned char buffer[4];
  ByteEncode32(buffer, local_space);

  unsigned char setup_code[] = {
    // setup stack frame
    0x48, 0x55,                  // push $rbp
    0x48, 0x89, 0xe5,            // mov  $rsp, $rbp    
    0x48, 0x81, 0xec,            // sub  %imm, $rsp
    buffer[0], buffer[1], buffer[2], buffer[3],      
    // save registers
    0x48, 0x53,                  // push $rbx
    0x48, 0x51,                  // push $rcx
    0x48, 0x52,                  // push $rdx
    0x48, 0x57,                  // push $rdi
    0x48, 0x56,                  // push $rsi
#ifdef _WIN64
    // R12 is callee-saved and not used by the register allocator; reserve it to
    // cache &stw_active (see below). On Linux it is already pushed in the block
    // below. The extra `sub rsp,8` keeps RSP 16-byte aligned for the slow-path
    // SafePoint call (one push would otherwise leave it misaligned).
    0x49, 0x54,                  // push r12
    0x48, 0x83, 0xec, 0x08,      // sub  rsp, 8   (alignment filler)
#else
    0x49, 0x50,                  // push r8
    0x49, 0x51,                  // push r9
    0x49, 0x52,                  // push r10
    0x49, 0x53,                  // push r11
    0x49, 0x54,                  // push r12
    0x49, 0x55,                  // push r13
    0x49, 0x56,                  // push r14
    0x49, 0x57,                  // push r15
#endif
  };
  const long setup_size = sizeof(setup_code);
  // copy setup
  for(long i = 0; i < setup_size; ++i) {
    AddMachineCode(setup_code[i]);
  }

#ifdef _WIN64
  // XMM6-XMM15 are callee-saved in the Windows x64 ABI and the allocator
  // hands out XMM10-XMM15. This code is entered by a plain C++ call
  // (jit_fun), whose MSVC-compiled caller may keep values in those
  // registers across the call; nothing saved them. 96 bytes keeps RSP
  // 16-byte aligned. A method that never takes a pool register leaves them
  // untouched, so Compile() turns this block and the epilogue's restore into
  // a jump over themselves once the body is emitted (xmm_pool_used); the
  // index and size are recorded for that. Linux/macOS: every XMM is
  // caller-saved, nothing to do.
  const unsigned char xmm_save_code[] = {
    0x48, 0x83, 0xec, 0x60,      // sub  rsp, 96
    0xf3, 0x44, 0x0f, 0x7f, 0x54, 0x24, 0x00,   // movdqu [rsp+0],  xmm10
    0xf3, 0x44, 0x0f, 0x7f, 0x5c, 0x24, 0x10,   // movdqu [rsp+16], xmm11
    0xf3, 0x44, 0x0f, 0x7f, 0x64, 0x24, 0x20,   // movdqu [rsp+32], xmm12
    0xf3, 0x44, 0x0f, 0x7f, 0x6c, 0x24, 0x30,   // movdqu [rsp+48], xmm13
    0xf3, 0x44, 0x0f, 0x7f, 0x74, 0x24, 0x40,   // movdqu [rsp+64], xmm14
    0xf3, 0x44, 0x0f, 0x7f, 0x7c, 0x24, 0x50,   // movdqu [rsp+80], xmm15
  };
  xmm_save_indices.push_back(code_index);
  xmm_save_size = (long)sizeof(xmm_save_code);
  for(size_t i = 0; i < sizeof(xmm_save_code); ++i) {
    AddMachineCode(xmm_save_code[i]);
  }
#endif

  // Cache &stw_active in R12 (callee-saved) once per method. Each LBL's GC
  // safepoint poll then becomes a 5-byte `cmp byte [r12],0` instead of a 10-byte
  // movabs + 3-byte cmp — the per-label win in label-dense integer loops.
  move_imm_reg((int64_t)MemoryManager::StwActiveAddr(), R12);
#ifdef _WIN64
  // F3: R13-R15 hold pinned loop locals. Callee-saved on Windows and not in the
  // push list above; three pushes plus 8 bytes keep RSP 16-byte aligned. POSIX
  // pushes R8-R15 unconditionally.
  if(method_pins) {
    push_reg(R13);
    push_reg(R14);
    push_reg(R15);
    sub_imm_reg(8, RSP);
  }
  // phase 4d: XMM6-XMM9 hold pinned Float loop locals. Callee-saved on Windows
  // and outside the pool; 64 bytes keeps RSP 16-byte aligned. Saved below the
  // integer pushes, restored above them in the epilogue.
  if(method_pins_float) {
    sub_imm_reg(64, RSP);
    for(int i = 0; i < PIN_FREG_COUNT; ++i) {
      EmitXmmSave(true, i * 16, PinFloatRegister(i));
    }
  }
#endif

  // The outgoing area for the method's native calls (EmitNativeCallSite),
  // below everything the epilogue pops: a callee's register homes and stack
  // slots, then its arguments, at this frame's stack pointer for the whole
  // body. A multiple of 16 keeps the alignment.
  if(out_area > 0) {
    sub_imm_reg(out_area, RSP);
  }
}

void JitAmd64::Epilog() 
{
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [<epilog>]" << std::endl;
#endif
  epilog_index = code_index;

  // jump to nominal (backpatched)
  AddMachineCode(0xe9);
  long jmp_nominal_pos = code_index;
  AddImm(0);

  // null deference
  nil_deref_handler_index = code_index;
  move_imm_reg(-1, RAX);
  AddMachineCode(0xe9);
  long jmp_null_pos = code_index;
  AddImm(0);

  // under bounds
  bounds_less_handler_index = code_index;
  move_imm_reg(-2, RAX);
  AddMachineCode(0xe9);
  long jmp_under_pos = code_index;
  AddImm(0);

  // over bounds
  bounds_greater_handler_index = code_index;
  move_imm_reg(-3, RAX);
  AddMachineCode(0xe9);
  long jmp_over_pos = code_index;
  AddImm(0);

  // divide by 0
  div_by_zero_handler_index = code_index;
  move_imm_reg(-4, RAX);
  AddMachineCode(0xe9);
  long jmp_div_pos = code_index;
  AddImm(0);

  // set nominal
  long nominal_index = code_index;
  move_imm_reg(0, RAX);

  // backpatch epilog jumps
  long teardown_index = code_index;
  long jmp_offset;

  jmp_offset = nominal_index - (jmp_nominal_pos + 4);
  memcpy(&code[(size_t)jmp_nominal_pos], &jmp_offset, 4);

  jmp_offset = teardown_index - (jmp_null_pos + 4);
  memcpy(&code[(size_t)jmp_null_pos], &jmp_offset, 4);

  jmp_offset = teardown_index - (jmp_under_pos + 4);
  memcpy(&code[(size_t)jmp_under_pos], &jmp_offset, 4);

  jmp_offset = teardown_index - (jmp_over_pos + 4);
  memcpy(&code[(size_t)jmp_over_pos], &jmp_offset, 4);

  jmp_offset = teardown_index - (jmp_div_pos + 4);
  memcpy(&code[(size_t)jmp_div_pos], &jmp_offset, 4);

  // the outgoing area (see Prolog); this is teardown_index, so the error
  // handlers release it too
  if(out_area > 0) {
    add_imm_reg(out_area, RSP);
  }
#ifdef _WIN64
  // F3: undo the prologue's pinned-register save. This is teardown_index, so
  // every exit path -- nominal or an error handler -- restores them.
  if(method_pins_float) {
    for(int i = 0; i < PIN_FREG_COUNT; ++i) {
      EmitXmmSave(false, i * 16, PinFloatRegister(i));
    }
    add_imm_reg(64, RSP);
  }
  if(method_pins) {
    add_imm_reg(8, RSP);
    pop_reg(R15);
    pop_reg(R14);
    pop_reg(R13);
  }
#endif

#ifdef _WIN64
  // the XMM10-XMM15 restore; patched together with the prologue's save when
  // the method never used the pool (see Prolog). Every exit path runs it:
  // the error stubs land on the teardown above, before this.
  const unsigned char xmm_restore_code[] = {
    0xf3, 0x44, 0x0f, 0x6f, 0x54, 0x24, 0x00,   // movdqu xmm10, [rsp+0]
    0xf3, 0x44, 0x0f, 0x6f, 0x5c, 0x24, 0x10,   // movdqu xmm11, [rsp+16]
    0xf3, 0x44, 0x0f, 0x6f, 0x64, 0x24, 0x20,   // movdqu xmm12, [rsp+32]
    0xf3, 0x44, 0x0f, 0x6f, 0x6c, 0x24, 0x30,   // movdqu xmm13, [rsp+48]
    0xf3, 0x44, 0x0f, 0x6f, 0x74, 0x24, 0x40,   // movdqu xmm14, [rsp+64]
    0xf3, 0x44, 0x0f, 0x6f, 0x7c, 0x24, 0x50,   // movdqu xmm15, [rsp+80]
    0x48, 0x83, 0xc4, 0x60,  // add  rsp, 96  (XMM save area)
  };
  xmm_restore_indices.push_back(code_index);
  xmm_restore_size = (long)sizeof(xmm_restore_code);
  for(size_t i = 0; i < sizeof(xmm_restore_code); ++i) {
    AddMachineCode(xmm_restore_code[i]);
  }
#endif

  unsigned char teardown_code[] = {
    // restore registers
#ifdef _WIN64
    0x48, 0x83, 0xc4, 0x08,  // add  rsp, 8   (undo alignment filler)
    0x49, 0x5c,       // pop r12
#else
    0x49, 0x5f,       // pop r15
    0x49, 0x5e,       // pop r14
    0x49, 0x5d,       // pop r13
    0x49, 0x5c,       // pop r12
    0x49, 0x5b,       // pop r11
    0x49, 0x5a,       // pop r10
    0x49, 0x59,       // pop r9
    0x49, 0x58,       // pop r8
#endif
    0x48, 0x5e,       // pop $rsi
    0x48, 0x5f,       // pop $rdi
    0x48, 0x5a,       // pop $rdx
    0x48, 0x59,       // pop $rcx
    0x48, 0x5b,       // pop $rbx
    // tear down stack frame and return
    0x48, 0x89, 0xec, // mov $rbp, $rsp
    0x48, 0x5d,       // pop $rbp
    0x48, 0xc3        // rtn
  };
  const long teardown_size = sizeof(teardown_code);
  // copy teardown
  for(long i = 0; i < teardown_size; ++i) {
    AddMachineCode(teardown_code[i]);
  }
}

void JitAmd64::RegisterRoot() {
  // calculate root address
  // note: the offset required to 
  // get to the first local variable
#ifdef _WIN64
  const long offset = org_local_space + RED_ZONE + TMP_REG_9 + 8;
#else
  const long offset = org_local_space + RED_ZONE + TMP_REG_9;
#endif
  // get to stack locals
  RegisterHolder* holder = GetRegister();
  move_reg_reg(RBP, holder->GetRegister());
  sub_imm_reg(-TMP_REG_9 + offset, holder->GetRegister());

  // set JIT memory to stack locals
  RegisterHolder* mem_holder = GetRegister();
  move_mem_reg(JIT_MEM, RBP, mem_holder->GetRegister());
  move_reg_mem(holder->GetRegister(), 0, mem_holder->GetRegister());

  // 10 slots to hold spilled registers (TMP_REG_0..9)
  const int index = ((offset - 8) >> 3) + 10;
  // Zero them with straight stores for the common frame sizes: the LOOP
  // instruction is microcoded, and its eleven iterations for a one-local
  // method cost more than that method's body. Large frames keep the loop.
  static const int ZERO_UNROLL_MAX = 24;
  if(index > 0 && index <= ZERO_UNROLL_MAX) {
    for(int i = 0; i < index; ++i) {
      move_imm_mem(0, i * (long)sizeof(size_t), holder->GetRegister());
    }
  }
  else if(index > 0) {
    move_imm_reg(index, RCX);
    long loop_target = code_index;
    move_imm_mem(0, 0, holder->GetRegister());
    add_imm_reg(sizeof(size_t), holder->GetRegister());
    // LOOP rel8: offset is from end of LOOP instruction (2 bytes) back to loop_target
    long loop_end = code_index + 2;
    loop((int8_t)(loop_target - loop_end));
  }

  move_mem_reg(JIT_OFFSET, RBP, mem_holder->GetRegister());
  move_imm_mem(offset, 0, mem_holder->GetRegister());

  // clean up
  ReleaseRegister(mem_holder);
  ReleaseRegister(holder);
}

void JitAmd64::ProcessParameters(long params, Register top) {
#ifdef _DEBUG_JIT
  std::wcout << L"CALLED_PARMS: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
  if(params < 1) {
    return;
  }

  // The arguments sit below `top`, the last one nearest it: the operand
  // stack's top when the bridge entry set it (its prologue dropped them
  // from the count already), or the end of the caller's outgoing area when
  // the native entry did (EmitNativePrologue). Each is read at a fixed
  // displacement below it; the operand stack is not touched here.
  long words = 0;
  for(long i = 0; i < params; ++i) {
    StackInstr* instr = method->GetInstruction(instr_index++);
    instr->SetOffset(code_index);

    // A parameter may arrive as STOR_* (pop into slot) or, when the optimizer
    // keeps the incoming arg on the stack to reuse it directly (e.g. forwarding
    // a param straight into a call: `f(x)` with x a param), as COPY_* (store to
    // slot AND leave on the working stack). COPY_* must be routed to ProcessCopy,
    // not ProcessStore, or it leaves the stack unbalanced. Crucially, an int
    // COPY_LOCL_INT_VAR param must be classified as int here, not fall through
    // to the float else-branch (which would read it into an XMM reg and crash).
    if(instr->GetType() == STOR_LOCL_INT_VAR || instr->GetType() == STOR_CLS_INST_INT_VAR ||
       instr->GetType() == COPY_LOCL_INT_VAR || instr->GetType() == COPY_CLS_INST_INT_VAR) {
      words++;
      RegisterHolder* dest_holder = GetRegister();
      move_mem_reg(-words * (long)sizeof(size_t), top, dest_holder->GetRegister());
      working_stack.push_front(new RegInstr(dest_holder));
      // store int (COPY keeps the value on the working stack for later use)
      if(instr->GetType() == COPY_LOCL_INT_VAR || instr->GetType() == COPY_CLS_INST_INT_VAR) {
        ProcessCopy(instr);
      }
      else {
        ProcessStore(instr);
      }
    }
    else if(instr->GetType() == STOR_FUNC_VAR) {
      // two words; the one nearer the top ends up on top of the working stack
      RegisterHolder* dest_holder = GetRegister();
      move_mem_reg(-(words + 1) * (long)sizeof(size_t), top, dest_holder->GetRegister());
      RegisterHolder* dest_holder2 = GetRegister();
      move_mem_reg(-(words + 2) * (long)sizeof(size_t), top, dest_holder2->GetRegister());
      words += 2;

      working_stack.push_front(new RegInstr(dest_holder2));
      working_stack.push_front(new RegInstr(dest_holder));

      // store int
      ProcessStore(instr);
      i++;
    }
    else {
      words++;
      RegisterHolder* dest_holder = GetXmmRegister();
      move_mem_xreg(-words * (long)sizeof(size_t), top, dest_holder->GetRegister());
      working_stack.push_front(new RegInstr(dest_holder));

      // store float (COPY keeps the value on the working stack for later use)
      if(instr->GetType() == COPY_FLOAT_VAR) {
        ProcessCopy(instr);
      }
      else {
        ProcessStore(instr);
      }
    }
  }

}

void JitAmd64::ProcessIntCallParameter() {
#ifdef _DEBUG_JIT
  std::wcout << L"INT_CALL: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
  // the value the callee left: count -= 1, then op_stack[count] in one load
  RegisterHolder* stack_pos_holder = GetRegister();
  move_mem_reg(STACK_POS, RBP, stack_pos_holder->GetRegister());
  dec_mem(0, stack_pos_holder->GetRegister());
#ifdef _WIN64
  move_mem_reg32(0, stack_pos_holder->GetRegister(), stack_pos_holder->GetRegister());
#else
  move_mem_reg(0, stack_pos_holder->GetRegister(), stack_pos_holder->GetRegister());
#endif
  RegisterHolder* op_stack_holder = GetRegister();
  move_mem_reg(OP_STACK, RBP, op_stack_holder->GetRegister());
  move_base_index_reg(0, op_stack_holder->GetRegister(), stack_pos_holder->GetRegister(), sizeof(size_t), op_stack_holder->GetRegister());
  working_stack.push_front(new RegInstr(op_stack_holder));

  ReleaseRegister(stack_pos_holder);
}

void JitAmd64::ProcessFunctionCallParameter() {
#ifdef _DEBUG_JIT
  std::wcout << L"FUNC_CALL: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
  // two words: count -= 2, then op_stack[count] and op_stack[count + 1]
  RegisterHolder* stack_pos_holder = GetRegister();
  move_mem_reg(STACK_POS, RBP, stack_pos_holder->GetRegister());
  sub_imm_mem(2, 0, stack_pos_holder->GetRegister());
#ifdef _WIN64
  move_mem_reg32(0, stack_pos_holder->GetRegister(), stack_pos_holder->GetRegister());
#else
  move_mem_reg(0, stack_pos_holder->GetRegister(), stack_pos_holder->GetRegister());
#endif
  RegisterHolder* op_stack_holder = GetRegister();
  move_mem_reg(OP_STACK, RBP, op_stack_holder->GetRegister());
  RegisterHolder* holder = GetRegister();
  lea_base_index_reg(0, op_stack_holder->GetRegister(), stack_pos_holder->GetRegister(), sizeof(size_t), holder->GetRegister());

  move_mem_reg(0, holder->GetRegister(), op_stack_holder->GetRegister());
  working_stack.push_front(new RegInstr(op_stack_holder));

  move_mem_reg(8, holder->GetRegister(), holder->GetRegister());
  working_stack.push_front(new RegInstr(holder));

  ReleaseRegister(stack_pos_holder);
}

void JitAmd64::ProcessFloatCallParameter() {
#ifdef _DEBUG_JIT
  std::wcout << L"FLOAT_CALL: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
  RegisterHolder* stack_pos_holder = GetRegister();
  move_mem_reg(STACK_POS, RBP, stack_pos_holder->GetRegister());
  dec_mem(0, stack_pos_holder->GetRegister());
#ifdef _WIN64
  move_mem_reg32(0, stack_pos_holder->GetRegister(), stack_pos_holder->GetRegister());
#else
  move_mem_reg(0, stack_pos_holder->GetRegister(), stack_pos_holder->GetRegister());
#endif
  RegisterHolder* op_stack_holder = GetRegister();
  move_mem_reg(OP_STACK, RBP, op_stack_holder->GetRegister());
  RegisterHolder* dest_holder = GetXmmRegister();
  move_base_index_xreg(0, op_stack_holder->GetRegister(), stack_pos_holder->GetRegister(), sizeof(size_t), dest_holder->GetRegister());
  working_stack.push_front(new RegInstr(dest_holder));

  ReleaseRegister(op_stack_holder);
  ReleaseRegister(stack_pos_holder);
}

void JitAmd64::ProcessInstructions() {
  while(instr_index < method->GetInstructionCount() && compile_success) {
    StackInstr* instr = method->GetInstruction(instr_index++);
    instr->SetOffset(code_index);
    if(!is_inlining) {
      // F3: a pinned loop's header (the back-edge target, the instruction after
      // the loop's label) loads its locals into R13-R15. The entry path and
      // jumps from outside land on the instruction's offset, before the loads;
      // jumps from inside the loop land on loop_offset, past them and on the
      // safepoint poll, so every iteration polls.
      const int pin_region = PinRegionStartingAt(instr_index - 1);
      if(pin_region >= 0) {
        EmitPinEntry(pin_region);
      }
      // GC safepoint at loop headers only: a label's target is the head of a
      // loop iff some jump to it is backward, and every cyclic path contains a
      // back-edge, so polling these alone reaches every JITed loop (else the
      // stop-the-world collector waits forever on an allocation-free loop).
      // The working stack is empty here, right after a label.
      if(safepoint_lbl_indices.find(instr_index - 1) != safepoint_lbl_indices.end()) {
        EmitJitSafePoint();
      }
    }
    
    switch(instr->GetType()) {
      // load literal
    case LOAD_CHAR_LIT:
    case LOAD_INT_LIT:
#ifdef _DEBUG_JIT
      std::wcout << L"LOAD_INT: value=" << instr->GetOperand() 
            << L"; regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      working_stack.push_front(new RegInstr(instr));
      break;
      
      // float literal
    case LOAD_FLOAT_LIT:
#ifdef _DEBUG_JIT
      std::wcout << L"LOAD_FLOAT_LIT: value=" << instr->GetFloatOperand()
            << L"; regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      // Bounds-check the per-method float-constant pool. Overrunning float_consts
      // writes the literal's bit pattern past the array end and trashes an adjacent
      // heap block; bail to the interpreter instead. (MAX_DBLS is larger here than
      // on arm64, so this is latent on x64, but guard it for parity.)
      if(floats_index >= MAX_DBLS) {
        compile_success = false;
        break;
      }
      float_consts[floats_index] = instr->GetFloatOperand();
      working_stack.push_front(new RegInstr(&float_consts[floats_index++]));
      break;
      
      // load self
    case LOAD_INST_MEM: {
#ifdef _DEBUG_JIT
      std::wcout << L"LOAD_INST_MEM; regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      working_stack.push_front(new RegInstr(instr));
    }
      break;

      // load self
    case LOAD_CLS_MEM: {
#ifdef _DEBUG_JIT
      std::wcout << L"LOAD_CLS_MEM; regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      working_stack.push_front(new RegInstr(instr));
    }
      break;
      
      // load variable
    case LOAD_LOCL_INT_VAR:
    case LOAD_CLS_INST_INT_VAR:   
    case LOAD_FLOAT_VAR:
    case LOAD_FUNC_VAR:
#ifdef _DEBUG_JIT
      std::wcout << L"LOAD_INT_VAR/LOAD_FLOAT_VAR/LOAD_FUNC_VAR: id=" << instr->GetOperand() << L"; regs=" 
            << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessLoad(instr);
      break;
    
      // store value
    case STOR_LOCL_INT_VAR:
    case STOR_CLS_INST_INT_VAR:
    case STOR_FLOAT_VAR:
    case STOR_FUNC_VAR:
#ifdef _DEBUG_JIT
      std::wcout << L"STOR_INT_VAR/STOR_FLOAT_VAR/STOR_FUNC_VAR: id=" << instr->GetOperand() 
            << L"; regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessStore(instr);
      break;

      // copy value
    case COPY_LOCL_INT_VAR:
    case COPY_CLS_INST_INT_VAR:
    case COPY_FLOAT_VAR:
#ifdef _DEBUG_JIT
      std::wcout << L"COPY_INT_VAR/COPY_FLOAT_VAR: id=" << instr->GetOperand() 
            << L"; regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessCopy(instr);
      break;
      
      // mathematical
    case AND_INT:
    case OR_INT:
    case ADD_INT:
    case SUB_INT:
    case MUL_INT:
    case DIV_INT:
    case MOD_INT:
    case BIT_AND_INT:
    case BIT_OR_INT:
    case BIT_XOR_INT:
      // comparison
    case LES_INT:
    case GTR_INT:
    case LES_EQL_INT:
    case GTR_EQL_INT:
    case EQL_INT:
    case NEQL_INT:
    case SHL_INT:
    case SHR_INT:
#ifdef _DEBUG_JIT
      std::wcout << L"INT ADD/SUB/MUL/DIV/MOD/BIT_AND/BIT_OR/BIT_XOR/LES/GTR/EQL/NEQL/SHL_INT/SHR_INT: regs=" 
            << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessIntCalculation(instr);
      break;

    case ADD_FLOAT:
    case SUB_FLOAT:
    case MUL_FLOAT:
    case DIV_FLOAT:
#ifdef _DEBUG_JIT
      std::wcout << L"FLOAT ADD/SUB/MUL/DIV: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessFloatCalculation(instr);
      break;

    case SIN_FLOAT:
    case COS_FLOAT:
    case TAN_FLOAT:
    case ASIN_FLOAT:
    case ACOS_FLOAT:
    case ATAN_FLOAT:
    case ACOSH_FLOAT:
    case ASINH_FLOAT:
    case ATANH_FLOAT:
    case LOG2_FLOAT:
    case CBRT_FLOAT:
    case COSH_FLOAT:
    case SINH_FLOAT:
    case TANH_FLOAT:
    case LOG_FLOAT:
    case EXP_FLOAT:
    case LOG10_FLOAT:
    case TRUNC_FLOAT:
    case GAMMA_FLOAT:
    case ATAN2_FLOAT:
    case MOD_FLOAT:
    case POW_FLOAT:
#ifdef _DEBUG_JIT
      std::wcout << L"FLOAT SIN/COS/TAN/SQRT/ASIN/ACOS/ATAN2/POW/MOD_FLOAT: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessFloatOperation(instr);
      break;

    case SQRT_FLOAT:
#ifdef _DEBUG_JIT
      std::wcout << L"FLOAT SQRT: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessFloatSquareRoot(instr);
      break;

    case ROUND_FLOAT:
#ifdef _DEBUG_JIT
      std::wcout << L"FLOAT ROUND: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessFloatRound(instr, L'r');
      break;

    case CEIL_FLOAT:
#ifdef _DEBUG_JIT
      std::wcout << L"FLOAT CEIL: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessFloatRound(instr, L'c');
      break;

    case FLOR_FLOAT:
#ifdef _DEBUG_JIT
      std::wcout << L"FLOAT FLOR: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessFloatRound(instr, L'f');
      break;

    case LES_FLOAT:
    case GTR_FLOAT:
    case LES_EQL_FLOAT:
    case GTR_EQL_FLOAT:
    case EQL_FLOAT:
    case NEQL_FLOAT: {
#ifdef _DEBUG_JIT
      std::wcout << L"FLOAT LES/GTR/EQL/NEQL: regs=" << aval_regs.size() << L"," 
            << aux_regs.size() << std::endl;
#endif
      ProcessFloatCalculation(instr);
      // ProcessFloatCalculation pops both operands; on an unexpected operand
      // kind it sets compile_success=false and pushes nothing back. Honor that
      // here before touching working_stack, or front()/pop_front() underflow
      // the (now-empty) deque and dereference a garbage RegInstr.
      if(!compile_success) {
        break;
      }

      RegInstr* left = working_stack.front();
      working_stack.pop_front(); // pop invalid xmm register
      ReleaseXmmRegister(left->GetRegister());

      delete left; 
      left = nullptr;
      
      RegisterHolder* holder = GetRegister();
      cmov_reg(holder->GetRegister(), instr->GetType());
      working_stack.push_front(new RegInstr(holder));
      
    }
      break;
      
    case RTRN:
#ifdef _DEBUG_JIT
      std::wcout << L"RTRN: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      FlushLocalCache();
      if(is_inlining) {
        // inlined method: leave return value on working_stack, stop processing
        instr_index = method->GetInstructionCount(); // exit ProcessInstructions loop
      }
      else if(native_entry_offset >= 0) {
        // Two exits for the two entries (EmitNativePrologue). The value goes
        // to XMM0 first, while the working stack still holds it: a native
        // caller takes it from there, and the method's own frame record
        // comes off the call stack. A bridge caller takes it from the
        // operand stack, as before; the entry kind says which. A value the
        // working stack does not hold -- a callback left it on the operand
        // stack, as CPY_CHAR_ARY does for Runtime->Copy -- is already where
        // the bridge exit wants it; the native exit pops it into XMM0.
        const MemoryType rtrn = method->GetReturn();
        const bool value_on_op_stack = (rtrn == INT_TYPE || rtrn == FLOAT_TYPE) && working_stack.empty();
        EmitReturnValue();
        cmp_imm_mem(rec_base + REC_KIND, RBP, 0);
        AddMachineCode(0x0f);
        AddMachineCode(0x85);          // jne native_ret
        const long native_patch = code_index;
        AddImm(0);
        ProcessReturn();
        AddMachineCode(0xe9);          // jmp epilog
        const long epilog_patch = code_index;
        AddImm(0);
        PatchForwardJump(native_patch);
        if(value_on_op_stack) {
          move_mem_reg(STACK_POS, RBP, RCX);
          dec_mem(0, RCX);
#ifdef _WIN64
          move_mem_reg32(0, RCX, RDX);
#else
          move_mem_reg(0, RCX, RDX);
#endif
          move_mem_reg(OP_STACK, RBP, RAX);
          move_base_index_xreg(0, RAX, RDX, sizeof(size_t), XMM0);
        }
        move_mem_reg(CALL_STACK_POS, RBP, RAX);
#ifdef _WIN64
        dec_mem32(0, RAX);
#else
        dec_mem(0, RAX);
#endif
        PatchForwardJump(epilog_patch);
        Epilog();
      }
      else {
        ProcessReturn();
        // teardown
        Epilog();
      }
      break;
      
    case MTHD_CALL:
    case MTHD_CALL_JIT: {
      StackMethod* called_method = program->GetClass(instr->GetOperand())->GetMethod(instr->GetOperand2());
      if(called_method) {
#ifdef _DEBUG_JIT
        assert(called_method);
        std::wcout << L"MTHD_CALL: name='" << called_method->GetName() << L"': id="<< instr->GetOperand()
              << L"," << instr->GetOperand2() << L", params=" << (called_method->GetParamCount() + 1)
              << L": regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
        // MTHD_CALL uses callback (ProcessInlineMethod has INSTANCE_MEM
        // offset issues with constructors that need further investigation).
        // A callee bound here -- anything but a `virtual` declaration, which
        // the bridge resolves per receiver -- takes the direct bridge entry
        // with its StackMethod* as the first argument.
        direct_callee = called_method->IsVirtual() ? nullptr : called_method;
        if(called_method->IsVirtual()) {
          virtual_site = new JitVirtualSite(called_method, instr->GetOperand(), instr->GetOperand2());
          virtual_sites.push_back(virtual_site);
        }
        call_return_type = called_method->GetReturn();
        ProcessStackCallback(MTHD_CALL, instr, instr_index, called_method->GetParamCount() + 1);
        direct_callee = nullptr;
        virtual_site = nullptr;
        // a native site put the result on the working stack itself
        if(!native_result_taken) {
          ProcessReturnParameters(called_method->GetReturn());
        }
        native_result_taken = false;
      }
    }
      break;

    case DYN_MTHD_CALL:
    case DYN_MTHD_CALL_JIT: {
      // Working stack at the call = [args..., func-ref word2 (instance), func-ref
      // word1 (packed cls<<16|mthd)] = operand + 2 entries. (Was +3, over-counting
      // by one -> ProcessStackCallback marshalled past the stack -> crash.)
      // The patched opcode is the same call: the interpreter rewrites a site
      // when a callee with matching operands compiles, and a method holding one
      // failed to compile for want of this case. The site keeps an inline cache
      // keyed by the func-ref word (phase 3).
      virtual_site = new JitVirtualSite(nullptr, 0, 0);
      virtual_sites.push_back(virtual_site);
      call_return_type = (MemoryType)instr->GetOperand2();
      ProcessStackCallback(DYN_MTHD_CALL, instr, instr_index, instr->GetOperand() + 2);
      virtual_site = nullptr;
      if(!native_result_taken) {
        ProcessReturnParameters((MemoryType)instr->GetOperand2());
      }
      native_result_taken = false;
    }
      break;
      
    case NEW_BYTE_ARY:
#ifdef _DEBUG_JIT
      std::wcout << L"NEW_BYTE_ARY: dim=" << instr->GetOperand() << L" regs=" << aval_regs.size()
            << L"," << aux_regs.size() << std::endl;
#endif
      ProcessStackCallback(NEW_BYTE_ARY, instr, instr_index, instr->GetOperand());
      ProcessReturnParameters(INT_TYPE);
      break;

    case NEW_CHAR_ARY:
#ifdef _DEBUG_JIT
      std::wcout << L"NEW_CHAR_ARY: dim=" << instr->GetOperand() << L" regs=" << aval_regs.size()
            << L"," << aux_regs.size() << std::endl;
#endif
      ProcessStackCallback(NEW_CHAR_ARY, instr, instr_index, instr->GetOperand());
      ProcessReturnParameters(INT_TYPE);
      break;
      
    case NEW_INT_ARY:
#ifdef _DEBUG_JIT
      std::wcout << L"NEW_INT_ARY: dim=" << instr->GetOperand() << L" regs=" << aval_regs.size() 
            << L"," << aux_regs.size() << std::endl;
#endif
      ProcessStackCallback(NEW_INT_ARY, instr, instr_index, instr->GetOperand());
      ProcessReturnParameters(INT_TYPE);
      break;

    case NEW_FLOAT_ARY:
#ifdef _DEBUG_JIT
      std::wcout << L"NEW_FLOAT_ARY: dim=" << instr->GetOperand() << L" regs=" << aval_regs.size() 
            << L"," << aux_regs.size() << std::endl;
#endif
      ProcessStackCallback(NEW_FLOAT_ARY, instr, instr_index, instr->GetOperand());
      ProcessReturnParameters(INT_TYPE);
      break;
      
    case NEW_OBJ_INST: {
#ifdef _DEBUG_JIT
      StackClass* called_klass = program->GetClass(instr->GetOperand());
      std::wcout << L"NEW_OBJ_INST: name='" << called_klass->GetName() << L"': id=" << instr->GetOperand()
            << L": regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      // note: object id passed in instruction param
      // Inline young-gen bump-alloc fast path; callback is the slow path. Both
      // leave the new pointer on the op stack for ProcessReturnParameters.
      StackClass* new_klass = program->GetClass(instr->GetOperand());
      const long inline_done = EmitNewObjectInline(new_klass);
      ProcessStackCallback(NEW_OBJ_INST, instr, instr_index, 0);
      if(inline_done >= 0) {
        const int off = (int)(code_index - (inline_done + 4));
        memcpy(&code[(size_t)inline_done], &off, 4);   // backpatch success jmp past callback
      }
      ProcessReturnParameters(INT_TYPE);
    }
      break;

    case NEW_FUNC_INST: {
#ifdef _DEBUG_JIT
      std::wcout << L"NEW_FUNC_INST: size=" << instr->GetOperand()
            << L": regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessStackCallback(NEW_FUNC_INST, instr, instr_index, 0);
      ProcessReturnParameters(INT_TYPE);
    }
      break;
      
    case THREAD_JOIN: {
#ifdef _DEBUG_JIT
      std::wcout << L"THREAD_JOIN: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessStackCallback(THREAD_JOIN, instr, instr_index, 0);
    }
      break;

    case THREAD_SLEEP: {
#ifdef _DEBUG_JIT
      std::wcout << L"THREAD_SLEEP: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessStackCallback(THREAD_SLEEP, instr, instr_index, 1);
    }
      break;
      
    case CRITICAL_START: {
#ifdef _DEBUG_JIT
      std::wcout << L"CRITICAL_START: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessStackCallback(CRITICAL_START, instr, instr_index, 1);
    }
      break;
      
    case CRITICAL_END: {
#ifdef _DEBUG_JIT
      std::wcout << L"CRITICAL_END: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessStackCallback(CRITICAL_END, instr, instr_index, 1);
    }
      break;
      
    case CPY_BYTE_ARY: {
#ifdef _DEBUG_JIT
      std::wcout << L"CPY_BYTE_ARY: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessStackCallback(CPY_BYTE_ARY, instr, instr_index, 5);
    }
      break;

    case CPY_CHAR_ARY: {
#ifdef _DEBUG_JIT
      std::wcout << L"CPY_CHAR_ARY: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessStackCallback(CPY_CHAR_ARY, instr, instr_index, 5);
    }
      break;
      
    case CPY_INT_ARY: {
#ifdef _DEBUG_JIT
      std::wcout << L"CPY_INT_ARY: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessStackCallback(CPY_INT_ARY, instr, instr_index, 5);
    }
      break;

    case CPY_FLOAT_ARY: {
#ifdef _DEBUG_JIT
      std::wcout << L"CPY_FLOAT_ARY: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessStackCallback(CPY_FLOAT_ARY, instr, instr_index, 5);
    }
      break;

    case ZERO_BYTE_ARY: {
#ifdef _DEBUG_JIT
      std::wcout << L"ZERO_BYTE_ARY: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessStackCallback(ZERO_BYTE_ARY, instr, instr_index, 5);
    }
      break;

    case ZERO_CHAR_ARY: {
#ifdef _DEBUG_JIT
      std::wcout << L"ZERO_CHAR_ARY: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessStackCallback(ZERO_CHAR_ARY, instr, instr_index, 5);
    }
      break;

    case ZERO_INT_ARY: {
#ifdef _DEBUG_JIT
      std::wcout << L"ZERO_INT_ARY: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessStackCallback(ZERO_INT_ARY, instr, instr_index, 5);
    }
      break;

    case ZERO_FLOAT_ARY: {
#ifdef _DEBUG_JIT
      std::wcout << L"ZERO_FLOAT_ARY: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessStackCallback(ZERO_FLOAT_ARY, instr, instr_index, 5);
    }
      break;

    case TRAP:
#ifdef _DEBUG_JIT
      std::wcout << L"TRAP: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessStackCallback(TRAP, instr, instr_index, instr->GetOperand());
      break;

    case TRAP_RTRN:
#ifdef _DEBUG_JIT
      std::wcout << L"TRAP_RTRN: args=" << instr->GetOperand() << L"; regs=" 
            << aval_regs.size() << L"," << aux_regs.size() << std::endl;
      assert(instr->GetOperand());
#endif      
      ProcessStackCallback(TRAP_RTRN, instr, instr_index, instr->GetOperand());
      ProcessReturnParameters(INT_TYPE);
      break;
      
    case STOR_BYTE_ARY_ELM:
#ifdef _DEBUG_JIT
      std::wcout << L"STOR_BYTE_ARY_ELM: regs=" << aval_regs.size() << L"," 
            << aux_regs.size() << std::endl;
#endif
      ProcessStoreByteElement(instr);
      break;

    case STOR_CHAR_ARY_ELM:
#ifdef _DEBUG_JIT
      std::wcout << L"STOR_CHAR_ARY_ELM: regs=" << aval_regs.size() << L"," 
            << aux_regs.size() << std::endl;
#endif
      ProcessStoreCharElement(instr);
      break;
      
    case STOR_INT_ARY_ELM:
#ifdef _DEBUG_JIT
      std::wcout << L"STOR_INT_ARY_ELM: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessStoreIntElement(instr);
      break;

    case STOR_FLOAT_ARY_ELM:
#ifdef _DEBUG_JIT
      std::wcout << L"STOR_FLOAT_ARY_ELM: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessStoreFloatElement(instr);
      break;

    case SWAP_INT: {
#ifdef _DEBUG_JIT
      std::wcout << L"SWAP_INT: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      RegInstr* left = working_stack.front();
      working_stack.pop_front();

      RegInstr* right = working_stack.front();
      working_stack.pop_front();

      working_stack.push_front(left);       
      working_stack.push_front(right);
    }
      break;

    case POP_INT:
    case POP_FLOAT: {
#ifdef _DEBUG_JIT
      std::wcout << L"POP_INT/POP_FLOAT: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      // note: there may be constants that aren't 
      // in registers and don't need to be popped
      if(!working_stack.empty()) {
        // pop and release
        RegInstr* left = working_stack.front();
        working_stack.pop_front(); 
        if(left->GetType() == REG_INT) {
          ReleaseRegister(left->GetRegister());
        }
        else if(left->GetType() == REG_FLOAT) {
          ReleaseXmmRegister(left->GetRegister());
        }
        // clean up
        delete left;
        left = nullptr;
      }
    }
      break;
      
    case F2I:
#ifdef _DEBUG_JIT
      std::wcout << L"F2I: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessFloatToInt(instr);
      break;

    case I2F:
#ifdef _DEBUG_JIT
      std::wcout << L"I2F: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessIntToFloat(instr);
      break;

    case I2S:
#ifdef _DEBUG_JIT
      std::wcout << L"I2S: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessStackCallback(I2S, instr, instr_index, 3);
      break;

    case S2I:
#ifdef _DEBUG_JIT
      std::wcout << L"S2I: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessStackCallback(S2I, instr, instr_index, 2);
      ProcessReturnParameters(INT_TYPE);
      break;

    case F2S:
#ifdef _DEBUG_JIT
      std::wcout << L"F2S: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessStackCallback(F2S, instr, instr_index, 2);
      break;
      
    case S2F:
#ifdef _DEBUG_JIT
      std::wcout << L"S2F: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessStackCallback(S2F, instr, instr_index, 1);
      ProcessReturnParameters(FLOAT_TYPE);
      break;

    case RAND_FLOAT:
#ifdef _DEBUG_JIT
      std::wcout << L"RAND_FLOAT: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessStackCallback(RAND_FLOAT, instr, instr_index, 0);
      ProcessReturnParameters(FLOAT_TYPE);
      break;
      
    case OBJ_TYPE_OF: {
#ifdef _DEBUG_JIT
      std::wcout << L"OBJ_TYPE_OF: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessStackCallback(OBJ_TYPE_OF, instr, instr_index, 1);
      ProcessReturnParameters(INT_TYPE);
    }
      break;
      
    case OBJ_INST_CAST: {
#ifdef _DEBUG_JIT
      std::wcout << L"OBJ_INST_CAST: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessStackCallback(OBJ_INST_CAST, instr, instr_index, 1);
      ProcessReturnParameters(INT_TYPE);
    }
      break;

    case LOAD_ARY_SIZE: {
#ifdef _DEBUG_JIT
      std::wcout << L"LOAD_ARY_SIZE: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      // Inline. The size is one word in the array header (index 2, the first
      // dimension: what StackInterpreter::LoadArySize pushes). This used to go
      // through ProcessStackCallback -- push the array onto the VM operand
      // stack, call C++, read the result back: ~35 instructions and a call per
      // evaluation of `i < a->Size()`, which made an array loop ten times
      // slower than the same loop with the size hoisted.
      RegInstr* left = working_stack.front();
      working_stack.pop_front();
      RegisterHolder* holder = nullptr;
      switch(left->GetType()) {
      case REG_INT:
        holder = left->GetRegister();
        break;

      case MEM_INT:
        holder = GetRegister();
        move_mem_reg((long)left->GetOperand(), RBP, holder->GetRegister());
        break;

      default:
        // an immediate is never an array reference
        compile_success = false;
        break;
      }
      delete left;
      left = nullptr;
      if(!holder) {
        break;
      }
      CheckNilDereference(holder->GetRegister());
      move_mem_reg(2 * sizeof(size_t), holder->GetRegister(), holder->GetRegister());
      working_stack.push_front(new RegInstr(holder));
    }
      break;
      
    case LOAD_BYTE_ARY_ELM:
#ifdef _DEBUG_JIT
      std::wcout << L"LOAD_BYTE_ARY_ELM: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessLoadByteElement(instr);
      break;
      
    case LOAD_INT_ARY_ELM:
#ifdef _DEBUG_JIT
      std::wcout << L"LOAD_INT_ARY_ELM: regs=" << aval_regs.size() << L"," 
            << aux_regs.size() << std::endl;
#endif
      ProcessLoadIntElement(instr);
      break;

    case LOAD_CHAR_ARY_ELM:
#ifdef _DEBUG_JIT
      std::wcout << L"LOAD_CHAR_ARY_ELM: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessLoadCharElement(instr);
      break;
      
    case LOAD_FLOAT_ARY_ELM:
#ifdef _DEBUG_JIT
      std::wcout << L"LOAD_FLOAT_ARY_ELM: regs=" << aval_regs.size() << L"," 
            << aux_regs.size() << std::endl;
#endif
      ProcessLoadFloatElement(instr);
      break;

    case BIT_NOT_INT:
#ifdef _DEBUG_JIT
      std::wcout << L"BIT_NOT_INT: regs=" << aval_regs.size() << L","
        << aux_regs.size() << std::endl;
#endif
      ProcessNot(instr);
      break;
      
    case TRY_START:
    case TRY_END:
      // Not reached: TRY_START/TRY_END are no longer in the whitelist, so a
      // method with a try region (which is what `?->` desugars to) is left to
      // the interpreter, whose handler stack makes recovery work. They used to
      // be no-ops here "because MTHD_CALL is not whitelisted" -- it has been
      // for some time, and a nil receiver under `?->` in a JIT-compiled caller
      // then exited the process instead of yielding Nil (nil_safe_ops under
      // OBJECK_JIT_THRESHOLD=1). ARM64 never listed them.
      compile_success = false;
      break;

    case JMP:
      ProcessJump(instr);
      break;

    case JMP_TABLE:
      ProcessJumpTable(instr);
      break;

    case JMP_TABLE_SLOT:
      // read by the table that precedes it; emits nothing of its own
      break;
      
    case LBL:
#ifdef _DEBUG_JIT
      std::wcout << L"______ LBL: id=" << instr->GetOperand() << L" ______" << std::endl;
#endif
      FlushLocalCache();
      // The safepoint poll and the F3 entry loads are emitted at the jump
      // TARGET, the instruction after this label (see the top of the loop):
      // the compiler resolves a label to the index of the next instruction, so
      // keying on the LBL's own index never matched and no loop was polled.
      // GC safepoint only at loop-header labels (back-edge targets). A label is
      // the target of a backward jump iff it heads a loop, and every cyclic path
      // in the bytecode contains such a back-edge, so polling these labels alone
      // guarantees every JITed loop reaches a safepoint (else the stop-the-world
      // collector deadlocks on a no-allocation loop). Forward-only labels
      // (if/else merges) re-execute at most once per call and are skipped — that
      // removes the poll from branchy straight-line code. The working stack is
      // empty at a label, so the poll loses no live JIT value. Inlined callees
      // contain no labels (CanInlineMethod rejects control flow), so all loops
      // live in this instruction stream and are covered by safepoint_lbl_indices.
      break;
      
    default:
      compile_success = false;
      return;
    }
    // F3: leaving a pinned loop by fall-through (a conditional back-edge not
    // taken): the locals go back to their slots
    if(!is_inlining && active_pin_region >= 0 && instr_index - 1 == pin_regions[active_pin_region].end) {
      EmitPinWriteBack(active_pin_region);
      active_pin_region = -1;
    }
  }
}

void Runtime::JitAmd64::ProcessNot([[maybe_unused]] StackInstr* instr)
{
  RegInstr* left = working_stack.front();
  working_stack.pop_front();

  switch(left->GetType()) {
  case IMM_INT: {
    RegisterHolder* holder = GetRegister();
    move_imm_reg(left->GetOperand(), holder->GetRegister());
    not_reg(holder->GetRegister());
    working_stack.push_front(new RegInstr(holder));
  }
    break;

  case REG_INT:
    not_reg(left->GetRegister()->GetRegister());
    working_stack.push_front(new RegInstr(left->GetRegister()));
    break;

  case MEM_INT: {
    RegisterHolder* holder = GetRegister();
    move_mem_reg((long)left->GetOperand(), RBP, holder->GetRegister());
    not_reg(holder->GetRegister());
    working_stack.push_front(new RegInstr(holder));
  }
    break;

  default:
    std::wcerr << L">>> Should never occur (compiler bug?) type=" << left->GetType() << L" <<<" << std::endl;
    exit(1);
    break;
  }
}

void JitAmd64::ProcessLoad(StackInstr* instr) {
  // method/function memory
  if(instr->GetOperand2() == LOCL) {
    if(instr->GetType() == LOAD_FUNC_VAR) {
      RegisterHolder* holder = GetRegister();
      move_mem_reg(instr->GetOperand3() + sizeof(size_t), RBP, holder->GetRegister());
      working_stack.push_front(new RegInstr(holder));

      RegisterHolder* holder2 = GetRegister();
      move_mem_reg(instr->GetOperand3(), RBP, holder2->GetRegister());
      working_stack.push_front(new RegInstr(holder2));
    }
    else {
      long offset = instr->GetOperand3();
      // phase 4d: a pinned Float local, likewise a copy out of its XMM register
      int pin_freg;
      if(instr->GetType() == LOAD_FLOAT_VAR && PinnedFloatSlot(offset, pin_freg)) {
        RegisterHolder* fholder = GetXmmRegister();
        move_xreg_xreg(PinFloatRegister(pin_freg), fholder->GetRegister());
        working_stack.push_front(new RegInstr(fholder));
        return;
      }
      // F3: a pinned loop local is read out of its register. A copy, not the
      // register itself: consumers write their REG_INT operands in place.
      int pin_reg;
      if(PinnedSlot(offset, pin_reg)) {
        RegisterHolder* holder = GetRegister();
        move_reg_reg(PinRegister(pin_reg), holder->GetRegister());
        working_stack.push_front(new RegInstr(holder));
        return;
      }
      // check local register cache for int locals
      auto reg_it = local_reg_cache.find(offset);
      if(reg_it != local_reg_cache.end()) {
        RegisterHolder* cached = reg_it->second;
        local_reg_cache.erase(reg_it);
        used_regs.push_back(cached);
        working_stack.push_front(new RegInstr(cached));
      }
      // check local register cache for float locals
      else {
        auto xreg_it = local_xreg_cache.find(offset);
        if(xreg_it != local_xreg_cache.end()) {
          RegisterHolder* cached = xreg_it->second;
          local_xreg_cache.erase(xreg_it);
          used_xregs.push_back(cached);
          working_stack.push_front(new RegInstr(cached));
        }
        else {
          working_stack.push_front(new RegInstr(instr));
        }
      }
    }
  }
  // class or instance memory
  else {
    RegInstr* left = working_stack.front();
    working_stack.pop_front();

    RegisterHolder* holder;
    if(left->GetType() == REG_INT) {
      holder = left->GetRegister();
    }
    else {
      holder = GetRegister();
      move_mem_reg((long)left->GetOperand(), RBP, holder->GetRegister());
    }
    CheckNilDereference(holder->GetRegister());

    // long value
    if(instr->GetType() == LOAD_LOCL_INT_VAR ||
       instr->GetType() == LOAD_CLS_INST_INT_VAR) {
      move_mem_reg(instr->GetOperand3(), holder->GetRegister(), holder->GetRegister());
      working_stack.push_front(new RegInstr(holder));
    }
    // function value
    else if(instr->GetType() == LOAD_FUNC_VAR) {
      RegisterHolder* holder2 = GetRegister();
      move_mem_reg(instr->GetOperand3() + sizeof(size_t), holder->GetRegister(), holder2->GetRegister());
      working_stack.push_front(new RegInstr(holder2));
      
      move_mem_reg(instr->GetOperand3(), holder->GetRegister(), holder->GetRegister());
      working_stack.push_front(new RegInstr(holder));
    }
    // float value
    else {
      RegisterHolder* xmm_holder = GetXmmRegister();
      move_mem_xreg(instr->GetOperand3(), holder->GetRegister(), xmm_holder->GetRegister());
      ReleaseRegister(holder);
      working_stack.push_front(new RegInstr(xmm_holder));    
    }

    delete left;
    left = nullptr;
  }
}

// Emit a GC safepoint poll. Parks this thread if a collection is in progress.
// Emitted at every label; a hot loop crosses one each iteration, so the common
// case (no collection) must stay off the call path: inline a load+test of the
// stop-the-world flag and only CALL MemoryManager::SafePoint() when it is set.
// At a label the working stack is empty and locals are flushed, so RAX and the
// call-clobbered volatiles are all dead here (callee-saved regs incl. the RBP
// frame are ABI-preserved). Without this a JITed no-allocation loop never
// reaches a safepoint and the stop-the-world collector deadlocks.
// Inline young-gen bump alloc for NEW_OBJ_INST (mirror MemoryManager::AllocateObject).
// Returns code offset of the success-path jmp (rel32) for the caller to backpatch.
long JitAmd64::EmitNewObjectInline(StackClass* cls) {
  const long size = cls->GetInstanceMemorySize();
  const size_t alloc_size = (size_t)size * 2 + sizeof(size_t) * EXTRA_BUF_SIZE;
  const size_t total_size = alloc_size + sizeof(size_t);
  const size_t aligned_total = (total_size + sizeof(size_t) - 1) & ~((size_t)(sizeof(size_t) - 1));
  const int64_t nil_type = instructions::MemoryType::NIL_TYPE;

  // Zero-on-alloc: the nursery is no longer memset during GC, so this fast path must
  // clear raw_mem[3..] (MARKED_FLAG + the object's field words) itself. We emit that as
  // an unrolled store sequence; for large objects the unroll would bloat the code, so
  // bail to the slow path (MemoryManager::AllocateObject zeroes there) by returning -1.
  const size_t zero_words = aligned_total / sizeof(size_t);
  const size_t ZERO_UNROLL_MAX = 24;   // max field+flag words to zero inline
  if(zero_words > (size_t)(1 + EXTRA_BUF_SIZE - 1) + ZERO_UNROLL_MAX) {
    return -1;
  }

  FlushLocalCache();
  move_reg_mem(RAX, TMP_REG_0, RBP);
  move_reg_mem(RBX, TMP_REG_1, RBP);
  move_reg_mem(RCX, TMP_REG_2, RBP);
  move_reg_mem(RDX, TMP_REG_3, RBP);

  move_imm_reg((int64_t)MemoryManager::YoungOffsetAddr(), RBX);   // RBX = &young_offset

  const long retry_index = code_index;
  move_mem_reg(0, RBX, RAX);                          // RAX = young_offset (expected)
  move_reg_reg(RAX, RCX);                             // RCX = expected
  add_imm_reg((int64_t)aligned_total, RCX);          // RCX = new_offset
  move_imm_reg((int64_t)MemoryManager::YoungRegionSizeAddr(), RDX);
  move_mem_reg(0, RDX, RDX);                          // RDX = young_region_size
  AddMachineCode(0x48); AddMachineCode(0x39); AddMachineCode(0xD1);   // cmp rcx, rdx
  AddMachineCode(0x0f); AddMachineCode(0x87);                         // ja OVERFLOW
  const long ja_pos = code_index; AddImm(0);
  AddMachineCode(0xf0); AddMachineCode(0x48); AddMachineCode(0x0f);
  AddMachineCode(0xb1); AddMachineCode(0x0b);        // lock cmpxchg [rbx], rcx
  AddMachineCode(0x0f); AddMachineCode(0x85);                         // jne retry
  const long jne_pos = code_index; AddImm(0);
  { int off = (int)(retry_index - (jne_pos + 4)); memcpy(&code[(size_t)jne_pos], &off, 4); }

  move_imm_reg((int64_t)MemoryManager::YoungRegionAddr(), RDX);
  move_mem_reg(0, RDX, RDX);                          // RDX = young_region base
  add_reg_reg(RAX, RDX);                              // RDX = raw_mem = base + offset
  // zero-on-alloc: clear raw_mem[3] (MARKED_FLAG) + field words raw_mem[4..]. raw_mem[0..2]
  // (size word, TYPE, SIZE_OR_CLS) are written explicitly just below, so skip them. Uses
  // only RAX (=0) and RDX (base), both already spilled to TMP_REG_* at entry.
  move_imm_reg(0, RAX);
  for(size_t w = (size_t)(1 + EXTRA_BUF_SIZE - 1); w < zero_words; ++w) {
    move_reg_mem(RAX, (long)(w * sizeof(size_t)), RDX);   // raw_mem[w] = 0
  }
  move_imm_reg((int64_t)alloc_size, RAX); move_reg_mem(RAX, 0, RDX);
  move_imm_reg(nil_type, RAX);            move_reg_mem(RAX, (long)sizeof(size_t), RDX);
  move_imm_reg((int64_t)(size_t)cls, RAX); move_reg_mem(RAX, (long)(sizeof(size_t) * 2), RDX);
  add_imm_reg((int64_t)(sizeof(size_t) * (EXTRA_BUF_SIZE + 1)), RDX);  // RDX = result ptr

  move_mem_reg(OP_STACK, RBP, RAX);                   // RAX = op_stack base
  move_mem_reg(STACK_POS, RBP, RBX);                  // RBX = &stack_pos counter
#ifdef _WIN64
  move_mem_reg32(0, RBX, RCX);                        // RCX = counter (32-bit on Win64)
#else
  move_mem_reg(0, RBX, RCX);                          // RCX = counter (64-bit)
#endif
  shl_imm_reg(3, RCX);
  add_reg_reg(RCX, RAX);                              // RAX = op_stack + counter*8
  move_reg_mem(RDX, 0, RAX);                          // op_stack[counter] = result
  inc_mem(0, RBX);                                    // ++*stack_pos

  // allocation_size += size (match AllocateObject — drives the major-GC trigger;
  // skipping it starves GC -> old-gen bloat -> O(n^2) collection time).
  move_imm_reg((int64_t)MemoryManager::AllocationSizeAddr(), RAX);
  // lock add qword [rax], size -- allocation_size is std::atomic (H3); the bare add
  // raced the locked old-gen updates and lost increments, skewing the major-GC trigger.
  AddMachineCode(0xF0); AddMachineCode(0x48); AddMachineCode(0x81); AddMachineCode(0x00); AddImm((int)size);

  move_mem_reg(TMP_REG_0, RBP, RAX);
  move_mem_reg(TMP_REG_1, RBP, RBX);
  move_mem_reg(TMP_REG_2, RBP, RCX);
  move_mem_reg(TMP_REG_3, RBP, RDX);
  AddMachineCode(0xe9);
  const long done_jmp_pos = code_index; AddImm(0);

  { int off = (int)(code_index - (ja_pos + 4)); memcpy(&code[(size_t)ja_pos], &off, 4); }   // OVERFLOW:
  move_mem_reg(TMP_REG_0, RBP, RAX);
  move_mem_reg(TMP_REG_1, RBP, RBX);
  move_mem_reg(TMP_REG_2, RBP, RCX);
  move_mem_reg(TMP_REG_3, RBP, RDX);
  return done_jmp_pos;
}

/**
 * F3: choose, per loop, the integer locals that live in R13-R15 for the loop's
 * extent (docs/JIT_LOOP_LOCALS_DESIGN.md, section 3). Runs after ProcessIndices,
 * so the slots are known, and before Prolog, which saves the registers when
 * anything pins.
 */
void JitAmd64::PlanPinRegions() {
  pin_regions.clear();
  method_pins = false;
  method_pins_float = false;
  active_pin_region = -1;
  const long count = method->GetInstructionCount();
  for(long i = 0; i < count; ++i) {
    instr_index_of[method->GetInstruction(i)] = i;
  }
  if(detected_loops.empty()) {
    return;
  }
  // diagnostics: OBJECK_JIT_PIN_MAX=<n> caps the pinned locals per loop (0 turns
  // pinning off); OBJECK_JIT_PIN_SKIP=<substring> leaves matching methods alone
  int pin_max = PIN_REG_COUNT;
  std::string env_max;
  if(JitEnvFlag("OBJECK_JIT_PIN_MAX", &env_max)) {
    pin_max = atoi(env_max.c_str());
    if(pin_max < 0) pin_max = 0;
    if(pin_max > PIN_REG_COUNT) pin_max = PIN_REG_COUNT;
  }
  std::string skip;
  if(JitEnvFlag("OBJECK_JIT_PIN_SKIP", &skip) && !skip.empty()) {
    const std::wstring name = method->GetName();
    if(name.find(std::wstring(skip.begin(), skip.end())) != std::wstring::npos) {
      pin_max = 0;
    }
  }
  if(pin_max == 0) {
    return;
  }
#ifdef _WIN64
  // phase 4d: Float locals pin in XMM6-XMM9, callee-saved on Windows. The full
  // register set by default; OBJECK_JIT_PIN_MAX caps floats too when it is set.
  int pin_fmax = PIN_FREG_COUNT;
  if(!env_max.empty()) {
    pin_fmax = std::min(pin_fmax, pin_max);
  }
#else
  // every XMM is caller-saved on POSIX: a call inside the loop would clobber a pin
  const int pin_fmax = 0;
#endif
  // maximal regions: sort the loops by header and merge overlapping and nested ones
  std::vector<std::pair<long, long> > spans;
  for(const LoopInfo& loop : detected_loops) {
    spans.push_back(std::make_pair(loop.header_index, loop.backedge_index));
  }
  std::sort(spans.begin(), spans.end());
  std::vector<std::pair<long, long> > regions;
  for(const auto& span : spans) {
    if(!regions.empty() && span.first <= regions.back().second) {
      regions.back().second = std::max(regions.back().second, span.second);
    }
    else {
      regions.push_back(span);
    }
  }
  // slot id -> declared type. The and/or temp (slot 0 when HasAndOr) has no
  // declaration and is left alone; FUNC_PARM takes two slots.
  std::unordered_map<long, ParamType> id_types;
  {
    long id = method->HasAndOr() ? 1 : 0;
    StackDclr** dclrs = method->GetDeclarations();
    const long num_dclrs = method->GetNumberDeclarations();
    for(long j = 0; j < num_dclrs; ++j) {
      id_types[id] = dclrs[j]->type;
      id += (dclrs[j]->type == FUNC_PARM) ? 2 : 1;
    }
  }
  for(const auto& region : regions) {
    const long header = region.first;
    const long end = region.second;
    // eligible only if no jump from outside lands inside (other than on the
    // header) and no jump from inside leaves backwards past the header
    bool eligible = true;
    for(long i = 0; i < count && eligible; ++i) {
      StackInstr* instr = method->GetInstruction(i);
      std::vector<long> targets;
      if(instr->GetType() == JMP) {
        targets.push_back(instr->GetOperand());
      }
      else if(instr->GetType() == JMP_TABLE) {
        // F8: a select's table is a bundle of edges, the default plus one per
        // slot. Only a JMP gets a write-back stub on the way out of a region,
        // so none of a table's edges may cross the region's boundary.
        targets.push_back(instr->GetOperand3());
        for(long s = 1; s <= instr->GetOperand2() && i + s < count; ++s) {
          targets.push_back(method->GetInstruction(i + s)->GetOperand());
        }
      }
      else {
        continue;
      }
      const bool src_in = (i >= header && i <= end);
      for(const long target : targets) {
        const bool dst_in = (target > header && target <= end);
        if((!src_in && dst_in) || (src_in && target < header) ||
           (src_in && !dst_in && instr->GetType() == JMP_TABLE)) {
          eligible = false;
          break;
        }
      }
    }
    if(!eligible) {
      continue;
    }
    // accesses per candidate slot, an access in a nested loop weighted by depth
    std::unordered_map<long, long> weights;
    std::unordered_map<long, long> fweights;   // phase 4d: Float candidates
    std::unordered_map<long, long> slot_ids;
    for(long i = header; i <= end; ++i) {
      StackInstr* instr = method->GetInstruction(i);
      const InstructionType type = instr->GetType();
      const bool int_access = (type == LOAD_LOCL_INT_VAR || type == STOR_LOCL_INT_VAR || type == COPY_LOCL_INT_VAR);
      const bool float_access = (type == LOAD_FLOAT_VAR || type == STOR_FLOAT_VAR || type == COPY_FLOAT_VAR);
      if((!int_access && !float_access) || instr->GetOperand2() != LOCL) {
        continue;
      }
      auto declared = id_types.find(instr->GetOperand());
      if(declared == id_types.end()) {
        continue;
      }
      if(int_access && declared->second != INT_PARM && declared->second != CHAR_PARM) {
        continue;
      }
      if(float_access && (pin_fmax == 0 || declared->second != FLOAT_PARM)) {
        continue;
      }
      long depth = 0;
      for(const LoopInfo& loop : detected_loops) {
        if(i >= loop.header_index && i <= loop.backedge_index) {
          ++depth;
        }
      }
      (float_access ? fweights : weights)[instr->GetOperand3()] += (1L << std::min(depth * 2, 12L));
      slot_ids[instr->GetOperand3()] = instr->GetOperand();
    }
    if(weights.empty() && fweights.empty()) {
      continue;
    }
    std::vector<std::pair<long, long> > ranked;   // (-weight, slot): heaviest first, ties by slot
    for(const auto& weight : weights) {
      ranked.push_back(std::make_pair(-weight.second, weight.first));
    }
    std::sort(ranked.begin(), ranked.end());
    std::vector<std::pair<long, long> > franked;
    for(const auto& weight : fweights) {
      franked.push_back(std::make_pair(-weight.second, weight.first));
    }
    std::sort(franked.begin(), franked.end());
    PinRegion pin;
    pin.header = header;
    pin.end = end;
    pin.loop_offset = -1;
    for(size_t k = 0; k < ranked.size() && (int)pin.slots.size() < pin_max; ++k) {
      pin.slots.push_back(ranked[k].second);
    }
    for(size_t k = 0; k < franked.size() && (int)pin.fslots.size() < pin_fmax; ++k) {
      pin.fslots.push_back(franked[k].second);
    }
    pin_regions.push_back(pin);
    if(!pin.slots.empty()) {
      method_pins = true;
    }
    if(!pin.fslots.empty()) {
      method_pins_float = true;
    }
    if(JitReportEnabled()) {
      std::wcerr << L"[jit] " << method->GetName() << L": pinned " << pin.slots.size()
                 << L" local(s) in loop [" << header << L"," << end << L"], slot id(s)";
      for(size_t k = 0; k < pin.slots.size(); ++k) {
        std::wcerr << L" " << slot_ids[pin.slots[k]];
      }
      if(!pin.fslots.empty()) {
        std::wcerr << L"; " << pin.fslots.size() << L" float(s), slot id(s)";
        for(size_t k = 0; k < pin.fslots.size(); ++k) {
          std::wcerr << L" " << slot_ids[pin.fslots[k]];
        }
      }
      std::wcerr << std::endl;
    }
  }
}

int JitAmd64::PinRegionStartingAt(long lbl_index) {
  for(size_t r = 0; r < pin_regions.size(); ++r) {
    if(pin_regions[r].header == lbl_index) {
      return (int)r;
    }
  }
  return -1;
}

int JitAmd64::PinRegionContaining(long instr_idx) {
  for(size_t r = 0; r < pin_regions.size(); ++r) {
    if(instr_idx >= pin_regions[r].header && instr_idx <= pin_regions[r].end) {
      return (int)r;
    }
  }
  return -1;
}

bool JitAmd64::PinnedSlot(long offset, int& reg_index) {
  if(active_pin_region < 0) {
    return false;
  }
  const std::vector<long>& slots = pin_regions[active_pin_region].slots;
  for(size_t i = 0; i < slots.size(); ++i) {
    if(slots[i] == offset) {
      reg_index = (int)i;
      return true;
    }
  }
  return false;
}

bool JitAmd64::PinnedFloatSlot(long offset, int& reg_index) {
  if(active_pin_region < 0) {
    return false;
  }
  const std::vector<long>& fslots = pin_regions[active_pin_region].fslots;
  for(size_t i = 0; i < fslots.size(); ++i) {
    if(fslots[i] == offset) {
      reg_index = (int)i;
      return true;
    }
  }
  return false;
}

// movdqu [rsp+disp], xmm  /  movdqu xmm, [rsp+disp]: the prologue/epilogue save
// of the pinned float registers (phase 4d), raw-encoded like the fixed XMM10-15
// block in the prologue bytes
void JitAmd64::EmitXmmSave(bool store, int disp, Register xmm) {
  AddMachineCode(0xf3);
  if(xmm > XMM7) {
    AddMachineCode(0x44);   // REX.R
  }
  AddMachineCode(0x0f);
  AddMachineCode(store ? 0x7f : 0x6f);
  unsigned char modrm = 0x44;   // mod=01 (disp8), rm=100 (SIB follows)
  RegisterEncode3(modrm, 2, xmm);
  AddMachineCode(modrm);
  AddMachineCode(0x24);         // SIB: base RSP, no index
  AddMachineCode((unsigned char)disp);
}

// entry loads; interior jumps to the header land on loop_offset, past them
void JitAmd64::EmitPinEntry(int region) {
  PinRegion& pin = pin_regions[region];
  for(size_t i = 0; i < pin.slots.size(); ++i) {
    move_mem_reg(pin.slots[i], RBP, PinRegister((int)i));
  }
  for(size_t i = 0; i < pin.fslots.size(); ++i) {
    move_mem_xreg(pin.fslots[i], RBP, PinFloatRegister((int)i));
  }
  pin.loop_offset = code_index;
  active_pin_region = region;
}

void JitAmd64::EmitPinWriteBack(int region) {
  const PinRegion& pin = pin_regions[region];
  for(size_t i = 0; i < pin.slots.size(); ++i) {
    move_reg_mem(PinRegister((int)i), pin.slots[i], RBP);
  }
  for(size_t i = 0; i < pin.fslots.size(); ++i) {
    move_xreg_mem(PinFloatRegister((int)i), pin.fslots[i], RBP);
  }
}

// one stub per jump that leaves a pinned loop: store the locals, then jump
// to the real target. Emitted after the body; reached only by those jumps.
void JitAmd64::EmitPinExitStubs() {
  pin_exit_stub_offsets.clear();
  if(pin_regions.empty()) {
    return;
  }
  for(const auto& entry : jump_table) {
    const long disp_offset = entry.first;
    StackInstr* jump = entry.second;
    auto src_index = instr_index_of.find(jump);
    if(src_index == instr_index_of.end()) {
      continue;
    }
    const int region = PinRegionContaining(src_index->second);
    if(region < 0) {
      continue;
    }
    const PinRegion& pin = pin_regions[region];
    const long target = jump->GetOperand();
    if(target >= pin.header && target <= pin.end) {
      continue;
    }
    pin_exit_stub_offsets[disp_offset] = code_index;
    EmitPinWriteBack(region);
    AddMachineCode(0xe9);
    const long pos = code_index;
    AddImm(0);
    const long dest_offset = method->GetInstruction(target)->GetOffset();
    const int32_t rel = (int32_t)(dest_offset - (pos + 4));
    memcpy(&code[(size_t)pos], &rel, sizeof(rel));
  }
}

void JitAmd64::EmitJitSafePoint() {
  // Fast path: cmp byte [r12], 0 ; je skip
  // R12 caches &stw_active (loaded once in the prologue), so the common no-GC
  // case is a single 5-byte memory compare with no per-label address load.
  AddMachineCode(0x41); AddMachineCode(0x80); AddMachineCode(0x3c); AddMachineCode(0x24); AddMachineCode(0x00);  // cmp byte ptr [r12], 0
  AddMachineCode(0x0f); AddMachineCode(0x84);                        // je rel32
  long je_pos = code_index;
  AddImm(0);
  // Slow path: a collection is active — park.
#ifdef _WIN64
  sub_imm_reg(32, RSP);   // shadow space (RSP stays 16-aligned)
  move_imm_reg((size_t)MemoryManager::SafePoint, R10);
  call_reg(R10);
  add_imm_reg(32, RSP);
#else
  // R11: the working stack is empty at a label, so no pool register is live;
  // R15 holds a pinned loop local (F3)
  move_imm_reg((size_t)MemoryManager::SafePoint, R11);
  call_reg(R11);
#endif
  // While this thread was parked, another thread's collection may have promoted
  // (moved) the young 'self' this frame runs on. The collector relocates
  // frame->mem[0], but it cannot see this frame's [RBP+INSTANCE_MEM] copy, and
  // every instance-variable access in the loop reads self from that slot. The
  // callback path refreshes the slot after its call for the same reason
  // (ProcessStackCallback); a loop-header park needs the same refresh, or the
  // loop keeps addressing fields through the old nursery address until the next
  // callback -- a JIT-only, multi-thread-only corruption (#746). Only the slow
  // path parks, so only it pays. The scratch register is the one that carried
  // the call target: clobbered by the call and dead at a label.
#ifdef _WIN64
  move_mem_reg(FRAME_MEM, RBP, R10);          // R10 = frame->mem
  move_mem_reg(0, R10, R10);                  // R10 = frame->mem[0], relocated self
  move_reg_mem(R10, INSTANCE_MEM, RBP);       // refresh the frame's self
#else
  move_mem_reg(FRAME_MEM, RBP, R11);
  move_mem_reg(0, R11, R11);
  move_reg_mem(R11, INSTANCE_MEM, RBP);
#endif
  // Backpatch the je to land here, past the slow-path call.
  long skip_index = code_index;
  long jmp_offset = skip_index - (je_pos + 4);
  memcpy(&code[(size_t)je_pos], &jmp_offset, 4);
}

// F8: a dense select. The value is bounds-checked against [base, base+range)
// and dispatched through a table of 32-bit offsets placed inline right after
// the indirect jump, so the table's address is RIP-relative and needs no data
// section:
//   sub idx, base ; cmp idx, range ; jae DEFAULT
//   lea tbl, [rip+TABLE] ; movsxd tmp, [tbl+idx*4] ; add tbl, tmp ; jmp tbl
//   TABLE: dd target0-TABLE, target1-TABLE, ...
// The jump to the default is an ordinary conditional jump through jump_table
// (with a synthetic JMP for the fixup); the entries are resolved in the same
// pass from each target's recorded offset. Every target lies after the table
// in code order, since the case bodies follow the slots.
void JitAmd64::ProcessJumpTable(StackInstr* instr) {
  FlushLocalCache();
  // the selector is an integer expression, never a pending fused compare
  if(skip_jump || working_stack.empty()) {
    compile_success = false;
    skip_jump = false;
    return;
  }
  const long base = instr->GetOperand();
  const long range = instr->GetOperand2();
  const long default_index = instr->GetOperand3();
  // instr_index already points at the first slot
  if(range <= 0 || instr_index + range > method->GetInstructionCount()) {
    compile_success = false;
    return;
  }
  for(long i = 0; i < range; ++i) {
    if(method->GetInstruction(instr_index + i)->GetType() != JMP_TABLE_SLOT) {
      compile_success = false;
      return;
    }
  }

  RegInstr* left = working_stack.front();
  working_stack.pop_front();
  RegisterHolder* idx_holder = nullptr;
  switch(left->GetType()) {
  case IMM_INT:
    idx_holder = GetRegister();
    move_imm_reg(left->GetOperand(), idx_holder->GetRegister());
    break;

  case REG_INT:
    idx_holder = left->GetRegister();
    break;

  case MEM_INT:
    idx_holder = GetRegister();
    move_mem_reg((long)left->GetOperand(), RBP, idx_holder->GetRegister());
    break;

  default:
    delete left;
    left = nullptr;
    compile_success = false;
    return;
  }
  delete left;
  left = nullptr;

  const Register idx = idx_holder->GetRegister();
  if(base != 0) {
    sub_imm_reg(base, idx);
  }
  cmp_imm_reg(range, idx);
  // jae DEFAULT: unsigned, so a negative index falls out with the large ones
  AddMachineCode(0x0f);
  AddMachineCode(0x83);
  StackInstr* to_default = new StackInstr(instr->GetLineNumber(), JMP, default_index, -1L);
  synthetic_jumps.push_back(to_default);
  jump_table.insert(std::pair<long, StackInstr*>(code_index, to_default));
  AddImm(0);

  RegisterHolder* tbl_holder = GetRegister();
  RegisterHolder* tmp_holder = GetRegister();
  const Register tbl = tbl_holder->GetRegister();
  const Register tmp = tmp_holder->GetRegister();
  const long lea_disp = lea_rip_reg(tbl);
  movsxd_base_index_reg(tbl, idx, tmp);
  add_reg_reg(tmp, tbl);
  jmp_reg(tbl);
  ReleaseRegister(tmp_holder);
  ReleaseRegister(tbl_holder);
  ReleaseRegister(idx_holder);

  // the table follows the jump; the lea's displacement counts from its own end
  const long table_offset = code_index;
  const int32_t disp = (int32_t)(table_offset - (lea_disp + 4));
  memcpy(&code[(size_t)lea_disp], &disp, sizeof(disp));
  for(long i = 0; i < range; ++i) {
    StackInstr* slot = method->GetInstruction(instr_index + i);
    TableEntry entry;
    entry.entry_offset = code_index;
    entry.table_offset = table_offset;
    entry.target_index = slot->GetOperand();
    table_entries.push_back(entry);
    AddImm(0);
  }
#ifdef _DEBUG_JIT
  std::wcout << L"JMP_TABLE: base=" << base << L", range=" << range << L", default=" << default_index << std::endl;
#endif
}

void JitAmd64::ProcessJump(StackInstr* instr) {
  FlushLocalCache();
  // A conditional jump consumes its comparison value; if the compile-time
  // stack model has diverged (e.g. a front-end type bug fed unexpected
  // operand kinds to an earlier instruction), fail the compile cleanly so
  // the method falls back to the interpreter instead of crashing.
  if(working_stack.empty() && (instr->GetOperand2() >= 0 || skip_jump)) {
    compile_success = false;
    skip_jump = false;
    return;
  }
  if(!skip_jump) {
#ifdef _DEBUG_JIT
    std::wcout << L"JMP: id=" << instr->GetOperand() << L", regs=" << aval_regs.size()
          << L"," << aux_regs.size() << std::endl;
#endif
    if(instr->GetOperand2() < 0) {
      AddMachineCode(0xe9);
    }
    else {
      RegInstr* left = working_stack.front();
      working_stack.pop_front();

      switch(left->GetType()) {
      case IMM_INT:{
        RegisterHolder* holder = GetRegister();
        move_imm_reg(left->GetOperand(), holder->GetRegister());
        cmp_imm_reg(instr->GetOperand2(), holder->GetRegister());
        ReleaseRegister(holder);
      }
        break;
        
      case REG_INT:
        cmp_imm_reg(instr->GetOperand2(), left->GetRegister()->GetRegister());
        ReleaseRegister(left->GetRegister());
        break;

      case MEM_INT: {
        RegisterHolder* holder = GetRegister();
        move_mem_reg((long)left->GetOperand(), RBP, holder->GetRegister());
        cmp_imm_reg(instr->GetOperand2(), holder->GetRegister());
        ReleaseRegister(holder);
      }
        break;

      default:
        std::wcerr << L">>> Should never occur (compiler bug?) type=" << left->GetType() << L" <<<" << std::endl;
        exit(1);
        break;
      }

#ifdef _DEBUG_JIT
        std::wcout << L"  " << (++instr_count) << L": [je]" << std::endl;
#endif
      // compare with register
      AddMachineCode(0x0f);
      AddMachineCode(0x84);
      
      // clean up
      delete left;
      left = nullptr;
    }
    // store update index
    jump_table.insert(std::pair<long, StackInstr*>(code_index, instr));
    // temp offset, updated in next pass
    AddImm(0);
  }
  else {
    RegInstr* left = working_stack.front();
    working_stack.pop_front(); 
    skip_jump = false;

    // release register
    if(left->GetType() == REG_INT) {
      ReleaseRegister(left->GetRegister());
    }

    // clean up
    delete left;
    left = nullptr;
  }
}

void JitAmd64::ProcessReturnParameters(MemoryType type) {
  switch(type) {
  case INT_TYPE:
    ProcessIntCallParameter();
    break;

  case FLOAT_TYPE:
    ProcessFloatCallParameter();
    break;

  case FUNC_TYPE:
    ProcessFunctionCallParameter();
    break;

  default:
    // NIL_TYPE = void return or unhandled type — nothing to pop
    break;
  }
}

void JitAmd64::ProcessLoadByteElement(StackInstr* instr) {
  RegisterHolder* elem_holder = ArrayIndex(instr, BYTE_ARY_TYPE);
  RegisterHolder* holder = GetRegister();
  xor_reg_reg(holder->GetRegister(), holder->GetRegister());
  move_mem8_reg(0, elem_holder->GetRegister(), holder->GetRegister());
  ReleaseRegister(elem_holder);
  working_stack.push_front(new RegInstr(holder));
}

void JitAmd64::ProcessLoadCharElement(StackInstr* instr) {
  RegisterHolder* holder = GetRegister(false);
  RegisterHolder* elem_holder = ArrayIndex(instr, CHAR_ARY_TYPE);
  xor_reg_reg(holder->GetRegister(), holder->GetRegister());
#ifdef _WIN64  
  move_mem16_reg(0, elem_holder->GetRegister(), holder->GetRegister());
#else
  move_mem32_reg(0, elem_holder->GetRegister(), holder->GetRegister());
#endif
  ReleaseRegister(elem_holder);
  working_stack.push_front(new RegInstr(holder));
}

void JitAmd64::ProcessLoadIntElement(StackInstr* instr) {
  RegisterHolder* elem_holder = ArrayIndex(instr, INT_TYPE);
  move_mem_reg(0, elem_holder->GetRegister(), elem_holder->GetRegister());
  working_stack.push_front(new RegInstr(elem_holder));
}

void JitAmd64::ProcessLoadFloatElement(StackInstr* instr) {
  RegisterHolder* elem_holder = ArrayIndex(instr, FLOAT_TYPE);
  RegisterHolder* holder = GetXmmRegister();
  move_mem_xreg(0, elem_holder->GetRegister(), holder->GetRegister());
  working_stack.push_front(new RegInstr(holder));
  ReleaseRegister(elem_holder);
}

void JitAmd64::ProcessStoreByteElement(StackInstr* instr) {
  RegisterHolder* elem_holder = ArrayIndex(instr, BYTE_ARY_TYPE);
  RegInstr* left = working_stack.front();
  working_stack.pop_front();
  
  switch(left->GetType()) {
  case IMM_INT:
    move_imm_mem8((int8_t)left->GetOperand(), 0, elem_holder->GetRegister());
    ReleaseRegister(elem_holder);
    break;

  case MEM_INT: {    
    // movb can only use al, bl, cl and dl registers
    RegisterHolder* holder = GetRegister(false);
    move_mem_reg((long)left->GetOperand(), RBP, holder->GetRegister());
    move_reg_mem8(holder->GetRegister(), 0, elem_holder->GetRegister());
    ReleaseRegister(holder);
    ReleaseRegister(elem_holder);
  }
    break;

  case REG_INT: {
    // movb can only use al, bl, cl and dl registers
    RegisterHolder* holder = left->GetRegister();
    if(holder->GetRegister() == RDI || holder->GetRegister() == RSI) {
      RegisterHolder* tmp_holder = GetRegister(false);
      move_reg_reg(holder->GetRegister(), tmp_holder->GetRegister());
      move_reg_mem8(tmp_holder->GetRegister(), 0, elem_holder->GetRegister());      
      ReleaseRegister(tmp_holder);
    }
    else {
      move_reg_mem8(holder->GetRegister(), 0, elem_holder->GetRegister());      
    }
    ReleaseRegister(holder);
    ReleaseRegister(elem_holder);
  }
    break;

  default:
    break;
  }
  
  delete left;
  left = nullptr;
}

void JitAmd64::ProcessStoreCharElement(StackInstr* instr) {
  RegisterHolder* elem_holder = ArrayIndex(instr, CHAR_ARY_TYPE);
  RegInstr* left = working_stack.front();
  working_stack.pop_front();
  
  switch(left->GetType()) {
  case IMM_INT:
    // Both encoders take an R8-R15 base (Rex16 / XB), so the element
    // register is stored through directly. The R8-R15 case used to copy it
    // to a second register, release the element holder there, store through
    // the released register and then release the holder again at the end of
    // this function: two entries in the free pool for one register, two
    // later allocations of the same register, and a miscompiled method. It
    // was reachable only when the element address landed in R8-R15 with a
    // constant character, which the phase-2 result pop's allocation order
    // made common (core_arrays_simple, arm64_char_arrays).
#ifdef _WIN64
    move_imm_mem16((int16_t)left->GetOperand(), 0, elem_holder->GetRegister());
#else
    move_imm_mem32(left->GetOperand(), 0, elem_holder->GetRegister());
#endif
    break;

  case MEM_INT: {    
    // movw can only use al, bl, cl and dl registers
    RegisterHolder* holder = GetRegister(false);
    move_mem_reg((long)left->GetOperand(), RBP, holder->GetRegister());
#ifdef _WIN64  
    move_reg_mem16(holder->GetRegister(), 0, elem_holder->GetRegister());
#else
    move_reg_mem32(holder->GetRegister(), 0, elem_holder->GetRegister());
#endif  
    ReleaseRegister(holder);
  }
    break;

  case REG_INT: {
    // movw can only use al, bl, cl and dl registers
    RegisterHolder* holder = left->GetRegister();
    if(holder->GetRegister() == RDI || holder->GetRegister() == RSI) {
      RegisterHolder* tmp_holder = GetRegister(false);
      move_reg_reg(holder->GetRegister(), tmp_holder->GetRegister());
#ifdef _WIN64    
      move_reg_mem16(tmp_holder->GetRegister(), 0, elem_holder->GetRegister());
#else
      move_reg_mem32(tmp_holder->GetRegister(), 0, elem_holder->GetRegister()); 
#endif    
      ReleaseRegister(tmp_holder);
    }
    else {
#ifdef _WIN64  
      move_reg_mem16(holder->GetRegister(), 0, elem_holder->GetRegister());
#else
      move_reg_mem32(holder->GetRegister(), 0, elem_holder->GetRegister());
#endif    
    }
    ReleaseRegister(holder);
  }
    break;

  default:
    break;
  }

  ReleaseRegister(elem_holder);
  
  delete left;
  left = nullptr;
}

void JitAmd64::ProcessStoreIntElement(StackInstr* instr) {
  // Capture the array base for the write barrier before ArrayIndex rewrites the base
  // register into the element address. An Int[] element store can deposit a young
  // object reference into an (always old-gen) array; a minor GC must see it via the
  // remembered set.
  RegInstr* arr = working_stack.front();
  RegisterHolder* base_holder = GetRegister();
  if(arr->GetType() == MEM_INT) {
    move_mem_reg((long)arr->GetOperand(), RBP, base_holder->GetRegister());
  }
  else {  // REG_INT (IMM_INT is rejected by ArrayIndex)
    move_reg_reg(arr->GetRegister()->GetRegister(), base_holder->GetRegister());
  }

  RegisterHolder* elem_holder = ArrayIndex(instr, INT_TYPE);
  RegInstr* left = working_stack.front();
  working_stack.pop_front();

  switch(left->GetType()) {
  case IMM_INT:
    move_imm_mem(left->GetOperand(), 0, elem_holder->GetRegister());
    break;

  case MEM_INT: {
    RegisterHolder* holder = GetRegister();
    move_mem_reg((long)left->GetOperand(), RBP, holder->GetRegister());
    move_reg_mem(holder->GetRegister(), 0, elem_holder->GetRegister());
    ReleaseRegister(holder);
  }
    break;

  case REG_INT: {
    RegisterHolder* holder = left->GetRegister();
    move_reg_mem(holder->GetRegister(), 0, elem_holder->GetRegister());
    ReleaseRegister(holder);
  }
    break;

  default:
    break;
  }
  ReleaseRegister(elem_holder);

  EmitWriteBarrier(base_holder->GetRegister());
  ReleaseRegister(base_holder);

  delete left;
  left = nullptr;
}

void JitAmd64::ProcessStoreFloatElement(StackInstr* instr) {
  RegisterHolder* elem_holder = ArrayIndex(instr, FLOAT_TYPE);
  RegInstr* left = working_stack.front();
  working_stack.pop_front();
  
  switch(left->GetType()) {
  case IMM_FLOAT:
    move_imm_memx(left, 0, elem_holder->GetRegister());
    break;

  case MEM_FLOAT: 
  case MEM_INT: {
    RegisterHolder* holder = GetXmmRegister();
    move_mem_xreg((long)left->GetOperand(), 
                  RBP, holder->GetRegister());
    move_xreg_mem(holder->GetRegister(), 0, elem_holder->GetRegister());
    ReleaseXmmRegister(holder);
  }
    break;

  case REG_FLOAT: {
    RegisterHolder* holder = left->GetRegister();
    move_xreg_mem(holder->GetRegister(), 0, elem_holder->GetRegister());
    ReleaseXmmRegister(holder);
  }
    break;

  default:
    break;
  }
  ReleaseRegister(elem_holder);
  
  delete left;
  left = nullptr;
}

void JitAmd64::ProcessFloatToInt([[maybe_unused]] StackInstr* instr) {
  RegInstr* left = working_stack.front();
  working_stack.pop_front();
  
  RegisterHolder* holder = GetRegister();
  switch(left->GetType()) {
  case IMM_FLOAT:
    cvt_imm_reg(left, holder->GetRegister());
    break;
    
  case MEM_FLOAT:
  case MEM_INT:
    cvt_mem_reg((long)left->GetOperand(), RBP, holder->GetRegister());
    break;

  case REG_FLOAT:
    cvt_xreg_reg(left->GetRegister()->GetRegister(), holder->GetRegister());
    ReleaseXmmRegister(left->GetRegister());
    break;

  default:
    break;
  }
  working_stack.push_front(new RegInstr(holder));

  delete left;
  left = nullptr;
}

void JitAmd64::ProcessIntToFloat([[maybe_unused]] StackInstr* instr) {
  RegInstr* left = working_stack.front();
  working_stack.pop_front();
  
  RegisterHolder* holder = GetXmmRegister();
  switch(left->GetType()) {
  case IMM_INT:
    cvt_imm_xreg(left, holder->GetRegister());
    break;
    
  case MEM_INT:
    cvt_mem_xreg((long)left->GetOperand(), RBP, holder->GetRegister());
    break;

  case REG_INT:
    cvt_reg_xreg(left->GetRegister()->GetRegister(), holder->GetRegister());
    ReleaseRegister(left->GetRegister());
    break;

  default:
    break;
  }
  working_stack.push_front(new RegInstr(holder));

  delete left;
  left = nullptr;
}

// Inline generational write barrier for a reference store into 'holder'. The fast
// path (holder young, or old but already in the remembered set) is a load + mask +
// compare + branch and never calls out. Only the first young-ref store into a given
// old object reaches the slow path, which preserves the caller-saved registers the
// JIT may have live (cached locals, COPY's pending working-stack value), hands the
// holder to the runtime, and restores them. Emit while 'holder' still holds the
// destination object pointer. No-op unless minor GC ever runs; under major-only GC
// the recorded remembered set is simply cleared each cycle.
void JitAmd64::EmitWriteBarrier(Register holder) {
  RegisterHolder* flags_holder = GetRegister();
  Register flags = flags_holder->GetRegister();

  // flags = holder[MARKED_FLAG]   (MARKED_FLAG == -1 word)
  move_mem_reg(MARKED_FLAG * (long)sizeof(size_t), holder, flags);
  // barrier needed iff (flags & (OLD|RSET)) == OLD  -- old-gen and not yet tracked
  and_imm_reg(GC_OLD_BIT | GC_RSET_BIT, flags);
  cmp_imm_reg(GC_OLD_BIT, flags);
  ReleaseRegister(flags_holder);

  // jne skip
  AddMachineCode(0x0F);
  AddMachineCode(0x85);
  const long skip_patch = code_index;
  AddImm(0);

  // --- slow path: record holder in the remembered set ---
  // Save the JIT-allocatable caller-saved GP registers (even count keeps the stack
  // 16-byte aligned for the call) so cached locals / pending values survive.
#ifdef _WIN64
  push_reg(RAX); push_reg(RCX); push_reg(RDX); push_reg(R8);
  push_reg(R9);  push_reg(R10); push_reg(R11); push_reg(R9);   // second R9: alignment pad
  move_reg_reg(holder, RCX);                                   // arg0 = holder (MS x64)
  sub_imm_reg(32, RSP);                                        // shadow space
  move_imm_reg((size_t)MemoryManager::JitWriteBarrier, R10);
  call_reg(R10);
  add_imm_reg(32, RSP);
  pop_reg(R9); pop_reg(R11); pop_reg(R10); pop_reg(R9);
  pop_reg(R8); pop_reg(RDX); pop_reg(RCX); pop_reg(RAX);
#else
  push_reg(RAX); push_reg(RCX); push_reg(RDX);
  push_reg(R8);  push_reg(R10); push_reg(R11);
  move_reg_reg(holder, RDI);                                   // arg0 = holder (System V)
  move_imm_reg((size_t)MemoryManager::JitWriteBarrier, R11);
  call_reg(R11);
  pop_reg(R11); pop_reg(R10); pop_reg(R8);
  pop_reg(RDX); pop_reg(RCX); pop_reg(RAX);
#endif

  // skip:
  const int32_t skip_rel = (int32_t)(code_index - (skip_patch + 4));
  memcpy(&code[skip_patch], &skip_rel, sizeof(skip_rel));
}

void JitAmd64::ProcessStore(StackInstr* instr) {
  Register dest;
  RegisterHolder* addr_holder = nullptr;
  bool is_local = (instr->GetOperand2() == LOCL);
  bool is_func_var = (instr->GetType() == STOR_FUNC_VAR);

  // instance/method memory
  if(is_local) {
    dest = RBP;
    // F3: a pinned loop local's register is the local; nothing reaches its
    // frame slot until the loop is left (exit stubs and the fall-through
    // write-back). A deferred MEM_INT here never names a pinned slot: loads of
    // pinned slots always produce REG_INT.
    // phase 4d: a pinned Float local's register is the local
    int pin_freg;
    if(instr->GetType() == STOR_FLOAT_VAR && PinnedFloatSlot(instr->GetOperand3(), pin_freg)) {
      RegInstr* value = working_stack.front();
      working_stack.pop_front();
      const Register fpin = PinFloatRegister(pin_freg);
      switch(value->GetType()) {
      case IMM_FLOAT:
        move_imm_xreg(value, fpin);
        break;
      case MEM_FLOAT:
        move_mem_xreg((long)value->GetOperand(), RBP, fpin);
        break;
      case REG_FLOAT:
        move_xreg_xreg(value->GetRegister()->GetRegister(), fpin);
        ReleaseXmmRegister(value->GetRegister());
        break;
      default:
        compile_success = false;
        break;
      }
      delete value;
      return;
    }
    int pin_reg;
    if(!is_func_var && PinnedSlot(instr->GetOperand3(), pin_reg)) {
      RegInstr* value = working_stack.front();
      working_stack.pop_front();
      const Register pin = PinRegister(pin_reg);
      switch(value->GetType()) {
      case IMM_INT:
        move_imm_reg(value->GetOperand(), pin);
        break;
      case MEM_INT:
        move_mem_reg((long)value->GetOperand(), RBP, pin);
        break;
      case REG_INT:
        move_reg_reg(value->GetRegister()->GetRegister(), pin);
        ReleaseRegister(value->GetRegister());
        break;
      default:
        compile_success = false;
        break;
      }
      delete value;
      return;
    }
    // invalidate cache for this offset (value is being overwritten)
    if(!is_func_var) {
      auto reg_it = local_reg_cache.find(instr->GetOperand3());
      if(reg_it != local_reg_cache.end()) {
        ReleaseRegister(reg_it->second);
        local_reg_cache.erase(reg_it);
      }
      auto xreg_it = local_xreg_cache.find(instr->GetOperand3());
      if(xreg_it != local_xreg_cache.end()) {
        ReleaseXmmRegister(xreg_it->second);
        local_xreg_cache.erase(xreg_it);
      }

      // Materialize any pending working-stack value that defers to this slot
      // BEFORE it is overwritten. A deferred LOAD of this local (a MEM_INT/
      // MEM_FLOAT still pointing at the slot) would otherwise read the post-store
      // value. This is what breaks TCO'd self-recursive tail calls where a bare
      // local is forwarded as an argument: e.g. `return Gcd(b, a%b)` compiles to
      // LOAD b; ...; STOR b (:=a%b); STOR a (:= the deferred b) -- without this,
      // the second store reads b's slot, which now holds a%b, so a := a%b instead
      // of the old b. (Mirrors the ARM64 fix.)
      const long stor_slot = instr->GetOperand3();
      for(size_t wi = 0; wi < working_stack.size(); ++wi) {
        RegInstr* pend = working_stack[wi];
        if(pend->GetType() == MEM_INT && (long)pend->GetOperand() == stor_slot) {
          RegisterHolder* mh = GetRegister();
          move_mem_reg(stor_slot, RBP, mh->GetRegister());
          delete pend;
          working_stack[wi] = new RegInstr(mh);
        }
        else if(pend->GetType() == MEM_FLOAT && (long)pend->GetOperand() == stor_slot) {
          RegisterHolder* mh = GetXmmRegister();
          move_mem_xreg(stor_slot, RBP, mh->GetRegister());
          delete pend;
          working_stack[wi] = new RegInstr(mh);
        }
      }
    }
  }
  // class or instance memory
  else {
    RegInstr* left = working_stack.front();
    working_stack.pop_front();

    if(left->GetRegister()) {
      addr_holder = left->GetRegister();
    }
    else {
      addr_holder = GetRegister();
      move_mem_reg((long)left->GetOperand(), RBP, addr_holder->GetRegister());
    }
    dest = addr_holder->GetRegister();
    CheckNilDereference(dest);

    delete left;
    left = nullptr;
  }

  RegInstr* left = working_stack.front();
  working_stack.pop_front();

  switch(left->GetType()) {
  case IMM_INT:
    if(is_func_var) {
      move_imm_mem(left->GetOperand(), instr->GetOperand3(), dest);

      RegInstr* left2 = working_stack.front();
      working_stack.pop_front();
      move_imm_mem(left2->GetOperand(), instr->GetOperand3() + sizeof(size_t), dest);

      delete left2;
      left2 = nullptr;
    }
    else {
      move_imm_mem(left->GetOperand(), instr->GetOperand3(), dest);
    }
    break;

  case MEM_INT: {
    RegisterHolder* holder = GetRegister();
    if(is_func_var) {
      move_mem_reg((long)left->GetOperand(), RBP, holder->GetRegister());
      move_reg_mem(holder->GetRegister(), instr->GetOperand3(), dest);

      RegInstr* left2 = working_stack.front();
      working_stack.pop_front();
      move_mem_reg((long)left2->GetOperand(), RBP, holder->GetRegister());
      move_reg_mem(holder->GetRegister(), instr->GetOperand3() + sizeof(size_t), dest);

      delete left2;
      left2 = nullptr;
    }
    else {
      move_mem_reg((long)left->GetOperand(), RBP, holder->GetRegister());
      move_reg_mem(holder->GetRegister(), instr->GetOperand3(), dest);
    }
    // cache the register for local stores, release otherwise
    if(is_local && !is_func_var) {
      CacheLocalRegister(instr->GetOperand3(), holder);
    }
    else {
      ReleaseRegister(holder);
    }
  }
    break;

  case REG_INT: {
    RegisterHolder* holder = left->GetRegister();
    if(is_func_var) {
      move_reg_mem(holder->GetRegister(), instr->GetOperand3(), dest);

      RegInstr* left2 = working_stack.front();
      working_stack.pop_front();
      RegisterHolder* holder2  = left2->GetRegister();

      move_reg_mem(holder2->GetRegister(), instr->GetOperand3() + sizeof(size_t), dest);
      ReleaseRegister(holder2);

      delete left2;
      left2 = nullptr;
    }
    else {
      move_reg_mem(holder->GetRegister(), instr->GetOperand3(), dest);
    }
    // cache the register for local stores, release otherwise
    if(is_local && !is_func_var) {
      CacheLocalRegister(instr->GetOperand3(), holder);
    }
    else {
      ReleaseRegister(holder);
    }
  }
    break;

  case IMM_FLOAT:
    move_imm_memx(left, instr->GetOperand3(), dest);
    break;

  case MEM_FLOAT: {
    RegisterHolder* holder = GetXmmRegister();
    move_mem_xreg((long)left->GetOperand(), RBP, holder->GetRegister());
    move_xreg_mem(holder->GetRegister(), instr->GetOperand3(), dest);
    // cache the register for local stores, release otherwise
    if(is_local) {
      CacheLocalXmmRegister(instr->GetOperand3(), holder);
    }
    else {
      ReleaseXmmRegister(holder);
    }
  }
    break;

  case REG_FLOAT: {
    RegisterHolder* holder = left->GetRegister();
    move_xreg_mem(holder->GetRegister(), instr->GetOperand3(), dest);
    // cache the register for local stores, release otherwise
    if(is_local) {
      CacheLocalXmmRegister(instr->GetOperand3(), holder);
    }
    else {
      ReleaseXmmRegister(holder);
    }
  }
    break;
  }

  if(addr_holder) {
    // Generational write barrier on instance-field reference stores (not static
    // class memory, which has no GC header).
    if(instr->GetOperand2() != CLS) {
      EmitWriteBarrier(dest);
    }
    ReleaseRegister(addr_holder);
  }

  delete left;
  left = nullptr;
}

void JitAmd64::ProcessCopy(StackInstr* instr) {
  Register dest;
  // instance/method memory
  if(instr->GetOperand2() == LOCL) {
    dest = RBP;
    // phase 4d: copy into a pinned Float local's register; the value stays
    int pin_freg;
    if(instr->GetType() == COPY_FLOAT_VAR && PinnedFloatSlot(instr->GetOperand3(), pin_freg)) {
      RegInstr* value = working_stack.front();
      const Register fpin = PinFloatRegister(pin_freg);
      switch(value->GetType()) {
      case IMM_FLOAT:
        move_imm_xreg(value, fpin);
        break;
      case MEM_FLOAT:
        move_mem_xreg((long)value->GetOperand(), RBP, fpin);
        break;
      case REG_FLOAT:
        move_xreg_xreg(value->GetRegister()->GetRegister(), fpin);
        break;
      default:
        compile_success = false;
        break;
      }
      return;
    }
    // F3: copy into a pinned loop local's register; the value stays on the
    // working stack as it is
    int pin_reg;
    if(PinnedSlot(instr->GetOperand3(), pin_reg)) {
      RegInstr* value = working_stack.front();
      const Register pin = PinRegister(pin_reg);
      switch(value->GetType()) {
      case IMM_INT:
        move_imm_reg(value->GetOperand(), pin);
        break;
      case MEM_INT:
        move_mem_reg((long)value->GetOperand(), RBP, pin);
        break;
      case REG_INT:
        move_reg_reg(value->GetRegister()->GetRegister(), pin);
        break;
      default:
        compile_success = false;
        break;
      }
      return;
    }
    // invalidate cache for this offset (value is being modified)
    auto reg_it = local_reg_cache.find(instr->GetOperand3());
    if(reg_it != local_reg_cache.end()) {
      ReleaseRegister(reg_it->second);
      local_reg_cache.erase(reg_it);
    }
    auto xreg_it = local_xreg_cache.find(instr->GetOperand3());
    if(xreg_it != local_xreg_cache.end()) {
      ReleaseXmmRegister(xreg_it->second);
      local_xreg_cache.erase(xreg_it);
    }
  }
  // class or instance memory
  else {
    RegInstr* left = working_stack.front();
    working_stack.pop_front();

    RegisterHolder* holder = GetRegister();
    move_mem_reg((long)left->GetOperand(), RBP, holder->GetRegister());
    CheckNilDereference(holder->GetRegister());
    dest = holder->GetRegister();
    // Generational write barrier on instance-field reference stores (not static
    // class memory). Emitted while 'holder' still pins the destination object.
    if(instr->GetOperand2() != CLS) {
      EmitWriteBarrier(dest);
    }
    ReleaseRegister(holder);

    delete left;
    left = nullptr;
  }

  RegInstr* left = working_stack.front();
  switch(left->GetType()) {
  case IMM_INT: {
    RegisterHolder* holder = GetRegister();
    move_imm_reg(left->GetOperand(), holder->GetRegister());
    move_reg_mem(holder->GetRegister(), instr->GetOperand3(), dest);
    // save register
    working_stack.pop_front();
    working_stack.push_front(new RegInstr(holder));

    delete left;
    left = nullptr;
  }
    break;

  case MEM_INT: {
    RegisterHolder* holder = GetRegister();
    move_mem_reg((long)left->GetOperand(), RBP, holder->GetRegister());
    move_reg_mem(holder->GetRegister(), instr->GetOperand3(), dest);
    // save register
    working_stack.pop_front();
    working_stack.push_front(new RegInstr(holder));

    delete left;
    left = nullptr;
  }
    break;

  case REG_INT: {
    RegisterHolder* holder = left->GetRegister();
    move_reg_mem(holder->GetRegister(), instr->GetOperand3(), dest);
  }
    break;

  case IMM_FLOAT: {
    RegisterHolder* holder = GetXmmRegister();
    move_imm_xreg(left, holder->GetRegister());
    move_xreg_mem(holder->GetRegister(), instr->GetOperand3(), dest);
    // save register
    working_stack.pop_front();
    working_stack.push_front(new RegInstr(holder));

    delete left;
    left = nullptr;
  }
    break;

  case MEM_FLOAT: {
    RegisterHolder* holder = GetXmmRegister();
    move_mem_xreg((long)left->GetOperand(), RBP, holder->GetRegister());
    move_xreg_mem(holder->GetRegister(), instr->GetOperand3(), dest);
    // save register
    working_stack.pop_front();
    working_stack.push_front(new RegInstr(holder));

    delete left;
    left = nullptr;
  }
    break;

  case REG_FLOAT: {
    RegisterHolder* holder = left->GetRegister();
    move_xreg_mem(holder->GetRegister(), instr->GetOperand3(), dest);
  }
    break;
  }
}

void JitAmd64::ProcessStackCallback(long instr_id, StackInstr* instr, long &instr_index, long params) {
  // flush cached locals before callback (callback may modify memory)
  FlushLocalCache();

  long non_params;
  if(params < 0) {
    non_params = 0;
  }
  else {
    non_params = (long)working_stack.size() - params;
  }
  
#ifdef _DEBUG_JIT
  std::wcout << L"Return: params=" << params << L", non-params=" << non_params << std::endl;
#endif

  std::stack<RegInstr*> regs;
  std::stack<long> dirty_regs;
  long reg_offset = TMP_REG_0;

  std::stack<RegInstr*> xmms;
  std::stack<long> dirty_xmms;
  long xmm_offset = TMP_XMM_0;

  long i = 0;
  for(std::deque<RegInstr*>::reverse_iterator iter = working_stack.rbegin(); iter != working_stack.rend(); ++iter) {
    RegInstr* left = (*iter);
    if(i < non_params) {
      switch(left->GetType()) {
        case REG_INT:
          move_reg_mem(left->GetRegister()->GetRegister(), reg_offset, RBP);
          dirty_regs.push(reg_offset);
          regs.push(left);
          reg_offset -= sizeof(size_t);
          break;

        case REG_FLOAT:
          move_xreg_mem(left->GetRegister()->GetRegister(), xmm_offset, RBP);
          dirty_xmms.push(xmm_offset);
          xmms.push(left);
          xmm_offset -= sizeof(double);
          break;

        default:
          break;
      }
      // update
      i++;
    }
  }

#ifdef _DEBUG_JIT
  assert(reg_offset >= TMP_REG_9);
  assert(xmm_offset >= TMP_XMM_2);
#endif

  if(dirty_regs.size() > 10 || dirty_xmms.size() > 3) {
    compile_success = false;
  }

  // A bound or cached callee is called straight into its native entry with
  // the values in this frame's outgoing area (EmitNativeCallSite); the
  // bridge is its slow path. Everything else -- an opcode's callback, or a
  // callee whose func-ref result needs two words -- takes the bridge with
  // the values on the operand stack.
  const bool native_site = (direct_callee || virtual_site) && call_return_type != FUNC_TYPE;
  if(native_site) {
    EmitNativeCallSite(instr_id, instr, instr_index, params);
  }
  else {
    // copy values to execution stack
    ProcessReturn(params);
    EmitBridgeCall(instr_id, instr, instr_index);
  }

  // restore register values
  while(!dirty_regs.empty()) {
    RegInstr* left = regs.top();
    move_mem_reg(dirty_regs.top(), RBP, left->GetRegister()->GetRegister());
    // update
    regs.pop();
    dirty_regs.pop();
  }

  while(!dirty_xmms.empty()) {
    RegInstr* left = xmms.top();
    move_mem_xreg(dirty_xmms.top(), RBP, left->GetRegister()->GetRegister());
    // update
    xmms.pop();
    dirty_xmms.pop();
  }

  // Reload INSTANCE_MEM from frame->mem[0] in case GC moved the young object
  // during the callback. GC updates frame->mem[0] but not [RBP+INSTANCE_MEM].
  {
    RegisterHolder* tmp = GetRegister();
    move_mem_reg(FRAME_MEM, RBP, tmp->GetRegister());           // tmp = frame->mem
    move_mem_reg(0, tmp->GetRegister(), tmp->GetRegister());     // tmp = frame->mem[0]
    move_reg_mem(tmp->GetRegister(), INSTANCE_MEM, RBP);         // INSTANCE_MEM = tmp
    ReleaseRegister(tmp);
  }

  // a native site's result is in XMM0, whichever path ran
  if(native_site) {
    if(call_return_type == FLOAT_TYPE) {
      RegisterHolder* holder = GetXmmRegister();
      move_xreg_xreg(XMM0, holder->GetRegister());
      working_stack.push_front(new RegInstr(holder));
    }
    else if(call_return_type == INT_TYPE) {
      RegisterHolder* holder = GetRegister();
      move_xreg_reg(XMM0, holder->GetRegister());
      working_stack.push_front(new RegInstr(holder));
    }
    native_result_taken = true;
  }
}

/**
 * The bridge sequence: JitDirectCall for a bound callee, JitStackCallback
 * with the opcode for anything else. The values are on the operand stack.
 */
void JitAmd64::EmitBridgeCall(long instr_id, StackInstr* instr, long instr_index) {
#ifdef _WIN64
  // set parameters: the direct entry takes the callee in place of the opcode
  if(direct_callee) {
    move_imm_reg((size_t)direct_callee, RCX);
  }
  else {
    move_imm_reg(instr_id, RCX);
  }
  move_imm_reg((size_t)instr, RDX);
  move_mem_reg(CLS_ID, RBP, R8);
  move_mem_reg(MTHD_ID, RBP, R9);
  push_imm(instr_index - 1);
  push_mem(CALL_STACK_POS, RBP);
  push_mem(CALL_STACK, RBP);
  push_mem(STACK_POS, RBP);
  push_mem(OP_STACK, RBP);
  push_mem(INSTANCE_MEM, RBP);

  // call function
  sub_imm_reg(32, RSP);
  move_imm_reg(direct_callee ? (size_t)JitCompiler::JitDirectCall : (size_t)JitCompiler::JitStackCallback, R10);
  call_reg(R10);
  add_imm_reg(80, RSP);
#else
  // save other registers
  push_reg(R15);
  push_reg(R14);
  push_reg(R13);
  push_reg(R8);
  
  // set parameters
  move_mem_reg(OP_STACK, RBP, R9);
  move_mem_reg(INSTANCE_MEM, RBP, R8);
  move_mem_reg(MTHD_ID, RBP, RCX);
  move_mem_reg(CLS_ID, RBP, RDX);
  move_imm_reg((size_t)instr, RSI);
  if(direct_callee) {
    move_imm_reg((size_t)direct_callee, RDI);
  }
  else {
    move_imm_reg(instr_id, RDI);
  }
  push_imm(instr_index - 1);
  push_mem(CALL_STACK_POS, RBP);
  push_mem(CALL_STACK, RBP);
  push_mem(STACK_POS, RBP);
  
  // call function
  move_imm_reg(direct_callee ? (size_t)JitCompiler::JitDirectCall : (size_t)JitCompiler::JitStackCallback, R15);
  call_reg(R15);
  add_imm_reg(32, RSP);
  
  // restore registers
  pop_reg(R8);
  pop_reg(R13);
  pop_reg(R14);
  pop_reg(R15);
#endif
}

void JitAmd64::ProcessReturn(long params) {
  // A params count above the modeled working stack means the instruction's
  // operand over-counts (the TRAP *_ARY_LEN emissions stamped 5 for 4 pushed
  // values; .obl files built before that fix still carry the bad operand).
  // The cleanup loop below would then front()/pop_front() an empty deque --
  // a compile-time crash. Values beyond the model never existed, so flushing
  // exactly what the model holds is the correct marshalling: clamp.
  if(params > (long)working_stack.size()) {
    params = (long)working_stack.size();
  }
  if(!working_stack.empty()) {
    // top = &op_stack[count]; every value goes at a growing displacement from
    // it and the count is bumped once at the end. Each value used to reload
    // the count pointer, store, increment the count and advance the base.
    RegisterHolder* op_stack_holder = GetRegister();
    move_mem_reg(OP_STACK, RBP, op_stack_holder->GetRegister());
    RegisterHolder* stack_pos_holder = GetRegister();
    move_mem_reg(STACK_POS, RBP, stack_pos_holder->GetRegister());
    RegisterHolder* top_holder = GetRegister();
#ifdef _WIN64
    move_mem_reg32(0, stack_pos_holder->GetRegister(), top_holder->GetRegister());
#else
    move_mem_reg(0, stack_pos_holder->GetRegister(), top_holder->GetRegister());
#endif
    lea_base_index_reg(0, op_stack_holder->GetRegister(), top_holder->GetRegister(), sizeof(size_t), top_holder->GetRegister());
    ReleaseRegister(op_stack_holder);
    const Register top = top_holder->GetRegister();

    long non_params;
    if(params < 0) {
      non_params = 0;
    }
    else {
      non_params = (long)working_stack.size() - params;
    }
#ifdef _DEBUG_JIT
    std::wcout << L"Return: params=" << params << L", non-params=" << non_params << std::endl;
#endif

    long i = 0;
    long disp = 0;
    for(std::deque<RegInstr*>::reverse_iterator iter = working_stack.rbegin(); iter != working_stack.rend(); ++iter) {
      // skip non-params... processed above
      RegInstr* left = (*iter);
      if(i < non_params) {
        i++;
      }
      else {
        switch(left->GetType()) {
          case IMM_INT:
            move_imm_mem(left->GetOperand(), disp, top);
            disp += sizeof(size_t);
            break;

          case MEM_INT:
          {
            RegisterHolder* temp_holder = GetRegister();
            move_mem_reg((long)left->GetOperand(), RBP, temp_holder->GetRegister());
            move_reg_mem(temp_holder->GetRegister(), disp, top);
            disp += sizeof(size_t);
            ReleaseRegister(temp_holder);
          }
          break;

          case REG_INT:
            move_reg_mem(left->GetRegister()->GetRegister(), disp, top);
            disp += sizeof(size_t);
            break;

          case IMM_FLOAT:
            move_imm_memx(left, disp, top);
            disp += sizeof(double);
            break;

          case MEM_FLOAT: {
            RegisterHolder* temp_holder = GetXmmRegister();
            move_mem_xreg((long)left->GetOperand(), RBP, temp_holder->GetRegister());
            move_xreg_mem(temp_holder->GetRegister(), disp, top);
            disp += sizeof(double);
            ReleaseXmmRegister(temp_holder);
          }
          break;

          case REG_FLOAT:
            move_xreg_mem(left->GetRegister()->GetRegister(), disp, top);
            disp += sizeof(double);
            break;
        }
      }
    }
    if(disp > 0) {
      add_imm_mem(disp / (long)sizeof(size_t), 0, stack_pos_holder->GetRegister());
    }
    ReleaseRegister(top_holder);
    ReleaseRegister(stack_pos_holder);

    // clean up working stack
    if(params < 0) {
      params = (long)working_stack.size();
    }
    for(long i = 0; i < params; ++i) {
      RegInstr* left = working_stack.front();
      working_stack.pop_front();

      // release register
      switch(left->GetType()) {
      case REG_INT:
        ReleaseRegister(left->GetRegister());
        break;

      case REG_FLOAT:
        ReleaseXmmRegister(left->GetRegister());
        break;

      default:
        break;
      }
      // clean up
      delete left;
      left = nullptr;
    }
  }
}

RegInstr* JitAmd64::ProcessIntFold(int64_t left_imm, int64_t right_imm, InstructionType type) {
  switch(type) {
  case AND_INT:
    return new RegInstr(IMM_INT, left_imm & right_imm);

  case OR_INT:
    return new RegInstr(IMM_INT, left_imm | right_imm);
    
  case ADD_INT:
    return new RegInstr(IMM_INT, left_imm + right_imm);
    
  case SUB_INT:
    return new RegInstr(IMM_INT, left_imm - right_imm);
    
  case MUL_INT:
    return new RegInstr(IMM_INT, left_imm * right_imm);
    
  case DIV_INT:
    if(right_imm == 0) return nullptr;
    return new RegInstr(IMM_INT, left_imm / right_imm);

  case MOD_INT:
    if(right_imm == 0) return nullptr;
    return new RegInstr(IMM_INT, left_imm % right_imm);

  case SHL_INT:
    return new RegInstr(IMM_INT, left_imm << right_imm);

  case SHR_INT:
    return new RegInstr(IMM_INT, left_imm >> right_imm);

  case BIT_AND_INT:
    return new RegInstr(IMM_INT, left_imm & right_imm);

  case BIT_OR_INT:
    return new RegInstr(IMM_INT, left_imm | right_imm);
    
  case BIT_XOR_INT:
    return new RegInstr(IMM_INT, left_imm ^ right_imm);
    
  case LES_INT:  
    return new RegInstr(IMM_INT, left_imm < right_imm);
    
  case GTR_INT:
    return new RegInstr(IMM_INT, left_imm > right_imm);
    
  case EQL_INT:
    return new RegInstr(IMM_INT, left_imm == right_imm);
    
  case NEQL_INT:
    return new RegInstr(IMM_INT, left_imm != right_imm);
    
  case LES_EQL_INT:
    return new RegInstr(IMM_INT, left_imm <= right_imm);
    
  case GTR_EQL_INT:
    return new RegInstr(IMM_INT, left_imm >= right_imm);
    
  default:
    return nullptr;
  }
}

void JitAmd64::ProcessIntCalculation(StackInstr* instruction) {
  RegInstr* left = working_stack.front();
  working_stack.pop_front();

  RegInstr* right = working_stack.front();
  working_stack.pop_front();
  
  switch(left->GetType()) {
    // intermidate
  case IMM_INT:
    switch(right->GetType()) {
    case IMM_INT: {
      RegInstr* folded = ProcessIntFold(left->GetOperand(), right->GetOperand(), instruction->GetType());
      if(folded) {
        working_stack.push_front(folded);
      }
      else {
        // div by zero at compile time: emit both as registers, let runtime handle
        RegisterHolder* lh = GetRegister();
        move_imm_reg(left->GetOperand(), lh->GetRegister());
        RegisterHolder* rh = GetRegister();
        move_imm_reg(right->GetOperand(), rh->GetRegister());
        math_reg_reg(rh->GetRegister(), lh->GetRegister(), instruction->GetType());
        ReleaseRegister(rh);
        working_stack.push_front(new RegInstr(lh));
      }
    }
      break;

    case REG_INT: {
      RegisterHolder* imm_holder = GetRegister();
      move_imm_reg(left->GetOperand(), imm_holder->GetRegister());
      RegisterHolder* holder = right->GetRegister();

      math_reg_reg(holder->GetRegister(), imm_holder->GetRegister(),
                   instruction->GetType());
      
      ReleaseRegister(holder);
      working_stack.push_front(new RegInstr(imm_holder));
    }
      break;
      
    case MEM_INT: {
      RegisterHolder* holder = GetRegister();
      move_mem_reg((long)right->GetOperand(), RBP, holder->GetRegister());

      RegisterHolder* imm_holder = GetRegister();
      move_imm_reg(left->GetOperand(), imm_holder->GetRegister());

      math_reg_reg(holder->GetRegister(), imm_holder->GetRegister(), 
                   instruction->GetType());
      
      ReleaseRegister(holder);
      working_stack.push_front(new RegInstr(imm_holder));
    }
      break;

    default:
      // unexpected operand kind (e.g. a float fed to an integer compare by a
      // front-end type bug): fail the compile cleanly so the method falls
      // back to the interpreter instead of corrupting the stack model
      compile_success = false;
      break;
    }      
    break; 

    // register
  case REG_INT:
    switch(right->GetType()) {
    case IMM_INT: {
      RegisterHolder* holder = left->GetRegister();
      math_imm_reg(right->GetOperand(), holder->GetRegister(), 
                   instruction->GetType());
      working_stack.push_front(new RegInstr(holder));
    }
      break;
    case REG_INT: {
      RegisterHolder* holder = right->GetRegister();
      math_reg_reg(holder->GetRegister(), left->GetRegister()->GetRegister(), instruction->GetType());
      working_stack.push_front(new RegInstr(left->GetRegister()));
      ReleaseRegister(holder);
    }
      break;
    case MEM_INT: {
      RegisterHolder* rhs = GetRegister();
      move_mem_reg((long)right->GetOperand(), RBP, rhs->GetRegister());
      RegisterHolder* lhs = left->GetRegister();
      math_reg_reg(rhs->GetRegister(), lhs->GetRegister(), instruction->GetType());
      ReleaseRegister(rhs);
      working_stack.push_front(new RegInstr(lhs));
    }
      break;

    default:
      // unexpected operand kind (e.g. a float fed to an integer compare by a
      // front-end type bug): fail the compile cleanly so the method falls
      // back to the interpreter instead of corrupting the stack model
      compile_success = false;
      break;
    }
    break;

    // memory
  case MEM_INT:
    switch(right->GetType()) {
    case IMM_INT: {
      RegisterHolder* holder = GetRegister();
      move_mem_reg((long)left->GetOperand(), RBP, holder->GetRegister());
      math_imm_reg(right->GetOperand(), holder->GetRegister(), instruction->GetType());
      working_stack.push_front(new RegInstr(holder));
    }
      break;
    case REG_INT: {
      RegisterHolder* rhs = GetRegister();
      move_mem_reg((long)left->GetOperand(), RBP, rhs->GetRegister());
      RegisterHolder* lhs = right->GetRegister();
      math_reg_reg(lhs->GetRegister(), rhs->GetRegister(), instruction->GetType());
      ReleaseRegister(lhs);
      working_stack.push_front(new RegInstr(rhs));
    }
      break;
    case MEM_INT: {
      RegisterHolder* holder = GetRegister();
      move_mem_reg((long)left->GetOperand(), RBP, holder->GetRegister());
      math_mem_reg((long)right->GetOperand(), holder->GetRegister(), instruction->GetType());
      working_stack.push_front(new RegInstr(holder));
    }
      break;

    default:
      // unexpected operand kind (e.g. a float fed to an integer compare by a
      // front-end type bug): fail the compile cleanly so the method falls
      // back to the interpreter instead of corrupting the stack model
      compile_success = false;
      break;
    }
    break;

  default:
    compile_success = false;
    break;
  }
  
  delete left;
  left = nullptr;
    
  delete right;
  right = nullptr;
}

void JitAmd64::ProcessFloatCalculation(StackInstr* instruction) {
  RegInstr* left = working_stack.front();
  working_stack.pop_front();
  
  RegInstr* right = working_stack.front();
  working_stack.pop_front();

  InstructionType type = instruction->GetType();
  switch(left->GetType()) {
    // intermidate
  case IMM_FLOAT:
    switch(right->GetType()) {
    case IMM_FLOAT: {
      RegisterHolder* left_holder = GetXmmRegister();
      move_imm_xreg(left, left_holder->GetRegister());      
      RegisterHolder* right_holder = GetXmmRegister();
      move_imm_xreg(right, right_holder->GetRegister());      
      
      if(type == LES_FLOAT || type == LES_EQL_FLOAT) {
        math_xreg_xreg(left_holder->GetRegister(), right_holder->GetRegister(), 
     instruction->GetType());
        ReleaseXmmRegister(left_holder);
        working_stack.push_front(new RegInstr(right_holder));
      }
      else {
        math_xreg_xreg(right_holder->GetRegister(), left_holder->GetRegister(), 
     instruction->GetType());
        ReleaseXmmRegister(right_holder);
        working_stack.push_front(new RegInstr(left_holder));
      }
    }
      break;
      
    case REG_FLOAT: {      
      RegisterHolder* imm_holder = GetXmmRegister();
      move_imm_xreg(left, imm_holder->GetRegister());
      
      if(type == LES_FLOAT || type == LES_EQL_FLOAT) {
        math_xreg_xreg(imm_holder->GetRegister(), right->GetRegister()->GetRegister(), type);
        ReleaseXmmRegister(imm_holder);
        working_stack.push_front(new RegInstr(right->GetRegister()));
      }
      else {
        math_xreg_xreg(right->GetRegister()->GetRegister(), imm_holder->GetRegister(), type);
        ReleaseXmmRegister(right->GetRegister());
        working_stack.push_front(new RegInstr(imm_holder));
      }
    }
      break;

    case MEM_FLOAT:
    case MEM_INT: {
      RegisterHolder* holder = GetXmmRegister();
      move_mem_xreg((long)right->GetOperand(), RBP, holder->GetRegister());

      RegisterHolder* imm_holder = GetXmmRegister();
      move_imm_xreg(left, imm_holder->GetRegister());

      if(type == LES_FLOAT || type == LES_EQL_FLOAT) {
        math_xreg_xreg(imm_holder->GetRegister(), holder->GetRegister(), type);
        ReleaseXmmRegister(imm_holder);
        working_stack.push_front(new RegInstr(holder));
      }
      else {
        math_xreg_xreg(holder->GetRegister(), imm_holder->GetRegister(), type);
        ReleaseXmmRegister(holder);
        working_stack.push_front(new RegInstr(imm_holder));
      }
    }
      break;

    default:
      // unexpected operand kind (e.g. a float fed to an integer compare by a
      // front-end type bug): fail the compile cleanly so the method falls
      // back to the interpreter instead of corrupting the stack model
      compile_success = false;
      break;
    }      
    break; 

    // register
  case REG_FLOAT:
    switch(right->GetType()) {
    case IMM_FLOAT: {
      RegisterHolder* right_holder = GetXmmRegister();
      move_imm_xreg(right, right_holder->GetRegister());

      RegisterHolder* left_holder = left->GetRegister();      
      if(type == LES_FLOAT || type == LES_EQL_FLOAT) {
        math_xreg_xreg(left_holder->GetRegister(), right_holder->GetRegister(), instruction->GetType());
        ReleaseXmmRegister(left_holder);      
        working_stack.push_front(new RegInstr(right_holder));
      }
      else {
        math_xreg_xreg(right_holder->GetRegister(), left_holder->GetRegister(), instruction->GetType());
        ReleaseXmmRegister(right_holder);      
        working_stack.push_front(new RegInstr(left_holder));
      }
    }
      break;

    case REG_FLOAT: {
      RegisterHolder* holder = right->GetRegister();
      if(type == LES_FLOAT || type == LES_EQL_FLOAT) {
        math_xreg_xreg(left->GetRegister()->GetRegister(), holder->GetRegister(), instruction->GetType());
        working_stack.push_front(new RegInstr(holder));
        ReleaseXmmRegister(left->GetRegister());
      }
      else {
        math_xreg_xreg(holder->GetRegister(), left->GetRegister()->GetRegister(), instruction->GetType());
        working_stack.push_front(new RegInstr(left->GetRegister()));
        ReleaseXmmRegister(holder);
      }
    }
      break;
      
    case MEM_FLOAT:
    case MEM_INT: {
      RegisterHolder* holder = left->GetRegister();
      if(type == LES_FLOAT || type == LES_EQL_FLOAT) {
        RegisterHolder* right_holder = GetXmmRegister();
        move_mem_xreg((long)right->GetOperand(), RBP, right_holder->GetRegister());
        math_xreg_xreg(holder->GetRegister(), right_holder->GetRegister(), instruction->GetType());
        ReleaseXmmRegister(holder);
        working_stack.push_front(new RegInstr(right_holder));
      }
      else {
        math_mem_xreg((long)right->GetOperand(), holder->GetRegister(), instruction->GetType());
        working_stack.push_front(new RegInstr(holder));
      }
    }
      break;

    default:
      // unexpected operand kind (e.g. a float fed to an integer compare by a
      // front-end type bug): fail the compile cleanly so the method falls
      // back to the interpreter instead of corrupting the stack model
      compile_success = false;
      break;
    }
    break;

    // memory
  case MEM_FLOAT:
  case MEM_INT:
    switch(right->GetType()) {
    case IMM_FLOAT: {
      RegisterHolder* holder = GetXmmRegister();
      move_mem_xreg((long)left->GetOperand(), RBP, holder->GetRegister());
      
      RegisterHolder* imm_holder = GetXmmRegister();
      move_imm_xreg(right, imm_holder->GetRegister());
      if(type == LES_FLOAT || type == LES_EQL_FLOAT) {
        math_xreg_xreg(holder->GetRegister(), imm_holder->GetRegister(), type);
        ReleaseXmmRegister(holder);
        working_stack.push_front(new RegInstr(imm_holder));
      }
      else {
        math_xreg_xreg(imm_holder->GetRegister(), holder->GetRegister(), type);
        ReleaseXmmRegister(imm_holder);
        working_stack.push_front(new RegInstr(holder));
      }
    }
      break;
      
    case REG_FLOAT: {
      RegisterHolder* holder = right->GetRegister();
      if(type == LES_FLOAT || type == LES_EQL_FLOAT) {
        math_mem_xreg((long)left->GetOperand(), holder->GetRegister(), instruction->GetType());
        working_stack.push_front(new RegInstr(holder));
      }
      else {
        RegisterHolder* right_holder = GetXmmRegister();
        move_mem_xreg((long)left->GetOperand(), RBP, right_holder->GetRegister());
        math_xreg_xreg(holder->GetRegister(), right_holder->GetRegister(), instruction->GetType());
        ReleaseXmmRegister(holder);
        working_stack.push_front(new RegInstr(right_holder));
      }
    }
      break;
      
    case MEM_FLOAT:
    case MEM_INT: {
      RegisterHolder* left_holder = GetXmmRegister();
      move_mem_xreg((long)left->GetOperand(), RBP, left_holder->GetRegister());

      RegisterHolder* right_holder = GetXmmRegister();
      move_mem_xreg((long)right->GetOperand(), RBP, right_holder->GetRegister());
      if(type == LES_FLOAT || type == LES_EQL_FLOAT) {
        math_xreg_xreg(left_holder->GetRegister(), right_holder->GetRegister(),  
     instruction->GetType());
        ReleaseXmmRegister(left_holder);
        working_stack.push_front(new RegInstr(right_holder));
      }
      else {
        math_xreg_xreg(right_holder->GetRegister(), left_holder->GetRegister(),  
     instruction->GetType());  
        ReleaseXmmRegister(right_holder);
        working_stack.push_front(new RegInstr(left_holder));
      }
    }
      break;

    default:
      // unexpected operand kind (e.g. a float fed to an integer compare by a
      // front-end type bug): fail the compile cleanly so the method falls
      // back to the interpreter instead of corrupting the stack model
      compile_success = false;
      break;
    }
    break;

  default:
    compile_success = false;
    break;
  }

  delete left;
  left = nullptr;

  delete right;
  right = nullptr;
}

void JitAmd64::ProcessFloatOperation(StackInstr* instruction) {
  RegInstr* left = working_stack.front();
  working_stack.pop_front();

  InstructionType type = instruction->GetType();
#ifdef _DEBUG_JIT
  // a Float local read through the register cache arrives as REG_FLOAT, which
  // call_xfunc takes; the assert predates the cache and stopped every tracing
  // run of a method that calls Sin/Cos/Sqrt on a cached local
  assert(left->GetType() == MEM_FLOAT || left->GetType() == REG_FLOAT);
#endif

  RegisterHolder* holder = nullptr;
  switch(type) {
  case SIN_FLOAT:
    holder = call_xfunc(sin, left);
    break;

  case COS_FLOAT:
    holder = call_xfunc(cos, left);
    break;

  case TAN_FLOAT:
    holder = call_xfunc(tan, left);
    break;

  case LOG_FLOAT:
    holder = call_xfunc(log, left);
    break;

  case LOG10_FLOAT:
    holder = call_xfunc(log10, left);
    break;

  case TRUNC_FLOAT:
    holder = call_xfunc(trunc, left);
    break;

  case EXP_FLOAT:
    holder = call_xfunc(exp, left);
    break;

  case ASIN_FLOAT:
    holder = call_xfunc(asin, left);
    break;

  case ACOS_FLOAT:
    holder = call_xfunc(acos, left);
    break;

  case ATAN_FLOAT:
    holder = call_xfunc(atan, left);
    break;

  case ACOSH_FLOAT:
    holder = call_xfunc(acosh, left);
    break;

  case ASINH_FLOAT:
    holder = call_xfunc(asinh, left);
    break;

  case ATANH_FLOAT:
    holder = call_xfunc(atanh, left);
    break;

  case LOG2_FLOAT:
    holder = call_xfunc(log2, left);
    break;

  case CBRT_FLOAT:
    holder = call_xfunc(cbrt, left);
    break;

  case COSH_FLOAT:
    holder = call_xfunc(cosh, left);
    break;

  case SINH_FLOAT:
    holder = call_xfunc(sinh, left);
    break;

  case TANH_FLOAT:
    holder = call_xfunc(tanh, left);
    break;

  case GAMMA_FLOAT:
    holder = call_xfunc(tgamma, left);
    break;

  case ATAN2_FLOAT:
    holder = call_xfunc2(atan2, left);
    break;

  case MOD_FLOAT:
    holder = call_xfunc2(fmod, left);
    break;

  case POW_FLOAT:
    holder = call_xfunc2(pow, left);
    break;

  default:
#ifdef _DEBUG_JIT
    assert(false);
#endif
    break;
  }

  if(!holder) {
    holder = GetXmmRegister();
    fstp_mem((long)left->GetOperand(), RBP);
    move_mem_xreg((long)left->GetOperand(), RBP, holder->GetRegister());
  }
  working_stack.push_front(new RegInstr(holder));

  delete left;
  left = nullptr;
}

void JitAmd64::ProcessFloatSquareRoot([[maybe_unused]] StackInstr* instruction) {
  RegInstr* left = working_stack.front();
  working_stack.pop_front();

  RegisterHolder* holder;
  if(left->GetType() == REG_FLOAT) {
    holder = left->GetRegister();
  }
  else {
    holder = GetXmmRegister();
    move_mem_xreg((long)left->GetOperand(), RBP, holder->GetRegister());
  }
  sqrt_xreg_xreg(holder->GetRegister(), holder->GetRegister());
  
  working_stack.push_front(new RegInstr(holder));

  delete left;
  left = nullptr;
}

void JitAmd64::ProcessFloatRound([[maybe_unused]] StackInstr* instruction, wchar_t mode) {
  RegInstr* left = working_stack.front();
  working_stack.pop_front();

  RegisterHolder* holder;
  if(left->GetType() == REG_FLOAT) {
    holder = left->GetRegister();
  }
  else {
    holder = GetXmmRegister();
    move_mem_xreg((long)left->GetOperand(), RBP, holder->GetRegister());
  }
  round_xreg_xreg(holder->GetRegister(), holder->GetRegister(), mode);

  working_stack.push_front(new RegInstr(holder));

  delete left;
  left = nullptr;
}

/////////////////// OPERATIONS ///////////////////

void JitAmd64::move_reg_reg(Register src, Register dest) {
  if(src != dest) {
#ifdef _DEBUG_JIT
    std::wcout << L"  " << (++instr_count) << L": [movq %" << GetRegisterName(src) 
          << L", %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
    // encode
    AddMachineCode(ROB(src, dest));
    AddMachineCode(0x89);
    unsigned char code = 0xc0;
    // write value
    RegisterEncode3(code, 2, src);
    RegisterEncode3(code, 5, dest);
    AddMachineCode(code);
  }
}

void JitAmd64::move_reg_mem8(Register src, long offset, Register dest) { 
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [movb %" << GetRegisterName(src) 
        << L", " << offset << L"(%" << GetRegisterName(dest) << L")" << L"]" 
        << std::endl;
#endif
  // encode
  AddMachineCode(RXB(src, dest));
  AddMachineCode(0x88);
  AddMachineCode(ModRM(dest, src));
  // write value
  AddImm(offset);
}

// REX for a 16-bit operation: only the R (reg field) and B (base) extension bits,
// after the 0x66 operand-size prefix; nothing when neither register is R8-R15
static unsigned char Rex16(Register reg_field, Register base) {
  unsigned char rex = 0x40;
  if(reg_field > RSP && reg_field < XMM0) {
    rex |= 0x04;
  }
  if(base > RSP && base < XMM0) {
    rex |= 0x01;
  }
  return rex == 0x40 ? 0 : rex;
}

void JitAmd64::move_reg_mem16(Register src, int32_t offset, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [movw %" << GetRegisterName(src)
    << L", " << offset << L"(%" << GetRegisterName(dest) << L")" << L"]"
    << std::endl;
#endif
  // encode
  AddMachineCode(0x66);
  if(const unsigned char rex = Rex16(src, dest)) {
    AddMachineCode(rex);
  }
  AddMachineCode(0x89);
  AddMachineCode(ModRM(dest, src));
  // write value
  AddImm(offset);
}

void JitAmd64::move_reg_mem32(Register src, long offset, Register dest) { 
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [movw %" << GetRegisterName(src) 
        << L", " << offset << L"(%" << GetRegisterName(dest) << L")" << L"]" 
        << std::endl;
#endif
  // encode
  AddMachineCode(RXB32(src, dest));
  AddMachineCode(0x89);
  AddMachineCode(ModRM(dest, src));
  // write value
  AddImm(offset);
}
    
void JitAmd64::move_reg_mem(Register src, long offset, Register dest) { 
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [movl %" << GetRegisterName(src) 
        << L", " << offset << L"(%" << GetRegisterName(dest) << L")" << L"]" 
        << std::endl;
#endif
  // encode
  AddMachineCode(RXB(src, dest));
  AddMachineCode(0x89);
  AddMachineCode(ModRM(dest, src));
  // write value
  AddImm(offset);
}

void JitAmd64::move_mem8_reg(long offset, Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [movb " << offset << L"(%" 
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest)
        << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(RXB(dest, src));
  AddMachineCode(0x0f);
  AddMachineCode(0xb6);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}

void JitAmd64::move_mem16_reg(int32_t offset, Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [movw " << offset << L"(%"
    << GetRegisterName(src) << L"), %" << GetRegisterName(dest)
    << L"]" << std::endl;
#endif
  // encode
  if(const unsigned char rex = Rex16(dest, src)) {
    AddMachineCode(rex);
  }
  AddMachineCode(0x0f);
  AddMachineCode(0xb7);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}

void JitAmd64::move_mem32_reg(long offset, Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [movw " << offset << L"(%" 
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest)
        << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(RXB32(dest, src));
  AddMachineCode(0x8b);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}

void JitAmd64::move_mem_reg(long offset, Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [movq " << offset << L"(%" 
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest)
        << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(RXB(dest, src));
  AddMachineCode(0x8b);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}

void JitAmd64::move_mem_reg32(long offset, Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [movl " << offset << L"(%"
    << GetRegisterName(src) << L"), %" << GetRegisterName(dest)
    << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(RXB32(dest, src));
  AddMachineCode(0x8b);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}
 
void JitAmd64::move_imm_memx(RegInstr* instr, long offset, Register dest) {
  RegisterHolder* tmp_holder = GetXmmRegister();
  move_imm_xreg(instr, tmp_holder->GetRegister());
  move_xreg_mem(tmp_holder->GetRegister(), offset, dest);
  ReleaseXmmRegister(tmp_holder);
}

void JitAmd64::move_imm_mem8(int8_t imm, long offset, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [movb $" << imm << L", " << offset 
        << L"(%" << GetRegisterName(dest) << L")" << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(XB(dest));
  AddMachineCode(0xc6);
  unsigned char code = 0x80;
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
  // write value
  AddImm(offset);
  AddMachineCode((unsigned char)imm);
}

void JitAmd64::move_imm_mem16(int16_t imm, int32_t offset, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [movw $" << imm << L", " << offset
    << L"(%" << GetRegisterName(dest) << L")" << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(0x66);
  if(const unsigned char rex = Rex16(RAX, dest)) {   // /0: no register in the reg field
    AddMachineCode(rex);
  }
  AddMachineCode(0xc7);
  unsigned char code = 0x80;
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
  // write value
  AddImm(offset);
  AddImm16(imm);
}

void JitAmd64::move_imm_mem32(int32_t imm, long offset, Register dest) {
  move_imm_mem(imm, offset, dest);
}

void JitAmd64::move_imm_mem(int64_t imm, long offset, Register dest) {
  if(imm < INT32_MIN || imm > INT32_MAX) {
    RegisterHolder* holder = GetRegister();
    move_imm_reg(imm, holder->GetRegister());
    move_reg_mem(holder->GetRegister(), offset, dest);
    ReleaseRegister(holder);
  }
  else {
#ifdef _DEBUG_JIT
    std::wcout << L"  " << (++instr_count) << L": [movq $" << imm << L", " << offset
      << L"(%" << GetRegisterName(dest) << L")" << L"]" << std::endl;
#endif
    // encode
    AddMachineCode(XB(dest));
    AddMachineCode(0xc7);
    unsigned char code = 0x80;
    RegisterEncode3(code, 5, dest);
    AddMachineCode(code);
    // write value
    AddImm(offset);
    AddImm((long)imm);
  }
}

#ifdef _WIN64
void JitAmd64::move_imm_reg(int64_t imm, Register reg) {
#else
void JitAmd64::move_imm_reg(long imm, Register reg) {
#endif
  // NOTE: XOR reg,reg clobbers FLAGS - cannot use as move_imm_reg(0) replacement
  // because callers may depend on preserved flags.

  // Optimization: MOV r/m64, imm32 (sign-extended) for values in 32-bit range
  if(imm >= INT32_MIN && imm <= INT32_MAX) {
#ifdef _DEBUG_JIT
    std::wcout << L"  " << (++instr_count) << L": [movq $" << imm << L", %"
          << GetRegisterName(reg) << L"] (imm32)" << std::endl;
#endif
    AddMachineCode(B(reg));
    AddMachineCode(0xc7);
    unsigned char code = 0xc0;
    RegisterEncode3(code, 5, reg);
    AddMachineCode(code);
    AddImm((int32_t)imm);
    return;
  }

#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [movsq $" << imm << L", %"
        << GetRegisterName(reg) << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(XB(reg));
  unsigned char code = 0xb8;
  RegisterEncode3(code, 5, reg);
  AddMachineCode(code);
  // write value
  AddImm64(imm);
}

void JitAmd64::move_imm_xreg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
#ifdef _WIN64  
  move_imm_reg(instr->GetOperand2(), imm_holder->GetRegister());  
#else
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
#endif  
  move_mem_xreg(0, imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}
    
void JitAmd64::move_mem_xreg(long offset, Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [movsd " << offset << L"(%" 
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(RXB(dest, src));
  AddMachineCode(0x0f);
  AddMachineCode(0x10);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}
    
void JitAmd64::move_xreg_mem(Register src, long offset, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [movsd %" << GetRegisterName(src) 
        << L", " << offset << L"(%" << GetRegisterName(dest) << L")" << L"]" 
        << std::endl;
#endif 
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(RXB(src, dest));
  AddMachineCode(0x0f);
  AddMachineCode(0x11);
  AddMachineCode(ModRM(dest, src));
  // write value
  AddImm(offset);
}
    
void JitAmd64::move_xreg_xreg(Register src, Register dest) {
  if(src != dest) {
#ifdef _DEBUG_JIT
    std::wcout << L"  " << (++instr_count) << L": [movsd %" << GetRegisterName(src) 
          << L", %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
    // encode
    AddMachineCode(0xf2);
    AddMachineCode(ROB(src, dest));
    AddMachineCode(0x0f);
    AddMachineCode(0x11);
    unsigned char code = 0xc0;
    // write value
    RegisterEncode3(code, 2, src);
    RegisterEncode3(code, 5, dest);
    AddMachineCode(code);
  }
}

void JitAmd64::move_reg_xreg(Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [movq %" << GetRegisterName(src)
        << L", %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
  // encode: 66 REX.W 0F 6E /r, the XMM register in the reg field
  AddMachineCode(0x66);
  AddMachineCode(ROB(dest, src));
  AddMachineCode(0x0f);
  AddMachineCode(0x6e);
  unsigned char code = 0xc0;
  RegisterEncode3(code, 2, dest);
  RegisterEncode3(code, 5, src);
  AddMachineCode(code);
}

void JitAmd64::move_xreg_reg(Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [movq %" << GetRegisterName(src)
        << L", %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
  // encode: 66 REX.W 0F 7E /r, the XMM register in the reg field
  AddMachineCode(0x66);
  AddMachineCode(ROB(src, dest));
  AddMachineCode(0x0f);
  AddMachineCode(0x7e);
  unsigned char code = 0xc0;
  RegisterEncode3(code, 2, src);
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
}

bool JitAmd64::cond_jmp(InstructionType type) {
  if(instr_index >= method->GetInstructionCount()) {
    return false;
  }
  
  StackInstr* next_instr = method->GetInstruction(instr_index);
  if(next_instr->GetType() == JMP && next_instr->GetOperand2() > -1) {
    // if(false) {
#ifdef _DEBUG_JIT
    std::wcout << L"JMP: id=" << next_instr->GetOperand() << L", regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
    AddMachineCode(0x0f);

    //
    // jump if true
    //
    if(next_instr->GetOperand2()) {
      switch(type) {
      case LES_INT:  
#ifdef _DEBUG_JIT
        std::wcout << L"  " << (++instr_count) << L": [jl]" << std::endl;
#endif
        AddMachineCode(0x8c);
        break;

      case GTR_INT:
#ifdef _DEBUG_JIT
        std::wcout << L"  " << (++instr_count) << L": [jg]" << std::endl;
#endif
        AddMachineCode(0x8f);
        break;

      case EQL_INT:
      case EQL_FLOAT:
#ifdef _DEBUG_JIT
        std::wcout << L"  " << (++instr_count) << L": [je]" << std::endl;
#endif
        AddMachineCode(0x84);
        break;

      case NEQL_INT:
      case NEQL_FLOAT:
#ifdef _DEBUG_JIT
        std::wcout << L"  " << (++instr_count) << L": [jne]" << std::endl;
#endif
        AddMachineCode(0x85);
        break;

      case LES_EQL_INT:
#ifdef _DEBUG_JIT
        std::wcout << L"  " << (++instr_count) << L": [jle]" << std::endl;
#endif
        AddMachineCode(0x8e);
        break;
        
      case GTR_EQL_INT:
#ifdef _DEBUG_JIT
        std::wcout << L"  " << (++instr_count) << L": [jge]" << std::endl;
#endif
        AddMachineCode(0x8d);
        break;
    
      case LES_FLOAT:
#ifdef _DEBUG_JIT
        std::wcout << L"  " << (++instr_count) << L": [ja]" << std::endl;
#endif
        AddMachineCode(0x87);
        break;
    
      case GTR_FLOAT:
#ifdef _DEBUG_JIT
        std::wcout << L"  " << (++instr_count) << L": [ja]" << std::endl;
#endif
        AddMachineCode(0x87);
        break;

      case LES_EQL_FLOAT:
#ifdef _DEBUG_JIT
        std::wcout << L"  " << (++instr_count) << L": [jae]" << std::endl;
#endif
        AddMachineCode(0x83);
        break;
    
      case GTR_EQL_FLOAT:
#ifdef _DEBUG_JIT
        std::wcout << L"  " << (++instr_count) << L": [jae]" << std::endl;
#endif
        AddMachineCode(0x83);
        break;
        
      default:
        break;
      }  
    }
    //
    // jump - false
    //
    else {
      switch(type) {
      case LES_INT:  
#ifdef _DEBUG_JIT
        std::wcout << L"  " << (++instr_count) << L": [jge]" << std::endl;
#endif
        AddMachineCode(0x8d);
        break;

      case GTR_INT:
#ifdef _DEBUG_JIT
        std::wcout << L"  " << (++instr_count) << L": [jle]" << std::endl;
#endif
        AddMachineCode(0x8e);
        break;

      case EQL_INT:
      case EQL_FLOAT:
#ifdef _DEBUG_JIT
        std::wcout << L"  " << (++instr_count) << L": [jne]" << std::endl;
#endif
        AddMachineCode(0x85);
        break;

      case NEQL_INT:
      case NEQL_FLOAT:
#ifdef _DEBUG_JIT
        std::wcout << L"  " << (++instr_count) << L": [je]" << std::endl;
#endif
        AddMachineCode(0x84);
        break;

      case LES_EQL_INT:
#ifdef _DEBUG_JIT
        std::wcout << L"  " << (++instr_count) << L": [jg]" << std::endl;
#endif
        AddMachineCode(0x8f);
        break;
        
      case GTR_EQL_INT:
#ifdef _DEBUG_JIT
        std::wcout << L"  " << (++instr_count) << L": [jl]" << std::endl;
#endif
        AddMachineCode(0x8c);
        break;

      case LES_FLOAT:
        AddMachineCode(0x86);
        break;
    
      case GTR_FLOAT:
        AddMachineCode(0x86);
        break;

      case LES_EQL_FLOAT:
        AddMachineCode(0x82);
        break;
    
      case GTR_EQL_FLOAT:
        AddMachineCode(0x82);
        break;
    
      default:
        break;
      }  
    }
    
    // store update index
    jump_table.insert(std::pair<long, StackInstr*>(code_index, next_instr));
    
    // temp offset
    AddImm(0);
    skip_jump = true;
    
    return true;
  }
  
  return false;
}

void JitAmd64::loop(long offset)
{
  AddMachineCode(0xe2);
  AddMachineCode((unsigned char)offset);
}

RegisterHolder* JitAmd64::call_xfunc(double(*func_ptr)(double), RegInstr* left)
{
  // The libc call below clobbers caller-saved GP/XMM registers. Any local
  // variable still held in the register cache (e.g. an object 'self'/param
  // loaded before a Float->Sin) would be destroyed, and ProcessLoad's
  // cache-hit path would then hand back the clobbered register -- a wild
  // (often float-bit) pointer that corrupts the heap on the next store.
  // Stores are write-through, so flushing just drops the cached registers;
  // later loads reload from memory.
  FlushLocalCache();

  // Preserve in-flight working-stack temps across the libc call. Under SysV
  // every XMM register and RAX/RCX/RDX/R8-R11 are caller-saved, so a pending
  // temp -- the 7 in "7 + x->Pow(5.0)->As(Int)" -- is destroyed by pow().
  // ARM64 fixed the same bug (db4c37b662); AMD64 never got it. Win64 keeps
  // XMM6-15 and RBX/RSI/RDI, which is why it never showed on Windows. Spill to
  // the scratch slots ProcessStackCallback already uses; with more temps than
  // slots, fall back to the interpreter for this method.
  std::vector<std::pair<Register, long> > spilled_regs;
  std::vector<std::pair<Register, long> > spilled_xregs;
  long spill_off = TMP_REG_0;
  long xspill_off = TMP_XMM_1;
  for(RegInstr* pending : working_stack) {
    if(pending->GetType() == REG_INT) {
      if(spill_off < TMP_REG_9) { compile_success = false; break; }
      const Register r = pending->GetRegister()->GetRegister();
      move_reg_mem(r, spill_off, RBP);
      spilled_regs.push_back(std::make_pair(r, spill_off));
      spill_off -= sizeof(size_t);
    }
    else if(pending->GetType() == REG_FLOAT) {
      if(xspill_off < TMP_XMM_2) { compile_success = false; break; }
      const Register r = pending->GetRegister()->GetRegister();
      move_xreg_mem(r, xspill_off, RBP);
      spilled_xregs.push_back(std::make_pair(r, xspill_off));
      xspill_off -= sizeof(double);
    }
  }

  move_xreg_mem(XMM0, TMP_XMM_0, RBP);
  if(left->GetType() == REG_FLOAT) {
    if(left->GetRegister()->GetRegister() != XMM0) {
      move_xreg_xreg(left->GetRegister()->GetRegister(), XMM0);
    }
    ReleaseXmmRegister(left->GetRegister());
  }
  else {
    move_mem_xreg((long)left->GetOperand(), RBP, XMM0);
  }

#ifdef _WIN64
  sub_imm_reg(32, RSP);
#endif
  RegisterHolder* call_holder = GetRegister();
  move_imm_reg((size_t)func_ptr, call_holder->GetRegister());
  call_reg(call_holder->GetRegister());
  ReleaseRegister(call_holder);
#ifdef _WIN64
  add_imm_reg(32, RSP);
#endif

  RegisterHolder* result_holder = GetXmmRegister();
  if(result_holder->GetRegister() != XMM0) {
    move_xreg_xreg(XMM0, result_holder->GetRegister());
    move_mem_xreg(TMP_XMM_0, RBP, XMM0);
  }

  // Restore the temps spilled above.
  for(size_t si = 0; si < spilled_regs.size(); ++si) {
    move_mem_reg(spilled_regs[si].second, RBP, spilled_regs[si].first);
  }
  for(size_t si = 0; si < spilled_xregs.size(); ++si) {
    move_mem_xreg(spilled_xregs[si].second, RBP, spilled_xregs[si].first);
  }

  return result_holder;
}

RegisterHolder* JitAmd64::call_xfunc2(double(*func_ptr)(double, double), RegInstr* left)
{
  RegInstr* right = working_stack.front();
  working_stack.pop_front();

  // See call_xfunc: drop cached locals across the clobbering libc call.
  FlushLocalCache();

  // Preserve in-flight working-stack temps across the libc call. Under SysV
  // every XMM register and RAX/RCX/RDX/R8-R11 are caller-saved, so a pending
  // temp -- the 7 in "7 + x->Pow(5.0)->As(Int)" -- is destroyed by pow().
  // ARM64 fixed the same bug (db4c37b662); AMD64 never got it. Win64 keeps
  // XMM6-15 and RBX/RSI/RDI, which is why it never showed on Windows. Spill to
  // the scratch slots ProcessStackCallback already uses; with more temps than
  // slots, fall back to the interpreter for this method.
  std::vector<std::pair<Register, long> > spilled_regs;
  std::vector<std::pair<Register, long> > spilled_xregs;
  long spill_off = TMP_REG_0;
  long xspill_off = TMP_XMM_2;
  for(RegInstr* pending : working_stack) {
    if(pending->GetType() == REG_INT) {
      if(spill_off < TMP_REG_9) { compile_success = false; break; }
      const Register r = pending->GetRegister()->GetRegister();
      move_reg_mem(r, spill_off, RBP);
      spilled_regs.push_back(std::make_pair(r, spill_off));
      spill_off -= sizeof(size_t);
    }
    else if(pending->GetType() == REG_FLOAT) {
      if(xspill_off < TMP_XMM_2) { compile_success = false; break; }
      const Register r = pending->GetRegister()->GetRegister();
      move_xreg_mem(r, xspill_off, RBP);
      spilled_xregs.push_back(std::make_pair(r, xspill_off));
      xspill_off -= sizeof(double);
    }
  }

#ifdef _DEBUG_JIT
  // as in ProcessFloatOperation: a cached Float local is REG_FLOAT
  assert(right->GetType() == MEM_FLOAT || right->GetType() == REG_FLOAT);
#endif

  move_xreg_mem(XMM1, TMP_XMM_1, RBP);
  if(left->GetType() == REG_FLOAT) {
    if(left->GetRegister()->GetRegister() != XMM1) {
      move_xreg_xreg(left->GetRegister()->GetRegister(), XMM1);
    }
    ReleaseXmmRegister(left->GetRegister());
  }
  else {
    move_mem_xreg((long)left->GetOperand(), RBP, XMM1);
  }

  move_xreg_mem(XMM0, TMP_XMM_0, RBP);
  if(right->GetType() == REG_FLOAT) {
    if(right->GetRegister()->GetRegister() != XMM0) {
      move_xreg_xreg(right->GetRegister()->GetRegister(), XMM0);
    }
    ReleaseXmmRegister(right->GetRegister());
  }
  else {
    move_mem_xreg((long)right->GetOperand(), RBP, XMM0);
  }

#ifdef _WIN64
  sub_imm_reg(32, RSP);
#endif
  RegisterHolder* call_holder = GetRegister();
  move_imm_reg((size_t)func_ptr, call_holder->GetRegister());
  call_reg(call_holder->GetRegister());
  ReleaseRegister(call_holder);
#ifdef _WIN64
  add_imm_reg(32, RSP);
#endif

  RegisterHolder* result_holder = GetXmmRegister();
  move_mem_xreg(TMP_XMM_1, RBP, XMM1);
  if(result_holder->GetRegister() != XMM0) {
    move_xreg_xreg(XMM0, result_holder->GetRegister());
    move_mem_xreg(TMP_XMM_0, RBP, XMM0);
  }

  delete right;
  right = nullptr;

  // Restore the temps spilled above.
  for(size_t si = 0; si < spilled_regs.size(); ++si) {
    move_mem_reg(spilled_regs[si].second, RBP, spilled_regs[si].first);
  }
  for(size_t si = 0; si < spilled_xregs.size(); ++si) {
    move_mem_xreg(spilled_xregs[si].second, RBP, spilled_xregs[si].first);
  }

  return result_holder;
}

void JitAmd64::math_imm_reg(int64_t imm, Register reg, InstructionType type)
{
  switch(type) {
  case AND_INT:
    and_imm_reg(imm, reg);
    break;

  case OR_INT:
    or_imm_reg(imm, reg);
    break;
    
  case ADD_INT:
    add_imm_reg(imm, reg);
    break;

  case SUB_INT:
    sub_imm_reg(imm, reg);
    break;

  case MUL_INT:
    mul_imm_reg(imm, reg);
    break;

  case DIV_INT:
    div_imm_reg(imm, reg);
    break;
    
  case MOD_INT:
    div_imm_reg(imm, reg, true);
    break;
    
  case SHL_INT:
    shl_imm_reg(imm, reg);
    break;
    
  case SHR_INT:
    sar_imm_reg(imm, reg);
    break;

  case BIT_AND_INT:
    and_imm_reg(imm, reg);
    break;
    
  case BIT_OR_INT:
    or_imm_reg(imm, reg);
    break;
    
  case BIT_XOR_INT:
    xor_imm_reg(imm, reg);
    break;
    
  case LES_INT:  
  case GTR_INT:
  case EQL_INT:
  case NEQL_INT:
  case LES_EQL_INT:
  case GTR_EQL_INT:
    cmp_imm_reg(imm, reg);
    if(!cond_jmp(type)) {
      cmov_reg(reg, type);
    }
    break;

  default:
    break;
  }
}

void JitAmd64::math_reg_reg(Register src, Register dest, InstructionType type) {
  switch(type) {
  case SHL_INT:
    shl_reg_reg(src, dest);
    break;
    
  case SHR_INT:
    sar_reg_reg(src, dest);
    break;
  case AND_INT:
    and_reg_reg(src, dest);
    break;

  case OR_INT:
    or_reg_reg(src, dest);
    break;
    
  case ADD_INT:
    add_reg_reg(src, dest);
    break;

  case SUB_INT:
    sub_reg_reg(src, dest);
    break;

  case MUL_INT:
    mul_reg_reg(src, dest);
    break;

  case DIV_INT:
    div_reg_reg(src, dest);
    break;

  case MOD_INT:
    div_reg_reg(src, dest, true);
    break;

  case BIT_AND_INT:
    and_reg_reg(src, dest);
    break;

  case BIT_OR_INT:
    or_reg_reg(src, dest);
    break;

  case BIT_XOR_INT:
    xor_reg_reg(src, dest);
    break;
    
  case LES_INT:  
  case GTR_INT:
  case EQL_INT:
  case NEQL_INT:
  case LES_EQL_INT:
  case GTR_EQL_INT:
    cmp_reg_reg(src, dest);
    if(!cond_jmp(type)) {
      cmov_reg(dest, type);
    }
    break;

  default:
    break;
  }
}

void JitAmd64::math_mem_reg(long offset, Register reg, InstructionType type) {
  switch(type) {
  case SHL_INT:
    shl_mem_reg(offset, RBP, reg);
    break;

  case SHR_INT:
    sar_mem_reg(offset, RBP, reg);
    break;
    
  case AND_INT:
    and_mem_reg(offset, RBP, reg);
    break;
    
  case OR_INT:
    or_mem_reg(offset, RBP, reg);
    break;
    
  case ADD_INT:
    add_mem_reg(offset, RBP, reg);
    break;
    
  case SUB_INT:
    sub_mem_reg(offset, RBP, reg);
    break;
    
  case MUL_INT:
    mul_mem_reg(offset, RBP, reg);
    break;

  case DIV_INT:
    div_mem_reg(offset, RBP, reg, false);
    break;
    
  case MOD_INT:
    div_mem_reg(offset, RBP, reg, true);
    break;

  case BIT_AND_INT:
    and_mem_reg(offset, RBP, reg);
    break;

  case BIT_OR_INT:
    or_mem_reg(offset, RBP, reg);
    break;

  case BIT_XOR_INT:
    xor_mem_reg(offset, RBP, reg);
    break;
    
  case LES_INT:
  case LES_EQL_INT:
  case GTR_INT:
  case EQL_INT:
  case NEQL_INT:  
  case GTR_EQL_INT:
    cmp_mem_reg(offset, RBP, reg);
    if(!cond_jmp(type)) {
      cmov_reg(reg, type);
    }
    break;

  default:
    break;
  }
}

void JitAmd64::math_imm_xreg(RegInstr* instr, Register reg, InstructionType type) {
  switch(type) {
  case ADD_FLOAT:
    add_imm_xreg(instr, reg);
    break;

  case SUB_FLOAT:
    sub_imm_xreg(instr, reg);
    break;

  case MUL_FLOAT:
    mul_imm_xreg(instr, reg);
    break;

  case DIV_FLOAT:
    div_imm_xreg(instr, reg);
    break;
    
  case LES_FLOAT:
  case LES_EQL_FLOAT:
  case GTR_FLOAT:
  case EQL_FLOAT:
  case NEQL_FLOAT:
  case GTR_EQL_FLOAT:
#ifdef _WIN64
    cmp_imm_xreg(instr->GetOperand2(), reg);
#else
    cmp_imm_xreg(instr->GetOperand(), reg);
#endif
    // See the note in math_xreg_xreg: cmov_reg must never be handed an XMM
    // register. The caller materialises the bool into a general-purpose one.
    cond_jmp(type);
    break;
    
  default:
    break;
  }
}

void JitAmd64::math_mem_xreg(long offset, Register dest, InstructionType type) {
  RegisterHolder* holder = GetXmmRegister();
  move_mem_xreg(offset, RBP, holder->GetRegister());
  math_xreg_xreg(holder->GetRegister(), dest, type);
  ReleaseXmmRegister(holder);
}

void JitAmd64::math_xreg_xreg(Register src, Register dest, InstructionType type) {
  switch(type) {
  case ADD_FLOAT:
    add_xreg_xreg(src, dest);
    break;

  case SUB_FLOAT:
    sub_xreg_xreg(src, dest);
    break;

  case MUL_FLOAT:
    mul_xreg_xreg(src, dest);
    break;

  case DIV_FLOAT:
    div_xreg_xreg(src, dest);
    break;
    
  case LES_FLOAT:
  case LES_EQL_FLOAT:
  case GTR_FLOAT:
  case EQL_FLOAT:
  case NEQL_FLOAT:
  case GTR_EQL_FLOAT:
    cmp_xreg_xreg(src, dest);
    // NO cmov_reg here. cmov_reg's first act is move_imm_reg(0, reg) -- a
    // GENERAL-PURPOSE move -- so handing it an XMM register emitted a mov into
    // whatever general-purpose register that XMM's number encodes to. The float
    // pool is XMM10..XMM15, which encode to R10..R15, and R12-R15 are
    // callee-saved: JIT-compiled code was quietly trashing a register the
    // surrounding C++ VM still relied on, so the fault landed inside the VM
    // after control returned to it. The bool is materialised by the LES_FLOAT/
    // GTR_FLOAT/... case in ProcessInstructions, which pops this meaningless xmm
    // result ("pop invalid xmm register") and calls cmov_reg on a register from
    // GetRegister(). ARM64 already does the equivalent -- it releases the FP
    // register and allocates a general-purpose one before cmov_reg.
    //
    // Only reachable when the compare is NOT fused with a following conditional
    // jump, i.e. when its result is stored or otherwise materialised
    // (`close := a < b;`) rather than branched on (`if(a < b)`), which is why
    // this survived: `if` is the common shape, and a clobber only faults when
    // the encoded register happens to be live.
    //
    // cmp_* leaves the flags for the caller; nothing between here and its
    // cmov_reg emits an instruction, so the flags still stand.
    cond_jmp(type);
    break;

  default:
    break;
  }
}    

void JitAmd64::cmp_reg_reg(Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [cmpq %" << GetRegisterName(src) 
        << L", %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(ROB(src, dest));
  AddMachineCode(0x39);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, src);
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
}

void JitAmd64::cmp_mem_reg(long offset, Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [cmpq " << offset << L"(%" 
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest) 
        << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(RXB(dest, src));
  AddMachineCode(0x3b);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}

// TODO: 64-bit literal operation for Windows
void JitAmd64::cmp_imm_reg(int64_t imm, Register reg) {
  if(imm < INT32_MIN || imm > INT32_MAX) {
    RegisterHolder* holder = GetRegister();
    move_imm_reg(imm, holder->GetRegister());
    cmp_reg_reg(holder->GetRegister(), reg);
    ReleaseRegister(holder);
  }
  else if(imm == 0) {
    // TEST reg, reg (3 bytes vs 7 for CMP reg, 0)
#ifdef _DEBUG_JIT
    std::wcout << L"  " << (++instr_count) << L": [testq %"
      << GetRegisterName(reg) << L", %" << GetRegisterName(reg)
      << L"] (zero cmp)" << std::endl;
#endif
    AddMachineCode(ROB(reg, reg));
    AddMachineCode(0x85);
    unsigned char code = 0xc0;
    RegisterEncode3(code, 2, reg);
    RegisterEncode3(code, 5, reg);
    AddMachineCode(code);
  }
  else if(imm >= INT8_MIN && imm <= INT8_MAX) {
    // CMP r64, imm8 (4 bytes vs 7)
#ifdef _DEBUG_JIT
    std::wcout << L"  " << (++instr_count) << L": [cmpq $" << imm << L", %"
      << GetRegisterName(reg) << L"] (imm8)" << std::endl;
#endif
    AddMachineCode(B(reg));
    AddMachineCode(0x83);
    unsigned char code = 0xf8;
    RegisterEncode3(code, 5, reg);
    AddMachineCode(code);
    AddMachineCode((unsigned char)(int8_t)imm);
  }
  else {
#ifdef _DEBUG_JIT
    std::wcout << L"  " << (++instr_count) << L": [cmpq $" << imm << L", %"
      << GetRegisterName(reg) << L"]" << std::endl;
#endif
    // encode
    AddMachineCode(XB(reg));
    AddMachineCode(0x81);
    unsigned char code = 0xf8;
    RegisterEncode3(code, 5, reg);
    AddMachineCode(code);
    // write value
    AddImm((long)imm);
  }
}

// TODO: 64-bit literal operation for Windows
void JitAmd64::cmp_imm_mem(long offset, Register src, int64_t imm) {
  if(imm < INT32_MIN || imm > INT32_MAX) {
    RegisterHolder* inm_holder = GetRegister();
    move_imm_reg(imm, inm_holder->GetRegister());
    
    RegisterHolder* holder = GetRegister();
    move_mem_reg(offset, src, holder->GetRegister());

    cmp_reg_reg(inm_holder->GetRegister(), holder->GetRegister());

    ReleaseRegister(inm_holder);
    ReleaseRegister(holder);
  }
  else {
#ifdef _DEBUG_JIT
    std::wcout << L"  " << (++instr_count) << L": [cmpq $" << imm << L", "
      << offset << L"(%" << GetRegisterName(src) << L")]" << std::endl;
#endif
    // encode
    AddMachineCode(XB(src));
    AddMachineCode(0x81);
    AddMachineCode(ModRM(src, RDI));
    // write value
    AddImm(offset);
    AddImm((long)imm);
  }
}

void JitAmd64::cmov_reg(Register reg, InstructionType oper) {
  // set register to 0; if eflag than set to 1
  move_imm_reg(0, reg);
  RegisterHolder* true_holder = GetRegister();
  move_imm_reg(1, true_holder->GetRegister());
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [cmovq %" 
        << GetRegisterName(reg) << L", %" 
        << GetRegisterName(true_holder->GetRegister()) << L" ]" << std::endl;
#endif
  // encode
  AddMachineCode(ROB(reg, true_holder->GetRegister()));
  AddMachineCode(0x0f);
  switch(oper) {    
  case GTR_INT:
    AddMachineCode(0x4f);
    break;

  case LES_INT:
    AddMachineCode(0x4c);
    break;
    
  case EQL_INT:
  case EQL_FLOAT:
    AddMachineCode(0x44);
    break;

  case NEQL_INT:
  case NEQL_FLOAT:
    AddMachineCode(0x45);
    break;
    
  case LES_FLOAT:
    AddMachineCode(0x47);
    break;
    
  case GTR_FLOAT:
    AddMachineCode(0x47);
    break;

  case LES_EQL_INT:
    AddMachineCode(0x4e);
    break;

  case GTR_EQL_INT:
    AddMachineCode(0x4d);
    break;
    
  case LES_EQL_FLOAT:
    AddMachineCode(0x43);
    break;

  case GTR_EQL_FLOAT:
    AddMachineCode(0x43);
    break;

  default:
    std::wcerr << L">>> Unknown compare! <<<" << std::endl;
    exit(1);
    break;
  }
  unsigned char code = 0xc0;
  
  // write value
  RegisterEncode3(code, 2, reg);
  RegisterEncode3(code, 5, true_holder->GetRegister());
  AddMachineCode(code);
  ReleaseRegister(true_holder);
}

// TODO: 64-bit literal operation for Windows
void JitAmd64::add_imm_mem(int64_t imm, long offset, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [addq $" << imm << L", " 
        << offset << L"(%"<< GetRegisterName(dest) << L")]" << std::endl;
#endif
  // encode
  AddMachineCode(XB(dest));
  AddMachineCode(0x81);
  AddMachineCode(ModRM(dest, RAX));
  // write value
  AddImm(offset);
  AddImm((long)imm); // TODO: load imm to reg, perform operation 
}
    
// TODO: 64-bit literal operation for Windows
void JitAmd64::add_imm_reg(int64_t imm, Register reg) {
  if(imm == 0) { return; }
  if(imm == 1) { inc_reg(reg); return; }
  if(imm == -1) { dec_reg(reg); return; }
  // x86-64 has no ADD with a 64-bit immediate. The 0x81 group takes an imm32
  // that is SIGN-EXTENDED to 64 bits, so a wider value silently loses its high
  // half: 0x7FFFFFFFFFFFFFFF became 0xFFFFFFFF, sign-extended back to -1, and
  // the operation turned into a no-op. Materialise it and use the register
  // form. move_imm_reg emits a mov, which leaves FLAGS alone, and the reg-reg
  // form sets them exactly as the immediate form would.
  if(imm < INT32_MIN || imm > INT32_MAX) {
    RegisterHolder* imm_holder = GetRegister();
    move_imm_reg(imm, imm_holder->GetRegister());
    add_reg_reg(imm_holder->GetRegister(), reg);
    ReleaseRegister(imm_holder);
    return;
  }
  if(imm >= INT8_MIN && imm <= INT8_MAX) {
#ifdef _DEBUG_JIT
    std::wcout << L"  " << (++instr_count) << L": [addq $" << imm << L", %"
          << GetRegisterName(reg) << L"] (imm8)" << std::endl;
#endif
    AddMachineCode(B(reg));
    AddMachineCode(0x83);
    unsigned char code = 0xc0;
    RegisterEncode3(code, 5, reg);
    AddMachineCode(code);
    AddMachineCode((unsigned char)(int8_t)imm);
    return;
  }
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [addq $" << imm << L", %"
        << GetRegisterName(reg) << L"]" << std::endl;
#endif
  AddMachineCode(B(reg));
  AddMachineCode(0x81);
  unsigned char code = 0xc0;
  RegisterEncode3(code, 5, reg);
  AddMachineCode(code);
  AddImm((long)imm);
}

void JitAmd64::add_imm_xreg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
#ifdef _WIN64
  move_imm_reg(instr->GetOperand2(), imm_holder->GetRegister());
#else
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
#endif
  add_mem_xreg(0, imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

void JitAmd64::sub_imm_xreg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
#ifdef _WIN64
  move_imm_reg(instr->GetOperand2(), imm_holder->GetRegister());
#else
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
#endif
  sub_mem_xreg(0, imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

void JitAmd64::div_imm_xreg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
#ifdef _WIN64
  move_imm_reg(instr->GetOperand2(), imm_holder->GetRegister());
#else
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
#endif
  div_mem_xreg(0, imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

void JitAmd64::mul_imm_xreg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
#ifdef _WIN64
  move_imm_reg(instr->GetOperand2(), imm_holder->GetRegister());
#else
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
#endif
  mul_mem_xreg(0, imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

void JitAmd64::add_reg_reg(Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [addq %" << GetRegisterName(src) 
        << L", %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(ROB(src, dest));
  AddMachineCode(0x01);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, src);
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
}

void JitAmd64::sub_xreg_xreg(Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [subsd %" << GetRegisterName(src) 
        << L", %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(ROB(src, dest));
  AddMachineCode(0x0f);
  AddMachineCode(0x5c);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, dest);
  RegisterEncode3(code, 5, src);
  AddMachineCode(code);       
}

void JitAmd64::mul_xreg_xreg(Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [mulsd %" << GetRegisterName(src) 
        << L", %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(ROB(dest, src));
  AddMachineCode(0x0f);
  AddMachineCode(0x59);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, dest);
  RegisterEncode3(code, 5, src);
  AddMachineCode(code);
}

void JitAmd64::div_xreg_xreg(Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [divsd %" << GetRegisterName(src) 
        << L", %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
  CheckDivideByZero(src);

  // encode
  AddMachineCode(0xf2);
  AddMachineCode(ROB(src, dest));
  AddMachineCode(0x0f);
  AddMachineCode(0x5e);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, dest);
  RegisterEncode3(code, 5, src);
  AddMachineCode(code);
}

void JitAmd64::sqrt_xreg_xreg(Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [sqrtsd %" << GetRegisterName(src)
    << L", %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(ROB(src, dest));
  AddMachineCode(0x0f);
  AddMachineCode(0x51);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, dest);
  RegisterEncode3(code, 5, src);
  AddMachineCode(code);
}

void JitAmd64::round_xreg_xreg(Register src, Register dest, wchar_t mode) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [roundsd(" << mode << L") % " << GetRegisterName(src)
    << L", %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(0x66);
  AddMachineCode(ROB(src, dest));
  AddMachineCode(0x0f);
  AddMachineCode(0x3a);
  AddMachineCode(0xb);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, dest);
  RegisterEncode3(code, 5, src);
  AddMachineCode(code);

  // ROUNDSD rounding-mode immediate: 0x0 = nearest, 0x1 = round down (floor),
  // 0x2 = round up (ceil), 0x3 = truncate. (Ceil and floor were swapped here,
  // so Float->Floor emitted ceil and vice versa — only observable once a method
  // using them was JIT-compiled.)
  if(mode == L'c') {
    AddMachineCode(0x2);
  }
  else if(mode == L'f') {
    AddMachineCode(0x1);
  }
  else {
    AddMachineCode(0x0);
  }
}


void JitAmd64::add_xreg_xreg(Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [addsd %" << GetRegisterName(src) 
        << L", %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(ROB(src, dest));
  AddMachineCode(0x0f);
  AddMachineCode(0x58);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, dest);
  RegisterEncode3(code, 5, src);
  AddMachineCode(code);
}
    
void JitAmd64::add_mem_reg(long offset, Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [addq " << offset << L"(%" 
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest) 
        << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(RXB(dest, src));
  AddMachineCode(0x03);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}

void JitAmd64::add_mem_xreg(long offset, Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [addsd " << offset << L"(%" 
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest) 
        << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(RXB(dest, src));
  AddMachineCode(0x0f);
  AddMachineCode(0x58);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);  
}

void JitAmd64::sub_mem_xreg(long offset, Register src, Register dest) {
  RegisterHolder* holder = GetXmmRegister();
  move_mem_xreg(offset, src, holder->GetRegister());
  sub_xreg_xreg(dest, holder->GetRegister());
  move_xreg_xreg(holder->GetRegister(), dest);
  ReleaseXmmRegister(holder);
}

void JitAmd64::mul_mem_xreg(long offset, Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [mulsd " << offset << L"(%" 
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest) 
        << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(RXB(dest, src));
  AddMachineCode(0x0f);
  AddMachineCode(0x59);
  AddMachineCode(ModRM(src, dest));
  AddImm(offset);
}

void JitAmd64::div_mem_xreg(long offset, Register src, Register dest) {
  RegisterHolder* holder = GetXmmRegister();
  move_mem_xreg(offset, src, holder->GetRegister());
  div_xreg_xreg(dest, holder->GetRegister());
  move_xreg_xreg(holder->GetRegister(), dest);
  ReleaseXmmRegister(holder);
}

// TODO: 64-bit literal operation for Windows
void JitAmd64::sub_imm_reg(int64_t imm, Register reg) {
  if(imm == 0) { return; }
  if(imm == 1) { dec_reg(reg); return; }
  if(imm == -1) { inc_reg(reg); return; }
  // x86-64 has no SUB with a 64-bit immediate. The 0x81 group takes an imm32
  // that is SIGN-EXTENDED to 64 bits, so a wider value silently loses its high
  // half: 0x7FFFFFFFFFFFFFFF became 0xFFFFFFFF, sign-extended back to -1, and
  // the operation turned into a no-op. Materialise it and use the register
  // form. move_imm_reg emits a mov, which leaves FLAGS alone, and the reg-reg
  // form sets them exactly as the immediate form would.
  if(imm < INT32_MIN || imm > INT32_MAX) {
    RegisterHolder* imm_holder = GetRegister();
    move_imm_reg(imm, imm_holder->GetRegister());
    sub_reg_reg(imm_holder->GetRegister(), reg);
    ReleaseRegister(imm_holder);
    return;
  }
  if(imm >= INT8_MIN && imm <= INT8_MAX) {
#ifdef _DEBUG_JIT
    std::wcout << L"  " << (++instr_count) << L": [subq $" << imm << L", %"
          << GetRegisterName(reg) << L"] (imm8)" << std::endl;
#endif
    AddMachineCode(B(reg));
    AddMachineCode(0x83);
    unsigned char code = 0xe8;
    RegisterEncode3(code, 5, reg);
    AddMachineCode(code);
    AddMachineCode((unsigned char)(int8_t)imm);
    return;
  }
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [subq $" << imm << L", %"
        << GetRegisterName(reg) << L"]" << std::endl;
#endif
  AddMachineCode(B(reg));
  AddMachineCode(0x81);
  unsigned char code = 0xe8;
  RegisterEncode3(code, 5, reg);
  AddMachineCode(code);
  AddImm((long)imm);
}

// TODO: 64-bit literal operation for Windows
void JitAmd64::sub_imm_mem(int64_t imm, long offset, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [subq $" << imm << L", " 
        << offset << L"(%"<< GetRegisterName(dest) << L")]" << std::endl;
#endif
  // encode
  AddMachineCode(XB(dest));
  AddMachineCode(0x81);
  AddMachineCode(ModRM(dest, RBP));
  // write value
  AddImm(offset);
  AddImm((long)imm); // TODO: load imm to reg, perform operation 
}

void JitAmd64::sub_reg_reg(Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [subq %" << GetRegisterName(src)
        << L", %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(ROB(src, dest));
  AddMachineCode(0x29);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, src);
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
}

void JitAmd64::sub_mem_reg(long offset, Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [subq " << offset << L"(%" 
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest) 
        << L"]" << std::endl;
#endif

  // encode
  AddMachineCode(RXB(dest, src));
  AddMachineCode(0x2b);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}

// TODO: 64-bit literal operation for Windows
// lea dest, [base + index * scale + disp]
void JitAmd64::lea_base_index_reg(long disp, Register base, Register index, int scale, Register dest) {
  unsigned char rex = 0x48;
  if(dest > RSP && dest < XMM0) {
    rex |= 0x04;   // REX.R
  }
  if(index > RSP && index < XMM0) {
    rex |= 0x02;   // REX.X
  }
  if(base > RSP && base < XMM0) {
    rex |= 0x01;   // REX.B
  }
  AddMachineCode(rex);
  AddMachineCode(0x8d);
  const bool disp8 = (disp >= -128 && disp <= 127);
  unsigned char modrm = disp8 ? 0x44 : 0x84;   // mod=01|10, rm=100 (SIB follows)
  RegisterEncode3(modrm, 2, dest);
  AddMachineCode(modrm);
  unsigned char sib;
  switch(scale) {
  case 1: sib = 0x00; break;
  case 2: sib = 0x40; break;
  case 4: sib = 0x80; break;
  default: sib = 0xc0; break;
  }
  RegisterEncode3(sib, 2, index);
  RegisterEncode3(sib, 5, base);
  AddMachineCode(sib);
  if(disp8) {
    AddMachineCode((unsigned char)(int8_t)disp);
  }
  else {
    AddImm((int32_t)disp);
  }
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [leaq " << disp << L"(%" << GetRegisterName(base) << L", %"
        << GetRegisterName(index) << L", " << scale << L"), %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
}

// RDX:RAX = RAX * qword [base + offset]   (F7 /5; the same shape as the idiv memory form)
void JitAmd64::move_base_index_reg(long disp, Register base, Register index, int scale, Register dest) {
  // mov dest, [base + index*scale + disp]: lea_base_index_reg's addressing with
  // opcode 8B
  unsigned char rex = 0x48;
  if(dest > RSP && dest < XMM0) {
    rex |= 0x04;   // REX.R
  }
  if(index > RSP && index < XMM0) {
    rex |= 0x02;   // REX.X
  }
  if(base > RSP && base < XMM0) {
    rex |= 0x01;   // REX.B
  }
  AddMachineCode(rex);
  AddMachineCode(0x8b);
  const bool disp8 = (disp >= -128 && disp <= 127);
  unsigned char modrm = disp8 ? 0x44 : 0x84;   // mod=01|10, rm=100 (SIB follows)
  RegisterEncode3(modrm, 2, dest);
  AddMachineCode(modrm);
  unsigned char sib;
  switch(scale) {
  case 1: sib = 0x00; break;
  case 2: sib = 0x40; break;
  case 4: sib = 0x80; break;
  default: sib = 0xc0; break;
  }
  RegisterEncode3(sib, 2, index);
  RegisterEncode3(sib, 5, base);
  AddMachineCode(sib);
  if(disp8) {
    AddMachineCode((unsigned char)(int8_t)disp);
  }
  else {
    AddImm((int32_t)disp);
  }
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [movq " << disp << L"(%" << GetRegisterName(base) << L", %"
        << GetRegisterName(index) << L", " << scale << L"), %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
}

void JitAmd64::move_base_index_xreg(long disp, Register base, Register index, int scale, Register dest) {
  // movsd dest, [base + index*scale + disp]
  AddMachineCode(0xf2);
  unsigned char rex = 0x40;
  if(dest > XMM7) {
    rex |= 0x04;   // REX.R
  }
  if(index > RSP && index < XMM0) {
    rex |= 0x02;   // REX.X
  }
  if(base > RSP && base < XMM0) {
    rex |= 0x01;   // REX.B
  }
  AddMachineCode(rex);
  AddMachineCode(0x0f);
  AddMachineCode(0x10);
  const bool disp8 = (disp >= -128 && disp <= 127);
  unsigned char modrm = disp8 ? 0x44 : 0x84;   // mod=01|10, rm=100 (SIB follows)
  RegisterEncode3(modrm, 2, dest);
  AddMachineCode(modrm);
  unsigned char sib;
  switch(scale) {
  case 1: sib = 0x00; break;
  case 2: sib = 0x40; break;
  case 4: sib = 0x80; break;
  default: sib = 0xc0; break;
  }
  RegisterEncode3(sib, 2, index);
  RegisterEncode3(sib, 5, base);
  AddMachineCode(sib);
  if(disp8) {
    AddMachineCode((unsigned char)(int8_t)disp);
  }
  else {
    AddImm((int32_t)disp);
  }
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [movsd " << disp << L"(%" << GetRegisterName(base) << L", %"
        << GetRegisterName(index) << L", " << scale << L"), %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
}

void JitAmd64::move_reg_base_index(Register src, long disp, Register base, Register index, int scale) {
  // mov [base + index*scale + disp], src: opcode 89 with a SIB byte
  unsigned char rex = 0x48;
  if(src > RSP && src < XMM0) {
    rex |= 0x04;   // REX.R
  }
  if(index > RSP && index < XMM0) {
    rex |= 0x02;   // REX.X
  }
  if(base > RSP && base < XMM0) {
    rex |= 0x01;   // REX.B
  }
  AddMachineCode(rex);
  AddMachineCode(0x89);
  const bool disp8 = (disp >= -128 && disp <= 127);
  unsigned char modrm = disp8 ? 0x44 : 0x84;   // mod=01|10, rm=100 (SIB follows)
  RegisterEncode3(modrm, 2, src);
  AddMachineCode(modrm);
  unsigned char sib;
  switch(scale) {
  case 1: sib = 0x00; break;
  case 2: sib = 0x40; break;
  case 4: sib = 0x80; break;
  default: sib = 0xc0; break;
  }
  RegisterEncode3(sib, 2, index);
  RegisterEncode3(sib, 5, base);
  AddMachineCode(sib);
  if(disp8) {
    AddMachineCode((unsigned char)(int8_t)disp);
  }
  else {
    AddImm((int32_t)disp);
  }
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [movq %" << GetRegisterName(src) << L", " << disp << L"(%"
        << GetRegisterName(base) << L", %" << GetRegisterName(index) << L", " << scale << L")]" << std::endl;
#endif
}

void JitAmd64::lea_mem_reg(long offset, Register src, Register dest) {
  // lea dest, [src + offset]: move_mem_reg's addressing with opcode 8D
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [leaq " << offset << L"(%"
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
  AddMachineCode(RXB(dest, src));
  AddMachineCode(0x8d);
  AddMachineCode(ModRM(src, dest));
  AddImm(offset);
}

void JitAmd64::inc_mem32(long offset, Register dest) {
  if(dest > RSP && dest < XMM0) {
    AddMachineCode(0x41);   // REX.B
  }
  AddMachineCode(0xff);
  unsigned char code = 0x80;
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
  AddImm(offset);
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [incl " << offset << L"(%"
        << GetRegisterName(dest) << L")" << L"]" << std::endl;
#endif
}

void JitAmd64::dec_mem32(long offset, Register dest) {
  if(dest > RSP && dest < XMM0) {
    AddMachineCode(0x41);   // REX.B
  }
  AddMachineCode(0xff);
  unsigned char code = 0x88;   // /1
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
  AddImm(offset);
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [decl " << offset << L"(%"
        << GetRegisterName(dest) << L")" << L"]" << std::endl;
#endif
}

void JitAmd64::PatchForwardJump(long patch_index) {
  const int32_t rel = (int32_t)(code_index - (patch_index + 4));
  memcpy(&code[(size_t)patch_index], &rel, sizeof(rel));
}

/**
 * The register-argument entry (docs/JIT_CALLING_CONVENTION_DESIGN.md,
 * section 11). A compiled method has two entries. The bridge entry, at
 * offset 0, is what JitRuntime::Execute calls: the eleven values of
 * jit_fun_ptr, the arguments on the operand stack, a frame record the
 * interpreter made. The native entry is what a compiled caller calls
 * (EmitNativeCallSite): self and the caller's frame pointer in the first
 * two argument registers, the arguments in the caller's outgoing area at
 * NATIVE_ARGS from this frame's pointer, and nothing else -- the method's
 * ids and class memory are its own constants, the operand stack and call
 * stack pointers are copied from the caller's frame, and the frame record
 * is built here in the frame and pushed on the call stack (RTRN pops it).
 * Each prologue sets `top` to the arguments and the two meet at
 * RegisterRoot; the body is emitted once.
 */
void JitAmd64::EmitBridgePrologue(long params, Register top, long& join_patch) {
#ifdef _DEBUG_JIT
  std::wcout << L"BRIDGE_ENTRY" << std::endl;
#endif
  Prolog();
  // method information
#ifdef _WIN64
  move_reg_mem(RCX, CLS_ID, RBP);
  move_reg_mem(RDX, MTHD_ID, RBP);
  move_reg_mem(R8, CLASS_MEM, RBP);
  move_reg_mem(R9, INSTANCE_MEM, RBP);
#else
  move_reg_mem(RDI, CLS_ID, RBP);
  move_reg_mem(RSI, MTHD_ID, RBP);
  move_reg_mem(RDX, CLASS_MEM, RBP);
  move_reg_mem(RCX, INSTANCE_MEM, RBP);
  move_reg_mem(R8, OP_STACK, RBP);
  move_reg_mem(R9, STACK_POS, RBP);
#endif
  move_imm_mem(0, rec_base + REC_KIND, RBP);
  if(params > 0) {
    // top = &op_stack[count], then the arguments come off the count; they
    // stay in place for ProcessParameters, and nothing between here and its
    // stores can park (the collector reads the stack up to the count)
    move_mem_reg(OP_STACK, RBP, R8);
    move_mem_reg(STACK_POS, RBP, R9);
#ifdef _WIN64
    move_mem_reg32(0, R9, R10);
#else
    move_mem_reg(0, R9, R10);
#endif
    lea_base_index_reg(0, R8, R10, sizeof(size_t), top);
    sub_imm_mem(params, 0, R9);
  }
  AddMachineCode(0xe9);          // jmp join
  join_patch = code_index;
  AddImm(0);
}

void JitAmd64::EmitNativePrologue(long params, Register top) {
  static const long FR_METHOD = (long)offsetof(StackFrame, method);
  static const long FR_MEM = (long)offsetof(StackFrame, mem);
  static const long FR_IP = (long)offsetof(StackFrame, ip);
  static const long FR_JIT_CALLED = (long)offsetof(StackFrame, jit_called);
  static const long FR_JIT_MEM = (long)offsetof(StackFrame, jit_mem);
  static const long FR_JIT_OFFSET = (long)offsetof(StackFrame, jit_offset);
  static const long FR_JIT_INST_MEM = (long)offsetof(StackFrame, jit_inst_mem);
  static_assert(sizeof(StackFrame) <= REC_MEM, "the frame record block holds a StackFrame before its mem words");
#ifdef _WIN64
  const Register self_reg = RCX;
  const Register ctx_reg = RDX;
#else
  const Register self_reg = RDI;
  const Register ctx_reg = RSI;
#endif
#ifdef _DEBUG_JIT
  std::wcout << L"NATIVE_ENTRY" << std::endl;
#endif
  native_entry_offset = code_index;
  Prolog();

  // the method's own constants, self, and the caller's stacks
  move_imm_mem(method->GetClass()->GetId(), CLS_ID, RBP);
  move_imm_mem(method->GetId(), MTHD_ID, RBP);
  move_imm_reg((int64_t)method->GetClass()->GetClassMemory(), R8);
  move_reg_mem(R8, CLASS_MEM, RBP);
  move_reg_mem(self_reg, INSTANCE_MEM, RBP);
  move_mem_reg(OP_STACK, ctx_reg, R8);
  move_reg_mem(R8, OP_STACK, RBP);
  move_mem_reg(STACK_POS, ctx_reg, R8);
  move_reg_mem(R8, STACK_POS, RBP);
  move_mem_reg(CALL_STACK, ctx_reg, R8);
  move_reg_mem(R8, CALL_STACK, RBP);
  move_mem_reg(CALL_STACK_POS, ctx_reg, R8);
  move_reg_mem(R8, CALL_STACK_POS, RBP);

  // the frame record: method, mem = &mem[0], ip = -1, jit_called = 0,
  // jit_mem = 0 (RegisterRoot sets it), jit_offset = 0, jit_inst_mem = 0;
  // mem = (self, 0); the entry kind RTRN tests
  move_imm_reg((int64_t)method, R8);
  move_reg_mem(R8, rec_base + FR_METHOD, RBP);
  lea_mem_reg(rec_base + REC_MEM, RBP, R8);
  move_reg_mem(R8, rec_base + FR_MEM, RBP);
  if(sizeof(long) == 4) {
    move_imm_mem32(-1, rec_base + FR_IP, RBP);
    move_imm_mem32(0, rec_base + FR_JIT_OFFSET, RBP);
  }
  else {
    move_imm_mem(-1, rec_base + FR_IP, RBP);
    move_imm_mem(0, rec_base + FR_JIT_OFFSET, RBP);
  }
  move_imm_mem8(0, rec_base + FR_JIT_CALLED, RBP);
  move_imm_mem(0, rec_base + FR_JIT_MEM, RBP);
  move_imm_mem(0, rec_base + FR_JIT_INST_MEM, RBP);
  move_reg_mem(self_reg, rec_base + REC_MEM, RBP);
  move_imm_mem(0, rec_base + REC_MEM + (long)sizeof(size_t), RBP);
  move_imm_mem(1, rec_base + REC_KIND, RBP);

  // push it: call_stack[pos] = &record; pos++ (the slot first, as PushFrame
  // orders them). The record is scanned only once this thread parks, which
  // it cannot do before RegisterRoot fills jit_mem.
  move_mem_reg(CALL_STACK, RBP, R8);
  move_mem_reg(CALL_STACK_POS, RBP, R9);
#ifdef _WIN64
  move_mem_reg32(0, R9, R10);
#else
  move_mem_reg(0, R9, R10);
#endif
  lea_mem_reg(rec_base, RBP, R11);
  move_reg_base_index(R11, 0, R8, R10, sizeof(size_t));
#ifdef _WIN64
  inc_mem32(0, R9);
#else
  inc_mem(0, R9);
#endif

  // where RegisterRoot and the callback's self reload look
  lea_mem_reg(rec_base + FR_JIT_MEM, RBP, R8);
  move_reg_mem(R8, JIT_MEM, RBP);
  lea_mem_reg(rec_base + FR_JIT_OFFSET, RBP, R8);
  move_reg_mem(R8, JIT_OFFSET, RBP);
  lea_mem_reg(rec_base + REC_MEM, RBP, R8);
  move_reg_mem(R8, FRAME_MEM, RBP);

  // the arguments end at NATIVE_ARGS + the parameter words
  if(params > 0) {
    lea_mem_reg(NATIVE_ARGS + params * (long)sizeof(size_t), RBP, top);
  }
}

/**
 * RTRN with a native entry: the method's value, the working stack's top,
 * into XMM0 -- Int bits or a Float -- ahead of the exit the entry kind
 * selects. Nothing for a method that returns Nil.
 */
void JitAmd64::EmitReturnValue() {
  const MemoryType rtrn = method->GetReturn();
  if((rtrn != INT_TYPE && rtrn != FLOAT_TYPE) || working_stack.empty()) {
    return;
  }
  RegInstr* left = working_stack.front();
  switch(left->GetType()) {
  case IMM_INT: {
    RegisterHolder* holder = GetRegister();
    move_imm_reg(left->GetOperand(), holder->GetRegister());
    move_reg_xreg(holder->GetRegister(), XMM0);
    ReleaseRegister(holder);
  }
    break;

  case MEM_INT:
  case MEM_FLOAT:
    move_mem_xreg((long)left->GetOperand(), RBP, XMM0);
    break;

  case REG_INT:
    move_reg_xreg(left->GetRegister()->GetRegister(), XMM0);
    break;

  case IMM_FLOAT:
    move_imm_xreg(left, XMM0);
    break;

  case REG_FLOAT:
    move_xreg_xreg(left->GetRegister()->GetRegister(), XMM0);
    break;
  }
}

/**
 * The call's values -- the arguments, the receiver and, for a func-ref
 * call, its packed word -- into this frame's outgoing area in operand-stack
 * order (the receiver above the arguments), and off the working stack. The
 * callee reads its arguments below NATIVE_ARGS plus its parameter words;
 * the slow path copies them all onto the operand stack.
 */
void JitAmd64::MarshalOutArgs(long params) {
  if(params < 1 || params > (long)working_stack.size()) {
    compile_success = false;
    return;
  }
  RegisterHolder* base_holder = GetRegister();
  const Register base = base_holder->GetRegister();
  move_reg_reg(RSP, base);

  const long non_params = (long)working_stack.size() - params;
  long i = 0;
  long disp = OUT_ARGS;
  for(std::deque<RegInstr*>::reverse_iterator iter = working_stack.rbegin(); iter != working_stack.rend(); ++iter) {
    RegInstr* left = (*iter);
    if(i < non_params) {
      i++;
      continue;
    }
    switch(left->GetType()) {
    case IMM_INT:
      move_imm_mem(left->GetOperand(), disp, base);
      break;

    case MEM_INT: {
      RegisterHolder* temp_holder = GetRegister();
      move_mem_reg((long)left->GetOperand(), RBP, temp_holder->GetRegister());
      move_reg_mem(temp_holder->GetRegister(), disp, base);
      ReleaseRegister(temp_holder);
    }
      break;

    case REG_INT:
      move_reg_mem(left->GetRegister()->GetRegister(), disp, base);
      break;

    case IMM_FLOAT:
      move_imm_memx(left, disp, base);
      break;

    case MEM_FLOAT: {
      RegisterHolder* temp_holder = GetXmmRegister();
      move_mem_xreg((long)left->GetOperand(), RBP, temp_holder->GetRegister());
      move_xreg_mem(temp_holder->GetRegister(), disp, base);
      ReleaseXmmRegister(temp_holder);
    }
      break;

    case REG_FLOAT:
      move_xreg_mem(left->GetRegister()->GetRegister(), disp, base);
      break;
    }
    disp += (long)sizeof(size_t);
  }
  ReleaseRegister(base_holder);

  // clean up working stack
  for(long j = 0; j < params; ++j) {
    RegInstr* left = working_stack.front();
    working_stack.pop_front();
    switch(left->GetType()) {
    case REG_INT:
      ReleaseRegister(left->GetRegister());
      break;

    case REG_FLOAT:
      ReleaseXmmRegister(left->GetRegister());
      break;

    default:
      break;
    }
    delete left;
    left = nullptr;
  }
}

/**
 * A native call site (see the header). R11 addresses the outgoing area: the
 * stack pointer's distance from RBP is not known until the XMM block is
 * patched, and RSP as a base needs a SIB byte the encoders do not emit.
 * Every live register is spilled to its TMP slot, as for a callback.
 */
void JitAmd64::EmitNativeCallSite(long instr_id, StackInstr* instr, long instr_index, long params) {
  static const long RECORD_KEY = (long)offsetof(JitVirtualRecord, key);
  static const long RECORD_ENTRY = (long)offsetof(JitVirtualRecord, entry);
  static const long RECORD_TARGET = (long)offsetof(JitVirtualRecord, target);
  const bool funcref = virtual_site && virtual_site->funcref;
  const long callee_params = params - (funcref ? 2 : 1);
  const long SELF = OUT_ARGS + callee_params * (long)sizeof(size_t);
  const long KEY = SELF + (long)sizeof(size_t);
#ifdef _DEBUG_JIT
  if(direct_callee) {
    std::wcout << L"NATIVE_CALL: name='" << direct_callee->GetName() << L"'" << std::endl;
  }
  else if(funcref) {
    std::wcout << L"FUNCREF_CALL" << std::endl;
  }
  else {
    std::wcout << L"VIRTUAL_CALL: name='" << virtual_site->declaration->GetName() << L"'" << std::endl;
  }
#endif
  MarshalOutArgs(params);

  std::vector<long> slow_patches;
  long error_patch = -1;
  long done_patch = -1;
  move_reg_reg(RSP, R11);
  if(direct_callee) {
    // rax = callee->native_entry, or slow
    move_imm_reg((int64_t)direct_callee->NativeEntryAddress(), RAX);
    move_mem_reg(0, RAX, RAX);
    cmp_imm_reg(0, RAX);
    AddMachineCode(0x0f);
    AddMachineCode(0x84);          // je slow
    slow_patches.push_back(code_index);
    AddImm(0);
    EmitNativeCall(SELF, slow_patches, error_patch, done_patch);
  }
  else {
    // the key: the receiver's class word, or the func-ref word. A Nil
    // receiver or a non-object goes to the bridge, which reports it.
    move_mem_reg(funcref ? KEY : SELF, R11, R9);
    if(funcref) {
      move_reg_reg(R9, RCX);
    }
    else {
      cmp_imm_reg(0, R9);
      AddMachineCode(0x0f);
      AddMachineCode(0x84);          // je slow: Nil receiver
      slow_patches.push_back(code_index);
      AddImm(0);
      cmp_imm_mem(TYPE * (long)sizeof(size_t), R9, instructions::NIL_TYPE);
      AddMachineCode(0x0f);
      AddMachineCode(0x85);          // jne slow: not an object instance
      slow_patches.push_back(code_index);
      AddImm(0);
      move_mem_reg(SIZE_OR_CLS * (long)sizeof(size_t), R9, RCX);   // the receiver's class
    }
    move_imm_reg((int64_t)virtual_site, RBX);
    move_mem_reg(0, RBX, RBX);     // the site's current record
    cmp_imm_reg(0, RBX);
    AddMachineCode(0x0f);
    AddMachineCode(0x84);          // je miss
    const long miss_patch_a = code_index;
    AddImm(0);
    cmp_mem_reg(RECORD_KEY, RBX, RCX);
    AddMachineCode(0x0f);
    AddMachineCode(0x85);          // jne miss
    const long miss_patch_b = code_index;
    AddImm(0);
    // hit: rax = record->entry; the record stays in RBX (callee-saved) for
    // the error report
    const long hit_index = code_index;
    move_mem_reg(RECORD_ENTRY, RBX, RAX);
    EmitNativeCall(SELF, slow_patches, error_patch, done_patch);

    // miss: a record from the resolver, or the bridge
    PatchForwardJump(miss_patch_a);
    PatchForwardJump(miss_patch_b);
    const int64_t resolver = funcref ? (int64_t)(size_t)JitCompiler::JitResolveFuncRefSite
                                     : (int64_t)(size_t)JitCompiler::JitResolveVirtualSite;
#ifdef _WIN64
    move_imm_reg((int64_t)virtual_site, RCX);
    move_reg_reg(R9, RDX);
    sub_imm_reg(32, RSP);
    move_imm_reg(resolver, RAX);
    call_reg(RAX);
    add_imm_reg(32, RSP);
#else
    move_imm_reg((int64_t)virtual_site, RDI);
    move_reg_reg(R9, RSI);
    move_imm_reg(resolver, RAX);
    call_reg(RAX);
#endif
    cmp_imm_reg(0, RAX);
    AddMachineCode(0x0f);
    AddMachineCode(0x84);          // je slow: no record
    slow_patches.push_back(code_index);
    AddImm(0);
    move_reg_reg(RAX, RBX);
    AddMachineCode(0xe9);          // jmp hit
    const int32_t back = (int32_t)(hit_index - (code_index + 4));
    AddImm(back);
  }

  // error: JitNativeCallError(status, callee, caller cls_id, caller mthd_id)
  PatchForwardJump(error_patch);
#ifdef _WIN64
  move_reg_reg(RAX, RCX);
  if(direct_callee) {
    move_imm_reg((int64_t)direct_callee, RDX);
  }
  else {
    move_mem_reg(RECORD_TARGET, RBX, RDX);
  }
  move_mem_reg(CLS_ID, RBP, R8);
  move_mem_reg(MTHD_ID, RBP, R9);
  sub_imm_reg(32, RSP);
#else
  move_reg_reg(RAX, RDI);
  if(direct_callee) {
    move_imm_reg((int64_t)direct_callee, RSI);
  }
  else {
    move_mem_reg(RECORD_TARGET, RBX, RSI);
  }
  move_mem_reg(CLS_ID, RBP, RDX);
  move_mem_reg(MTHD_ID, RBP, RCX);
#endif
  move_imm_reg((int64_t)(size_t)JitCompiler::JitNativeCallError, RAX);
  call_reg(RAX);                 // does not return

  // slow: the values onto the operand stack, in order, then the bridge
  for(const long patch : slow_patches) {
    PatchForwardJump(patch);
  }
  move_reg_reg(RSP, R11);
  move_mem_reg(OP_STACK, RBP, RAX);
  move_mem_reg(STACK_POS, RBP, RCX);
#ifdef _WIN64
  move_mem_reg32(0, RCX, RDX);
#else
  move_mem_reg(0, RCX, RDX);
#endif
  lea_base_index_reg(0, RAX, RDX, sizeof(size_t), RAX);
  for(long i = 0; i < params; ++i) {
    move_mem_reg(OUT_ARGS + i * (long)sizeof(size_t), R11, RDX);
    move_reg_mem(RDX, i * (long)sizeof(size_t), RAX);
  }
  add_imm_mem(params, 0, RCX);
  EmitBridgeCall(instr_id, instr, instr_index);
  // the result from the operand stack into XMM0, where the native path leaves it
  if(call_return_type == INT_TYPE || call_return_type == FLOAT_TYPE) {
    move_mem_reg(STACK_POS, RBP, RCX);
    dec_mem(0, RCX);
#ifdef _WIN64
    move_mem_reg32(0, RCX, RDX);
#else
    move_mem_reg(0, RCX, RDX);
#endif
    move_mem_reg(OP_STACK, RBP, RAX);
    move_base_index_xreg(0, RAX, RDX, sizeof(size_t), XMM0);
  }
  PatchForwardJump(done_patch);
}

/**
 * With RAX holding the entry: a full call stack goes to the bridge, which
 * reports it; self and this frame's pointer go into the argument registers;
 * the call; a negative status goes to the error block. RBX survives the
 * call (the callee saves it).
 */
void JitAmd64::EmitNativeCall(long self_offset, std::vector<long>& slow_patches, long& error_patch, long& done_patch) {
  move_mem_reg(CALL_STACK_POS, RBP, RCX);
#ifdef _WIN64
  move_mem_reg32(0, RCX, RDX);
#else
  move_mem_reg(0, RCX, RDX);
#endif
  cmp_imm_reg(CALL_STACK_SIZE, RDX);
  AddMachineCode(0x0f);
  AddMachineCode(0x8d);          // jge slow
  slow_patches.push_back(code_index);
  AddImm(0);
  // R11 again: the resolver may have run on the way here
  move_reg_reg(RSP, R11);
#ifdef _WIN64
  move_mem_reg(self_offset, R11, RCX);
  move_reg_reg(RBP, RDX);
#else
  move_mem_reg(self_offset, R11, RDI);
  move_reg_reg(RBP, RSI);
#endif
  call_reg(RAX);
  // status < 0: report and exit
  cmp_imm_reg(0, RAX);
  AddMachineCode(0x0f);
  AddMachineCode(0x8c);          // jl error
  error_patch = code_index;
  AddImm(0);
  AddMachineCode(0xe9);          // jmp done
  done_patch = code_index;
  AddImm(0);
}

void JitAmd64::imul_mem(long offset, Register base) {
  AddMachineCode(XB(base));
  AddMachineCode(0xf7);
  AddMachineCode(ModRM(base, RBP));   // /5 in the reg field
  AddImm(offset);
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [imulq " << offset << L"(%" << GetRegisterName(base) << L")]" << std::endl;
#endif
}

// n / d or n % d for a constant d (|d| >= 2, not a power of two) without idiv:
// see MagicSigned64. RAX and RDX are imul's implicit operands and are saved
// exactly as div_reg_reg saves them -- only when something lives in them.
void JitAmd64::EmitMagicDivision(int64_t d, Register dest, bool is_mod) {
  int64_t magic;
  int shift;
  MagicSigned64(d, magic, shift);

  const bool save_rax = (dest != RAX && !IsRegisterFree(RAX));
  const bool save_rdx = (dest != RDX && !IsRegisterFree(RDX));
  if(save_rdx) {
    move_reg_mem(RDX, TMP_REG_1, RBP);
  }
  if(save_rax) {
    move_reg_mem(RAX, TMP_REG_0, RBP);
  }
  // n is needed after the multiply; keep it in a slot when dest is a register the multiply clobbers
  const bool n_in_slot = (dest == RAX || dest == RDX);
  if(n_in_slot) {
    move_reg_mem(dest, TMP_REG_3, RBP);
  }
  // n goes to RAX first: when dest is RDX, loading the magic constant into
  // RDX before this copy multiplied magic by itself (the fixture's
  // JoinAndFloats probe: `t := y->ToInt(); return t % 1000` after a loop)
  if(dest != RAX) {
    move_reg_reg(dest, RAX);
  }
  // the magic constant goes through a slot so no register is taken from the pool
  move_imm_reg(magic, RDX);
  move_reg_mem(RDX, TMP_REG_2, RBP);
  imul_mem(TMP_REG_2, RBP);                       // RDX:RAX = n * magic
  if(d > 0 && magic < 0) {
    if(n_in_slot) { add_mem_reg(TMP_REG_3, RBP, RDX); } else { add_reg_reg(dest, RDX); }
  }
  else if(d < 0 && magic > 0) {
    if(n_in_slot) { sub_mem_reg(TMP_REG_3, RBP, RDX); } else { sub_reg_reg(dest, RDX); }
  }
  if(shift > 0) {
    sar_imm_reg(shift, RDX);
  }
  // q += (q >>> 63); the low product in RAX is dead, so RAX is scratch from here
  move_reg_reg(RDX, RAX);
  shr_imm_reg(63, RAX);
  add_reg_reg(RAX, RDX);                          // RDX = quotient
  if(is_mod) {
    // r = n - q * d
    move_imm_reg(d, RAX);
    mul_reg_reg(RAX, RDX);                        // RDX = q * d
    if(n_in_slot) {
      move_mem_reg(TMP_REG_3, RBP, RAX);
      sub_reg_reg(RDX, RAX);                      // RAX = n - q * d
      if(dest != RAX) {
        move_reg_reg(RAX, dest);
      }
    }
    else {
      sub_reg_reg(RDX, dest);                     // dest = n - q * d
    }
  }
  else if(dest != RDX) {
    move_reg_reg(RDX, dest);
  }
  if(save_rax && dest != RAX) {
    move_mem_reg(TMP_REG_0, RBP, RAX);
  }
  if(save_rdx && dest != RDX) {
    move_mem_reg(TMP_REG_1, RBP, RDX);
  }
}

void JitAmd64::mul_imm_reg(int64_t imm, Register reg) {
  if(imm == 0) { move_imm_reg(0, reg); return; }
  if(imm == 1) { return; }
  // IMUL r64, r/m64, imm32 sign-extends a 32-bit immediate; there is no
  // 64-bit form. add_imm_reg and sub_imm_reg have had this guard since the
  // 0x7FFFFFFFFFFFFFFF no-op was found; this one was missed, so
  // n * 4294967311 multiplied by 15 instead. Materialise and use the
  // register form, exactly as they do.
  if(imm < INT32_MIN || imm > INT32_MAX) {
    RegisterHolder* imm_holder = GetRegister();
    move_imm_reg(imm, imm_holder->GetRegister());
    mul_reg_reg(imm_holder->GetRegister(), reg);
    ReleaseRegister(imm_holder);
    return;
  }
  if(imm == -1) {
    // NEG r64: REX.W + F7 /3
    AddMachineCode(B(reg));
    AddMachineCode(0xf7);
    unsigned char code = 0xd8;
    RegisterEncode3(code, 5, reg);
    AddMachineCode(code);
    return;
  }
  // power of 2: use SHL
  if(imm > 0 && (imm & (imm - 1)) == 0) {
    int shift = 0;
    int64_t tmp = imm;
    while(tmp > 1) { tmp >>= 1; shift++; }
    shl_imm_reg(shift, reg);
    return;
  }
  // multiply by 3: LEA reg, [reg + reg*2]
  if(imm == 3) {
    bool ext = (reg > RSP && reg < XMM0) || reg > XMM7;
    AddMachineCode(ext ? 0x4f : 0x48);
    AddMachineCode(0x8d);
    unsigned char modrm = 0x04;
    RegisterEncode3(modrm, 2, reg);
    AddMachineCode(modrm);
    unsigned char sib = 0x40;
    RegisterEncode3(sib, 2, reg);
    RegisterEncode3(sib, 5, reg);
    AddMachineCode(sib);
    return;
  }
  // multiply by 5: LEA reg, [reg + reg*4]
  if(imm == 5) {
    bool ext = (reg > RSP && reg < XMM0) || reg > XMM7;
    AddMachineCode(ext ? 0x4f : 0x48);
    AddMachineCode(0x8d);
    unsigned char modrm = 0x04;
    RegisterEncode3(modrm, 2, reg);
    AddMachineCode(modrm);
    unsigned char sib = 0x80;
    RegisterEncode3(sib, 2, reg);
    RegisterEncode3(sib, 5, reg);
    AddMachineCode(sib);
    return;
  }
  // multiply by 9: LEA reg, [reg + reg*8]
  if(imm == 9) {
    bool ext = (reg > RSP && reg < XMM0) || reg > XMM7;
    AddMachineCode(ext ? 0x4f : 0x48);
    AddMachineCode(0x8d);
    unsigned char modrm = 0x04;
    RegisterEncode3(modrm, 2, reg);
    AddMachineCode(modrm);
    unsigned char sib = 0xc0;
    RegisterEncode3(sib, 2, reg);
    RegisterEncode3(sib, 5, reg);
    AddMachineCode(sib);
    return;
  }
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [imuq $" << imm
        << L", %"<< GetRegisterName(reg) << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(ROB(reg, reg));
  AddMachineCode(0x69);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, reg);
  RegisterEncode3(code, 5, reg);
  AddMachineCode(code);
  // write value
  AddImm((long)imm); // TODO: load imm to reg, perform operation
}

void JitAmd64::mul_reg_reg(Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [imuq %" << GetRegisterName(src) << L", %"<< GetRegisterName(dest) << L"]" << std::endl;
#endif
  // encode. IMUL r64, r/m64 (0F AF /r) writes the product to the REG field,
  // the opposite of the ADD/SUB/AND family (01 /r etc.), whose r/m field is
  // the destination. This used to encode reg=src, r/m=dest -- the ADD
  // layout -- so the product landed in src, and its one caller compensated
  // by passing the arguments the wrong way round. Every other reg_reg
  // primitive here means "dest = dest OP src"; this one now does too.
  AddMachineCode(ROB(dest, src));
  AddMachineCode(0x0f);
  AddMachineCode(0xaf);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, dest);
  RegisterEncode3(code, 5, src);
  AddMachineCode(code);
}

void JitAmd64::mul_mem_reg(long offset, Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [imuq " << offset << L"(%" 
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest) 
        << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(RXB(dest, src));
  AddMachineCode(0x0f);
  AddMachineCode(0xaf);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}

void JitAmd64::div_imm_reg(int64_t imm, Register reg, bool is_mod) {
  if(imm == 1) {
    if(is_mod) {
      move_imm_reg(0, reg);
    }
    return;
  }

  // strength reduction: power-of-2 division/modulo
  if(imm > 1 && (imm & (imm - 1)) == 0) {
    int shift = 0;
    int64_t tmp = imm;
    while(tmp > 1) { tmp >>= 1; shift++; }

    if(is_mod) {
      // x % (2^n) for signed: result = x - (x / (2^n)) * (2^n)
      // equivalent to: sign-correct then AND
      // 1) copy reg to temp for sign correction
      // 2) sar temp, 63 (all sign bits)
      // 3) shr temp, (64 - shift) (isolate correction bits)
      // 4) add reg, temp
      // 5) and reg, (imm - 1)
      // 6) sub reg, temp
      RegisterHolder* tmp_holder = GetRegister();
      Register tmp_reg = tmp_holder->GetRegister();
      move_reg_reg(reg, tmp_reg);
      sar_imm_reg(63, tmp_reg);
      shr_imm_reg(64 - shift, tmp_reg);
      add_reg_reg(tmp_reg, reg);
      and_imm_reg(imm - 1, reg);
      sub_reg_reg(tmp_reg, reg);
      ReleaseRegister(tmp_holder);
    }
    else {
      // x / (2^n) for signed: bias negative values to round toward zero
      // 1) copy reg to temp
      // 2) sar temp, 63 (broadcast sign bit)
      // 3) shr temp, (64 - shift) (correction = (2^n - 1) if negative, 0 if positive)
      // 4) add reg, temp
      // 5) sar reg, shift
      RegisterHolder* tmp_holder = GetRegister();
      Register tmp_reg = tmp_holder->GetRegister();
      move_reg_reg(reg, tmp_reg);
      sar_imm_reg(63, tmp_reg);
      shr_imm_reg(64 - shift, tmp_reg);
      add_reg_reg(tmp_reg, reg);
      sar_imm_reg(shift, reg);
      ReleaseRegister(tmp_holder);
    }
    return;
  }

  // Every other non-zero constant: multiply by its magic number instead of
  // idiv (10-20 cycles of latency against 3). The two constant divisions in
  // the assessment's integer loop were 85% of its time. -1 and 0 keep the
  // idiv path: -1 so INT64_MIN / -1 behaves exactly as the interpreter's
  // C++ division does, 0 so the runtime check fires.
  if(imm != 0 && imm != -1) {
    EmitMagicDivision(imm, reg, is_mod);
    return;
  }
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(imm, imm_holder->GetRegister());
  div_reg_reg(imm_holder->GetRegister(), reg, is_mod, imm != 0);
  ReleaseRegister(imm_holder);
}

void JitAmd64::div_mem_reg(long offset, Register src, Register dest, bool is_mod) {
  CheckDivideByZero(offset, src);
  
  if(is_mod) {
    if(dest != RDX) {
      move_reg_mem(RDX, TMP_REG_1, RBP);
    }
    move_reg_mem(RAX, TMP_REG_0, RBP);
  }
  else {
    if(dest != RAX) {
      move_reg_mem(RAX, TMP_REG_0, RBP);
    }
    move_reg_mem(RDX, TMP_REG_1, RBP);
  }

  // ============
  move_reg_reg(dest, RAX);
  AddMachineCode(0x48); // cdq
  AddMachineCode(0x99);
  
  // encode
  AddMachineCode(XB(src));
  AddMachineCode(0xf7);
  AddMachineCode(ModRM(src, RDI));
  // write value
  AddImm(offset);
  
#ifdef _DEBUG_JIT
  if(is_mod) {
    std::wcout << L"  " << (++instr_count) << L": [imod " << offset << L"(%" 
          << GetRegisterName(src) << L")]" << std::endl;
  }
  else {
    std::wcout << L"  " << (++instr_count) << L": [idiv " << offset << L"(%" 
          << GetRegisterName(src) << L")]" << std::endl;
  }
#endif
  // ============

  if(is_mod) {
    if(dest != RDX) {
      move_reg_reg(RDX, dest);
      move_mem_reg(TMP_REG_1, RBP, RDX);
    }

    if(dest != RAX) {
      move_mem_reg(TMP_REG_0, RBP, RAX);
    }
  }
  else {
    if(dest != RAX) {
      move_reg_reg(RAX, dest);
      move_mem_reg(TMP_REG_0, RBP, RAX);
    }
    
    if(dest != RDX) {
      move_mem_reg(TMP_REG_1, RBP, RDX);
    }
  }
}

void JitAmd64::div_reg_reg(Register src, Register dest, bool is_mod, bool src_nonzero) {
  // A divisor the compiler already knows is non-zero (an immediate) needs no
  // runtime check; every constant `/` and `%` used to pay a test+branch.
  if(!src_nonzero) {
    CheckDivideByZero(src);
  }

  // idiv clobbers RAX and RDX. They were saved to the spill slots and restored
  // unconditionally -- four memory operations per division whether or not
  // either held anything. Save a register only if it is allocated (a value on
  // the working stack, a cached local) or if it IS the divisor, which the
  // memory-operand form below reads back from its slot.
  const bool save_rax = (src == RAX) || (dest != RAX && !IsRegisterFree(RAX));
  const bool save_rdx = (src == RDX) || (dest != RDX && !IsRegisterFree(RDX));
  if(save_rdx) {
    move_reg_mem(RDX, TMP_REG_1, RBP);
  }
  if(save_rax) {
    move_reg_mem(RAX, TMP_REG_0, RBP);
  }
  // ============
  move_reg_reg(dest, RAX);
  AddMachineCode(0x48); // cdq
  AddMachineCode(0x99);
  
  if(src != RAX && src != RDX) {
    // encode
    AddMachineCode(B(src));
    AddMachineCode(0xf7);
    unsigned char code = 0xf8;
    // write value
    RegisterEncode3(code, 5, src);
    AddMachineCode(code);
    
#ifdef _DEBUG_JIT
    if(is_mod) {
      std::wcout << L"  " << (++instr_count) << L": [imod %" 
            << GetRegisterName(src) << L"]" << std::endl;
    }
    else {
      std::wcout << L"  " << (++instr_count) << L": [idiv %" 
            << GetRegisterName(src) << L"]" << std::endl;
    }
#endif
  }
  else {
    // encode
    AddMachineCode(XB(RBP));
    AddMachineCode(0xf7);
    AddMachineCode(ModRM(RBP, RDI));
    // write value
    if(src == RAX) {
      AddImm(TMP_REG_0);
    }
    else {
      AddImm(TMP_REG_1);
    }
    
#ifdef _DEBUG_JIT
    if(is_mod) {
      std::wcout << L"  " << (++instr_count) << L": [imod " << TMP_REG_0 << L"(%" 
            << GetRegisterName(RBP) << L")]" << std::endl;
    }
    else {
      std::wcout << L"  " << (++instr_count) << L": [idiv " << TMP_REG_0 << L"(%" 
            << GetRegisterName(RBP) << L")]" << std::endl;
    }
#endif
  }
  // ============
  if(is_mod) {
    if(dest != RDX) {
      move_reg_reg(RDX, dest);
    }
  }
  else {
    if(dest != RAX) {
      move_reg_reg(RAX, dest);
    }
  }
  if(save_rax && dest != RAX) {
    move_mem_reg(TMP_REG_0, RBP, RAX);
  }
  if(save_rdx && dest != RDX) {
    move_mem_reg(TMP_REG_1, RBP, RDX);
  }
}

void JitAmd64::inc_reg(Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [incq %"
        << GetRegisterName(dest) << L"]" << std::endl;
#endif
  AddMachineCode(B(dest));
  AddMachineCode(0xff);
  unsigned char code = 0xc0;
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
}

void JitAmd64::dec_reg(Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [decq %"
        << GetRegisterName(dest) << L"]" << std::endl;
#endif
  AddMachineCode(B(dest));
  AddMachineCode(0xff);
  unsigned char code = 0xc8;
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
}

void JitAmd64::dec_mem(long offset, Register dest) {
  AddMachineCode(XB(dest));
  AddMachineCode(0xff);
  unsigned char code = 0x88;
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
  AddImm(offset);
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [decq " << offset << L"(%" 
        << GetRegisterName(dest) << L")" << L"]" << std::endl;
#endif
}

void JitAmd64::inc_mem(long offset, Register dest) {
  AddMachineCode(XB(dest));
  AddMachineCode(0xff);
  unsigned char code = 0x80;
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
  AddImm(offset);
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [incq " << offset << L"(%" 
        << GetRegisterName(dest) << L")" << L"]" << std::endl;
#endif
}

void JitAmd64::shl_imm_reg(int64_t value, Register dest) {
  if(value == 1) {
    // SHL r64, 1: REX.W + D1 /4 (3 bytes vs 4)
    AddMachineCode(B(dest));
    AddMachineCode(0xd1);
    unsigned char code = 0xe0;
    RegisterEncode3(code, 5, dest);
    AddMachineCode(code);
#ifdef _DEBUG_JIT
    std::wcout << L"  " << (++instr_count) << L": [shlq $1, %"
          << GetRegisterName(dest) << L"]" << std::endl;
#endif
    return;
  }
  AddMachineCode(B(dest));
  AddMachineCode(0xc1);
  unsigned char code = 0xe0;
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
  AddMachineCode((unsigned char)value);
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [shlq $" << value << L", %"
        << GetRegisterName(dest) << L"]" << std::endl;
#endif
}

void JitAmd64::shl_reg_reg(Register src, Register dest)
{
  Register old_dest;
  RegisterHolder* reg_holder = nullptr;
  if(dest == RCX) {
    reg_holder = GetRegister();
    old_dest = dest;
    dest = reg_holder->GetRegister();
    move_reg_reg(old_dest, dest);
  }
  
  if(src != RCX) {
    move_reg_mem(RCX, TMP_REG_0, RBP);
    move_reg_reg(src, RCX);
  }
  
  // encode
  AddMachineCode(B(dest));
  AddMachineCode(0xd3);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, RSP);
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [shlq %" << GetRegisterName(src) 
        << L", %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
  
  if(src != RCX) {
    move_mem_reg(TMP_REG_0, RBP, RCX);
  }
  
  if(reg_holder) {
    move_reg_reg(dest, old_dest);
    ReleaseRegister(reg_holder);
  }
}

void JitAmd64::shl_mem_reg(long offset, Register src, Register dest) 
{
  RegisterHolder* mem_holder = GetRegister();
  move_mem_reg(offset, src, mem_holder->GetRegister());
  shl_reg_reg(mem_holder->GetRegister(), dest);
  ReleaseRegister(mem_holder);
}

void JitAmd64::shr_imm_reg(int64_t value, Register dest) {
  AddMachineCode(B(dest));
  AddMachineCode(0xc1);
  unsigned char code = 0xe8;
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
  AddMachineCode((unsigned char)value);
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [shrq $" << value << L", %" 
        << GetRegisterName(dest) << L"]" << std::endl;
#endif
}

void JitAmd64::sar_imm_reg(int64_t value, Register dest) {
  AddMachineCode(B(dest));
  AddMachineCode(0xc1);
  unsigned char code = 0xf8;                  // /7 (SAR) in the reg field
  RegisterEncode3(code, 5, dest);             // dest in the rm field
  AddMachineCode(code);
  AddMachineCode((unsigned char)value);
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [sarq $" << value << L", %"
        << GetRegisterName(dest) << L"]" << std::endl;
#endif
}

void JitAmd64::shr_reg_reg(Register src, Register dest)
{
  Register old_dest;
  RegisterHolder* reg_holder = nullptr;
  if(dest == RCX) {
    reg_holder = GetRegister();
    old_dest = dest;
    dest = reg_holder->GetRegister();
    move_reg_reg(old_dest, dest);
  }
  
  if(src != RCX) {
    move_reg_mem(RCX, TMP_REG_0, RBP);
    move_reg_reg(src, RCX);
  }
    
  // encode
  AddMachineCode(B(dest));
  AddMachineCode(0xd3);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, RBP);
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [shrq %" << GetRegisterName(RCX) 
        << L", %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
  
  if(src != RCX) {
    move_mem_reg(TMP_REG_0, RBP, RCX);
  }
  
  if(reg_holder) {
    move_reg_reg(dest, old_dest);
    ReleaseRegister(reg_holder);
  }
}

void JitAmd64::shr_mem_reg(long offset, Register src, Register dest) 
{
  RegisterHolder* mem_holder = GetRegister();
  move_mem_reg(offset, src, mem_holder->GetRegister());
  shr_reg_reg(mem_holder->GetRegister(), dest);
  ReleaseRegister(mem_holder);
}

// Arithmetic right shift -- what Objeck's `>>` means: the interpreter shifts a
// signed INT64 and the ARM64 backend emits asr. This backend emitted the LOGICAL
// shr for SHR_INT, so every negative operand of `>>` in JIT'd x64 code came out
// as a huge positive number. The shr_* encoders stay: the power-of-two division
// trick in div_imm_reg needs a logical shift of the sign mask.
void JitAmd64::sar_reg_reg(Register src, Register dest)
{
  Register old_dest;
  RegisterHolder* reg_holder = nullptr;
  if(dest == RCX) {
    reg_holder = GetRegister();
    old_dest = dest;
    dest = reg_holder->GetRegister();
    move_reg_reg(old_dest, dest);
  }
  
  if(src != RCX) {
    move_reg_mem(RCX, TMP_REG_0, RBP);
    move_reg_reg(src, RCX);
  }
    
  // encode
  AddMachineCode(B(dest));
  AddMachineCode(0xd3);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, RDI);   // /7: SAR, not /5 SHR
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [sarq %" << GetRegisterName(RCX) 
        << L", %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
  
  if(src != RCX) {
    move_mem_reg(TMP_REG_0, RBP, RCX);
  }
  
  if(reg_holder) {
    move_reg_reg(dest, old_dest);
    ReleaseRegister(reg_holder);
  }
}

void JitAmd64::sar_mem_reg(long offset, Register src, Register dest) 
{
  RegisterHolder* mem_holder = GetRegister();
  move_mem_reg(offset, src, mem_holder->GetRegister());
  sar_reg_reg(mem_holder->GetRegister(), dest);
  ReleaseRegister(mem_holder);
}

void JitAmd64::push_mem(long offset, Register dest) {
  AddMachineCode(B(dest));
  AddMachineCode(0xff);
  unsigned char code = 0xb0;
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
  AddImm(offset);
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [pushq " << offset << L"(%" 
        << GetRegisterName(dest) << L")" << L"]" << std::endl;
#endif
}

void JitAmd64::push_reg(Register reg) {
  AddMachineCode(B(reg));
  unsigned char code = 0x50;
  RegisterEncode3(code, 5, reg);
  AddMachineCode(code);
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [pushq %" << GetRegisterName(reg) 
        << L"]" << std::endl;
#endif
}

void JitAmd64::push_imm(long value) {
  AddMachineCode(0x68);
  AddImm(value);
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [pushq $" << value << L"]" << std::endl;
#endif
}

void JitAmd64::pop_reg(Register reg) {
  AddMachineCode(B(reg));  
  unsigned char code = 0x58;
  RegisterEncode3(code, 5, reg);
  AddMachineCode(code);
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [popq %" << GetRegisterName(reg) 
        << L"]" << std::endl;
#endif
}

void JitAmd64::call_reg(Register reg) {
  AddMachineCode(B(reg));  
  AddMachineCode(0xff);
  unsigned char code = 0xd0;
  RegisterEncode3(code, 5, reg);
  AddMachineCode(code);
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [call %" << GetRegisterName(reg)
        << L"]" << std::endl;
#endif
}

// jmp reg (FF /4)
void JitAmd64::jmp_reg(Register reg) {
  AddMachineCode(B(reg));
  AddMachineCode(0xff);
  unsigned char code = 0xe0;
  RegisterEncode3(code, 5, reg);
  AddMachineCode(code);
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [jmp %" << GetRegisterName(reg)
        << L"]" << std::endl;
#endif
}

// lea dest, [rip + disp32] with a zero displacement; returns the offset of
// the displacement so the caller can patch it once the target is known
long JitAmd64::lea_rip_reg(Register dest) {
  unsigned char rex = 0x48;
  if(dest > RSP && dest < XMM0) {
    rex |= 0x04;   // REX.R
  }
  AddMachineCode(rex);
  AddMachineCode(0x8d);
  unsigned char modrm = 0x05;   // mod=00, rm=101: RIP-relative
  RegisterEncode3(modrm, 2, dest);
  AddMachineCode(modrm);
  const long pos = code_index;
  AddImm(0);
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [lea %" << GetRegisterName(dest)
        << L", [rip+disp32]]" << std::endl;
#endif
  return pos;
}

// movsxd dest, dword [base + index*4] (REX.W 63 /r); mod=01 with a zero disp8
// so a base of RBP/R13 encodes as a base rather than as "no base"
void JitAmd64::movsxd_base_index_reg(Register base, Register index, Register dest) {
  unsigned char rex = 0x48;
  if(dest > RSP && dest < XMM0) {
    rex |= 0x04;   // REX.R
  }
  if(index > RSP && index < XMM0) {
    rex |= 0x02;   // REX.X
  }
  if(base > RSP && base < XMM0) {
    rex |= 0x01;   // REX.B
  }
  AddMachineCode(rex);
  AddMachineCode(0x63);
  unsigned char modrm = 0x44;   // mod=01 (disp8), rm=100 (SIB follows)
  RegisterEncode3(modrm, 2, dest);
  AddMachineCode(modrm);
  unsigned char sib = 0x80;     // scale=4
  RegisterEncode3(sib, 2, index);
  RegisterEncode3(sib, 5, base);
  AddMachineCode(sib);
  AddMachineCode(0x00);         // disp8
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [movsxd %" << GetRegisterName(dest)
        << L", [%" << GetRegisterName(base) << L"+%" << GetRegisterName(index) << L"*4]]" << std::endl;
#endif
}

void JitAmd64::cmp_xreg_xreg(Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [ucomisd %" << GetRegisterName(src) 
        << L", %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(0x66);
  AddMachineCode(ROB(src, dest));
  AddMachineCode(0x0f);
  AddMachineCode(0x2e);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, dest);
  RegisterEncode3(code, 5, src);
  AddMachineCode(code);
}

void JitAmd64::cmp_mem_xreg(long offset, Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [ucomisd " << offset << L"(%" 
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest) 
        << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(0x66);
   AddMachineCode(RXB(dest, src));
  AddMachineCode(0x0f);
  AddMachineCode(0x2e);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}

void JitAmd64::cmp_imm_xreg(size_t addr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(addr, imm_holder->GetRegister());
  cmp_mem_xreg(0, imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

void JitAmd64::cvt_xreg_reg(Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [cvtsd2si %" << GetRegisterName(src) 
        << L", %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(ROB(dest, src));
  AddMachineCode(0x0f);
  AddMachineCode(0x2c);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, dest);
  RegisterEncode3(code, 5, src);
  AddMachineCode(code);
}

void JitAmd64::cvt_imm_reg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
#ifdef _WIN64
  move_imm_reg(instr->GetOperand2(), imm_holder->GetRegister());
#else
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
#endif
  cvt_mem_reg(0, imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

void JitAmd64::cvt_mem_reg(long offset, Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [cvtsd2si " << offset << L"(%" 
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(RXB(dest, src));
  AddMachineCode(0x0f);
  AddMachineCode(0x2c);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}

void JitAmd64::cvt_reg_xreg(Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [cvtsi2sd %" << GetRegisterName(src) 
        << L", %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(ROB(dest, src));
  AddMachineCode(0x0f);
  AddMachineCode(0x2a);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, dest);
  RegisterEncode3(code, 5, src);
  AddMachineCode(code);
}

void JitAmd64::cvt_imm_xreg(RegInstr* instr, Register reg) {
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
  cvt_reg_xreg(imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

void JitAmd64::cvt_mem_xreg(long offset, Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [cvtsi2sd " << offset << L"(%" 
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(RXB(dest, src));
  AddMachineCode(0x0f);
  AddMachineCode(0x2a);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}

// TODO: 64-bit literal operation for Windows
void JitAmd64::and_imm_reg(int64_t imm, Register reg) {
  if(imm == 0) { move_imm_reg(0, reg); return; }
  if(imm == -1) { return; }
  // x86-64 has no AND with a 64-bit immediate. The 0x81 group takes an imm32
  // that is SIGN-EXTENDED to 64 bits, so a wider value silently loses its high
  // half: 0x7FFFFFFFFFFFFFFF became 0xFFFFFFFF, sign-extended back to -1, and
  // the operation turned into a no-op. Materialise it and use the register
  // form. move_imm_reg emits a mov, which leaves FLAGS alone, and the reg-reg
  // form sets them exactly as the immediate form would.
  if(imm < INT32_MIN || imm > INT32_MAX) {
    RegisterHolder* imm_holder = GetRegister();
    move_imm_reg(imm, imm_holder->GetRegister());
    and_reg_reg(imm_holder->GetRegister(), reg);
    ReleaseRegister(imm_holder);
    return;
  }
  if(imm >= INT8_MIN && imm <= INT8_MAX) {
#ifdef _DEBUG_JIT
    std::wcout << L"  " << (++instr_count) << L": [andq $" << imm << L", %"
          << GetRegisterName(reg) << L"] (imm8)" << std::endl;
#endif
    AddMachineCode(B(reg));
    AddMachineCode(0x83);
    unsigned char code = 0xe0;
    RegisterEncode3(code, 5, reg);
    AddMachineCode(code);
    AddMachineCode((unsigned char)(int8_t)imm);
    return;
  }
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [andq $" << imm << L", %"
        << GetRegisterName(reg) << L"]" << std::endl;
#endif
  AddMachineCode(B(reg));
  AddMachineCode(0x81);
  unsigned char code = 0xe0;
  RegisterEncode3(code, 5, reg);
  AddMachineCode(code);
  AddImm((long)imm);
}

void JitAmd64::and_reg_reg(Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [andq %" << GetRegisterName(src) 
        << L", %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(ROB(src, dest));
  AddMachineCode(0x21);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, src);
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
}

void JitAmd64::and_mem_reg(long offset, Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [andq " << offset << L"(%" 
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(RXB(src, dest));
  AddMachineCode(0x23);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}

// TODO: 64-bit literal operation for Windows
void JitAmd64::not_reg(Register reg) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [not $" << GetRegisterName(reg) << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(B(reg));
  AddMachineCode(0xf7);
  AddMachineCode(REXW(reg));
}

// TODO: 64-bit literal operation for Windows
void JitAmd64::or_imm_reg(int64_t imm, Register reg) {
  if(imm == 0) { return; }
  if(imm == -1) { move_imm_reg(-1, reg); return; }
  // x86-64 has no OR with a 64-bit immediate. The 0x81 group takes an imm32
  // that is SIGN-EXTENDED to 64 bits, so a wider value silently loses its high
  // half: 0x7FFFFFFFFFFFFFFF became 0xFFFFFFFF, sign-extended back to -1, and
  // the operation turned into a no-op. Materialise it and use the register
  // form. move_imm_reg emits a mov, which leaves FLAGS alone, and the reg-reg
  // form sets them exactly as the immediate form would.
  if(imm < INT32_MIN || imm > INT32_MAX) {
    RegisterHolder* imm_holder = GetRegister();
    move_imm_reg(imm, imm_holder->GetRegister());
    or_reg_reg(imm_holder->GetRegister(), reg);
    ReleaseRegister(imm_holder);
    return;
  }
  if(imm >= INT8_MIN && imm <= INT8_MAX) {
#ifdef _DEBUG_JIT
    std::wcout << L"  " << (++instr_count) << L": [orq $" << imm << L", %"
          << GetRegisterName(reg) << L"] (imm8)" << std::endl;
#endif
    AddMachineCode(B(reg));
    AddMachineCode(0x83);
    unsigned char code = 0xc8;
    RegisterEncode3(code, 5, reg);
    AddMachineCode(code);
    AddMachineCode((unsigned char)(int8_t)imm);
    return;
  }
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [orq $" << imm << L", %"
        << GetRegisterName(reg) << L"]" << std::endl;
#endif
  AddMachineCode(B(reg));
  AddMachineCode(0x81);
  unsigned char code = 0xc8;
  RegisterEncode3(code, 5, reg);
  AddMachineCode(code);
  AddImm((long)imm);
}

void JitAmd64::or_reg_reg(Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [orq %" << GetRegisterName(src) 
        << L", %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(ROB(src, dest));
  AddMachineCode(0x09);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, src);
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
}

void JitAmd64::or_mem_reg(long offset, Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [orq " << offset << L"(%" 
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(RXB(src, dest));
  AddMachineCode(0x0b);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}

// TODO: 64-bit literal operation for Windows
void JitAmd64::xor_imm_reg(int64_t imm, Register reg) {
  if(imm == 0) { return; }
  if(imm == -1) { not_reg(reg); return; }
  // x86-64 has no XOR with a 64-bit immediate. The 0x81 group takes an imm32
  // that is SIGN-EXTENDED to 64 bits, so a wider value silently loses its high
  // half: 0x7FFFFFFFFFFFFFFF became 0xFFFFFFFF, sign-extended back to -1, and
  // the operation turned into a no-op. Materialise it and use the register
  // form. move_imm_reg emits a mov, which leaves FLAGS alone, and the reg-reg
  // form sets them exactly as the immediate form would.
  if(imm < INT32_MIN || imm > INT32_MAX) {
    RegisterHolder* imm_holder = GetRegister();
    move_imm_reg(imm, imm_holder->GetRegister());
    xor_reg_reg(imm_holder->GetRegister(), reg);
    ReleaseRegister(imm_holder);
    return;
  }
  if(imm >= INT8_MIN && imm <= INT8_MAX) {
#ifdef _DEBUG_JIT
    std::wcout << L"  " << (++instr_count) << L": [xorq $" << imm << L", %"
          << GetRegisterName(reg) << L"] (imm8)" << std::endl;
#endif
    AddMachineCode(B(reg));
    AddMachineCode(0x83);
    unsigned char code = 0xf0;
    RegisterEncode3(code, 5, reg);
    AddMachineCode(code);
    AddMachineCode((unsigned char)(int8_t)imm);
    return;
  }
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [xorq $" << imm << L", %"
        << GetRegisterName(reg) << L"]" << std::endl;
#endif
  AddMachineCode(B(reg));
  AddMachineCode(0x81);
  unsigned char code = 0xf0;
  RegisterEncode3(code, 5, reg);
  AddMachineCode(code);
  AddImm((long)imm);
}

void JitAmd64::xor_reg_reg(Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [xorq %" << GetRegisterName(src) 
        << L", %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(ROB(src, dest));
  AddMachineCode(0x31);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, src);
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
}

void JitAmd64::xor_mem_reg(long offset, Register src, Register dest) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [xorq " << offset << L"(%" 
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
  // encode
  AddMachineCode(RXB(src, dest));
  AddMachineCode(0x33);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}

// --- x87 ---

void JitAmd64::fld_mem(int32_t offset, Register src) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [fld " << offset << L"(%"
    << GetRegisterName(src) << L")]" << std::endl;
#endif
  // encode
  AddMachineCode(0xdd);
  AddMachineCode(ModRM(src, RAX));
  // write value
  AddImm(offset);
}

void JitAmd64::fstp_mem(int32_t offset, Register src) {
#ifdef _DEBUG_JIT
  std::wcout << L"  " << (++instr_count) << L": [fld " << offset << L"(%"
    << GetRegisterName(src) << L")]" << std::endl;
#endif
  // encode
  AddMachineCode(0xdd);
  AddMachineCode(ModRM(src, RBX));
  // write value
  AddImm(offset);
}

void JitAmd64::fsin() {
  AddMachineCode(0xd9);
  AddMachineCode(0xfe);
}

void JitAmd64::fcos() {
  AddMachineCode(0xd9);
  AddMachineCode(0xff);
}

void JitAmd64::ftan() {
  AddMachineCode(0xd9);
  AddMachineCode(0xf2);
  AddMachineCode(0xdd);
  AddMachineCode(0xd8);
}

void JitAmd64::fsqrt() {
  AddMachineCode(0xd9);
  AddMachineCode(0xfa);
}

void JitAmd64::fround() {
  AddMachineCode(0xd9);
  AddMachineCode(0xfc);
}

void JitAmd64::flog() {
  AddMachineCode(0xd9);
  AddMachineCode(0xe9);
}

void JitAmd64::flog10() {
  AddMachineCode(0xd9);
  AddMachineCode(0xec);
}

/**
 * Calculates the AMD64 MOD R/M
 * offset
 */
unsigned char JitAmd64::ModRM(Register eff_adr, Register mod_rm)
{
  unsigned char byte;

  switch(mod_rm) {
  case RSP:
  case XMM4:
  case R12:
  case XMM12:
    byte = 0xa0;
    break;

  case RAX:
  case XMM0:
  case R8:
  case XMM8:
    byte = 0x80;
    break;

  case RBX:
  case XMM3:
  case R11:
  case XMM11:
    byte = 0x98;
    break;

  case RCX:
  case XMM1:
  case R9:
  case XMM9:
    byte = 0x88;
    break;

  case RDX:
  case XMM2:
  case R10:
  case XMM10:
    byte = 0x90;
    break;

  case RDI:
  case XMM7:
  case R15:
  case XMM15:
    byte = 0xb8;
    break;

  case RSI:
  case XMM6:
  case R14:
  case XMM14:
    byte = 0xb0;
    break;

  case RBP:
  case XMM5:
  case R13:
  case XMM13:
    byte = 0xa8;
    break;

  default:
    std::wcerr << L"internal error" << std::endl;
    exit(1);
    break;
  }

  switch(eff_adr) {
  case RAX:
  case XMM0:
  case R8:
  case XMM8:
    break;

  case RBX:
  case XMM3:
  case R11:
  case XMM11:
    byte += 3;
    break;

  case RCX:
  case XMM1:
  case R9:
  case XMM9:
    byte += 1;
    break;

  case RDX:
  case XMM2:
  case R10:
  case XMM10:
    byte += 2;
    break;

  case RDI:
  case XMM7:
  case R15:
  case XMM15:
    byte += 7;
    break;

  case RSI:
  case XMM6:
  case R14:
  case XMM14:
    byte += 6;
    break;

  case RBP:
  case XMM5:
  case R13:
  case XMM13:
    byte += 5;
    break;

  case XMM4:
  case R12:
  case XMM12:
    byte += 4;
    break;

    // should never happen for esp
  case RSP:
    std::wcerr << L"invalid register reference" << std::endl;
    exit(1);
    break;

  default:
    std::wcerr << L"internal error" << std::endl;
    exit(1);
    break;
  }

  return byte;
}

/**
 * Returns the name of a register
 */
std::wstring JitAmd64::GetRegisterName(Register reg)
{
  switch(reg) {
  case RAX:
    return L"rax";

  case RBX:
    return L"rbx";

  case RCX:
    return L"rcx";

  case RDX:
    return L"rdx";

  case RDI:
    return L"rdi";

  case RSI:
    return L"rsi";

  case RBP:
    return L"rbp";

  case RSP:
    return L"rsp";

  case R8:
    return L"r8";

  case R9:
    return L"r9";

  case R10:
    return L"r10";

  case R11:
    return L"r11";

  case R12:
    return L"r12";

  case R13:
    return L"r13";

  case R14:
    return L"r14";

  case R15:
    return L"r15";

  case XMM0:
    return L"xmm0";

  case XMM1:
    return L"xmm1";

  case XMM2:
    return L"xmm2";

  case XMM3:
    return L"xmm3";

  case XMM4:
    return L"xmm4";

  case XMM5:
    return L"xmm5";

  case XMM6:
    return L"xmm6";

  case XMM7:
    return L"xmm7";

  case XMM8:
    return L"xmm8";

  case XMM9:
    return L"xmm9";

  case XMM10:
    return L"xmm10";

  case XMM11:
    return L"xmm11";

  case XMM12:
    return L"xmm12";

  case XMM13:
    return L"xmm13";

  case XMM14:
    return L"xmm14";

  case XMM15:
    return L"xmm15";
  }

  return L"?";
}

/**
 * Encodes an array with the
 * binary ID of a register
 */
void JitAmd64::RegisterEncode3(unsigned char& code, long offset, Register reg)
{
#ifdef _DEBUG_JIT
  assert(offset == 2 || offset == 5);
#endif

  unsigned char reg_id;
  switch(reg) {
  case RAX:
  case XMM0:
  case R8:
  case XMM8:
    reg_id = 0x0;
    break;

  case RBX:
  case XMM3:
  case R11:
  case XMM11:
    reg_id = 0x3;
    break;

  case RCX:
  case XMM1:
  case R9:
  case XMM9:
    reg_id = 0x1;
    break;

  case RDX:
  case XMM2:
  case R10:
  case XMM10:
    reg_id = 0x2;
    break;

  case RDI:
  case XMM7:
  case R15:
  case XMM15:
    reg_id = 0x7;
    break;

  case RSI:
  case XMM6:
  case R14:
  case XMM14:
    reg_id = 0x6;
    break;

  case RSP:
  case XMM4:
  case R12:
  case XMM12:
    reg_id = 0x4;
    break;

  case RBP:
  case XMM5:
  case R13:
  case XMM13:
    reg_id = 0x5;
    break;

  default:
    std::wcerr << L"internal error" << std::endl;
    exit(1);
    break;
  }

  if(offset == 2) {
    reg_id = reg_id << 3;
  }
  code = code | reg_id;
}

RegisterHolder* JitAmd64::ArrayIndex(StackInstr* instr, MemoryType type)
{
  RegInstr* holder = working_stack.front();
  working_stack.pop_front();

  RegisterHolder* array_holder;
  switch(holder->GetType()) {
  case IMM_INT:
    std::wcerr << L">>> trying to index a constant! <<<" << std::endl;
    exit(1);
    break;

  case REG_INT:
    array_holder = holder->GetRegister();
    break;

  case MEM_INT:
    array_holder = GetRegister();
    move_mem_reg((long)holder->GetOperand(), RBP, array_holder->GetRegister());
    break;

  default:
    std::wcerr << L"internal error" << std::endl;
    exit(1);
    break;
  }
  CheckNilDereference(array_holder->GetRegister());

  /* Algorithm:
   long index = PopInt();
   const long dim = instr->GetOperand();

   for(int i = 1; i < dim; ++i) {
     index *= array[i];
     index += PopInt();
   }
  */

  delete holder;
  holder = nullptr;

  // get initial index
  RegisterHolder* index_holder;
  holder = working_stack.front();
  working_stack.pop_front();
  switch(holder->GetType()) {
  case IMM_INT:
    index_holder = GetRegister();
    move_imm_reg(holder->GetOperand(), index_holder->GetRegister());
    break;

  case REG_INT:
    index_holder = holder->GetRegister();
    break;

  case MEM_INT:
    index_holder = GetRegister();
    move_mem_reg((long)holder->GetOperand(), RBP, index_holder->GetRegister());
    break;

  default:
    std::wcerr << L"internal error" << std::endl;
    exit(1);
    break;
  }

  const long dim = instr->GetOperand();
  for(int i = 1; i < dim; ++i) {
    // index *= array[i];
    mul_mem_reg((i + 2) * sizeof(size_t), array_holder->GetRegister(), index_holder->GetRegister());
    delete holder;
    holder = nullptr;

    holder = working_stack.front();
    working_stack.pop_front();
    switch(holder->GetType()) {
    case IMM_INT:
      add_imm_reg(holder->GetOperand(), index_holder->GetRegister());
      break;

    case REG_INT:
      add_reg_reg(holder->GetRegister()->GetRegister(), index_holder->GetRegister());
      break;

    case MEM_INT:
      add_mem_reg((long)holder->GetOperand(), RBP, index_holder->GetRegister());
      break;

    default:
      break;
    }
  }

  // Bounds check on the UNSCALED index against the element count (header
  // word 0). Index and count used to be shifted by the element size before
  // the compare -- two shifts that changed nothing about the comparison --
  // and the address was then built with two adds. One lea does it.
  RegisterHolder* bounds_holder = GetRegister();
  move_mem_reg(0, array_holder->GetRegister(), bounds_holder->GetRegister());
  CheckArrayBounds(index_holder->GetRegister(), bounds_holder->GetRegister());
  ReleaseRegister(bounds_holder);

  int scale;
  switch(type) {
  case BYTE_ARY_TYPE:
    scale = 1;
    break;

  case CHAR_ARY_TYPE:
#ifdef _WIN64
    scale = 2;
#else
    scale = 4;
#endif
    break;

  default:
    scale = 8;   // INT_TYPE, FLOAT_TYPE
    break;
  }
  // array = array + index * scale + (size, dimension, dimension sizes) header
  lea_base_index_reg((instr->GetOperand() + 2) * sizeof(size_t), array_holder->GetRegister(),
                     index_holder->GetRegister(), scale, array_holder->GetRegister());
  ReleaseRegister(index_holder);

  delete holder;
  holder = nullptr;

  return array_holder;
}

void JitAmd64::ProcessIndices()
{
#ifdef _DEBUG_JIT
  std::wcout << L"Calculating indices for variables..." << std::endl;
#endif
  std::multimap<long, StackInstr*> values;
  for(long i = 0; i < method->GetInstructionCount(); ++i) {
    StackInstr* instr = method->GetInstruction(i);
    switch(instr->GetType()) {
    case LOAD_LOCL_INT_VAR:
    case LOAD_CLS_INST_INT_VAR:
    case STOR_LOCL_INT_VAR:
    case STOR_CLS_INST_INT_VAR:
    case LOAD_FUNC_VAR:
    case STOR_FUNC_VAR:
    case COPY_LOCL_INT_VAR:
    case COPY_CLS_INST_INT_VAR:
    case LOAD_FLOAT_VAR:
    case STOR_FLOAT_VAR:
    case COPY_FLOAT_VAR:
      values.insert(std::pair<long, StackInstr*>(instr->GetOperand(), instr));
      break;

    default:
      break;
    }
  }

  // Lay the frame out from the declarations, not from the references: one
  // slot per declaration in id order (two for a func-ref), after the and/or
  // temp at id 0 when the method has one. The collector walks a JIT frame by
  // declaration, one word each (CheckJitRoots), so a declared local that no
  // instruction references still needs its slot. It used to get none, and
  // every slot below it was then read under the next declaration's type: an
  // object where an object array was declared had its first field taken for
  // a length, which is how a compiled HttpRequestHandler:ServeOne brought the
  // collector down on Linux with every method compiled. Any id beyond the
  // declarations (none is expected) is allocated below them as before.
  // jit_frame_unreferenced_local.obs fails on the old layout.
  std::unordered_map<long, long> slot_offsets;
  long index = RED_ZONE;
  {
    long id = 0;
    if(method->HasAndOr()) {
      index -= sizeof(size_t);
      slot_offsets[0] = index;
      id = 1;
    }
    StackDclr** dclrs = method->GetDeclarations();
    const long num_dclrs = method->GetNumberDeclarations();
    for(long j = 0; j < num_dclrs; ++j) {
      const long words = (dclrs[j]->type == FUNC_PARM) ? 2 : 1;
      index -= words * (long)sizeof(size_t);
      slot_offsets[id] = index;
      id += words;
    }
  }
  long last_id = -1;
  std::multimap<long, StackInstr*>::iterator value;
  for(value = values.begin(); value != values.end(); ++value) {
    long id = value->first;
    StackInstr* instr = value->second;
    // instance reference
    if(instr->GetOperand2() == INST || instr->GetOperand2() == CLS) {
      instr->SetOperand3(instr->GetOperand() * sizeof(size_t));
    }
    // local reference
    else {
      const auto slot = slot_offsets.find(id);
      if(slot != slot_offsets.end()) {
        instr->SetOperand3(slot->second);
      }
      else {
        // note: all local variables are allocated in 4 or 8 bytes
        // blocks depending upon type
        if(last_id != id) {
          switch(instr->GetType()) {
          case LOAD_LOCL_INT_VAR:
          case LOAD_CLS_INST_INT_VAR:
          case STOR_LOCL_INT_VAR:
          case STOR_CLS_INST_INT_VAR:
          case COPY_LOCL_INT_VAR:
          case COPY_CLS_INST_INT_VAR:
            index -= sizeof(size_t);
            break;

          case LOAD_FUNC_VAR:
          case STOR_FUNC_VAR:
            index -= sizeof(size_t) * 2;
            break;

          default:
            index -= sizeof(double);
            break;
          }
        }
        instr->SetOperand3(index);
        last_id = id;
      }
    }
#ifdef _DEBUG_JIT
    if(instr->GetOperand2() == INST || instr->GetOperand2() == CLS) {
      std::wcout << L"native memory: index=" << instr->GetOperand() << L"; jit index="
        << instr->GetOperand3() << std::endl;
    }
    else {
      std::wcout << L"native std::stack: index=" << instr->GetOperand() << L"; jit index="
        << instr->GetOperand3() << std::endl;
    }
#endif
  }
  org_local_space = local_space = -(index + TMP_REG_9);

#ifdef _DEBUG_JIT
  std::wcout << L"Local space required: " << (local_space + 16) << L" byte(s)" << std::endl;
#endif
}

// forward declaration (defined below)
static bool CanJitInstruction(InstructionType type);

// ── Method Inlining ────────────────────────────────────────────────────

bool JitAmd64::CanInlineMethod(StackMethod* callee) {
  if(!callee || callee == method) return false;           // no recursion
  if(callee->IsVirtual()) return false;                   // static dispatch only
  if(callee->GetInstructionCount() > MAX_INLINE_SIZE) return false;
  if(HasFrameDependentTrap(callee)) return false;         // null frame in trap callback

  for(long i = 0; i < callee->GetInstructionCount(); ++i) {
    InstructionType type = callee->GetInstruction(i)->GetType();
    // no calls, no control flow, no async
    if(type == MTHD_CALL || type == DYN_MTHD_CALL || type == ASYNC_MTHD_CALL) return false;
    if(type == JMP || type == LBL || type == JMP_TABLE || type == JMP_TABLE_SLOT) return false;
    // all instructions must be JIT-compilable
    if(!CanJitInstruction(type) && type != RTRN) return false;
  }
  return true;
}

static bool UsesInstanceMem(StackMethod* method) {
  for(long i = 0; i < method->GetInstructionCount(); ++i) {
    if(method->GetInstruction(i)->GetType() == LOAD_INST_MEM) return true;
  }
  return false;
}

long JitAmd64::ComputeInlineLocalSpace(StackMethod* callee) {
  std::set<long> seen_ids;
  long space = 0;
  // reserve save slot for INSTANCE_MEM when inlining instance methods
  if(UsesInstanceMem(callee)) space += sizeof(size_t);
  for(long i = 0; i < callee->GetInstructionCount(); ++i) {
    StackInstr* ci = callee->GetInstruction(i);
    if(ci->GetOperand2() == INST || ci->GetOperand2() == CLS) continue;
    long var_id = ci->GetOperand();
    switch(ci->GetType()) {
    case LOAD_LOCL_INT_VAR: case STOR_LOCL_INT_VAR: case COPY_LOCL_INT_VAR:
    case LOAD_CLS_INST_INT_VAR: case STOR_CLS_INST_INT_VAR: case COPY_CLS_INST_INT_VAR:
      if(seen_ids.insert(var_id).second) space += sizeof(size_t);
      break;
    case LOAD_FUNC_VAR: case STOR_FUNC_VAR:
      if(seen_ids.insert(var_id).second) space += sizeof(size_t) * 2;
      break;
    case LOAD_FLOAT_VAR: case STOR_FLOAT_VAR: case COPY_FLOAT_VAR:
      if(seen_ids.insert(var_id).second) space += sizeof(double);
      break;
    default:
      break;
    }
  }
  return space;
}

void JitAmd64::ProcessInlineMethod(StackMethod* callee, [[maybe_unused]] StackInstr* call_instr, [[maybe_unused]] long& caller_instr_index) {
#ifdef _DEBUG_JIT
  std::wcout << L"=== INLINE: method='" << callee->GetName() << L"' ===" << std::endl;
#endif

  // Save caller context
  StackMethod* saved_method = method;
  long saved_instr_index = instr_index;
  bool saved_skip_jump = skip_jump;

  // Save callee's original operand3 values (will be restored after inlining)
  const long callee_instr_count = callee->GetInstructionCount();
  std::vector<long> saved_operand3(callee_instr_count);
  for(long i = 0; i < callee_instr_count; ++i) {
    saved_operand3[i] = callee->GetInstruction(i)->GetOperand3();
  }

  // Instance method handling: self is on top of working_stack and must be
  // routed to INSTANCE_MEM so LOAD_INST_MEM works inside the inlined code.
  const bool is_instance_method = UsesInstanceMem(callee);
  long save_inst_offset = 0;

  if(is_instance_method) {
    // Reserve a save slot for the caller's INSTANCE_MEM
    save_inst_offset = inline_local_offset;
    inline_local_offset -= sizeof(size_t);

    // Save caller's INSTANCE_MEM to the save slot
    RegisterHolder* tmp = GetRegister();
    move_mem_reg(INSTANCE_MEM, RBP, tmp->GetRegister());
    move_reg_mem(tmp->GetRegister(), save_inst_offset, RBP);
    ReleaseRegister(tmp);

    // Pop self from working_stack and write to INSTANCE_MEM
    RegInstr* self_val = working_stack.front();
    working_stack.pop_front();
    switch(self_val->GetType()) {
    case REG_INT:
      move_reg_mem(self_val->GetRegister()->GetRegister(), INSTANCE_MEM, RBP);
      ReleaseRegister(self_val->GetRegister());
      break;
    case MEM_INT: {
      RegisterHolder* holder = GetRegister();
      move_mem_reg((long)self_val->GetOperand(), RBP, holder->GetRegister());
      move_reg_mem(holder->GetRegister(), INSTANCE_MEM, RBP);
      ReleaseRegister(holder);
    }
      break;
    case IMM_INT:
      move_imm_mem(self_val->GetOperand(), INSTANCE_MEM, RBP);
      break;
    default:
      break;
    }
    delete self_val;
  }

  // Run mini-ProcessIndices for callee at the inline offset region
  {
    std::multimap<long, StackInstr*> vars;
    for(long i = 0; i < callee_instr_count; ++i) {
      StackInstr* ci = callee->GetInstruction(i);
      switch(ci->GetType()) {
      case LOAD_LOCL_INT_VAR: case LOAD_CLS_INST_INT_VAR:
      case STOR_LOCL_INT_VAR: case STOR_CLS_INST_INT_VAR:
      case LOAD_FUNC_VAR: case STOR_FUNC_VAR:
      case COPY_LOCL_INT_VAR: case COPY_CLS_INST_INT_VAR:
      case LOAD_FLOAT_VAR: case STOR_FLOAT_VAR: case COPY_FLOAT_VAR:
        vars.insert(std::pair<long, StackInstr*>(ci->GetOperand(), ci));
        break;
      default: break;
      }
    }

    long index = inline_local_offset;
    long last_id = -1;
    for(auto iter = vars.begin(); iter != vars.end(); ++iter) {
      long id = iter->first;
      StackInstr* ci = iter->second;
      if(ci->GetOperand2() == INST || ci->GetOperand2() == CLS) {
        ci->SetOperand3(ci->GetOperand() * sizeof(size_t));
      }
      else {
        if(last_id != id) {
          switch(ci->GetType()) {
          case LOAD_LOCL_INT_VAR: case LOAD_CLS_INST_INT_VAR:
          case STOR_LOCL_INT_VAR: case STOR_CLS_INST_INT_VAR:
          case COPY_LOCL_INT_VAR: case COPY_CLS_INST_INT_VAR:
            index -= sizeof(size_t); break;
          case LOAD_FUNC_VAR: case STOR_FUNC_VAR:
            index -= sizeof(size_t) * 2; break;
          default:
            index -= sizeof(double); break;
          }
        }
        ci->SetOperand3(index);
        last_id = id;
      }
    }
    inline_local_offset = index;
  }

  // Store parameters from working_stack into callee's local slots.
  // The callee's first param_count instructions are STOR_* instructions
  // that ProcessParameters would normally handle. For inlining, the values
  // are already on working_stack, so we just call ProcessStore directly.
  const long param_count = callee->GetParamCount();
  for(long i = 0; i < param_count && i < callee_instr_count; ++i) {
    StackInstr* param_instr = callee->GetInstruction(i);
    param_instr->SetOffset(code_index);
    ProcessStore(param_instr);
  }

  // Switch to callee context and process remaining instructions
  method = callee;
  instr_index = param_count;
  skip_jump = false;
  is_inlining = true;
  inline_callee = callee;

  ProcessInstructions();

  // Restore callee's original operand3 values
  for(long i = 0; i < callee_instr_count; ++i) {
    callee->GetInstruction(i)->SetOperand3(saved_operand3[i]);
  }

  // Restore caller's INSTANCE_MEM if we saved it
  if(is_instance_method) {
    RegisterHolder* tmp = GetRegister();
    move_mem_reg(save_inst_offset, RBP, tmp->GetRegister());
    move_reg_mem(tmp->GetRegister(), INSTANCE_MEM, RBP);
    ReleaseRegister(tmp);
  }

  // Restore caller context
  method = saved_method;
  instr_index = saved_instr_index;
  skip_jump = saved_skip_jump;
  is_inlining = false;
  inline_callee = nullptr;

  // Handle return value: it's already on working_stack from the inlined code.
  // The caller will use it naturally (ProcessReturnParameters is NOT called).
#ifdef _DEBUG_JIT
  std::wcout << L"=== END INLINE: method='" << callee->GetName() << L"' ===" << std::endl;
#endif
}

// Returns true if the instruction type is supported by the JIT compiler.
// This whitelist must match the cases handled in ProcessInstructions().
static bool CanJitInstruction(InstructionType type) {
  switch(type) {
    // loads
  case LOAD_CHAR_LIT:
  case LOAD_INT_LIT:
  case LOAD_FLOAT_LIT:
  case LOAD_INST_MEM:
  case LOAD_CLS_MEM:
  case LOAD_LOCL_INT_VAR:
  case LOAD_CLS_INST_INT_VAR:
  case LOAD_FLOAT_VAR:
  case LOAD_FUNC_VAR:
    // stores
  case STOR_LOCL_INT_VAR:
  case STOR_CLS_INST_INT_VAR:
  case STOR_FLOAT_VAR:
  case STOR_FUNC_VAR:
    // copies
  case COPY_LOCL_INT_VAR:
  case COPY_CLS_INST_INT_VAR:
  case COPY_FLOAT_VAR:
    // int math
  case AND_INT:
  case OR_INT:
  case ADD_INT:
  case SUB_INT:
  case MUL_INT:
  case DIV_INT:
  case MOD_INT:
  case BIT_AND_INT:
  case BIT_OR_INT:
  case BIT_XOR_INT:
  case BIT_NOT_INT:
  case SHL_INT:
  case SHR_INT:
    // int compare
  case LES_INT:
  case GTR_INT:
  case LES_EQL_INT:
  case GTR_EQL_INT:
  case EQL_INT:
  case NEQL_INT:
    // float math
  case ADD_FLOAT:
  case SUB_FLOAT:
  case MUL_FLOAT:
  case DIV_FLOAT:
  case SIN_FLOAT:
  case COS_FLOAT:
  case TAN_FLOAT:
  case ASIN_FLOAT:
  case ACOS_FLOAT:
  case ATAN_FLOAT:
  case ACOSH_FLOAT:
  case ASINH_FLOAT:
  case ATANH_FLOAT:
  case LOG2_FLOAT:
  case CBRT_FLOAT:
  case COSH_FLOAT:
  case SINH_FLOAT:
  case TANH_FLOAT:
  case LOG_FLOAT:
  case EXP_FLOAT:
  case LOG10_FLOAT:
  case TRUNC_FLOAT:
  case GAMMA_FLOAT:
  case ATAN2_FLOAT:
  case MOD_FLOAT:
  case POW_FLOAT:
  case SQRT_FLOAT:
  case ROUND_FLOAT:
  case CEIL_FLOAT:
  case FLOR_FLOAT:
  case RAND_FLOAT:
    // float compare
  case LES_FLOAT:
  case GTR_FLOAT:
  case LES_EQL_FLOAT:
  case GTR_EQL_FLOAT:
  case EQL_FLOAT:
  case NEQL_FLOAT:
    // control flow
  case MTHD_CALL:
  case MTHD_CALL_JIT:
  case DYN_MTHD_CALL:        // P2: function-reference / closure call JIT
  case DYN_MTHD_CALL_JIT:
  case JMP:
  case JMP_TABLE:
  case JMP_TABLE_SLOT:
  case LBL:
  case RTRN:
    // memory allocation
  case NEW_BYTE_ARY:
  case NEW_CHAR_ARY:
  case NEW_INT_ARY:
  case NEW_FLOAT_ARY:
  case NEW_OBJ_INST:
  case NEW_FUNC_INST:
    // array copy/zero
  case CPY_BYTE_ARY:
  case CPY_CHAR_ARY:
  case CPY_INT_ARY:
  case CPY_FLOAT_ARY:
  case ZERO_BYTE_ARY:
  case ZERO_CHAR_ARY:
  case ZERO_INT_ARY:
  case ZERO_FLOAT_ARY:
    // array access
  case LOAD_BYTE_ARY_ELM:
  case LOAD_CHAR_ARY_ELM:
  case LOAD_INT_ARY_ELM:
  case LOAD_FLOAT_ARY_ELM:
  case STOR_BYTE_ARY_ELM:
  case STOR_CHAR_ARY_ELM:
  case STOR_INT_ARY_ELM:
  case STOR_FLOAT_ARY_ELM:
  case LOAD_ARY_SIZE:
    // traps
  case TRAP:
  case TRAP_RTRN:
    // conversions
  case F2I:
  case I2F:
  case I2S:
  case S2I:
  case F2S:
  case S2F:
    // casting
  case OBJ_TYPE_OF:
  case OBJ_INST_CAST:
    // threading
  case THREAD_JOIN:
  case THREAD_SLEEP:
  case CRITICAL_START:
  case CRITICAL_END:
    // stack
  case SWAP_INT:
  case POP_INT:
  case POP_FLOAT:
    // (TRY_START/TRY_END are deliberately absent: a method with a try region
    //  runs in the interpreter -- see the TRY_START case in ProcessInstructions)
    return true;

  default:
    return false;
  }
}


// OBJECK_JIT_REPORT=1 names every method the JIT hands back to the interpreter,
// and why. One unsupported opcode returns the WHOLE method to the interpreter
// and nothing said so; the opcode number maps to `obc -asm`'s listing.
static bool JitReportEnabled() {
#ifdef _WIN32
  static const bool enabled = []() { size_t len = 0; getenv_s(&len, nullptr, 0, "OBJECK_JIT_REPORT"); return len > 0; }();
#else
  static const bool enabled = getenv("OBJECK_JIT_REPORT") != nullptr;
#endif
  return enabled;
}

bool JitAmd64::Compile(StackMethod* cm)
{
  compile_success = true;

  if(!cm->GetNativeCode()) {
#ifdef _TIMING
    clock_t start = clock();
#endif
    skip_jump = false;
    method = cm;

    // Pre-scan: reject methods with unsupported instructions, detect loops
    detected_loops.clear();
    safepoint_lbl_indices.clear();
    pin_regions.clear();
    instr_index_of.clear();
    pin_exit_stub_offsets.clear();
    active_pin_region = -1;
    method_pins = false;
    method_pins_float = false;
    is_inlining = false;
    inline_callee = nullptr;
    direct_callee = nullptr;
    virtual_site = nullptr;
    virtual_sites.clear();
    inline_local_offset = 0;
    xmm_pool_used = false;
    xmm_save_indices.clear();
    xmm_restore_indices.clear();
    xmm_save_size = xmm_restore_size = 0;
    rec_base = out_area = 0;
    native_entry_offset = -1;
    call_return_type = NIL_TYPE;
    native_result_taken = false;

    for(long i = 0; i < method->GetInstructionCount(); ++i) {
      StackInstr* scan_instr = method->GetInstruction(i);
      if(!CanJitInstruction(scan_instr->GetType())) {
        if(JitReportEnabled()) {
          std::wcerr << L"[jit] " << method->GetName() << L": not compiled -- unsupported opcode "
                     << scan_instr->GetType() << L" at instruction " << i << std::endl;
        }
        return false;
      }
      // DYN_MTHD_CALL return marshalling (ProcessReturnParameters) is driven by
      // operand2 (the func-ref's return MemoryType). For a generic func-ref
      // (`(H)~H`) compiled in library mode the return type collapses to NIL_TYPE
      // — indistinguishable from a genuinely void func-ref (`(H)~Nil`). If we
      // JIT such a call and the result IS consumed (e.g. `a[i] := f(x)` in
      // Collection.Map/Filter/Reduce), no value is marshalled back and the next
      // instruction underflows the working stack -> crash. Reject these methods
      // so they run in the interpreter (which marshals results dynamically and
      // handles them correctly). Concrete-return func-refs (e.g. spectralnorm's
      // `~Float` closures) keep a non-NIL operand2 and still JIT.
      if((scan_instr->GetType() == DYN_MTHD_CALL || scan_instr->GetType() == DYN_MTHD_CALL_JIT) &&
         scan_instr->GetOperand2() == instructions::MemoryType::NIL_TYPE) {
        return false;
      }
      // detect loops via backward jumps (a JMP operand is the target instruction
      // index; a target earlier than the jump is a loop back-edge)
      if(scan_instr->GetType() == JMP && scan_instr->GetOperand() < i) {
        detected_loops.push_back({scan_instr->GetOperand(), i});
        // the back-edge target label is a loop header — it needs a safepoint poll
        safepoint_lbl_indices.insert(scan_instr->GetOperand());
      }
    }

    // frame->mem-dependent traps can't run from JIT code (null frame in the
    // trap callback; locals live in native stack slots)
    if(HasFrameDependentTrap(method)) {
      return false;
    }

#ifdef _DEBUG_JIT
    long cls_id = method->GetClass()->GetId();
    long mthd_id = method->GetId();
    std::wcout << L"---------- Compiling Native Code: method_id=" << cls_id << L","
      << mthd_id << L"; mthd_name='" << method->GetName() << L"'; params="
      << method->GetParamCount() << L" ----------" << std::endl;
#endif
    // code buffer memory
    code_buf_max = BUFFER_SIZE;
    code = (unsigned char*)malloc(code_buf_max);
    
    // float_consts memory
#ifdef _WIN64
    float_consts = (double*)VirtualAlloc(nullptr, sizeof(double) * MAX_DBLS, MEM_COMMIT, PAGE_READWRITE);
    if(!float_consts) {
      std::wcerr << L"Unable to allocate JIT memory for float_consts!" << std::endl;
      exit(1);
    }
#else
    if(posix_memalign((void**)& float_consts, PAGE_SIZE, sizeof(double) * MAX_DBLS)) {
      std::wcerr << L"Unable to reallocate JIT memory!" << std::endl;
      exit(1);
    }
#endif    
    local_space = floats_index = instr_index = code_index = epilog_index = instr_count = 0;
    float_consts[floats_index++] = 0.0;

    rax_reg = new RegisterHolder(RAX);
#ifdef _WIN64
    // general use registers. R8-R11 are caller-saved, need no prologue save,
    // and every path that calls out (the interpreter callback, native calls,
    // the write barrier) spills or pushes them like RCX/RDX. Four registers
    // meant any expression with five live values spilled -- or the method
    // fell back to the interpreter. POSIX gets the same registers via aux_regs.
    aval_regs.push_back(new RegisterHolder(R11));
    aval_regs.push_back(new RegisterHolder(R10));
    aval_regs.push_back(new RegisterHolder(R9));
    aval_regs.push_back(new RegisterHolder(R8));
    aval_regs.push_back(new RegisterHolder(RDX));
    aval_regs.push_back(new RegisterHolder(RCX));
    aval_regs.push_back(new RegisterHolder(RBX));
    aval_regs.push_back(rax_reg);
    // aux general use registers
    aux_regs.push(new RegisterHolder(RSI));
    aux_regs.push(new RegisterHolder(RDI));
    // floating point registers
    aval_xregs.push_back(new RegisterHolder(XMM15));
    aval_xregs.push_back(new RegisterHolder(XMM14));
    aval_xregs.push_back(new RegisterHolder(XMM13));
    aval_xregs.push_back(new RegisterHolder(XMM12));
    aval_xregs.push_back(new RegisterHolder(XMM11));
    aval_xregs.push_back(new RegisterHolder(XMM10));
#ifdef _DEBUG_JIT
    std::wcout << L"Compiling code for Windows AMD64 architecture..." << std::endl;
#endif
#else
    // general use registers
    aval_regs.push_back(new RegisterHolder(RDX));
    aval_regs.push_back(new RegisterHolder(RCX));
    aval_regs.push_back(new RegisterHolder(RBX));
    aval_regs.push_back(rax_reg);
    // aux general use registers
    //        aux_regs.push(new RegisterHolder(RDI));
    //        aux_regs.push(new RegisterHolder(RSI));
    // R13-R15 hold pinned loop locals (F3) and are not allocatable
    // aux_regs.push(new RegisterHolder(R12));
    aux_regs.push(new RegisterHolder(R11));
    aux_regs.push(new RegisterHolder(R10));
    // aux_regs.push(new RegisterHolder(R9));
    aux_regs.push(new RegisterHolder(R8));
    // floating point registers
    aval_xregs.push_back(new RegisterHolder(XMM15));
    aval_xregs.push_back(new RegisterHolder(XMM14));
    aval_xregs.push_back(new RegisterHolder(XMM13));
    aval_xregs.push_back(new RegisterHolder(XMM12));
    aval_xregs.push_back(new RegisterHolder(XMM11));
    aval_xregs.push_back(new RegisterHolder(XMM10));
#ifdef _DEBUG_JIT
    std::wcout << L"Compiling code for Posix AMD64 architecture..." << std::endl;
#endif
#endif

    // process offsets
    ProcessIndices();
    // F3: pick the loop locals that live in registers -- needs the slots, and
    // the prologue needs to know whether to save the registers
    PlanPinRegions();

    // compute extra frame space for inline callees
    long extra_inline_space = 0;
    for(long i = 0; i < method->GetInstructionCount(); ++i) {
      StackInstr* si = method->GetInstruction(i);
      if(si->GetType() == MTHD_CALL) {
        StackMethod* callee = program->GetClass(si->GetOperand())->GetMethod(si->GetOperand2());
        if(CanInlineMethod(callee)) {
          extra_inline_space += ComputeInlineLocalSpace(callee);
        }
      }
    }
    // inline locals start after caller's locals (mirrors ProcessIndices index computation)
    inline_local_offset = -(local_space + TMP_REG_9);
    local_space += extra_inline_space;

    // The frame record block for the native entry (EmitNativePrologue) goes
    // below the locals and the inline space: the StackFrame, its two mem
    // words and the entry kind. Outside the region the collector walks
    // (org_local_space), like the inline space.
    rec_base = -(local_space + TMP_REG_9) - REC_SIZE;
    local_space += REC_SIZE;

    // the outgoing area for the method's native call sites, sized for the
    // widest: a callee's register homes and stack slots, then its arguments,
    // the receiver and (a func-ref call) the func-ref word
    long out_words = 0;
    for(long i = 0; i < method->GetInstructionCount(); ++i) {
      StackInstr* si = method->GetInstruction(i);
      long words = 0;
      if(si->GetType() == MTHD_CALL || si->GetType() == MTHD_CALL_JIT) {
        StackMethod* callee = program->GetClass(si->GetOperand())->GetMethod(si->GetOperand2());
        if(callee) {
          words = callee->GetParamCount() + 1;
        }
      }
      else if(si->GetType() == DYN_MTHD_CALL || si->GetType() == DYN_MTHD_CALL_JIT) {
        words = si->GetOperand() + 2;
      }
      if(words > out_words) {
        out_words = words;
      }
    }
    if(out_words > 0) {
      out_area = OUT_ARGS + out_words * (long)sizeof(size_t);
      while(out_area % 16 != 0) {
        out_area += 8;
      }
    }

    // the frame's size, aligned, once: both prologues reserve it
    local_space += 16;
    while(local_space % 16 != 0) {
      local_space += 8;
    }
    local_space += 8;

    // The two entries (see EmitNativePrologue): the bridge entry at offset
    // 0, then the native entry, meeting at RegisterRoot. `top` addresses
    // the arguments for ProcessParameters and is held across both. A method
    // whose result is a func-ref, two words, has the bridge entry only.
    RegisterHolder* top_holder = GetRegister();
    const Register top = top_holder->GetRegister();
#ifdef _DEBUG_JIT
    // the prologues use R8-R11 and the argument registers as scratch
    assert(top == RAX || top == RBX);
#endif
    long join_patch = -1;
    EmitBridgePrologue(method->GetParamCount(), top, join_patch);
    if(method->GetReturn() != FUNC_TYPE) {
      EmitNativePrologue(method->GetParamCount(), top);
    }
    PatchForwardJump(join_patch);

    // register root
    RegisterRoot();

    // translate parameters
    ProcessParameters(method->GetParamCount(), top);
    ReleaseRegister(top_holder);
    // translate program
    ProcessInstructions();
    if(!compile_success) {
      if(JitReportEnabled()) {
        std::wcerr << L"[jit] " << method->GetName() << L": compile failed at instruction " << instr_index << std::endl;
      }
#ifdef _WIN64
      VirtualFree(float_consts, 0, MEM_RELEASE);
#else
      free(float_consts);
#endif
      float_consts = nullptr;

      free(code);
      code = nullptr;

      for(JitVirtualSite* site : virtual_sites) {
        delete site;
      }
      virtual_sites.clear();

      // On mid-compilation failure, register holders may be shared across
      // multiple lists (aval_regs, used_regs, aux_regs, working_stack).
      // Deleting from one list risks double-free when the destructor
      // iterates another. Clear all lists without deleting holders
      // (small leak of ~48 bytes per failed method is acceptable).
      working_stack.clear();
      local_reg_cache.clear();
      local_xreg_cache.clear();
      aval_regs.clear();
      used_regs.clear();
      aval_xregs.clear();
      used_xregs.clear();
      while(!aux_regs.empty()) {
        aux_regs.pop();
      }

      return false;
    }

    // F3: a jump that leaves a pinned loop goes through a stub that stores the
    // pinned locals first; the stubs sit after the method body
    EmitPinExitStubs();
    // show content
    std::unordered_map<long, StackInstr*>::iterator iter;
    for(iter = jump_table.begin(); iter != jump_table.end(); ++iter) {
      StackInstr* instr = iter->second;
      const long src_offset = iter->first;
      const long dest_index = instr->GetOperand();
      long dest_offset = method->GetInstruction(dest_index)->GetOffset();
      // F3: an exit jump lands on its stub; a jump from inside a pinned loop to
      // the loop's header skips the entry loads
      auto stub = pin_exit_stub_offsets.find(src_offset);
      if(stub != pin_exit_stub_offsets.end()) {
        dest_offset = stub->second;
      }
      else if(!pin_regions.empty()) {
        auto src_index = instr_index_of.find(instr);
        if(src_index != instr_index_of.end()) {
          const int region = PinRegionContaining(src_index->second);
          if(region >= 0 && dest_index == pin_regions[region].header) {
            dest_offset = pin_regions[region].loop_offset;
          }
        }
      }
      const long offset = dest_offset - src_offset - 4; // 64-bit jump offset
      memcpy(&code[(size_t)src_offset], &offset, 4);
#ifdef _DEBUG_JIT
      std::wcout << L"jump update: src=" << src_offset
        << L"; dest=" << dest_offset << std::endl;
#endif
    }

    // F8: each table entry is the target's offset relative to its table
    for(const TableEntry& entry : table_entries) {
      const long dest_offset = method->GetInstruction(entry.target_index)->GetOffset();
      const int32_t rel = (int32_t)(dest_offset - entry.table_offset);
      memcpy(&code[(size_t)entry.entry_offset], &rel, sizeof(rel));
    }

    for(size_t i = 0; i < nil_deref_offsets.size(); ++i) {
      const long index = nil_deref_offsets[i];
      long offset = nil_deref_handler_index - (index + 4);
      memcpy(&code[(size_t)index], &offset, 4);
    }

    for(size_t i = 0; i < bounds_less_offsets.size(); ++i) {
      const long index = bounds_less_offsets[i];
      long offset = bounds_less_handler_index - (index + 4);
      memcpy(&code[(size_t)index], &offset, 4);
    }

    for(size_t i = 0; i < bounds_greater_offsets.size(); ++i) {
      const long index = bounds_greater_offsets[i];
      long offset = bounds_greater_handler_index - (index + 4);
      memcpy(&code[(size_t)index], &offset, 4);
    }

    for(size_t i = 0; i < div_by_zero_offsets.size(); ++i) {
      const long index = div_by_zero_offsets[i];
      long offset = div_by_zero_handler_index - (index + 4);
      memcpy(&code[(size_t)index], &offset, 4);
    }
#ifdef _DEBUG_JIT
    std::wcout << L"Caching JIT code: actual=" << code_index
      << L", buffer=" << code_buf_max << L" byte(s)" << std::endl;
#endif
#ifdef _WIN64
    // phase 2: a method that never took an XMM pool register leaves
    // XMM10-XMM15 untouched, so it need not save and restore them. Both
    // blocks become a two-byte jump over themselves (each is 46 bytes: the
    // rsp adjustment and six movdqu); the stack stays 16-byte aligned since
    // the adjustment is 96 both ways, and nothing jumps into either block.
    // A method with several returns has several epilogues (RTRN emits one
    // each) and two entries have two prologues, so every block is patched.
    if(!xmm_pool_used && !xmm_save_indices.empty() && !xmm_restore_indices.empty()) {
      for(const long save_index : xmm_save_indices) {
        code[(size_t)save_index] = 0xeb;
        code[(size_t)save_index + 1] = (unsigned char)(xmm_save_size - 2);
      }
      for(const long restore_index : xmm_restore_indices) {
        code[(size_t)restore_index] = 0xeb;
        code[(size_t)restore_index + 1] = (unsigned char)(xmm_restore_size - 2);
      }
    }
#endif
    // store compiled code
    NativeCode* native_code = new NativeCode(page_manager->GetPage(code, code_index), code_index, float_consts, native_entry_offset);
    native_code->SetVirtualSites(virtual_sites);
    method->SetNativeCode(native_code);

    free(code);
    code = nullptr;

#ifdef _TIMING
    std::wcout << L"JIT compiling: method='" << method->GetName() << L"', time="
      << (double)(clock() - start) / CLOCKS_PER_SEC << L" second(s)." << std::endl;
#endif

    compile_success = true;
  }

  return compile_success;
}

/**
 * JitExecutor class
 */
StackProgram* JitRuntime::program;

void JitRuntime::Initialize(StackProgram* p) 
{
  program = p;
}

// Executes machine code
long JitRuntime::Execute(StackMethod* method, size_t* inst, size_t* op_stack, size_t* stack_pos, StackFrame** call_stack, long* call_stack_pos, StackFrame* frame) 
{
  const long cls_id = method->GetClass()->GetId();
  const long mthd_id = method->GetId();
  NativeCode* native_code = method->GetNativeCode();

#ifdef _DEBUG_JIT
  std::wcout << L"=== MTHD_CALL (native): id=" << cls_id << L"," << mthd_id << L"; name='" << method->GetName()
        << L"'; self=" << inst << L"(" << (size_t)inst << L"); std::stack=" << op_stack << L"; stack_pos="
        << (*stack_pos) << L"; params=" << method->GetParamCount() << L"; code=" << (size_t*)native_code->GetCode() << L"; code_index="
        << native_code->GetSize() << L" ===" << std::endl;
  assert((*stack_pos) >= method->GetParamCount());
#endif

  // create function
  jit_fun_ptr jit_fun = (jit_fun_ptr)native_code->GetCode();

  // execute
  const long status = jit_fun(cls_id, mthd_id, method->GetClass()->GetClassMemory(), inst, op_stack,
                              stack_pos, call_stack, call_stack_pos, &(frame->jit_mem), &(frame->jit_offset), frame->mem);

#ifdef _DEBUG_JIT
  std::wcout << L"JIT return=: " << status << std::endl;
#endif 

  return status;
}

/**
 * RegInstr class
 */
RegInstr::RegInstr(StackInstr* si)
{
  switch(si->GetType()) {
  case LOAD_CHAR_LIT:
    type = IMM_INT;
    operand = si->GetOperand();
    break;

  case LOAD_INT_LIT:
    type = IMM_INT;
    operand = si->GetInt64Operand();
    break;

  case LOAD_CLS_MEM:
    type = MEM_INT;
    operand = CLASS_MEM;
    break;

  case LOAD_INST_MEM:
    type = MEM_INT;
    operand = INSTANCE_MEM;
    break;

  case LOAD_LOCL_INT_VAR:
  case LOAD_CLS_INST_INT_VAR:
  case STOR_LOCL_INT_VAR:
  case STOR_CLS_INST_INT_VAR:
  case LOAD_FUNC_VAR:
  case STOR_FUNC_VAR:
  case COPY_LOCL_INT_VAR:
  case COPY_CLS_INST_INT_VAR:
    type = MEM_INT;
    operand = si->GetOperand3();
    break;

  case LOAD_FLOAT_VAR:
  case STOR_FLOAT_VAR:
  case COPY_FLOAT_VAR:
    type = MEM_FLOAT;
    operand = si->GetOperand3();
    break;

  default:
#ifdef _DEBUG_JIT
    assert(false);
#endif
    break;
  }
  instr = si;
  holder = nullptr;
}

/**
 * Manage executable buffers of memory
 */
PageHolder::PageHolder()
{
  index = 0;
  available = PAGE_SIZE;

#ifdef _WIN64    
  buffer = (unsigned char*)VirtualAlloc(nullptr, PAGE_SIZE, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
  if(!buffer) {
    std::wcerr << L"Unable to allocate JIT memory!" << std::endl;
    exit(1);
  }
#else
  if(posix_memalign((void**)& buffer, PAGE_SIZE, PAGE_SIZE)) {
    std::wcerr << L"Unable to allocate JIT memory!" << std::endl;
    exit(1);
  }

  if(mprotect(buffer, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC) < 0) {
    std::wcerr << L"Unable to mprotect" << std::endl;
    exit(1);
  }
#endif
}

PageManager::PageManager()
{
  for(int i = 0; i < 4; ++i) {
    holders.push_back(new PageHolder(PAGE_SIZE * (i + 1)));
  }
}

PageManager::~PageManager()
{
  while(!holders.empty()) {
    PageHolder* tmp = holders.front();
    holders.erase(holders.begin());
    // delete
    delete tmp;
    tmp = nullptr;
  }
}

unsigned char* PageManager::GetPage(unsigned char* code, int32_t size)
{
  // page_manager is a process-global singleton, but several mutator threads can
  // JIT-compile DIFFERENT methods concurrently (the compile-once election only
  // serializes a single method). Without this lock, concurrent GetPage calls race
  // the shared `holders` vector (push_back -> realloc/UAF) and a holder's bump
  // index (two methods handed overlapping code regions -> corrupt machine code ->
  // SIGILL/SIGSEGV with garbage values). Serialize code-page allocation.
  static std::mutex page_mutex;
  std::lock_guard<std::mutex> lock(page_mutex);

  bool placed = false;

  unsigned char* temp = nullptr;
  for(size_t i = 0; !placed && i < holders.size(); ++i) {
    PageHolder* holder = holders[i];
    if(holder->CanAddCode(size)) {
      temp = holder->AddCode(code, size);
      placed = true;
    }
  }

  if(!placed) {
    PageHolder* buffer = new PageHolder(size);
    temp = buffer->AddCode(code, size);
    holders.push_back(buffer);
  }

  return temp;
}
