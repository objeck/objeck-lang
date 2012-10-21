/***************************************************************************
 * JIT compiler for the AMD64 architecture.
 *
 * Copyright (c) 2008-2012 Randy Hollines
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
#ifndef __REG_ALLOC_H__
#define __REG_ALLOC_H__

#include "../../os/posix/memory.h"
#include "../../os/posix/posix.h"
#include <sys/mman.h>
#include <errno.h>

#include "../../common.h"
#include "../../interpreter.h"

using namespace std;

namespace Runtime {
  // offsets for Intel (AMD-64) addresses
#define CLS_ID -8
#define MTHD_ID -16
#define CLASS_MEM -24
#define INSTANCE_MEM -32
#define OP_STACK -40
#define STACK_POS -48
  // float temps
#define TMP_XMM_0 -64
#define TMP_XMM_1 -72
#define TMP_XMM_2 -80
  // integer temps
#define TMP_REG_0 -88
#define TMP_REG_1 -96
#define TMP_REG_2 -104
#define TMP_REG_3 -112
#define TMP_REG_4 -120
#define TMP_REG_5 -128

#define RED_ZONE -128  
#define MAX_DBLS 64
#define PAGE_SIZE 4096

  // register type
  typedef enum _RegType { 
    IMM_INT = -4000,
    REG_INT,
    MEM_INT,
    IMM_FLOAT,
    REG_FLOAT,
    MEM_FLOAT,
  } RegType;
  
  // general and SSE registers
  typedef enum _Register { 
    RAX = -5000, 
    RBX, 
    RCX, 
    RDX, 
    RDI,
    RSI,
    RBP,
    RSP,
    R8,
    R9,
    R10,
    R11,
    R12,
    R13,
    R14,
    R15,
    XMM0, 
    XMM1,
    XMM2,
    XMM3,
    XMM4,
    XMM5,
    XMM6,
    XMM7,
    XMM8,
    XMM9,
    XMM10,
    XMM11,
    XMM12,
    XMM13,
    XMM14,
    XMM15,
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
	type = REG_INT;
      }
      else {
	type = REG_FLOAT;
      }
      holder = h;
      instr = NULL;
    }
  
    RegInstr(StackInstr* si, double* da) {
      type = IMM_FLOAT;
      operand = (long)da;
      holder = NULL;
      instr = NULL;
    }
    
    RegInstr(RegType t, long o) {
      type = t;
      operand = o;
    }
    
    RegInstr(StackInstr* si) {
      switch(si->GetType()) {
      case LOAD_INT_LIT:
	type = IMM_INT;
	operand = si->GetOperand();
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

    void SetOperand(long o) {
      operand = o;
    }

    long GetOperand() {
      return operand;
    }
  };
  
  /********************************
   * prototype for jit function
   ********************************/
  typedef long (*jit_fun_ptr)(long cls_id, long mthd_id, 
			      long* cls_mem, long* inst, 
			      long* op_stack, long *stack_pos);
  
  /********************************
   * JitCompilerIA64 class
   ********************************/
  class JitCompilerIA64 {
    static StackProgram* program;
    list<RegInstr*> working_stack;
    vector<RegisterHolder*> aval_regs;
    list<RegisterHolder*> used_regs;
    stack<RegisterHolder*> aux_regs;
    vector<RegisterHolder*> aval_xregs;
    list<RegisterHolder*> used_xregs;
    unordered_map<int, StackInstr*> jump_table; // jump addresses are 64-bits
    long org_local_space, local_space;
    StackMethod* method;
    long instr_count;
    BYTE_VALUE* code;
    long code_index;   
    double* floats;     
    long floats_index;
    long instr_index;
    long code_buf_max;
    bool compile_success;
    bool skip_jump;

    // setup and teardown
    void Prolog();
    void Epilog(long imm);
    
    // stack conversion operations
    void ProcessParameters(long count);
    void RegisterRoot();
    void UnregisterRoot();
    void ProcessInstructions();
    void ProcessLiteral(StackInstr* instruction) ;
    void ProcessVariable(StackInstr* instruction);
    void ProcessLoad(StackInstr* instr);
    void ProcessStore(StackInstr* instruction);
    void ProcessCopy(StackInstr* instr);
    RegInstr* ProcessIntFold(long left_imm, long right_imm, InstructionType type);
    void ProcessIntCalculation(StackInstr* instruction);
    void ProcessFloatCalculation(StackInstr* instruction);
    void ProcessReturn(long params = -1);
    void ProcessStackCallback(long instr_id, StackInstr* instr, 
			      long &instr_index, long params);
    void ProcessFunctionCallParameter();
    void ProcessIntCallParameter();
    void ProcessFloatCallParameter(); 
    void ProcessReturnParameters(MemoryType type);
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

    /********************************
     * Add byte code to buffer
     ********************************/
    void AddMachineCode(BYTE_VALUE b) {
      if(code_index == code_buf_max) {
	BYTE_VALUE* tmp;	
	if(posix_memalign((void**)&tmp, PAGE_SIZE, code_buf_max * 2)) {
	  cerr << "Unable to reallocate JIT memory!" << endl;
	  exit(1);
	}
	memcpy(tmp, code, code_index);
	free(code);
	code = tmp;
	code_buf_max *= 2;
	if(mprotect(code, code_buf_max, PROT_READ | PROT_WRITE | PROT_EXEC) < 0) {
	  cerr << "Unable to mprotect" << endl;
	  exit(1);
	}
      }
      code[code_index++] = b;
    }
    
    /********************************
     * Encodes and writes out 32-bit
     * integer values; note sizeof(int)
     ********************************/
    inline void AddImm(int imm) {
      BYTE_VALUE buffer[sizeof(int)];
      ByteEncode32(buffer, imm);
      for(size_t i = 0; i < sizeof(int); i++) {
	AddMachineCode(buffer[i]);
      }
    }
    
    /********************************
     * Encodes and writes out 64-bit
     * integer values
     ********************************/
    inline void AddImm64(long imm) {
      BYTE_VALUE buffer[sizeof(long)];
      ByteEncode64(buffer, imm);
      for(size_t i = 0; i < sizeof(long); i++) {
	AddMachineCode(buffer[i]);
      }
    }
  
    
    /********************************
     * Encoding for AMD64 "B" bits
     ********************************/
    inline BYTE_VALUE B(Register b) {
      if((b > RSP && b < XMM0) || b > XMM7) {
	return 0x49;
      }
      
      return 0x48;
    }

    /********************************
     * Encoding for AMD64 "XB" bits
     ********************************/
    inline BYTE_VALUE XB(Register b) {
      if((b > RSP && b < XMM0) || b > XMM7) {
	return 0x4b;
      }
      
      return 0x4a;
    }
    
    /********************************
     * Encoding for AMD64 "RXB" bits
     ********************************/
    inline BYTE_VALUE RXB(Register r, Register b) {
      BYTE_VALUE value = 0x4a;
      if((r > RSP && r < XMM0) || r > XMM7) {
	value += 0x4;
      }
      
      if((b > RSP && b < XMM0) || b > XMM7) {
	value += 0x1;
      }
      
      return value;
    }
    
    /********************************
     * Encoding for AMD64 "ROB" bits
     ********************************/
    inline BYTE_VALUE ROB(Register r, Register b) {
      BYTE_VALUE value = 0x48;
      if((r > RSP && r < XMM0) || r > XMM7) {
	value += 0x4;
      }

      if((b > RSP && b < XMM0) || b > XMM7) {
	value += 0x1;
      }
      
      return value;
    }
    
    /********************************
     * Caculates the AMD64 MOD R/M
     * offset
     ********************************/
    inline BYTE_VALUE ModRM(Register eff_adr, Register mod_rm) {
      BYTE_VALUE byte;

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
	cerr << "internal error" << endl;
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
	cerr << "invalid register reference" << endl;
	exit(1);
	break;

      default:
	cerr << "internal error" << endl;
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
      case RAX:
	return "rax";

      case RBX:
	return "rbx";

      case RCX:
	return "rcx";

      case RDX:
	return "rdx";

      case RDI:
	return "rdi";

      case RSI:
	return "rsi";

      case RBP:
	return "rbp";

      case RSP:
	return "rsp";

      case R8:
	return "r8";

      case R9:
	return "r9";
	
      case R10:
	return "r10";

      case R11:
	return "r11";

      case R12:
	return "r12";
	
      case R13:
	return "r13";
	
      case R14:
	return "r14";
	
      case R15:
	return "r15";

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
	
      case XMM8:
	return "xmm8";

      case XMM9:
	return "xmm9";

      case XMM10:
	return "xmm10";

      case XMM11:
	return "xmm11";

      case XMM12:
	return "xmm12";

      case XMM13:
	return "xmm13";

      case XMM14:
	return "xmm14";
	
      case XMM15:
	return "xmm15";
      }

      return "?";
    }

    /********************************
     * Encodes a byte array with a
     * 32-bit value
     ********************************/
    inline void ByteEncode32(BYTE_VALUE buffer[], int value) {
      memcpy(buffer, &value, sizeof(int));
    }

    /********************************
     * Encodes a byte array with a
     * 64-bit value
     ********************************/
    inline void ByteEncode64(BYTE_VALUE buffer[], long value) {
      memcpy(buffer, &value, sizeof(long));
    }
    
    /********************************
     * Encodes an array with the 
     * binary ID of a register
     ********************************/
    inline void RegisterEncode3(BYTE_VALUE& code, long offset, Register reg) {
#ifdef _DEBUG
      assert(offset == 2 || offset == 5);
#endif
      
      BYTE_VALUE reg_id;
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
	cerr << "internal error" << endl;
	exit(1);
	break;
      }

      if(offset == 2) {
	reg_id = reg_id << 3;
      }
      code = code | reg_id;
    }
    
    /***********************************
     * Check for 'Nil' dereferencing
     **********************************/
    inline void CheckNilDereference(Register reg) {
      const long offset = 43;
      cmp_imm_reg(0, reg);
#ifdef _DEBUG
      cout << "  " << (++instr_count) << ": [jne $" << offset << "]" << endl;
#endif
      // jump not equal
      AddMachineCode(0x0f);
      AddMachineCode(0x85);
      AddImm(offset);
      Epilog(-1);
    }
    
    /***********************************
     * Checks array bounds
     **********************************/
    inline void CheckArrayBounds(Register reg, Register max_reg) {
      const long offset = 43;
      
      // less than zero
      cmp_imm_reg(-1, reg);
#ifdef _DEBUG
      cout << "  " << (++instr_count) << ": [jg $" << offset << "]" << endl;
#endif
      // jump not equal
      AddMachineCode(0x0f);
      AddMachineCode(0x8f);
      AddImm(offset);
      Epilog(-2);
      
      // greater than max
      cmp_reg_reg(max_reg, reg);
#ifdef _DEBUG
      cout << "  " << (++instr_count) << ": [jl $" << offset << "]" << endl;
#endif
      // jump not equal
      AddMachineCode(0x0f);
      AddMachineCode(0x8c);
      AddImm(offset);
      Epilog(-3);
    }
    
    // Gets an avaiable register from
    // the pool of registers
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
	  aux_regs.push(new RegisterHolder(RAX));
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

    // Returns a register to the pool
    void ReleaseRegister(RegisterHolder* h) {
#ifdef _VERBOSE
      cout << "\t * releasing " << GetRegisterName(h->GetRegister())
	   << " *" << endl;
#endif

#ifdef _DEBUG
      assert(h->GetRegister() < XMM0);
      for(size_t i  = 0; i < aval_regs.size(); i++) {
	assert(h != aval_regs[i]);
      }
#endif

      if(h->GetRegister() == RDI || h->GetRegister() == RSI) {
	aux_regs.push(h);
      }
      else {
	aval_regs.push_back(h);
	used_regs.remove(h);
      }
    }

    // Gets an avaiable register from
    // the pool of registers
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

    // Returns a register to the pool
    void ReleaseXmmRegister(RegisterHolder* h) {
#ifdef _DEBUG
      assert(h->GetRegister() >= XMM0);
      for(size_t i = 0; i < aval_xregs.size(); i++) {
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
      move_mem_reg(OP_STACK, RBP, op_stack_holder->GetRegister());
      return op_stack_holder;
    }

    // move instructions
    void move_reg_mem8(Register src, long offset, Register dest);
    void move_mem8_reg(long offset, Register src, Register dest);
    void move_imm_mem8(long imm, long offset, Register dest);
    void move_reg_reg(Register src, Register dest);
    void move_reg_mem(Register src, long offset, Register dest);
    void move_mem_reg(long offset, Register src, Register dest);
    void move_imm_memx(RegInstr* instr, long offset, Register dest);
    void move_imm_mem(long imm, long offset, Register dest);
    void move_imm_reg(long imm, Register reg);
    void move_imm_xreg(RegInstr* instr, Register reg);
    void move_mem_xreg(long offset, Register src, Register dest);
    void move_xreg_mem(Register src, long offset, Register dest);
    void move_xreg_xreg(Register src, Register dest);

    // math instructions
    void math_imm_reg(long imm, Register reg, InstructionType type);    
    void math_imm_xreg(RegInstr* instr, Register reg, InstructionType type);
    void math_reg_reg(Register src, Register dest, InstructionType type);
    void math_xreg_xreg(Register src, Register dest, InstructionType type);
    void math_mem_reg(long offset, Register reg, InstructionType type);
    void math_mem_xreg(long offset, Register reg, InstructionType type);

    // logical
    void and_imm_reg(long imm, Register reg);
    void and_reg_reg(Register src, Register dest);
    void and_mem_reg(long offset, Register src, Register dest);
    void or_imm_reg(long imm, Register reg);
    void or_reg_reg(Register src, Register dest);
    void or_mem_reg(long offset, Register src, Register dest);
    void xor_imm_reg(long imm, Register reg);    
    void xor_reg_reg(Register src, Register dest);
    void xor_mem_reg(long offset, Register src, Register dest);
    
    // add instructions
    void add_imm_mem(long imm, long offset, Register dest);    
    void add_imm_reg(long imm, Register reg);    
    void add_imm_xreg(RegInstr* instr, Register reg);
    void add_xreg_xreg(Register src, Register dest);
    void add_mem_reg(long offset, Register src, Register dest);
    void add_mem_xreg(long offset, Register src, Register dest);
    void add_reg_reg(Register src, Register dest);

    // sub instructions
    void sub_imm_xreg(RegInstr* instr, Register reg);
    void sub_xreg_xreg(Register src, Register dest);
    void sub_mem_xreg(long offset, Register src, Register dest);
    void sub_imm_reg(long imm, Register reg);
    void sub_imm_mem(long imm, long offset, Register dest);
    void sub_reg_reg(Register src, Register dest);
    void sub_mem_reg(long offset, Register src, Register dest);

    // mul instructions
    void mul_imm_xreg(RegInstr* instr, Register reg);
    void mul_xreg_xreg(Register src, Register dest);
    void mul_mem_xreg(long offset, Register src, Register dest);
    void mul_imm_reg(long imm, Register reg);
    void mul_reg_reg(Register src, Register dest);
    void mul_mem_reg(long offset, Register src, Register dest);

    // div instructions
    void div_imm_xreg(RegInstr* instr, Register reg);
    void div_xreg_xreg(Register src, Register dest);
    void div_mem_xreg(long offset, Register src, Register dest);
    void div_imm_reg(long imm, Register reg, bool is_mod = false);
    void div_reg_reg(Register src, Register dest, bool is_mod = false);
    void div_mem_reg(long offset, Register src, Register dest, bool is_mod = false);

    // compare instructions
    void cmp_reg_reg(Register src, Register dest);
    void cmp_mem_reg(long offset, Register src, Register dest);
    void cmp_imm_reg(long imm, Register reg);
    void cmp_xreg_xreg(Register src, Register dest);
    void cmp_mem_xreg(long offset, Register src, Register dest);
    void cmp_imm_xreg(RegInstr* instr, Register reg);
    void cmov_reg(Register reg, InstructionType oper);
    
    // inc/dec instructions
    void dec_reg(Register dest);
    void dec_mem(long offset, Register dest);
    void inc_mem(long offset, Register dest);
    
    // shift instructions
    void shl_reg_reg(Register src, Register dest);
    void shl_mem_reg(int32_t offset, Register src, Register dest);
    void shl_imm_reg(int32_t value, Register dest);

    void shr_reg_reg(Register src, Register dest);
    void shr_mem_reg(int32_t offset, Register src, Register dest);
    void shr_imm_reg(int32_t value, Register dest);
    
    // push/pop instructions
    void push_imm(long value);
    void push_reg(Register reg);
    void pop_reg(Register reg);
    void push_mem(long offset, Register src);
    
    // type conversion instructions
    void round_imm_xreg(RegInstr* instr, Register reg, bool is_floor);
    void round_mem_xreg(long offset, Register src, Register dest, bool is_floor);
    void round_xreg_xreg(Register src, Register dest, bool is_floor);
    void cvt_xreg_reg(Register src, Register dest);
    void cvt_imm_reg(RegInstr* instr, Register reg);
    void cvt_mem_reg(long offset, Register src, Register dest);
    void cvt_reg_xreg(Register src, Register dest);
    void cvt_imm_xreg(RegInstr* instr, Register reg);
    void cvt_mem_xreg(long offset, Register src, Register dest);
    
    // function call instruction
    void call_reg(Register reg);

    // generates a conditional jump
    bool cond_jmp(InstructionType type);

    static long PopInt(long* op_stack, long *stack_pos) {
      long value = op_stack[--(*stack_pos)];
#ifdef _DEBUG
      cout << "\t[pop_i: value=" << (long*)value << "(" << value << ")]" << "; pos=" << (*stack_pos) << endl;
#endif

      return value;
    }

    static void PushInt(long* op_stack, long *stack_pos, long value) {
      op_stack[(*stack_pos)++] = value;
#ifdef _DEBUG
      cout << "\t[push_i: value=" << (long*)value << "(" << value << ")]" << "; pos=" << (*stack_pos) << endl;
#endif
    }

    // TODO: return value and unwind whole program
    // TOOD: time to refactor... too large!!!
    // ....
    // Process call backs from ASM
    // code
    static void StackCallback(const long instr_id, StackInstr* instr, const long cls_id, 
			      const long mthd_id, long* inst, long* op_stack, 
			      long *stack_pos, const long ip) {
#ifdef _DEBUG
      cout << "Stack Call: instr=" << instr_id
	   << ", oper_1=" << instr->GetOperand() << ", oper_2=" << instr->GetOperand2() 
	   << ", oper_3=" << instr->GetOperand3() << ", self=" << inst << "(" 
	   << (long)inst << "), stack=" << op_stack << ", stack_addr=" << stack_pos 
	   << ", stack_pos=" << (*stack_pos) << endl;
#endif
      switch(instr_id) {
      case MTHD_CALL: 
      case DYN_MTHD_CALL: {
#ifdef _DEBUG
        cout << "jit oper: MTHD_CALL: cls=" << instr->GetOperand() << ", mthd=" << instr->GetOperand2() << endl;
#endif
	StackInterpreter intpr;
	intpr.Execute((long*)op_stack, (long*)stack_pos, ip, program->GetClass(cls_id)->GetMethod(mthd_id), (long*)inst, true);
      }
	break;
	
      case NEW_BYTE_ARY: {
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
	long* mem = (long*)MemoryManager::Instance()->AllocateArray(size + ((dim + 2) * sizeof(long)), BYTE_ARY_TYPE, (long*)op_stack, *stack_pos);
	mem[0] = size;
	mem[1] = dim;
	memcpy(mem + 2, indices, dim * sizeof(long));
	PushInt(op_stack, stack_pos, (long)mem);
	
#ifdef _DEBUG
	cout << "jit oper: NEW_BYTE_ARY: dim=" << dim << "; size=" << size 
	     << "; index=" << (*stack_pos) << "; mem=" << mem << endl;
#endif
      }
	break;

      case NEW_INT_ARY: {
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
	long* mem = (long*)MemoryManager::
	  Instance()->AllocateArray(size + dim + 2, INT_TYPE, (long*)op_stack, *stack_pos);
#ifdef _DEBUG
	cout << "jit oper: NEW_INT_ARY: dim=" << dim << "; size=" << size 
	     << "; index=" << (*stack_pos) << "; mem=" << mem << endl;
#endif
	mem[0] = size;
	mem[1] = dim;
	memcpy(mem + 2, indices, dim * sizeof(long));
	PushInt(op_stack, stack_pos, (long)mem);
      }
	break;
	
      case NEW_FLOAT_ARY: {
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
	size *= 2;
	long* mem = (long*)MemoryManager::
	  Instance()->AllocateArray(size + dim + 2, INT_TYPE, (long*)op_stack, *stack_pos);
#ifdef _DEBUG
	cout << "jit oper: NEW_FLOAT_ARY: dim=" << dim << "; size=" << size 
	     << "; index=" << (*stack_pos) << "; mem=" << mem << endl; 
#endif
	mem[0] = size / 2;
	mem[1] = dim;
	memcpy(mem + 2, indices, dim * sizeof(long));
	PushInt(op_stack, stack_pos, (long)mem);
      }
	break;
	
      case NEW_OBJ_INST: {
#ifdef _DEBUG
	cout << "jit oper: NEW_OBJ_INST: id=" << instr->GetOperand() << endl; 
#endif
	long* mem = (long*)MemoryManager::Instance()->AllocateObject(instr->GetOperand(), 
								     (long*)op_stack, *stack_pos);
	PushInt(op_stack, stack_pos, (long)mem);
      }
	break;
	
      case OBJ_TYPE_OF: {
	long* mem = (long*)PopInt(op_stack, stack_pos);
	long* result = MemoryManager::Instance()->ValidObjectCast(mem, instr->GetOperand(),
								  program->GetHierarchy(),
								  program->GetInterfaces());
	if(result) {
	  PushInt(op_stack, stack_pos, 1);
	}
	else {
	  PushInt(op_stack, stack_pos, 0);
	}
      }
	break;
	
      case OBJ_INST_CAST: {
	long* mem = (long*)PopInt(op_stack, stack_pos);
	long to_id = instr->GetOperand();
#ifdef _DEBUG
	cout << "jit oper: OBJ_INST_CAST: from=" << mem << ", to=" << to_id << endl; 
#endif	
	long result = (long)MemoryManager::Instance()->ValidObjectCast((long*)mem, to_id, 
								       program->GetHierarchy(),
								       program->GetInterfaces());
	if(!result && mem) {
	  StackClass* to_cls = MemoryManager::GetClass((long*)mem);	  
	  cerr << ">>> Invalid object cast: '" << (to_cls ? to_cls->GetName() : "?" )  
	       << "' to '" << program->GetClass(to_id)->GetName() << "' <<<" << endl;
	  cerr << "  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
	  exit(1);
	}
	PushInt(op_stack, stack_pos, result);
      }
	break;

	//----------- threads -----------
      
      case THREAD_JOIN: {
	long* instance = inst;
	if(!instance) {
	  cerr << "Atempting to dereference a 'Nil' memory instance" << endl;
	  cerr << "  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
	  exit(1);
	}
      
	void* status;
	pthread_t vm_thread = (pthread_t)instance[0];      
	if(pthread_join(vm_thread, &status)) {
	  cerr << "Unable to join thread!" << endl;
	  exit(-1);
	}
      }
	break;
      
      case THREAD_SLEEP:
	sleep(PopInt(op_stack, stack_pos));
	break;
      
      case THREAD_MUTEX: {
	long* instance = inst;
	if(!instance) {
	  cerr << "Atempting to dereference a 'Nil' memory instance" << endl;
	  cerr << "  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
	  exit(1);
	}
	pthread_mutex_init((pthread_mutex_t*)&instance[1], NULL);
      }
	break;
	
      case CRITICAL_START: {
	long* instance = (long*)PopInt(op_stack, stack_pos);
	if(!instance) {
	  cerr << "Atempting to dereference a 'Nil' memory instance" << endl;
	  cerr << "  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
	  exit(1);
	}      
	pthread_mutex_lock((pthread_mutex_t*)&instance[1]);
      }
	break;

      case CRITICAL_END: {
	long* instance = (long*)PopInt(op_stack, stack_pos);
	if(!instance) {
	  cerr << "Atempting to dereference a 'Nil' memory instance" << endl;
	  cerr << "  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
	  exit(1);
	}      
	pthread_mutex_unlock((pthread_mutex_t*)&instance[1]);
      }
	break;

	// ---------------- memory copy ----------------
      case CPY_BYTE_ARY: {
	long length = PopInt(op_stack, stack_pos);;
	const long src_offset = PopInt(op_stack, stack_pos);;
	long* src_array = (long*)PopInt(op_stack, stack_pos);;
	const long dest_offset = PopInt(op_stack, stack_pos);;
	long* dest_array = (long*)PopInt(op_stack, stack_pos);;      
	
	if(!src_array || !dest_array) {
	  cerr << ">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
	  cerr << "  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
	  exit(1);
	}
    
	const long src_array_len = src_array[2];
	const long dest_array_len = dest_array[2];
	if(length > 0 && src_offset + length <= src_array_len && dest_offset + length <= dest_array_len) {
	  char* src_array_ptr = (char*)(src_array + 3);
	  char* dest_array_ptr = (char*)(dest_array + 3);
	  memcpy(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length);
	  PushInt(op_stack, stack_pos, 1);
	}
	else {
	  PushInt(op_stack, stack_pos, 0);
	}
      }
	break;

      case CPY_INT_ARY: {
	long length = PopInt(op_stack, stack_pos);;
	const long src_offset = PopInt(op_stack, stack_pos);;
	long* src_array = (long*)PopInt(op_stack, stack_pos);;
	const long dest_offset = PopInt(op_stack, stack_pos);;
	long* dest_array = (long*)PopInt(op_stack, stack_pos);;      
      
	if(!src_array || !dest_array) {
	  cerr << ">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
	  cerr << "  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
	  exit(1);
	}
      
	const long src_array_len = src_array[0];
	const long dest_array_len = dest_array[0];
	if(length > 0 && src_offset + length <= src_array_len && dest_offset + length <= dest_array_len) {
	  long* src_array_ptr = src_array + 3;
	  long* dest_array_ptr = dest_array + 3;
	  memcpy(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length * sizeof(long));
	  PushInt(op_stack, stack_pos, 1);
	}
	else {
	  PushInt(op_stack, stack_pos, 0);
	}
      }
	break;

      case CPY_FLOAT_ARY: {
	long length = PopInt(op_stack, stack_pos);;
	const long src_offset = PopInt(op_stack, stack_pos);;
	long* src_array = (long*)PopInt(op_stack, stack_pos);;
	const long dest_offset = PopInt(op_stack, stack_pos);;
	long* dest_array = (long*)PopInt(op_stack, stack_pos);;      
      
	if(!src_array || !dest_array) {
	  cerr << ">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
	  cerr << "  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
	  exit(1);
	}
      
	const long src_array_len = src_array[0];
	const long dest_array_len = dest_array[0];
	if(length > 0 && src_offset + length <= src_array_len && dest_offset + length <= dest_array_len) {
	  long* src_array_ptr = src_array + 3;
	  long* dest_array_ptr = dest_array + 3;
	  memcpy(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length * sizeof(FLOAT_VALUE));
	  PushInt(op_stack, stack_pos, 1);
	}
	else {
	  PushInt(op_stack, stack_pos, 0);
	}
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
#ifdef _DEBUG
	  cout << "  STD_OUT_BOOL" << endl;
#endif
	  long value = PopInt(op_stack, stack_pos);
	  cout << ((value == 0) ? "false" : "true");
	}
	  break;
	  
	case STD_OUT_BYTE:
#ifdef _DEBUG
	  cout << "  STD_OUT_BYTE" << endl;
#endif
	  cout <<  (unsigned char)PopInt(op_stack, stack_pos);
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
	  cout << PopInt(op_stack, stack_pos);
	  break;

	case STD_OUT_FLOAT: {
#ifdef _DEBUG
	  cout << "  STD_OUT_FLOAT" << endl;
#endif
	  double value = 0.0;
	  --(*stack_pos);	  
	  memcpy(&value, &op_stack[(*stack_pos)], sizeof(double));
	  cout.precision(9);
	  cout << value;
	  break;
	}
	  break;

	case STD_OUT_CHAR_ARY: {
	  long* array = (long*)PopInt(op_stack, stack_pos);
	  if(array) {
	    BYTE_VALUE* str = (BYTE_VALUE*)(array + 3);
#ifdef _DEBUG
	    cout << "  STD_OUT_CHAR_ARY: addr=" << array << "(" << long(array) << ")" << endl;
#endif
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
	  cout << "  STD_OUT_BYTE_ARY: addr=" << array << "(" << long(array) << ")" << endl;
#endif	  
	  
	  if(array && offset >= 0 && offset + num < array[0]) {
	    char* buffer = (char*)(array + 3);
	    cout.write(buffer + offset, num);
	    PushInt(op_stack, stack_pos, 1);
	  } 
	  else {
	    cout << "Nil";
	    PushInt(op_stack, stack_pos, 0);
	  }
	}
	  break;
	  
	  // ---------------- standard error i/o ----------------
	case STD_ERR_BOOL: {
#ifdef _DEBUG
	  cout << "  STD_ERR_BOOL" << endl;
#endif
	  int32_t value = PopInt(op_stack, stack_pos);
	  cerr << ((value == 0) ? "false" : "true");
	}
	  break;
	  
	case STD_ERR_BYTE:
#ifdef _DEBUG
	  cout << "  STD_ERR_BYTE" << endl;
#endif
	  cerr <<  (unsigned char)PopInt(op_stack, stack_pos);
	  break;

	case STD_ERR_CHAR:
#ifdef _DEBUG
	  cout << "  STD_ERR_CHAR" << endl;
#endif
	  cerr <<  (char)PopInt(op_stack, stack_pos);
	  break;

	case STD_ERR_INT:
#ifdef _DEBUG
	  cout << "  STD_ERR_INT" << endl;
#endif
	  cerr <<  PopInt(op_stack, stack_pos);
	  break;

	case STD_ERR_FLOAT: {
#ifdef _DEBUG
	  cout << "  STD_ERR_FLOAT" << endl;
#endif
	  double value;      
	  (*stack_pos) -= 2;
	  memcpy(&value, &op_stack[(*stack_pos)], sizeof(double));
	  cerr.precision(9);
	  cerr << value;
	  break;
	}
	  break;

	case STD_ERR_CHAR_ARY: {
	  int32_t* array = (int32_t*)PopInt(op_stack, stack_pos);
	  if(array) {
#ifdef _DEBUG
	    cout << "  STD_ERR_CHAR_ARY: addr=" << array << "(" << long(array) << ")" << endl;
#endif
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
	  
	  if(array && offset >= 0 && offset + num < array[0]) {
	    char* buffer = (char*)(array + 3);
	    cerr.write(buffer + offset, num);
	    PushInt(op_stack, stack_pos, 1);
	  } 
	  else {
	    cerr << "Nil";
	    PushInt(op_stack, stack_pos, 0);
	  }
	}
	  break;
	  	  
	  // ---------------- system time ----------------
	case SYS_TIME: {
	  time_t raw_time;
	  time (&raw_time);
	  
	  struct tm* local_time = localtime (&raw_time);
	  long* time = (long*)inst;
	  time[0] = local_time->tm_mday; // day
	  time[1] = local_time->tm_mon; // month
	  time[2] = local_time->tm_year; // year
	  time[3] = local_time->tm_hour; // hours
	  time[4] = local_time->tm_min; // mins
	  time[5] = local_time->tm_sec; // secs
	  time[6] = local_time->tm_isdst > 0; // savings time
	}
	  break;
	  
	  // ---------------- ip socket i/o ----------------
	case SOCK_TCP_CONNECT: {
	  long port = PopInt(op_stack, stack_pos);
	  long* array = (long*)PopInt(op_stack, stack_pos);
	  long* instance = (long*)PopInt(op_stack, stack_pos);
	  if(instance && array) {
	    array = (long*)array[0];
	    const char* addr = (char*)(array + 3);
	    SOCKET sock = IPSocket::Open(addr, port);
#ifdef _DEBUG
	    cout << "# socket connect: addr='" << addr << ":" << port << "'; instance=" << instance << "(" << (long)instance << ")" <<
	      "; addr=" << sock << "(" << (long)sock << ") #" << endl;
#endif
	    instance[0] = (long)sock;
	  }
	}
	  break;  
    
	case SOCK_TCP_CLOSE: {
	  long* instance = (long*)PopInt(op_stack, stack_pos);
	  if(instance && (SOCKET)instance[0] > -1) {
	    SOCKET sock = (SOCKET)instance[0];
#ifdef _DEBUG
	    cout << "# socket close: addr=" << sock << "(" << (long)sock << ") #" << endl;
#endif
	    instance[0] = (long)NULL;
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
	    if(sock > -1) {
	      IPSocket::WriteBytes(data, strlen(data), sock);
	    }
	  }
	}
	  break;
	  
	case SOCK_TCP_IN_STRING: {
	  long* array = (long*)PopInt(op_stack, stack_pos);
	  long* instance = (long*)PopInt(op_stack, stack_pos);
	  char* buffer = (char*)(array + 3);
	  const long num = array[0] - 1;
	  SOCKET sock = (SOCKET)instance[0];

	  int status;
	  if(sock > -1) {
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
	  break;

	  // ---------------- file i/o ----------------
	case FILE_OPEN_READ: {
	  long* array = (long*)PopInt(op_stack, stack_pos);
	  long* instance = (long*)PopInt(op_stack, stack_pos);
	  if(instance && array) {
	    array = (long*)array[0];	
	    const char* name = (char*)(array + 3);
	    instance[0] = (long)File::FileOpen(name, "r");
	  }
	}
	  break;
	
	case FILE_OPEN_WRITE: {
	  long* array = (long*)PopInt(op_stack, stack_pos);
	  long* instance = (long*)PopInt(op_stack, stack_pos);
	  if(instance && array) {
	    array = (long*)array[0];
	    const char* name = (char*)(array + 3);
	    instance[0] = (long)File::FileOpen(name, "w");
	  }
	}
	  break;
	
	case FILE_OPEN_READ_WRITE: {
	  long* array = (long*)PopInt(op_stack, stack_pos);
	  long* instance = (long*)PopInt(op_stack, stack_pos);
	  if(array && instance) {
	    array = (long*)array[0];
	    const char* name = (char*)(array + 3);
	    instance[0] = (long)File::FileOpen(name, "w+");
	  }
	}
	  break;
	  
	case FILE_CLOSE: {
	  long* instance = (long*)PopInt(op_stack, stack_pos);
	  if(instance && (FILE*)instance[0]) {
	    FILE* file = (FILE*)instance[0];
	    instance[0] = (long)NULL;
	    fclose(file);
	  }
	}
	  break;

	case FILE_FLUSH: {
	  long* instance = (long*)PopInt(op_stack, stack_pos);
	  if(instance && (FILE*)instance[0]) {
	    FILE* file = (FILE*)instance[0];
	    instance[0] = (long)NULL;
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
	    const char* name = (char*)(array + 3);	    
	    if(file) {
	      fputs(name, file);
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
	  long value = (long)MemoryManager::GetObjectID((long*)PopInt(op_stack, stack_pos));
	  PushInt(op_stack, stack_pos, value);
	}
	  break;
	  
	case LOAD_ARY_SIZE: {
#ifdef _DEBUG
	  cout << "  LOAD_ARY_SIZE" << endl;
#endif
	  long* array = (long*)PopInt(op_stack, stack_pos);
	  if(!array) {
	    cerr << "Atempting to dereference a 'Nil' memory instance" << endl;
	    cerr << "  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
	    exit(1);
	  }
	  PushInt(op_stack, stack_pos, (long)array[2]);
	}  
	  break;
	  
	case LOAD_MULTI_ARY_SIZE: {
	  long* array = (long*)PopInt(op_stack, stack_pos);
	  if(!array) {
	    cerr << "Atempting to dereference a 'Nil' memory instance" << endl;
	    cerr << "  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
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

	  PushInt(op_stack, stack_pos, (long)mem);
	}
	  break;
	  
	case CPY_CHAR_STR_ARY: {
 	  long index = PopInt(op_stack, stack_pos);
	  BYTE_VALUE* value_str = program->GetCharStrings()[index];
	  // copy array
	  long* array = (long*)PopInt(op_stack, stack_pos);
	  if(array) {
	    const long size = array[2];
	    BYTE_VALUE* str = (BYTE_VALUE*)(array + 3);
	    memcpy(str, value_str, size);
#ifdef _DEBUG
	    cout << "  CPY_CHAR_STR_ARY: addr=" << array << "(" << long(array) 
		 << "), from='" << value_str << "', to='" << str << "'" << endl;
#endif
	    PushInt(op_stack, stack_pos, (long)array);
	  }
	  else {
	    PushInt(op_stack, stack_pos, 0);
	  }
	}
	  break;

	case CPY_CHAR_STR_ARYS: {
	  // copy array
	  long* array = (long*)PopInt(op_stack, stack_pos);
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
	  PushInt(op_stack, stack_pos, (long)array);
	}
	  break;
    
	case CPY_INT_STR_ARY: {
	  long index = PopInt(op_stack, stack_pos);
	  int32_t* value_str = program->GetIntStrings()[index];
	  // copy array
	  long* array = (long*)PopInt(op_stack, stack_pos);    
	  const long size = array[0];
	  const long dim = array[1];    
	  long* str = (long*)(array + dim + 2);
	  for(long i = 0; i < size; i++) {
	    str[i] = value_str[i];
	  }
#ifdef _DEBUG
	  cout << "stack oper: CPY_INT_STR_ARY" << endl;
#endif
	  PushInt(op_stack, stack_pos, (long)array);
	}
	  break;
    
	case CPY_FLOAT_STR_ARY: {
	  long index = PopInt(op_stack, stack_pos);
	  double* value_str = program->GetFloatStrings()[index];
	  // copy array
	  long* array = (long*)PopInt(op_stack, stack_pos);    
	  const long size = array[0];
	  const long dim = array[1];    
	  double* str = (double*)(array + dim + 2);
	  for(long i = 0; i < size; i++) {
	    str[i] = value_str[i];
	  }
    
#ifdef _DEBUG
	  cout << "stack oper: CPY_FLOAT_STR_ARY" << endl;
#endif
	  PushInt(op_stack, stack_pos, (long)array);
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
	  
	  // ---------------- file i/o ----------------
	case FILE_IN_BYTE: {
	  long* instance = (long*)PopInt(op_stack, stack_pos);
	  if(instance && (FILE*)instance[0]) {
	    FILE* file = (FILE*)instance[0];	  
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
	  if(array && instance && (FILE*)instance[0] && offset >=0 && offset + num < array[0]) {
	    FILE* file = (FILE*)instance[0];
	    char* buffer = (char*)(array + 3);
	    PushInt(op_stack, stack_pos, fread(buffer + offset, 1, num, file));        
	  } 
	  else {
	    PushInt(op_stack, stack_pos, -1);
	  }
	}
	  break;

	case FILE_OUT_BYTE: {
	  long value = PopInt(op_stack, stack_pos);
	  long* instance = (long*)PopInt(op_stack, stack_pos);
	  if(instance && (FILE*)instance[0]) {
	    FILE* file = (FILE*)instance[0];
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
	  if(array && instance && (FILE*)instance[0] && offset >=0 && offset + num < array[0]) {
	    FILE* file = (FILE*)instance[0];
	    char* buffer = (char*)(array + 3);
	    PushInt(op_stack, stack_pos, fwrite(buffer + offset, 1, num, file));
	  } 
	  else {
	    PushInt(op_stack, stack_pos, -1);
	  }
	}
	  break;
	  
	case FILE_SEEK: {
	  long pos = PopInt(op_stack, stack_pos);
	  long* instance = (long*)PopInt(op_stack, stack_pos);
	  if(instance && (FILE*)instance[0]) {
	    FILE* file = (FILE*)instance[0];
	    if(fseek(file, pos, SEEK_CUR) != 0) {
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
	  if(instance && (FILE*)instance[0]) {
	    FILE* file = (FILE*)instance[0];
	    PushInt(op_stack, stack_pos, feof(file) != 0);
	  } 
	  else {
	    PushInt(op_stack, stack_pos, 1);
	  }
	}
	  break;

	case FILE_IS_OPEN: {
	  long* instance = (long*)PopInt(op_stack, stack_pos);
	  if(instance && (FILE*)instance[0]) {
	    PushInt(op_stack, stack_pos, 1);
	  } 
	  else {
	    PushInt(op_stack, stack_pos, 0);
	  }
	}
	  break;

	case FILE_EXISTS: {
	  long* array = (long*)PopInt(op_stack, stack_pos);
	  if(array) {
	    array = (long*)array[0];
	    const char* name = (char*)(array + 3);
	    PushInt(op_stack, stack_pos, File::FileExists(name));
	  }
	  else {
	    PushInt(op_stack, stack_pos, 0);
	  }
	}
	  break;

	case FILE_SIZE: {
	  long* array = (long*)PopInt(op_stack, stack_pos);
	  if(array) {
	    array = (long*)array[0];
	    const char* name = (char*)(array + 3);
	    PushInt(op_stack, stack_pos, File::FileSize(name));
	  }
	  else {
	    PushInt(op_stack, stack_pos, -1);
	  }
	}
	  break;

	case FILE_DELETE: {
	  long* array = (long*)PopInt(op_stack, stack_pos);
	  if(array) {
	    array = (long*)array[0];
	    const char* name = (char*)(array + 3);
	    if(remove(name) != 0) {
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

	case FILE_RENAME: {
	  long* to = (long*)PopInt(op_stack, stack_pos);
	  long* from = (long*)PopInt(op_stack, stack_pos);

	  if(!to || !from) {
	    PushInt(op_stack, stack_pos, 0);
	    return;
	  }
	  
	  to = (long*)to[0];
	  const char* to_name = (char*)(to + 3);

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
	  if(array) {
	    array = (long*)array[0];
	    const char* name = (char*)(array + 3);
	    PushInt(op_stack, stack_pos, File::MakeDir(name));
	  }
	  else {
	    PushInt(op_stack, stack_pos, 0);
	  }
	}
	  break;
	
	case DIR_EXISTS: {
	  long* array = (long*)PopInt(op_stack, stack_pos);
	  if(array) {
	    array = (long*)array[0];
	    const char* name = (char*)(array + 3);
	    PushInt(op_stack, stack_pos, File::IsDir(name));
	  }
	  else {
	    PushInt(op_stack, stack_pos, 0);
	  }
	}
	  break;
	
	case DIR_LIST: {
	  long* array = (long*)PopInt(op_stack, stack_pos);
	  if(array) {
	    array = (long*)array[0];
	    char* name = (char*)(array + 3);

	    vector<string> files = File::ListDir(name);

	    // create 'System.String' object array
	    const long str_obj_array_size = files.size();
	    const long str_obj_array_dim = 1;  
	    long* str_obj_array = (long*)MemoryManager::Instance()->AllocateArray(str_obj_array_size + 
										  str_obj_array_dim + 2, 
										  INT_TYPE, 
										  (long*)op_stack, 
										  *stack_pos, false);
	    str_obj_array[0] = str_obj_array_size;
	    str_obj_array[1] = str_obj_array_dim;
	    str_obj_array[2] = str_obj_array_size;
	    long* str_obj_array_ptr = str_obj_array + 3;
    
	    // create and assign 'System.String' instances to array
	    for(size_t i = 0; i < files.size(); i++) {
	      // get value string
	      string &value_str = files[i];
      
	      // create character array
	      const long char_array_size = value_str.size();
	      const long char_array_dim = 1;
	      long* char_array = (long*)MemoryManager::Instance()->AllocateArray(char_array_size + 1 + 
										 ((char_array_dim + 2) * 
										  sizeof(long)),  
										 BYTE_ARY_TYPE, 
										 (long*)op_stack, 
										 *stack_pos, false);
	      char_array[0] = char_array_size;
	      char_array[1] = char_array_dim;
	      char_array[2] = char_array_size;
      
	      // copy string
	      char* char_array_ptr = (char*)(char_array + 3);
	      strcpy(char_array_ptr, value_str.c_str()); 
      
	      // create 'System.String' object instance
	      long* str_obj = MemoryManager::Instance()->AllocateObject(program->GetStringObjectId(), 
									(long*)op_stack, *stack_pos,
									false);
	      str_obj[0] = (long)char_array;
	      str_obj[1] = char_array_size;

	      // add to object array
	      str_obj_array_ptr[i] = (long)str_obj;
	    }
	  
	    PushInt(op_stack, stack_pos, (long)str_obj_array);
	  }
	  else {
	    PushInt(op_stack, stack_pos, 0);
	  }
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
    
    // Calculates array element offset. 
    // Note: this code must match up 
    // with the interpreter's 'ArrayIndex'
    // method.
    RegisterHolder* ArrayIndex(StackInstr* instr, MemoryType type) {
      RegInstr* holder = working_stack.front();
      working_stack.pop_front();

      RegisterHolder* array_holder;
      switch(holder->GetType()) {
      case IMM_INT:
	cerr << ">>> trying to index a constant! <<<" << endl;
	exit(1);
	break;

      case REG_INT:
	array_holder = holder->GetRegister();
	break;

      case MEM_INT:
	array_holder = GetRegister();
	move_mem_reg(holder->GetOperand(), RBP, array_holder->GetRegister());
	break;
	
      default:
	cerr << "internal error" << endl;
	exit(1);
	break;
      }
      
      CheckNilDereference(array_holder->GetRegister());
      
      /* Algorithm:
	 long index = PopInt();
	 const long dim = instr->GetOperand();
	
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
      case IMM_INT:
	index_holder = GetRegister();
	move_imm_reg(holder->GetOperand(), index_holder->GetRegister());
	break;

      case REG_INT:
	index_holder = holder->GetRegister();
	break;

      case MEM_INT:
	index_holder = GetRegister();
	move_mem_reg(holder->GetOperand(), RBP, index_holder->GetRegister());
	break;
	
      default:
	cerr << "internal error" << endl;
	exit(1);
	break;
      }
      
      const long dim = instr->GetOperand();
      for(int i = 1; i < dim; i++) {
	// index *= array[i];
        mul_mem_reg((i + 2) * sizeof(long), array_holder->GetRegister(), 
		    index_holder->GetRegister());
        if(holder) {
          delete holder;
          holder = NULL;
        }

        holder = working_stack.front();
        working_stack.pop_front();
        switch(holder->GetType()) {
	case IMM_INT:
	  add_imm_reg(holder->GetOperand(), index_holder->GetRegister());
	  break;

	case REG_INT:
	  add_reg_reg(holder->GetRegister()->GetRegister(), 
		      index_holder->GetRegister());
	  break;

	case MEM_INT:
	  add_mem_reg(holder->GetOperand(), RBP, index_holder->GetRegister());
	  break;

	default:
	  break;
        }
      }
      
      // bounds check
      RegisterHolder* bounds_holder = GetRegister();
      move_mem_reg(0, array_holder->GetRegister(), bounds_holder->GetRegister()); 
      
      // ajust indices
      switch(type) {
      case BYTE_ARY_TYPE:
	break;

      case INT_TYPE:
      case FLOAT_TYPE:
	shl_imm_reg(3, index_holder->GetRegister());
	shl_imm_reg(3, bounds_holder->GetRegister());
	break;
	
      default:
	break;
      }
      CheckArrayBounds(index_holder->GetRegister(), bounds_holder->GetRegister());
      ReleaseRegister(bounds_holder);

      // skip first 2 integers (size and dimension) and all dimension indices
      add_imm_reg((instr->GetOperand() + 2) * sizeof(long), index_holder->GetRegister());
      add_reg_reg(index_holder->GetRegister(), array_holder->GetRegister());
      ReleaseRegister(index_holder);

      delete holder;
      holder = NULL;
      
      return array_holder;
    }
        
    // Caculates the indices for
    // memory references.
    void ProcessIndices() {
#ifdef _DEBUG
      cout << "Calculating indices for variables..." << endl;
#endif
      multimap<long, StackInstr*> values;
      for(long i = 0; i < method->GetInstructionCount(); i++) {
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
	  values.insert(pair<long, StackInstr*>(instr->GetOperand(), instr));
	  break;
	  
	default:
	  break;
	}
      }
      
      long index = RED_ZONE;
      long last_id = -1;
      multimap<long, StackInstr*>::iterator value;
      for(value = values.begin(); value != values.end(); ++value) {
	long id = value->first;
	StackInstr* instr = value->second;
	// instance reference
	if(instr->GetOperand2() == INST || instr->GetOperand2() == CLS) {
	  // note: all instance variables are allocted in 4-byte blocks,
	  // for floats the assembler allocates 2 4-byte blocks
	  instr->SetOperand3(instr->GetOperand() * sizeof(long));
	}
	// local reference
	else {
	  // note: all local variables are allocted in 4 or 8 bytes ` 
	  // blocks depending upon type
	  if(last_id != id) {
	    if(instr->GetType() == LOAD_LOCL_INT_VAR || 
	       instr->GetType() == LOAD_CLS_INST_INT_VAR || 
	       instr->GetType() == STOR_LOCL_INT_VAR ||
	       instr->GetType() == STOR_CLS_INST_INT_VAR ||
	       instr->GetType() == COPY_LOCL_INT_VAR ||
	       instr->GetType() == COPY_CLS_INST_INT_VAR) {
	      index -= sizeof(long);
	    }
	    else if(instr->GetType() == LOAD_FUNC_VAR || 
		    instr->GetType() == STOR_FUNC_VAR) {
	      index -= sizeof(long) * 2;
	    }
	    else {
	      index -= sizeof(double);
	    }
	  }
	  instr->SetOperand3(index);
	  last_id = id;
	}
#ifdef _DEBUG
	if(instr->GetOperand2() == INST || instr->GetOperand2() == CLS) {
	  cout << "native memory: index=" << instr->GetOperand() << "; jit index="
	       << instr->GetOperand3() << endl;
	}
	else {
	  cout << "native stack: index=" << instr->GetOperand() << "; jit index="
	       << instr->GetOperand3() << endl;
	}
#endif
      }
      org_local_space = local_space = -(index + TMP_REG_5);
      
#ifdef _DEBUG
      cout << "Local space required: " << (local_space + 16) << " byte(s)" << endl;
#endif
    }
    
  public: 
    static void Initialize(StackProgram* p);
    
    JitCompilerIA64() {
    }
    
    ~JitCompilerIA64() {
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

    //
    // Compiles stack code into AMD64 machine code
    //
    bool Compile(StackMethod* cm) {
      compile_success = true;
      
      if(!cm->GetNativeCode()) {
#ifdef _TIMING
	clock_t start = clock();
#endif
	skip_jump = false;
	method = cm;
	
#ifdef _DEBUG
	long cls_id = method->GetClass()->GetId();
	long mthd_id = method->GetId();	
	cout << "---------- Compiling Native Code: method_id=" << cls_id << "," 
	     << mthd_id << "; mthd_name='" << method->GetName() << "'; params=" 
	     << method->GetParamCount() << " ----------" << endl;
#endif	
	// code buffer memory
	code_buf_max = PAGE_SIZE;
	if(posix_memalign((void**)&code, PAGE_SIZE, PAGE_SIZE)) {
	  cerr << "Unable to reallocate JIT memory!" << endl;
	  exit(1);
	}
	if(mprotect(code, code_buf_max, PROT_READ | PROT_WRITE | PROT_EXEC) < 0) {
	  cerr << "Unable to mprotect" << endl;
	  exit(1);
	}
	// floats memory
	if(posix_memalign((void**)&floats, PAGE_SIZE, sizeof(double) * MAX_DBLS)) {
	  cerr << "Unable to reallocate JIT memory!" << endl;
	  exit(1);
	}
	floats_index = instr_index = code_index = instr_count = 0;
	// general use registers
	//	aval_regs.push_back(new RegisterHolder(RDX));
	//	aval_regs.push_back(new RegisterHolder(RCX));
	aval_regs.push_back(new RegisterHolder(RBX));
	aval_regs.push_back(new RegisterHolder(RAX));
	// aux general use registers
	//        aux_regs.push(new RegisterHolder(RDI));
	//        aux_regs.push(new RegisterHolder(RSI));
	aux_regs.push(new RegisterHolder(R15));
	aux_regs.push(new RegisterHolder(R14));
	aux_regs.push(new RegisterHolder(R13));
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
#ifdef _DEBUG
	cout << "Compiling code for AMD64 architecture..." << endl;
#endif
	
	// process offsets
	ProcessIndices();
	// setup
	Prolog();
	// method information

	move_reg_mem(RDI, CLS_ID, RBP);
	move_reg_mem(RSI, MTHD_ID, RBP);
	move_reg_mem(RDX, CLASS_MEM, RBP);
	move_reg_mem(RCX, INSTANCE_MEM, RBP);
	move_reg_mem(R8, OP_STACK, RBP);
	move_reg_mem(R9, STACK_POS, RBP);
	

	// register root
	RegisterRoot();
	// translate parameters
	ProcessParameters(method->GetParamCount());
	// tranlsate program
	ProcessInstructions();
	if(!compile_success) {
	  return false;
	}

	// show content
	unordered_map<int, StackInstr*>::iterator iter;
	for(iter = jump_table.begin(); iter != jump_table.end(); ++iter) {
	  StackInstr* instr = iter->second;
	  long src_offset = iter->first;
	  long dest_index = method->GetLabelIndex(instr->GetOperand()) + 1;
	  long dest_offset = method->GetInstruction(dest_index)->GetOffset();
	  long offset = dest_offset - src_offset - 4; // 64-bit jump offset
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
	method->SetNativeCode(new NativeCode(code, code_index, floats));
	compile_success = true;
	
#ifndef _JIT_SERIAL
	// release our lock, native code has been compiled and set
	pthread_mutex_unlock(&cm->jit_mutex);
#endif

#ifdef _TIMING
	cout << "JIT compiling: method='" << method->GetName() << "', time=" 
	     << (double)(clock() - start) / CLOCKS_PER_SEC << " second(s)." << endl;
#endif
      }
      
      return compile_success;
    }
  };    
  
  /********************************
   * JitExecutorIA32 class
   ********************************/
  class JitExecutorIA32 {
    static StackProgram* program;
    StackMethod* method;
    BYTE_VALUE* code;
    long code_index; 
    double* floats;
    
    long ExecuteMachineCode(long cls_id, long mthd_id, long* inst, 
			    BYTE_VALUE* code, const long code_size, 
			    long* op_stack, long *stack_pos);
    
  public:
    static void Initialize(StackProgram* p);

    JitExecutorIA32() {
    }

    ~JitExecutorIA32() {
    }    
    
    // Executes machine code
    long Execute(StackMethod* cm, long* inst, long* op_stack, long* stack_pos) {
      method = cm;
      long cls_id = method->GetClass()->GetId();
      long mthd_id = method->GetId();
      

      NativeCode* native_code = method->GetNativeCode();
      code = native_code->GetCode();
      code_index = native_code->GetSize();
      floats = native_code->GetFloats();
      
#ifdef _DEBUG
      cout << "=== MTHD_CALL (native): id=" << cls_id << "," << mthd_id 
	   << "; name='" << method->GetName() << "'; self=" << inst << "(" << (long)inst 
	   << "); stack=" << op_stack << "; stack_pos=" << (*stack_pos) << "; params=" 
	   << method->GetParamCount() << "; code=" << (void*)code << "; code_index=" 
	   << code_index << " ===" << endl;
      assert((*stack_pos) >= method->GetParamCount());

      if(method->GetName() == "System.String:Append:c*,") {
	cout << "..." << endl;
      }
#endif

      
      
      // execute
      return ExecuteMachineCode(cls_id, mthd_id, (long*)inst, code, code_index, 
				(long*)op_stack, (long*)stack_pos);
    }
  };
}
#endif
