/***************************************************************************
 * JIT compiler for the x86 architecture.
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

#ifndef __REG_ALLOC_H__
#define __REG_ALLOC_H__

#ifndef _WIN32
#include "../../os/posix/posix.h"
#include <sys/mman.h>
#include <errno.h>
#else
#include "../../os/windows/windows.h"
#endif

#include "../../common.h"
#include "../../interpreter.h"
#include "../../memory.h"

using namespace std;

namespace Runtime {
  // offsets for Intel (IA-32) addresses
#define CLS_ID 8
#define MTHD_ID 12
#define CLASS_MEM 16
#define INSTANCE_MEM 20
#define OP_STACK 24
#define STACK_POS 28
#define RTRN_VALUE 32
  // float temps
#define TMP_XMM_0 -8
#define TMP_XMM_1 -16
#define TMP_XMM_2 -24
  // integer temps
#define TMP_REG_0 -28
#define TMP_REG_1 -32
#define TMP_REG_2 -36
#define TMP_REG_3 -40
#define TMP_REG_4 -44
#define TMP_REG_5 -48

#define MAX_DBLS 64

  // register type
  typedef enum _RegType { 
    IMM_32 = -4000,
    REG_32,
    MEM_32,
    IMM_64,
    REG_64,
    MEM_64,
  } RegType;
  
  // general and SSE (x86) registers
  typedef enum _Register { 
    EAX = -5000, 
    EBX, 
    ECX, 
    EDX, 
    EDI,
    ESI,
    EBP,
    ESP,
    XMM0, 
    XMM1,
    XMM2,
    XMM3,
    XMM4,
    XMM5,
    XMM6,
    XMM7
  } Register;

  /********************************
   * RegisterHolder class
   ********************************/
  class RegisterHolder {
    Register reg;
  
  public:
    RegisterHolder(Register r) {
      reg = r;
    }

    ~RegisterHolder() {
    }

    Register GetRegister() {
      return reg;
    }
  };

  /********************************
   * RegInstr class
   ********************************/
  class RegInstr {
    RegType type;
    long operand;
    RegisterHolder* holder;
    StackInstr* instr;
    
  public:    
    RegInstr(RegisterHolder* h) {
      if(h->GetRegister() < XMM0) {
	type = REG_32;
      }
      else {
	type = REG_64;
      }
      holder = h;
      instr = NULL;
    }
  
    RegInstr(StackInstr* si, double* da) {
      type = IMM_64;
      operand = (long)da;
      holder = NULL;
      instr = NULL;
    }
  
    RegInstr(StackInstr* si) {
      switch(si->GetType()) {
      case LOAD_INT_LIT:
	type = IMM_32;
	operand = si->GetOperand();
	break;

      case LOAD_INST_MEM:
	type = MEM_32;
	operand = INSTANCE_MEM;
	break;

      case LOAD_INT_VAR:
      case STOR_INT_VAR:
      case COPY_INT_VAR:
	type = MEM_32;
	operand = si->GetOperand3();
	break;

      case LOAD_FLOAT_VAR:
      case STOR_FLOAT_VAR:
      case COPY_FLOAT_VAR:
	type = MEM_64;
	operand = si->GetOperand3();
	break;

      default:
#ifdef _DEBUG
	assert(false);
#endif
	break;
      }
      instr = si;
      holder = NULL;
    }
  
    ~RegInstr() {
    }
  
    StackInstr* GetInstruction() {
      return instr;
    }

    RegisterHolder* GetRegister() {
      return holder;
    }

    void SetType(RegType t) {
      type = t;
    }
  
    RegType GetType() {
      return type;
    }

    void SetOperand(int32_t o) {
      operand = o;
    }

    int32_t GetOperand() {
      return operand;
    }
  };
  
  typedef void (*jit_fun_ptr)(int32_t cls_id, int32_t mthd_id, 
			      int32_t* cls_mem, int32_t* inst, 
			      int32_t* op_stack, int32_t *stack_pos, 
			      int32_t &rtrn_value);
  
  class JitCompilerIA32 {
    static StackProgram* program;
    list<RegInstr*> working_stack;
    vector<RegisterHolder*> aval_regs;
    list<RegisterHolder*> used_regs;
    stack<RegisterHolder*> aux_regs;
    vector<RegisterHolder*> aval_xregs;
    list<RegisterHolder*> used_xregs;
    map<int32_t, StackInstr*> jump_table;
    int32_t local_space;
    
    StackMethod* mthd;
    int32_t instr_count;
    BYTE_VALUE* code;
    int32_t code_index;    
    FLOAT_VALUE* floats;
    int32_t floats_index;
    int32_t instr_index;
    int32_t code_buf_max;
    bool compile_success;

    // setup and teardown
    void Prolog();
    void Epilog(int32_t imm);
    
    // stack conversion operations
    void ProcessParameters(int32_t count);
    void RegisterRoot();
    void UnregisterRoot();
    void ProcessInstructions();
    void ProcessLiteral(StackInstr* instruction) ;
    void ProcessVariable(StackInstr* instruction);
    void ProcessLoad(StackInstr* instr);
    void ProcessStore(StackInstr* instruction);
    void ProcessCopy(StackInstr* instr);
    void ProcessIntCalculation(StackInstr* instruction);
    void ProcessIntShift(StackInstr* instruction);
    void ProcessFloatCalculation(StackInstr* instruction);
    void ProcessReturn(int32_t params = -1);
    void ProcessStackCallback(int32_t instr_id, StackInstr* instr, 
			      int32_t &instr_index, int32_t params);
    // return 
    void ProcessIntCallParameter();
    void ProcessFloatCallParameter(); 
    void ProcessReturnParameters(bool is_int);
    void ProcessLoadByteElement(StackInstr* instr);
    void ProcessStoreByteElement(StackInstr* instr);
    void ProcessLoadIntElement(StackInstr* instr);
    void ProcessStoreIntElement(StackInstr* instr);
    void ProcessLoadFloatElement(StackInstr* instr);
    void ProcessStoreFloatElement(StackInstr* instr);
    void ProcessJump(StackInstr* instr);
    void ProcessLogic(StackInstr* instr);
    void ProcessFloor(StackInstr* instr);
    void ProcessCeiling(StackInstr* instr);
    void ProcessFloatToInt(StackInstr* instr);
    void ProcessIntToFloat(StackInstr* instr);
    // execute
    int32_t ExecuteMachineCode(int32_t cls_id, int32_t mthd_id, int32_t* inst,
			       BYTE_VALUE* code, const int32_t code_size, 
			       int32_t* op_stack, int32_t *stack_pos);

    /********************************
     * Add byte code to buffer
     ********************************/
    void AddMachineCode(BYTE_VALUE b) {
      if(code_index == code_buf_max) {
#ifndef _WIN32
	BYTE_VALUE* tmp = (BYTE_VALUE*)valloc(code_buf_max * 2);
	memcpy(tmp, code, code_index);
	free(code);
	code = tmp;
#else
	code = (BYTE_VALUE*)realloc(code, code_buf_max * 2); 
#endif
	code_buf_max *= 2;
      }
      code[code_index++] = b;
    }
    
    /********************************
     * Encodes and writes out 32-bit
     * integer values
     ********************************/
    void AddImm(int32_t imm) {
      BYTE_VALUE buffer[sizeof(int32_t)];
      ByteEncode32(buffer, imm);
      for(int32_t i = 0; i < sizeof(int32_t); i++) {
	AddMachineCode(buffer[i]);
      }
    }
    
    /********************************
     * Caculates the IA-32 MOD R/M
     * offset
     ********************************/
    BYTE_VALUE ModRM(Register eff_adr, Register mod_rm) {
      BYTE_VALUE byte;

      switch(mod_rm) {
      case ESP:
      case XMM4:
	byte = 0xa0;
	break;

      case EAX:
      case XMM0:
	byte = 0x80;
	break;

      case EBX:
      case XMM3:
	byte = 0x98;
	break;

      case ECX:
      case XMM1:
	byte = 0x88;
	break;

      case EDX:
      case XMM2:
	byte = 0x90;
	break;

      case EDI:
      case XMM7:
	byte = 0xb8;
	break;

      case ESI:
      case XMM6:
	byte = 0xb0;
	break;

      case EBP:
      case XMM5:
	byte = 0xa8;
	break;
      }

      switch(eff_adr) {
      case EAX:
      case XMM0:
	break;

      case EBX:
      case XMM3:
	byte += 3;
	break;

      case ECX:
      case XMM1:
	byte += 1;
	break;

      case EDX:
      case XMM2:
	byte += 2;
	break;

      case EDI:
      case XMM7:
	byte += 7;
	break;

      case ESI:
      case XMM6:
	byte += 6;
	break;

      case EBP:
      case XMM5:
	byte += 5;
	break;

      case XMM4:
	byte += 4;
	break;
	
	// should never happen for esp
      case ESP:
	cerr << ">>> invalid register reference <<<" << endl;
	exit(1);
	break;
      }

      return byte;
    }

    /********************************
     * Returns the name of a register
     ********************************/
    string GetRegisterName(Register reg) {
      switch(reg) {
      case EAX:
	return "eax";

      case EBX:
	return "ebx";

      case ECX:
	return "ecx";

      case EDX:
	return "edx";

      case EDI:
	return "edi";

      case ESI:
	return "esi";

      case EBP:
	return "ebp";

      case ESP:
	return "esp";

      case XMM0:
	return "xmm0";

      case XMM1:
	return "xmm1";

      case XMM2:
	return "xmm2";

      case XMM3:
	return "xmm3";

      case XMM4:
	return "xmm4";

      case XMM5:
	return "xmm5";

      case XMM6:
	return "xmm6";
	
      case XMM7:
	return "xmm7";
      }

      return "unknown";
    }

    /********************************
     * Encodes a byte array with a
     * 32-bit value
     ********************************/
    void ByteEncode32(BYTE_VALUE buffer[], int32_t value) {
      memcpy(buffer, &value, sizeof(int32_t));
    }
    
    /********************************
     * Encodes an array with the 
     * binary ID of a register
     ********************************/
    void RegisterEncode3(BYTE_VALUE& code, int32_t offset, Register reg) {
#ifdef _DEBUG
      assert(offset == 2 || offset == 5);
#endif
      
      BYTE_VALUE reg_id;
      switch(reg) {
      case EAX:
      case XMM0:
	reg_id = 0x0;
	break;

      case EBX:
      case XMM3:
	reg_id = 0x3;     
	break;

      case ECX:
      case XMM1:
	reg_id = 0x1;
	break;

      case EDX:
      case XMM2:
	reg_id = 0x2;
	break;

      case EDI:
      case XMM7:
	reg_id = 0x7;
	break;

      case ESI:
      case XMM6:
	reg_id = 0x6;
	break;

      case ESP:
      case XMM4:
	reg_id = 0x4;
	break;

      case EBP:
      case XMM5:
	reg_id = 0x5;
	break;
      }

      if(offset == 2) {
	reg_id = reg_id << 3;
      }
      code = code | reg_id;
    }

    inline void CheckNilDereference(Register reg) {
      const int offset = 14;
      cmp_imm_reg(0, reg);
#ifdef _DEBUG
      cout << "  " << (++instr_count) << ": [jne $" << offset << "]" << endl;
#endif
      // jump not equal
      AddMachineCode(0x0F);
      AddMachineCode(0x85);
      AddImm(offset);
      Epilog(0);
      move_imm_reg(0, EAX);
    }
    
    /********************************
     * Gets an avaiable register from
     * the pool of registers
     ********************************/
    RegisterHolder* GetRegister(bool use_aux = true) {
      RegisterHolder* holder;
      if(aval_regs.empty()) {
	if(use_aux && !aux_regs.empty()) {
	  holder = aux_regs.top();
	  aux_regs.pop();
	}
	else {
	  compile_success = false;
#ifdef _DEBUG
	  cout << ">>> No general registers avaiable! <<<" << endl;
#endif
	  aux_regs.push(new RegisterHolder(EAX));
	  holder = aux_regs.top();
	  aux_regs.pop();
	}
      }
      else {
        holder = aval_regs.back();
        aval_regs.pop_back();
        used_regs.push_back(holder);
      }
#ifdef _VERBOSE
      cout << "\t * allocating " << GetRegisterName(holder->GetRegister())
	   << " *" << endl;
#endif

      return holder;
    }

    /********************************
     * Returns a register to the pool
     ********************************/
    void ReleaseRegister(RegisterHolder* h) {
#ifdef _VERBOSE
      cout << "\t * releasing " << GetRegisterName(h->GetRegister())
	   << " *" << endl;
#endif

#ifdef _DEBUG
      assert(h->GetRegister() < XMM0);
      for(int i  = 0; i < aval_regs.size(); i++) {
	assert(h != aval_regs[i]);
      }
#endif

      if(h->GetRegister() == EDI || h->GetRegister() == ESI) {
	aux_regs.push(h);
      }
      else {
	aval_regs.push_back(h);
	used_regs.remove(h);
      }
    }

    /********************************
     * Gets an avaiable register from
     * the pool of registers
     ********************************/
    RegisterHolder* GetXmmRegister() {
      RegisterHolder* holder;
      if(aval_xregs.empty()) {
	compile_success = false;
#ifdef _DEBUG
	cout << ">>> No XMM registers avaiable! <<<" << endl;
#endif
	aval_xregs.push_back(new RegisterHolder(XMM0));
	holder = aval_xregs.back();
        aval_xregs.pop_back();
        used_xregs.push_back(holder);
      }
      else {
        holder = aval_xregs.back();
        aval_xregs.pop_back();
        used_xregs.push_back(holder);
      }
#ifdef _VERBOSE
      cout << "\t * allocating " << GetRegisterName(holder->GetRegister())
	   << " *" << endl;
#endif

      return holder;
    }

    /********************************
     * Returns a register to the pool
     ********************************/
    void ReleaseXmmRegister(RegisterHolder* h) {
#ifdef _DEBUG
      assert(h->GetRegister() >= XMM0);
      for(int i = 0; i < aval_xregs.size(); i++) {
	assert(h != aval_xregs[i]);
      }
#endif
      
#ifdef _VERBOSE
      cout << "\t * releasing: " << GetRegisterName(h->GetRegister())
	   << " * " << endl;
#endif
      aval_xregs.push_back(h);
      used_xregs.remove(h);
    }

    RegisterHolder* GetStackPosRegister() {
      RegisterHolder* op_stack_holder = GetRegister();
      move_mem_reg(OP_STACK, EBP, op_stack_holder->GetRegister());
      return op_stack_holder;
    }

    // move instructions
    void move_reg_mem8(Register src, int32_t offset, Register dest);
    void move_mem8_reg(int32_t offset, Register src, Register dest);
    void move_imm_mem8(int32_t imm, int32_t offset, Register dest);
    void move_reg_reg(Register src, Register dest);
    void move_reg_mem(Register src, int32_t offset, Register dest);
    void move_mem_reg(int32_t offset, Register src, Register dest);
    void move_imm_memx(RegInstr* instr, int32_t offset, Register dest);
    void move_imm_mem(int32_t imm, int32_t offset, Register dest);
    void move_imm_reg(int32_t imm, Register reg);
    void move_imm_xreg(RegInstr* instr, Register reg);
    void move_mem_xreg(int32_t offset, Register src, Register dest);
    void move_xreg_mem(Register src, int32_t offset, Register dest);
    void move_xreg_xreg(Register src, Register dest);

    // math instructions
    void math_imm_reg(int32_t imm, Register reg, InstructionType type);    
    void math_imm_xreg(RegInstr* instr, Register reg, InstructionType type);
    void math_reg_reg(Register src, Register dest, InstructionType type);
    void math_xreg_xreg(Register src, Register dest, InstructionType type);
    void math_mem_reg(int32_t offset, Register reg, InstructionType type);
    void math_mem_xreg(int32_t offset, Register reg, InstructionType type);

    // logical
    void and_imm_reg(int32_t imm, Register reg);
    void and_reg_reg(Register src, Register dest);
    void and_mem_reg(int32_t offset, Register src, Register dest);
    void or_imm_reg(int32_t imm, Register reg);
    void or_reg_reg(Register src, Register dest);
    void or_mem_reg(int32_t offset, Register src, Register dest);
    void xor_reg_reg(Register src, Register dest);

    // add instructions
    void add_imm_mem(int32_t imm, int32_t offset, Register dest);    
    void add_imm_reg(int32_t imm, Register reg);    
    void add_imm_xreg(RegInstr* instr, Register reg);
    void add_xreg_xreg(Register src, Register dest);
    void add_mem_reg(int32_t offset, Register src, Register dest);
    void add_mem_xreg(int32_t offset, Register src, Register dest);
    void add_reg_reg(Register src, Register dest);

    // sub instructions
    void sub_imm_xreg(RegInstr* instr, Register reg);
    void sub_xreg_xreg(Register src, Register dest);
    void sub_mem_xreg(int32_t offset, Register src, Register dest);
    void sub_imm_reg(int32_t imm, Register reg);
    void sub_imm_mem(int32_t imm, int32_t offset, Register dest);
    void sub_reg_reg(Register src, Register dest);
    void sub_mem_reg(int32_t offset, Register src, Register dest);

    // mul instructions
    void mul_imm_xreg(RegInstr* instr, Register reg);
    void mul_xreg_xreg(Register src, Register dest);
    void mul_mem_xreg(int32_t offset, Register src, Register dest);
    void mul_imm_reg(int32_t imm, Register reg);
    void mul_reg_reg(Register src, Register dest);
    void mul_mem_reg(int32_t offset, Register src, Register dest);

    // div instructions
    void div_imm_xreg(RegInstr* instr, Register reg);
    void div_xreg_xreg(Register src, Register dest);
    void div_mem_xreg(int32_t offset, Register src, Register dest);
    void div_imm_reg(int32_t imm, Register reg, bool is_mod = false);
    void div_reg_reg(Register src, Register dest, bool is_mod = false);
    void div_mem_reg(int32_t offset, Register src, Register dest, bool is_mod = false);

    // compare instructions
    void cmp_reg_reg(Register src, Register dest);
    void cmp_mem_reg(int32_t offset, Register src, Register dest);
    void cmp_imm_reg(int32_t imm, Register reg);
    void cmp_xreg_xreg(Register src, Register dest);
    void cmp_mem_xreg(int32_t offset, Register src, Register dest);
    void cmp_imm_xreg(RegInstr* instr, Register reg);
    void cmov_reg(Register reg, InstructionType oper);
    
    // inc/dec instructions
    void dec_reg(Register dest);
    void dec_mem(int32_t offset, Register dest);
    void inc_mem(int32_t offset, Register dest);
    
    // shift instructions
    void shl_reg(Register dest, int32_t value);
    void shl_mem(int32_t offset, Register src, int32_t value);
    void shr_reg(Register dest, int32_t value);
    void shr_mem(int32_t offset, Register src, int32_t value);
    
    // push/pop instructions
    void push_imm(int32_t value);
    void push_reg(Register reg);
    void pop_reg(Register reg);
    void push_mem(int32_t offset, Register src);
    
    // type conversion instructions
    void round_imm_xreg(RegInstr* instr, Register reg, bool is_floor);
    void round_mem_xreg(int32_t offset, Register src, Register dest, bool is_floor);
    void round_xreg_xreg(Register src, Register dest, bool is_floor);
    void cvt_xreg_reg(Register src, Register dest);
    void cvt_imm_reg(RegInstr* instr, Register reg);
    void cvt_mem_reg(int32_t offset, Register src, Register dest);
    void cvt_reg_xreg(Register src, Register dest);
    void cvt_imm_xreg(RegInstr* instr, Register reg);
    void cvt_mem_xreg(int32_t offset, Register src, Register dest);
    
    // function call instruction
    void call_reg(Register reg);
    
    static int32_t PopInt(int32_t* op_stack, int32_t *stack_pos) {
      int32_t value = op_stack[--(*stack_pos)];
#ifdef _DEBUG
      cout << "\t[pop_i: value=" << (int32_t*)value << "(" << value << ")]" << "; pos=" << (*stack_pos) << endl;
#endif

      return value;
    }

    static void PushInt(int32_t* op_stack, int32_t *stack_pos, int32_t value) {
      op_stack[(*stack_pos)++] = value;
#ifdef _DEBUG
      cout << "\t[push_i: value=" << (int32_t*)value << "(" << value << ")]" << "; pos=" << (*stack_pos) << endl;
#endif
    }

    // TODO: return value and unwind whole program
    // TOOD: time to refactor... too large!!!
    /********************************
     * Process call backs from ASM
     * code
     ********************************/
    static void StackCallback(const int32_t instr_id, StackInstr* instr, const int32_t cls_id, 
			      const int32_t mthd_id, int32_t* inst, int32_t* op_stack, 
			      int32_t *stack_pos, const int32_t ip) {
#ifdef _DEBUG
      cout << "Stack Call: instr=" << instr_id
	   << ", oper_1=" << instr->GetOperand() << ", oper_2=" << instr->GetOperand2() 
	   << ", oper_3=" << instr->GetOperand3() << ", self=" << inst << "(" << (long)inst << "), stack=" 
	   << op_stack << ", stack_addr=" << stack_pos << ", stack_pos=" << (*stack_pos) << endl;
#endif
      switch(instr_id) {
      case MTHD_CALL: {
#ifdef _DEBUG
        cout << "jit oper: MTHD_CALL: cls=" << instr->GetOperand() << ", mthd=" << instr->GetOperand2() << endl;
#endif
	StackInterpreter intpr;
	intpr.Execute((long*)op_stack, (long*)stack_pos, ip, program->GetClass(cls_id)->GetMethod(mthd_id), (long*)inst, true);
      }
	break;

      case NEW_BYTE_ARY: {
	int32_t indices[8];
	int32_t value = PopInt(op_stack, stack_pos);
	int32_t size = value;
	indices[0] = value;
	int32_t dim = 1;
	for(int32_t i = 1; i < instr->GetOperand(); i++) {
	  int32_t value = PopInt(op_stack, stack_pos);
	  size *= value;
	  indices[dim++] = value;
	}
	int32_t* mem = (int32_t*)MemoryManager::Instance()->AllocateArray(size + ((dim + 2) * sizeof(int32_t)), BYTE_ARY_TYPE, (long*)op_stack, *stack_pos);
	mem[0] = size;
	mem[1] = dim;
	memcpy(mem + 2, indices, dim * sizeof(int32_t));
  PushInt(op_stack, stack_pos, (int32_t)mem);
	
#ifdef _DEBUG
	cout << "jit oper: NEW_BYTE_ARY: dim=" << dim << "; size=" << size 
	     << "; index=" << (*stack_pos) << "; mem=" << mem << endl;
#endif
      }
	break;

      case NEW_INT_ARY: {
	int32_t indices[8];
	int32_t value = PopInt(op_stack, stack_pos);
	int32_t size = value;
	indices[0] = value;
	int32_t dim = 1;
	for(int32_t i = 1; i < instr->GetOperand(); i++) {
	  int32_t value = PopInt(op_stack, stack_pos);
	  size *= value;
	  indices[dim++] = value;
	}
	int32_t* mem = (int32_t*)MemoryManager::
	  Instance()->AllocateArray(size + dim + 2, INT_TYPE, (long*)op_stack, *stack_pos);
#ifdef _DEBUG
	cout << "jit oper: NEW_INT_ARY: dim=" << dim << "; size=" << size 
	     << "; index=" << (*stack_pos) << "; mem=" << mem << endl;
#endif
	mem[0] = size;
	mem[1] = dim;
	memcpy(mem + 2, indices, dim * sizeof(int32_t));
  PushInt(op_stack, stack_pos, (int32_t)mem);
      }
	break;
	
      case NEW_FLOAT_ARY: {
	int32_t indices[8];
	int32_t value = PopInt(op_stack, stack_pos);
	int32_t size = value;
	indices[0] = value;
	int32_t dim = 1;
	for(int32_t i = 1; i < instr->GetOperand(); i++) {
	  int32_t value = PopInt(op_stack, stack_pos);
	  size *= value;
	  indices[dim++] = value;
	}
	size *= 2;
	int32_t* mem = (int32_t*)MemoryManager::
	  Instance()->AllocateArray(size + dim + 2, INT_TYPE, (long*)op_stack, *stack_pos);
#ifdef _DEBUG
	cout << "jit oper: NEW_FLOAT_ARY: dim=" << dim << "; size=" << size 
	     << "; index=" << (*stack_pos) << "; mem=" << mem << endl; 
#endif
	mem[0] = size / 2;
	mem[1] = dim;
	memcpy(mem + 2, indices, dim * sizeof(int32_t));
  PushInt(op_stack, stack_pos, (int32_t)mem);
      }
	break;
	
      case NEW_OBJ_INST: {
#ifdef _DEBUG
	cout << "jit oper: NEW_OBJ_INST: id=" << instr->GetOperand() << endl; 
#endif
	int32_t* mem = (int32_t*)MemoryManager::Instance()->AllocateObject(instr->GetOperand(), 
									   (long*)op_stack, *stack_pos);
  PushInt(op_stack, stack_pos, (int32_t)mem);
      }
	break;
	
      case OBJ_INST_CAST: {
	int32_t* mem = (int32_t*)PopInt(op_stack, stack_pos);
	int32_t to_id = instr->GetOperand();
#ifdef _DEBUG
	cout << "jit oper: OBJ_INST_CAST: from=" << mem << ", to=" << to_id << endl; 
#endif	
	int32_t result = (int32_t)MemoryManager::Instance()->ValidObjectCast((long*)mem, to_id, 
									     program->GetHierarchy());
	if(!result && mem) {
	  StackClass* to_cls = MemoryManager::Instance()->GetClass((long*)mem);	  
	  cerr << ">>> Invalid object cast: '" << (to_cls ? to_cls->GetName() : "?" )  
	       << "' to '" << program->GetClass(to_id)->GetName() << "' <<<" << endl;
	  exit(1);
	}
  PushInt(op_stack, stack_pos, result);
      }
	break;
	
	////////////////////////
	// trap
	////////////////////////
      case TRAP: 
#ifdef _DEBUG
	cout << "jit oper: TRAP: id=" << op_stack[(*stack_pos) - 1] << endl; 
#endif
	switch(PopInt(op_stack, stack_pos)) {
	  // ---------------- standard i/o ----------------
	case STD_OUT_BOOL: {
	  int32_t value = PopInt(op_stack, stack_pos);
	  cout << ((value == 0) ? "false" : "true");
	}
	  break;
	  
	case STD_OUT_BYTE:
#ifdef _DEBUG
	  cout << "  STD_OUT_BYTE" << endl;
#endif
	  cout <<  (void*)PopInt(op_stack, stack_pos);
	  break;

	case STD_OUT_CHAR:
#ifdef _DEBUG
	  cout << "  STD_OUT_CHAR" << endl;
#endif
	  cout <<  (char)PopInt(op_stack, stack_pos);
	  break;

	case STD_OUT_INT:
#ifdef _DEBUG
	  cout << "  STD_OUT_INT" << endl;
#endif
	  cout <<  PopInt(op_stack, stack_pos);
	  break;

	case STD_OUT_FLOAT: {
#ifdef _DEBUG
	  cout << "  STD_OUT_FLOAT" << endl;
#endif
	  FLOAT_VALUE value;      
	  (*stack_pos) -= 2;
	  memcpy(&value, &op_stack[(*stack_pos)], sizeof(FLOAT_VALUE));
	  cout << value;
	  break;
	}
	  break;

	case STD_OUT_CHAR_ARY: {
	  int32_t* array = (int32_t*)PopInt(op_stack, stack_pos);
	  BYTE_VALUE* str = (BYTE_VALUE*)(array + 3);
#ifdef _DEBUG
	  cout << "  STD_OUT_CHAR_ARY: addr=" << array << "(" << long(array) << ")" << endl;
#endif
	  cout << str;
	}
	  break;

	  // ---------------- file i/o ----------------
	case FILE_OPEN_READ: {
	  long* array = (long*)PopInt(op_stack, stack_pos);
	  array = (long*)array[0];
	  long* instance = (long*)PopInt(op_stack, stack_pos);	
	  const char* name = (char*)(array + 3);
	  instance[0] = (long)File::FileOpen(name, "r");
	}
	  break;
	
	case FILE_OPEN_WRITE: {
	  long* array = (long*)PopInt(op_stack, stack_pos);
	  array = (long*)array[0];
	  long* instance = (long*)PopInt(op_stack, stack_pos);
	  const char* name = (char*)(array + 3);
	  instance[0] = (long)File::FileOpen(name, "a");
	}
	  break;
	
	case FILE_OPEN_READ_WRITE: {
	  long* array = (long*)PopInt(op_stack, stack_pos);
	  array = (long*)array[0];
	  long* instance = (long*)PopInt(op_stack, stack_pos);	
	  const char* name = (char*)(array + 3);
	  instance[0] = (long)File::FileOpen(name, "a+");
	}
	  break;

	case FILE_CLOSE: {
	  long* instance = (long*)PopInt(op_stack, stack_pos);
	  FILE* file = (FILE*)instance[0];
	
	  if(file) {
	    instance[0] = NULL;
	    fclose(file);
	  }
	}
	  break;

	case FILE_IN_STRING: {
	  long* array = (long*)PopInt(op_stack, stack_pos);
	  long* instance = (long*)PopInt(op_stack, stack_pos);	
	  char* buffer = (char*)(array + 3);
	  const long num = array[0] - 1;
	  FILE* file = (FILE*)instance[0];
	  
	  if(file) {
	    fgets(buffer, num, file);	  
	    int end_index = strlen(buffer) - 1;
	    if(end_index >= 0) {
	      if(buffer[end_index] == '\n') {
		buffer[end_index] = '\0';
	      }
	    }
	  }
	}
	  break;

	case FILE_OUT_STRING: {
	  long* array = (long*)PopInt(op_stack, stack_pos);
	  long* instance = (long*)PopInt(op_stack, stack_pos);
	  FILE* file = (FILE*)instance[0];	
	  const char* name = (char*)(array + 3);
	
	  if(file) {
	    fputs(name, file);
	  }
	}
	  break;
	
	case FILE_REWIND: {
	  long* instance = (long*)PopInt(op_stack, stack_pos);
	  FILE* file = (FILE*)instance[0];	
	
	  if(file) {
	    rewind(file);
	  }
	}
	  break;
	  // --- END TRAP --- //
	}
	break;
	
	////////////////////////
	// trap and return value
	////////////////////////
      case TRAP_RTRN:
#ifdef _DEBUG
	cout << "jit oper: TRAP_RTRN: id=" << op_stack[(*stack_pos) - 1] << endl; 
#endif
	switch(PopInt(op_stack, stack_pos)) {
      case LOAD_CLS_INST_ID: {
#ifdef _DEBUG
	  cout << "  LOAD_CLS_INST_ID" << endl;
#endif
	  int32_t value = (int32_t)MemoryManager::Instance()->GetObjectID((long*)PopInt(op_stack, stack_pos));
    PushInt(op_stack, stack_pos, value);
  }
	  break;
	  
	case LOAD_ARY_SIZE: {
#ifdef _DEBUG
	  cout << "  LOAD_ARY_SIZE" << endl;
#endif
	  int32_t* array = (int32_t*)PopInt(op_stack, stack_pos);
    PushInt(op_stack, stack_pos, (int32_t)array[2]);
	}  
	  break;

	case CPY_STR_ARY: {
 	  int32_t index = PopInt(op_stack, stack_pos);
	  BYTE_VALUE* value_str = program->GetCharStrings()[index];
	  // copy array
	  int32_t* array = (int32_t*)PopInt(op_stack, stack_pos);
	  const int32_t size = array[0];
	  BYTE_VALUE* str = (BYTE_VALUE*)(array + 3);
	  for(long i = 0; value_str[i] != '\0' && i < size; i++) {
	    str[i] = value_str[i];
	  }
#ifdef _DEBUG
	  cout << "  CPY_STR_ARY: addr=" << array << "(" << long(array) 
	       << "), from='" << value_str << "', to='" << str << "'" << endl;
#endif
    PushInt(op_stack, stack_pos, (int32_t)array);
	}
	  break;
	  
	case STD_IN_STRING: {
	  long* array = (long*)PopInt(op_stack, stack_pos);
	  char* buffer = (char*)(array + 3);
	  const long num = array[0];
	  cin.getline(buffer, num);
	}
	  break;
	  
	  // ---------------- file i/o ----------------
	case FILE_IN_BYTE: {
	  long* instance = (long*)PopInt(op_stack, stack_pos);
	  FILE* file = (FILE*)instance[0];	  
	  if(file) {
	    if(fgetc(file) == EOF) {
        PushInt(op_stack, stack_pos, 0);
	    }
	    else {
        PushInt(op_stack, stack_pos, 1);
	    }
	  }
	  else {
        PushInt(op_stack, stack_pos, 0);
	  }
	}
	  break;


	case FILE_IN_BYTE_ARY: {
	  long* array = (long*)PopInt(op_stack, stack_pos);
	  const long num = PopInt(op_stack, stack_pos);
	  const long offset = PopInt(op_stack, stack_pos);
	  long* instance = (long*)PopInt(op_stack, stack_pos);
	  FILE* file = (FILE*)instance[0];
	
	  if(file && offset + num < array[0]) {
	    char* buffer = (char*)(array + 3);
	    if(fread(buffer + offset, 1, num, (FILE*)instance[0]) != num) {
        PushInt(op_stack, stack_pos, 0);
	    }
	    else {
        PushInt(op_stack, stack_pos, 1);
	    }
	  }
	  else {
        PushInt(op_stack, stack_pos, 0);
	  }
	}
	  break;

	case FILE_OUT_BYTE: {
	  long value = PopInt(op_stack, stack_pos);
	  long* instance = (long*)PopInt(op_stack, stack_pos);
	  FILE* file = (FILE*)instance[0];
	
	  if(file) {
	    if(fputc(value, file) != value) {
        PushInt(op_stack, stack_pos, 0);
	    }
	    else {
        PushInt(op_stack, stack_pos, 1);
	    }
	  }
	  else {
        PushInt(op_stack, stack_pos, 0);
	  }
	}
	  break;

	case FILE_OUT_BYTE_ARY: {
	  long* array = (long*)PopInt(op_stack, stack_pos);
	  const long num = PopInt(op_stack, stack_pos);
	  const long offset = PopInt(op_stack, stack_pos);
	  long* instance = (long*)PopInt(op_stack, stack_pos);
	  FILE* file = (FILE*)instance[0];
	
	  if(file && offset + num < array[0]) {
	    char* buffer = (char*)(array + 3);
	    if(fwrite(buffer + offset, 1, num, (FILE*)instance[0]) < 0) {
        PushInt(op_stack, stack_pos, 0);
	    }
	    else {
        PushInt(op_stack, stack_pos, 1);
	    }
	  }
	  else {
        PushInt(op_stack, stack_pos, 0);
	  }
	}
	  break;

	case FILE_SEEK: {
	  long pos = PopInt(op_stack, stack_pos);
	  long* instance = (long*)PopInt(op_stack, stack_pos);
	  FILE* file = (FILE*)instance[0];	
	
	  if(file) {
	    if(fseek(file, pos, SEEK_CUR) < 0) {
        PushInt(op_stack, stack_pos, 0);
	    }
	    else {
        PushInt(op_stack, stack_pos, 1);
	    }
	  }
	  else {
        PushInt(op_stack, stack_pos, 0);
	  }
	}
	  break;

	case FILE_EOF: {
	  long* instance = (long*)PopInt(op_stack, stack_pos);
	  FILE* file = (FILE*)instance[0];	
	
	  if(file) {
      PushInt(op_stack, stack_pos, feof(file) != 0);
	  }
	  else {
	    PushInt(op_stack, stack_pos, 1);
	  }
	}
	  break;

	case FILE_OPEN: {
	  long* instance = (long*)PopInt(op_stack, stack_pos);
	  FILE* file = (FILE*)instance[0];	
	
	  if(file) {
	    PushInt(op_stack, stack_pos, 1);
	  }
	  else {
	    PushInt(op_stack, stack_pos, 0);
	  }
	}
	  break;

	case FILE_EXISTS: {
	  long* array = (long*)PopInt(op_stack, stack_pos);
	  array = (long*)array[0];
	  const char* name = (char*)(array + 3);

	  PushInt(op_stack, stack_pos, File::FileExists(name));
	}
	  break;
	
	case FILE_SIZE: {
	  long* array = (long*)PopInt(op_stack, stack_pos);
	  array = (long*)array[0];
	  const char* name = (char*)(array + 3);

	  PushInt(op_stack, stack_pos, File::FileSize(name));
	
	}
	  break;

	case FILE_DELETE: {
	  long* array = (long*)PopInt(op_stack, stack_pos);
	  array = (long*)array[0];
	  const char* name = (char*)(array + 3);

	  if(remove(name) != 0) {
	    PushInt(op_stack, stack_pos, 0);
	  }
	  else {
	    PushInt(op_stack, stack_pos, 1);
	  }
	}
	  break;

	case FILE_RENAME: {
	  long* to = (long*)PopInt(op_stack, stack_pos);
	  to = (long*)to[0];
	  const char* to_name = (char*)(to + 3);

	  long* from = (long*)PopInt(op_stack, stack_pos);
	  from = (long*)from[0];
	  const char* from_name = (char*)(from + 3);
	
	  if(rename(from_name, to_name) != 0) {
	    PushInt(op_stack, stack_pos, 0);
	  }
	  else {
	    PushInt(op_stack, stack_pos, 1);
	  }
	}
	  break;

	  //----------- directory functions -----------
	case DIR_CREATE: {
	  long* array = (long*)PopInt(op_stack, stack_pos);
	  array = (long*)array[0];
	  const char* name = (char*)(array + 3);

	  PushInt(op_stack, stack_pos, File::MakeDir(name));
	}
	  break;
	
	case DIR_EXISTS: {
	  long* array = (long*)PopInt(op_stack, stack_pos);
	  array = (long*)array[0];
	  const char* name = (char*)(array + 3);

	  PushInt(op_stack, stack_pos, File::IsDir(name));
	}
	  break;
	
	case DIR_LIST: {
	  long* array = (long*)PopInt(op_stack, stack_pos);
	  array = (long*)array[0];
	  char* name = (char*)(array + 3);

	  vector<string> files = File::ListDir(name);

	  // create 'System.String' object array
	  long str_obj_array_size = files.size();
	  const long str_obj_array_dim = 1;  
	  long* str_obj_array = (long*)MemoryManager::Instance()->AllocateArray(str_obj_array_size + 
										str_obj_array_dim + 2, 
										INT_TYPE, (long*)op_stack, 
										*stack_pos);
	  str_obj_array[0] = str_obj_array_size;
	  str_obj_array[1] = str_obj_array_dim;
	  str_obj_array[2] = str_obj_array_size;
	  long* str_obj_array_ptr = str_obj_array + 3;
    
	  // create and assign 'System.String' instances to array
	  for(unsigned int i = 0; i < files.size(); i++) {
	    // get value string
	    string &value_str = files[i];
      
	    // create character array
	    const long char_array_size = value_str.size();
	    const long char_array_dim = 1;
	    long* char_array = (long*)MemoryManager::Instance()->AllocateArray(char_array_size + 1 + 
									       ((char_array_dim + 2) * 
										sizeof(long)),  
									       BYTE_ARY_TYPE, 
									       (long*)op_stack, *stack_pos);
	    char_array[0] = char_array_size;
	    char_array[1] = char_array_dim;
	    char_array[2] = char_array_size;
      
	    // copy string
	    char* char_array_ptr = (char*)(char_array + 3);
	    strcpy(char_array_ptr, value_str.c_str()); 
      
	    // create 'System.String' object instance
	    long* str_obj = MemoryManager::Instance()->AllocateObject(program->GetStringClassId(), 
								      (long*)op_stack, *stack_pos);
	    str_obj[0] = (long)char_array;
	    str_obj[1] = char_array_size;

	    // add to object array
	    str_obj_array_ptr[i] = (long)str_obj;
	  }
	  
	  PushInt(op_stack, stack_pos, (long)str_obj_array);
	}
	  break;
	  // --- END TRAP_RTRN --- //
	}
	break;
      }
#ifdef _DEBUG
      cout << "  ending stack: pos=" << (*stack_pos) << endl;
#endif
    } 

    /********************************
     * Calculates array element offset. 
     * Note: this code must match up 
     * with the interpreter's 'ArrayIndex'
     * method. Bounds checks are not done on
     * JIT code.
     ********************************/
    RegisterHolder* ArrayIndex(StackInstr* instr, int32_t type) {
      RegInstr* holder = working_stack.front();
      working_stack.pop_front();

      RegisterHolder* array_holder;
      switch(holder->GetType()) {
      case IMM_32:
	cerr << ">>> trying to index a constant! <<<" << endl;
	exit(1);
	break;

      case REG_32:
	array_holder = holder->GetRegister();
	break;

      case MEM_32:
	array_holder = GetRegister();
	move_mem_reg(holder->GetOperand(), EBP, array_holder->GetRegister());
	break;
      }

      /* Algorithm:
	 int32_t index = PopInt();
	 const int32_t dim = instr->GetOperand();
	
	 for(int i = 1; i < dim; i++) {
	 index *= array[i];
	 index += PopInt();
	 }
      */

      if(holder) {
        delete holder;
        holder = NULL;
      }

      // get initial index
      RegisterHolder* index_holder;
      holder = working_stack.front();
      working_stack.pop_front();
      switch(holder->GetType()) {
      case IMM_32:
	index_holder = GetRegister();
	move_imm_reg(holder->GetOperand(), index_holder->GetRegister());
	break;

      case REG_32:
	index_holder = holder->GetRegister();
	break;

      case MEM_32:
	index_holder = GetRegister();
	move_mem_reg(holder->GetOperand(), EBP, index_holder->GetRegister());
	break;
      }

      const int32_t dim = instr->GetOperand();
      for(int i = 1; i < dim; i++) {
	// index *= array[i];
        mul_mem_reg((i + 2) * sizeof(int32_t), array_holder->GetRegister(), 
		    index_holder->GetRegister());
        if(holder) {
          delete holder;
          holder = NULL;
        }

        holder = working_stack.front();
        working_stack.pop_front();
        switch(holder->GetType()) {
	case IMM_32:
	  add_imm_reg(holder->GetOperand(), index_holder->GetRegister());
	  break;

	case REG_32:
	  add_reg_reg(holder->GetRegister()->GetRegister(), 
		      index_holder->GetRegister());
	  break;

	case MEM_32:
	  add_mem_reg(holder->GetOperand(), EBP, index_holder->GetRegister());
	  break;
        }
      }
      
      switch(type) {
      case BYTE_ARY_TYPE:
	break;

      case INT_TYPE:
	shl_reg(index_holder->GetRegister(), 2);
	break;

      case FLOAT_TYPE:
	shl_reg(index_holder->GetRegister(), 3);
	break;
      }
      // skip first 2 integers (size and dimension) and all dimension indices
      add_imm_reg((instr->GetOperand() + 2) * sizeof(int32_t), index_holder->GetRegister());      
      add_reg_reg(index_holder->GetRegister(), array_holder->GetRegister());
      ReleaseRegister(index_holder);

      delete holder;
      holder = NULL;
      
      return array_holder;
    }
        
    /********************************
     * Caculates the indices for
     * memory references.
     ********************************/
    void ProcessIndices() {
#ifdef _DEBUG
      cout << "Calculating indices for variables..." << endl;
#endif
      multimap<int32_t, StackInstr*> values;
      for(int32_t i = 0; i < mthd->GetInstructionCount(); i++) {
	StackInstr* instr = mthd->GetInstruction(i);
	switch(instr->GetType()) {
	case LOAD_INT_VAR:
	case STOR_INT_VAR:
	case COPY_INT_VAR:
	case LOAD_FLOAT_VAR:
	case STOR_FLOAT_VAR:
	case COPY_FLOAT_VAR:
	  values.insert(pair<int32_t, StackInstr*>(instr->GetOperand(), instr));
	  break;
	}
      }
      
      int32_t index = TMP_REG_5;
      int32_t last_id = -1;
      multimap<int32_t, StackInstr*>::iterator value;
      for(value = values.begin(); value != values.end(); value++) {
	int32_t id = value->first;
	StackInstr* instr = (*value).second;
	// instance reference
	if(instr->GetOperand2() == INST) {
	  // note: all instance variables are allocted in 4-byte blocks,
	  // for floats the assembler allocates 2 4-byte blocks
	  instr->SetOperand3(instr->GetOperand() * sizeof(int32_t));
	}
	// local reference
	else {
	  // note: all local variables are allocted in 4 or 8 bytes ` 
	  // blocks depending upon type
	  if(last_id != id) {
	    if(instr->GetType() == LOAD_INT_VAR || 
	       instr->GetType() == STOR_INT_VAR ||
	       instr->GetType() == COPY_INT_VAR) {
	      index -= sizeof(int32_t);
	    }
	    else {
	      index -= sizeof(FLOAT_VALUE);
	    }
	  }
	  instr->SetOperand3(index);
	  last_id = id;
	}
#ifdef _DEBUG
	if(instr->GetOperand2() == INST) {
	  cout << "native memory: index=" << instr->GetOperand() << "; jit index="
	       << instr->GetOperand3() << endl;
	}
	else {
	  cout << "native stack: index=" << instr->GetOperand() << "; jit index="
	       << instr->GetOperand3() << endl;
	}
#endif
      }
      local_space = -(index + TMP_REG_5);
      
#ifdef _DEBUG
      cout << "Local space required: " << (local_space + 8) << " byte(s)" << endl;
#endif
    }
    
  public: 
    static void Initialize(StackProgram* p);
    
    JitCompilerIA32() {}
    
    ~JitCompilerIA32() {
      while(!working_stack.empty()) {
        RegInstr* instr = working_stack.front();
        working_stack.pop_front();
	if(instr) {
	  delete instr;
	  instr = NULL;
	}
      }      
      
      while(!aval_regs.empty()) {
        RegisterHolder* holder = aval_regs.back();
        aval_regs.pop_back();
	if(holder) {
	  delete holder;
	  holder = NULL;
	}
      }

      while(!aval_xregs.empty()) {
        RegisterHolder* holder = aval_xregs.back();
        aval_xregs.pop_back();
	if(holder) {
	  delete holder;
	  holder = NULL;
	}
      }

      while(!used_regs.empty()) {
        RegisterHolder* holder = used_regs.front();
	if(holder) {
	  delete holder;
	  holder = NULL;
	}
        // next
        used_regs.pop_front();
      }
      used_regs.clear();

      while(!used_xregs.empty()) {
        RegisterHolder* holder = used_xregs.front();
	if(holder) {
	  delete holder;
	  holder = NULL;
	}
        // next
        used_xregs.pop_front();
      }
      used_xregs.clear();
      
      while(!aux_regs.empty()) {
        RegisterHolder* holder = aux_regs.top();
	if(holder) {
	  delete holder;
	  holder = NULL;
        }
        aux_regs.pop();
      }
    }
    
    /****************************
     * Compiles stack code
     ****************************/
    bool Compile(StackMethod* cm) {
      compile_success = true;
      
      if(!cm->GetNativeCode()) {
	mthd = cm;
	int32_t cls_id = mthd->GetClass()->GetId();
	int32_t mthd_id = mthd->GetId();
	
#ifdef _DEBUG
	cout << "---------- Compiling Native Code: method_id=" << cls_id << "," 
	     << mthd_id << "; mthd_name='" << mthd->GetName() << "'; params=" 
	     << mthd->GetParamCount() << " ----------" << endl;

#endif
	code_buf_max = 4096;
#ifndef _WIN32
	code = (BYTE_VALUE*)valloc(code_buf_max);
	floats = (FLOAT_VALUE*)valloc(sizeof(FLOAT_VALUE) * MAX_DBLS * 2);
#else
	code = (BYTE_VALUE*)malloc(code_buf_max);
        floats = new FLOAT_VALUE[MAX_DBLS];
#endif

	floats_index = instr_index = code_index = instr_count = 0;
	// general use registers
	aval_regs.push_back(new RegisterHolder(EDX));
	aval_regs.push_back(new RegisterHolder(ECX));
	aval_regs.push_back(new RegisterHolder(EBX));
	aval_regs.push_back(new RegisterHolder(EAX));
	// aux general use registers
        aux_regs.push(new RegisterHolder(EDI));
        aux_regs.push(new RegisterHolder(ESI));
	// floating point registers
	aval_xregs.push_back(new RegisterHolder(XMM7));
	aval_xregs.push_back(new RegisterHolder(XMM6));
	aval_xregs.push_back(new RegisterHolder(XMM5));
	aval_xregs.push_back(new RegisterHolder(XMM4)); 
	aval_xregs.push_back(new RegisterHolder(XMM3));
	aval_xregs.push_back(new RegisterHolder(XMM2)); 
	aval_xregs.push_back(new RegisterHolder(XMM1));
	aval_xregs.push_back(new RegisterHolder(XMM0));   
#ifdef _DEBUG
	cout << "Compiling code for IA-32 architecture..." << endl;
#endif
	
	// process offsets
	ProcessIndices();
	// setup
	Prolog();
	// method information
	move_imm_mem(cls_id, CLS_ID, EBP);
	move_imm_mem(mthd_id, MTHD_ID, EBP);
	// register root
	RegisterRoot();
	// translate parameters
	ProcessParameters(mthd->GetParamCount());
	// tranlsate program
	ProcessInstructions();
	if(!compile_success) {
	  return false;
	}

	// show content
	map<int32_t, StackInstr*>::iterator iter;
	for(iter = jump_table.begin(); iter != jump_table.end(); iter++) {
	  StackInstr* instr = iter->second;
	  int32_t src_offset = iter->first;
	  int32_t dest_index = mthd->GetLabelIndex(instr->GetOperand()) + 1;
	  int32_t dest_offset = mthd->GetInstruction(dest_index)->GetOffset();
	  int32_t offset = dest_offset - src_offset - 4;
	  memcpy(&code[src_offset], &offset, 4); 
#ifdef _DEBUG
	  cout << "jump update: src=" << src_offset 
	       << "; dest=" << dest_offset << endl;
#endif
	}
#ifdef _DEBUG
	cout << "Caching JIT code: actual=" << code_index 
	     << ", buffer=" << code_buf_max << " byte(s)" << endl;
#endif
	// store compiled code
#ifndef _WIN32
	if(mprotect(code, code_index, PROT_EXEC)) {
	  perror("Couldn't mprotect");
	  exit(errno);
	}
#endif
	mthd->SetNativeCode(new NativeCode(code, code_index, floats));
	compile_success = true;
      }
      
      return compile_success;
    }
    
    /****************************
     * Executes machine code
     ****************************/
    long Execute(StackMethod* cm, long* inst, long* op_stack, long* stack_pos) {
      mthd = cm;
      int32_t cls_id = mthd->GetClass()->GetId();
      int32_t mthd_id = mthd->GetId();
      
#ifdef _DEBUG
      cout << "=== MTHD_CALL (native): id=" << cls_id << "," << mthd_id 
	   << "; name='" << mthd->GetName() << "'; self=" << inst << "(" << (long)inst 
     << "); stack=" << op_stack << "; stack_pos=" << (*stack_pos) << "; params=" 
     << mthd->GetParamCount() << " ===" << endl;
      assert((*stack_pos) >= mthd->GetParamCount());
#endif

      NativeCode* native_code = mthd->GetNativeCode();
      code = native_code->GetCode();
      code_index = native_code->GetSize();
      floats = native_code->GetFloats();
      
      // execute
      return ExecuteMachineCode(cls_id, mthd_id, (int32_t*)inst, code, code_index, 
				(int32_t*)op_stack, (int32_t*)stack_pos);
    }
  };
}
#endif
