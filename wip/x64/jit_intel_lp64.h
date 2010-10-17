/***************************************************************************
 * JIT compiler for the x64 architecture.
 *
 * Copyright (c) 2008, 2009, 2010 Randy Hollines
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

#include "common.h"
#include "stack_intpr.h"
#include "mem_mgr.h"
// #include <sys/mman.h>

using namespace std;

namespace Runtime {
  // offsets for AMD64 addresses
#define CLS_ID -8
#define MTHD_ID -16
#define INSTANCE -24
#define OP_STACK -32
#define STACK_POS -40
#define TMP_POS_1 -48
#define TMP_POS_2 -56

#define MAX_DBLS 32
  
  // register type
  typedef enum _RegType { 
    IMM_32 = -4000,
    REG_32,
    MEM_32,
    IMM_64,
    REG_64,
    MEM_64,
  } RegType;
  
  // general and SEE (AMD64) registers
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
    bool is_spilled;  
  
  public:
    RegisterHolder(Register r) {
      reg = r;
      is_spilled = false;
    }

    ~RegisterHolder() {
#ifdef _VERBOSE
      cout << "*** deleting RegisterHolder ***" << endl;
#endif
    }

    Register GetRegister() {
      return reg;
    }

    void SetSpilled(bool s) {
      is_spilled = s;
    }

    bool GetSpilled() {
      return is_spilled;
    }
  };

  /********************************
   * RegInstr class
   ********************************/
  class RegInstr {
    RegType type;
    long long operand;
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
      operand = (long long)da;
      holder = NULL;
      instr = NULL;
    }
  
    RegInstr(StackInstr* si) {
      switch(si->GetType()) {
      case LOAD_long long:
	type = IMM_32;
	operand = si->GetOperand();
	break;

      case LOAD_INST_MEM:
	type = MEM_32;
	operand = INSTANCE;
	break;

      case LOAD_INT_VAR:
      case STOR_INT_VAR:
	type = MEM_32;
	operand = si->GetOperand3();
	break;
      case LOAD_FLOAT_VAR:
      case STOR_FLOAT_VAR:
	type = MEM_64;
	operand = si->GetOperand3();
	break;
      default:
	assert(false);
	break;
      }
      instr = si;
      holder = NULL;
    }
  
    ~RegInstr() {
#ifdef _VERBOSE
      cout << "*** deleting RegInstr ***" << endl;
#endif
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

    void SetOperand(long long o) {
      operand = o;
    }

    long long GetOperand() {
      return operand;
    }
  };
  
  typedef void (*jit_fun_ptr)(long long cls_id, long long mthd_id, void* inst, 
			      long long* op_stack, long long& stack_pos);
  
  /********************************
   * JitCompilerAmd64 class
   ********************************/
  class JitCompilerAmd64 : public JitCompiler {
    static StackProgram* prgm;
    stack<RegInstr*> working_stack;
    map<long long, StackInstr*> jump_table;
    map<long long, Register> stor_regs;
    list<RegisterHolder*> aval_regs;
    list<RegisterHolder*> used_regs;
    list<RegisterHolder*> aval_xregs;
    list<RegisterHolder*> used_xregs;
    long long var_count;
    
    StackMethod* mthd;
    long long instr_count;
    BYTE* code;
    long long code_index;    
    DBL_LIT* floats;
    long long floats_index;
    long long param_count;
    long long instr_index;
    long long params_offset;
    long long code_buf_max;

    // setup and teardown
    void Prolog();
    void Epilog();
    
    // stack conversion operations
    void ProcessReturnParameter();
    void ProcessParameters(long long count);
    void ProcessInstructions();
    void ProcessLiteral(StackInstr* instruction) ;
    void ProcessVariable(StackInstr* instruction);
    void ProcessLoad(StackInstr* instr);
    void ProcessStore(StackInstr* instruction);
    void ProcessIntCalculation(StackInstr* instruction);
    void ProcessFloatCalculation(StackInstr* instruction);
    void ProcessReturn(long long params = 0);
    void ProcessStackCall(long long instr_id, long long &instr_index, 
			  long long params);
    void ProcessLoadByteElement(StackInstr* instr);
    void ProcessStoreByteElement(StackInstr* instr);
    void ProcessLoadIntElement(StackInstr* instr);
    void ProcessStoreIntElement(StackInstr* instr);
    void ProcessLoadFloatElement(StackInstr* instr);
    void ProcessStoreFloatElement(StackInstr* instr);
    void ProcessJump(StackInstr* instr);
    void ProcessLogic(StackInstr* instr);
    void ProcessFloatToInt(StackInstr* instr);
    void ProcessIntToFloat(StackInstr* instr);
    // execute
    void ExecuteMachineCode(long long cls_id, long long mthd_id, void* inst, 
			    BYTE* code, const long long code_size, 
			    long long* op_stack, long long& stack_pos);





    /********************************
     * Add byte code to buffer
     ********************************/
    inline void AddMachineCode(BYTE b) {
      if(code_index == code_buf_max) {
	// allocate new buffer and copy values
	code = (BYTE*)realloc(code, code_buf_max * 2);
	code_buf_max *= 2;
      }
      code[code_index++] = b;      
    }
    
    /********************************
     * Encodes and writes out 32-bit
     * integer values; note sizeof(int)
     ********************************/
    inline void AddImm(long long imm) {
      BYTE buffer[sizeof(long long)];
      ByteEncode32(buffer, imm);
      for(long long i = 0; i < sizeof(long long); i++) {
	AddMachineCode(buffer[i]);
      }
    }

    /********************************
     * Encodes and writes out 64-bit
     * integer values
     ********************************/
    inline void AddImm64(long long imm) {
      BYTE buffer[sizeof(long long)];
      ByteEncode32(buffer, imm);
      for(long long i = 0; i < sizeof(long long); i++) {
	AddMachineCode(buffer[i]);
      }
    }
    
    /********************************
     * Encoding for AMD64 "B" bits
     ********************************/
    inline BYTE B(Register b) {
      if(b <= RSP) {
	return 0x48;
      }
      else {
	return 0x49;
      }
    }

    /********************************
     * Encoding for AMD64 "XB" bits
     ********************************/
    inline BYTE XB(Register b) {
      if(b <= RSP) {
	return 0x4a;
      }
      else {
	return 0x4b;
      }
    }
    
    /********************************
     * Encoding for AMD64 "RXB" bits
     ********************************/
    inline BYTE RXB(Register r, Register b) {
      if((r <= RSP || (r > R15 && r <= XMM7))  && 
	 (b <= RSP || (b > R15 && b <= XMM7))) {
	return 0x4a;
      }
      else if(((r > RSP && r <= R15) || r > XMM7) && 
	      ((b > RSP && b <= R15) || b > XMM7)) {
	return 0x4f;
      }
      else if((r > RSP && r <= R15) || r > XMM7) {
	return 0x4e;
      }
      else {
	return 0x4b;
      }
    }
    
    /********************************
     * Encoding for AMD64 "ROB" bits
     ********************************/
    inline BYTE ROB(Register r, Register b) {
      if((r <= RSP || (r > R15 && r <= XMM7))  && 
	 (b <= RSP || (b > R15 && b <= XMM7))) {
	return 0x48;
      }
      else if(((r > RSP && r <= R15) || r > XMM7) && 
	      ((b > RSP && b <= R15) || b > XMM7)) {
	return 0x4d;
      }
      else if((r > RSP && r <= R15) || r > XMM7) {
	return 0x4c;
      }
      else {
	return 0x49;
      }
    }
    
    /********************************
     * Caculates the IA-32 MOD R/M
     * offset
     ********************************/
    inline BYTE ModRM(Register eff_adr, Register mod_rm) {
      BYTE byte;

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
    }

    /********************************
     * Encodes a byte array with a
     * 32-bit value
     ********************************/
    inline void ByteEncode32(BYTE buffer[], long long value) {
      memcpy(buffer, &value, sizeof(long long));
    }
    
    /********************************
     * Encodes an array with the 
     * binary ID of a register
     ********************************/
    inline void RegisterEncode3(BYTE& code, long long offset, Register reg) {
#ifdef _DEBUG
      assert(offset == 2 || offset == 5);
#endif
      
      BYTE reg_id;
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
      }

      if(offset == 2) {
	reg_id = reg_id << 3;
      }
      code = code | reg_id;
    }
    
    /********************************
     * Gets an avaiable register from
     * the pool of registers
     ********************************/
    inline RegisterHolder* GetRegister() {
      RegisterHolder* holder;
      if(aval_regs.empty()) {
	cerr << ">>> No general registers avaiable! <<<" << endl;
	exit(1);
      }
      else {
        holder = aval_regs.front();
        aval_regs.pop_front();
        used_regs.push_back(holder);
      }
#ifdef _VERBOSE
      cout << "\t * allocating " << GetRegisterName(holder->GetRegister())
	   << " *" << endl;
#endif

      // remove from memory index pool
      long long index = 0;
      map<long long, Register>::iterator iter = stor_regs.begin();
      while(iter != stor_regs.end()) {
	if((*iter).second == holder->GetRegister()) {
	  index = (*iter).first;
#ifdef _VERBOSE
	  cout << "\tremoving association: " << index << " to " 
	       << GetRegisterName((*iter).second) << endl;
#endif
	}
	// update
	iter++;
      }
      if(index != 0) {
	stor_regs.erase(index);
      }

      return holder;
    }

    /********************************
     * Returns a register to the pool
     ********************************/
    inline void ReleaseRegister(RegisterHolder* h) {
#ifdef _DEBUG
      assert(h->GetRegister() < XMM0);
#endif

#ifdef _VERBOSE
      cout << "\t * releasing " << GetRegisterName(h->GetRegister())
	   << " *" << endl;
#endif
      
      aval_regs.push_back(h);
      used_regs.remove(h);
    }

    /********************************
     * Gets an avaiable register from
     * the pool of registers
     ********************************/
    inline RegisterHolder* GetXmmRegister() {
      RegisterHolder* holder;
      if(aval_xregs.empty()) {
	cerr << ">>> No XMM registers avaiable! <<<" << endl;
	exit(1);
      }
      else {
        holder = aval_xregs.front();
        aval_xregs.pop_front();
        used_xregs.push_back(holder);
      }
#ifdef _VERBOSE
      cout << "\t * allocating " << GetRegisterName(holder->GetRegister())
	   << " *" << endl;
#endif

      // remove from memory index pool
      long long index = 0;
      map<long long, Register>::iterator iter = stor_regs.begin();
      while(iter != stor_regs.end()) {
	if((*iter).second == holder->GetRegister()) {
	  index = (*iter).first;
#ifdef _DEBUG
	  cout << "\tremoving association: " << index << " to " 
	       << GetRegisterName((*iter).second) << endl;
#endif
	}
	// update
	iter++;
      }
      if(index != 0) {
	stor_regs.erase(index);
      }
      
      return holder;
    }

    /********************************
     * Returns a register to the pool
     ********************************/
    inline void ReleaseXmmRegister(RegisterHolder* h) {
#ifdef _DEBUG
      assert(h->GetRegister() >= XMM0);
#endif
      
#ifdef _VERBOSE
      cout << "\t * releasing: " << GetRegisterName(h->GetRegister())
	   << " * " << endl;
#endif
      aval_xregs.push_back(h);
      used_xregs.remove(h);
    }

    // move instructions
    void move_reg_mem8(Register src, long long offset, Register dest);
    void move_mem8_reg(long long offset, Register src, Register dest);
    void move_imm_mem8(long long imm, long long offset, Register dest);
    void move_reg_reg(Register src, Register dest);
    void move_reg_mem(Register src, long long offset, Register dest);
    void move_mem_reg(long long offset, Register src, Register dest);
    void move_imm_memx(RegInstr* instr, long long offset, Register dest);
    void move_imm_mem(long long imm, long long offset, Register dest);
    void move_imm_reg(long long imm, Register reg);
    void move_imm_xreg(RegInstr* instr, Register reg);
    void move_mem_xreg(long long offset, Register src, Register dest);
    void move_xreg_mem(Register src, long long offset, Register dest);
    void move_xreg_xreg(Register src, Register dest);

    // math instructions
    void math_imm_reg(long long imm, Register reg, InstructionType type);    
    void math_imm_xreg(RegInstr* instr, Register reg, InstructionType type);
    void math_reg_reg(Register src, Register dest, InstructionType type);
    void math_xreg_xreg(Register src, Register dest, InstructionType type);
    void math_mem_reg(long long offset, Register reg, InstructionType type);
    void math_mem_xreg(long long offset, Register reg, InstructionType type);

    // add instructions
    void add_imm_mem(long long imm, long long offset, Register dest);    
    void add_imm_reg(long long imm, Register reg);    
    void add_imm_xreg(RegInstr* instr, Register reg);
    void add_xreg_xreg(Register src, Register dest);
    void add_mem_reg(long long offset, Register src, Register dest);
    void add_mem_xreg(long long offset, Register src, Register dest);
    void add_reg_reg(Register src, Register dest);

    // sub instructions
    void sub_imm_xreg(RegInstr* instr, Register reg);
    void sub_xreg_xreg(Register src, Register dest);
    void sub_mem_xreg(long long offset, Register src, Register dest);
    void sub_imm_reg(long long imm, Register reg);
    void sub_reg_reg(Register src, Register dest);
    void sub_mem_reg(long long offset, Register src, Register dest);

    // mul instructions
    void mul_imm_xreg(RegInstr* instr, Register reg);
    void mul_xreg_xreg(Register src, Register dest);
    void mul_mem_xreg(long long offset, Register src, Register dest);
    void mul_imm_reg(long long imm, Register reg);
    void mul_reg_reg(Register src, Register dest);
    void mul_mem_reg(long long offset, Register src, Register dest);

    // div instructions
    void div_imm_xreg(RegInstr* instr, Register reg);
    void div_xreg_xreg(Register src, Register dest);
    void div_mem_xreg(long long offset, Register src, Register dest);
    void div_imm_reg(long long imm, Register reg, bool is_mod = false);
    void div_reg_reg(Register src, Register dest, bool is_mod = false);
    void div_mem_reg(long long offset, Register src, Register dest, 
		     bool is_mod = false);
    // compare instructions
    void cmp_reg_reg(Register src, Register dest);
    void cmp_mem_reg(long long offset, Register src, Register dest);
    void cmp_imm_reg(long long imm, Register reg);
    void cmp_xreg_xreg(Register src, Register dest);
    void cmp_mem_xreg(long long offset, Register src, Register dest, 
		      bool swap = false);
    void cmp_imm_xreg(RegInstr* instr, Register reg, bool swap = false);
    void cmov_reg(Register reg, InstructionType oper);
    
    // inc/dec instructions
    void dec_mem(long long offset, Register dest);
    void inc_mem(long long offset, Register dest);
    
    // shift instructions
    void shl_reg(Register dest, long long value);
    void shr_reg(Register dest, long long value);
    
    // push/pop instructions
    void push_imm(long long value);
    void push_reg(Register reg);
    void pop_reg(Register reg);
    void push_mem(long long offset, Register src);
    
    // type conversion instructions
    void cvt_xreg_reg(Register src, Register dest);
    void cvt_imm_reg(RegInstr* instr, Register reg);
    void cvt_mem_reg(long long offset, Register src, Register dest);
    void cvt_reg_xreg(Register src, Register dest);
    void cvt_imm_xreg(RegInstr* instr, Register reg);
    void cvt_mem_xreg(long long offset, Register src, Register dest);
    
    // function call instruction
    void call_reg(Register reg);
    
    /********************************
     * Process call backs from ASM
     * code
     ********************************/
    static void StackCall(long long instr_id, long long cls_id, long long mthd_id,
			  void* inst, long long* op_stack, 
			  long long& stack_pos, long long ip) {
#ifdef _DEBUG
      cout << "stack call - instruction id: " << instr_id 
	   << "; instance id: " << inst << "; stack: " 
	   << op_stack << "; stack pos: " << stack_pos << endl;
#endif
      switch(instr_id) {
      case MTHD_CALL: {
	StackInterpreter intpr;
	intpr.Execute(op_stack, stack_pos, ip, cls_id, mthd_id);
      }
	break;

      case NEW_BYTE_ARY: {
	long long indices[8];
	long long value = op_stack[--stack_pos];
	long long size = value;
	indices[0] = value;
	long long dim = 1;
	while(stack_pos > 0) {
	  long long value = op_stack[--stack_pos];
	  size *= value;
	  indices[dim++] = value;
	}
	long long* mem = (long long*)MemoryManager::Instance()->Allocate(size + ((dim + 1) * sizeof(long long)), 
								     BYTE_TYPE);
#ifdef _DEBUG
	cout << "byte array: dim: " << dim << "; size: " << size 
	     << "; index: " << stack_pos << "; mem: " << mem << endl;
#endif
	mem[0] = size;
	memcpy(mem + 1, indices, dim * sizeof(long long));
	op_stack[stack_pos++] = (long long)mem;
      }
	break;

      case NEW_INT_ARY: {
	long long indices[8];
	long long value = op_stack[--stack_pos];
	long long size = value;
	indices[0] = value;
	long long dim = 1;
	while(stack_pos > 0) {
	  long long value = op_stack[--stack_pos];
	  size *= value;
	  indices[dim++] = value;
	}
	long long* mem = (long long*)MemoryManager::Instance()->Allocate(size + dim + 1, 
								     INT_TYPE);
#ifdef _DEBUG
	cout << "long long array: dim: " << dim << "; size: " << size 
	     << "; index: " << stack_pos << "; mem: " << mem << endl;
#endif
	mem[0] = size;
	memcpy(mem + 1, indices, dim * sizeof(long long));
	op_stack[stack_pos++] = (long long)mem;
      }
	break;

      case NEW_FLOAT_ARY: {
	long long indices[8];
	long long value = op_stack[--stack_pos];
	long long size = value;
	indices[0] = value;
	long long dim = 1;
	while(stack_pos > 0) {
	  long long value = op_stack[--stack_pos];
	  size *= value;
	  indices[dim++] = value;
	}
	long long* mem = (long long*)MemoryManager::Instance()->Allocate(size + dim + 1, INT_TYPE);
#ifdef _DEBUG
	cout << "float array: dim: " << dim << "; size: " << size 
	     << "; index: " << stack_pos << "; mem: " << mem << endl; 
#endif
	mem[0] = size;
	memcpy(mem + 1, indices, dim * sizeof(long long));
	op_stack[stack_pos++] = (long long)mem;
      }
	break;
	
      case NEW_OBJ_INST: {
	long long obj_id = op_stack[--stack_pos];
	StackClass* cls = prgm->GetClass(obj_id);
	if(!cls) {
	  cerr << "Unable to find class!" << endl;
	  exit(1);
	}
	long long* mem = (long long*)MemoryManager::Instance()->Allocate(cls->GetMemorySize(), INT_TYPE);
	op_stack[stack_pos++] = (long long)mem;  
#ifdef _DEBUG
	cout << "object: id: " << obj_id << ", mem: " << mem << endl;
#endif

      }
	break;

      case STD_OUT_BYTE:
	cout <<  (BYTE)op_stack[--stack_pos] << endl;
	break;
	
      case STD_OUT_INT:
	cout <<  op_stack[--stack_pos] << endl;
	break;
	
      case STD_OUT_FLOAT: {
	DBL_LIT value;      
	stack_pos--;
	memcpy(&value, &op_stack[stack_pos], sizeof(DBL_LIT));
	// cout << value << endl;

	printf("%f\n", value);

	break;
      }
      }
    }
    
    /********************************
     * Creates machine code for
     * arra offsets
     ********************************/
    inline RegisterHolder* ArrayOffset(StackInstr* instr, long long type) {
      RegInstr* reg = working_stack.top();
      working_stack.pop();
      RegisterHolder* array_holder = GetRegister();
      move_mem_reg(reg->GetOperand(), RBP, array_holder->GetRegister());

      // get dimension and index
      long long dim = instr->GetOperand();
      RegisterHolder* index_holder;
      reg = working_stack.top();
      working_stack.pop();
      switch(reg->GetType()) {
      case IMM_32:
	index_holder = GetRegister();
	move_imm_reg(reg->GetOperand(), index_holder->GetRegister());
	break;
      case REG_32:
	index_holder = reg->GetRegister();
	break;
      case MEM_32:
	index_holder = GetRegister();
	move_mem_reg(reg->GetOperand(), RBP, index_holder->GetRegister());
	break;
      }

      while(--dim > 0) {
        mul_mem_reg(dim, array_holder->GetRegister(), 
		    index_holder->GetRegister());      
        reg = working_stack.top();
        working_stack.pop();
        switch(reg->GetType()) {
	case IMM_32:
	  math_imm_reg(reg->GetOperand(), index_holder->GetRegister(), ADD_INT);
	  break;
	case REG_32:
	  math_reg_reg(reg->GetRegister()->GetRegister(), 
		       index_holder->GetRegister(), ADD_INT);
	  break;
	case MEM_32:
	  math_mem_reg(reg->GetOperand(), index_holder->GetRegister(), ADD_INT);
	  break;
        }
      }
      math_imm_reg(instr->GetOperand(), index_holder->GetRegister(), ADD_INT);

      switch(type) {
      case BYTE_TYPE:
	break;
      case INT_TYPE:
      case FLOAT_TYPE:
	shl_reg(index_holder->GetRegister(), 4);
	break;
      }
      
      math_reg_reg(index_holder->GetRegister(), 
		   array_holder->GetRegister(), ADD_INT);
      ReleaseRegister(index_holder);   

      return array_holder;
    }
    
    /********************************
     * Caculates indices for variables
     ********************************/
    void ProcessIndices() {
#ifdef _DEBUG
      cout << "Calculating indices for variables..." << endl;
#endif
      multimap<long long, StackInstr*> values;
      for(long long i = 0; i < mthd->GetInstructionCount(); i++) {
	StackInstr* instr = mthd->GetInstruction(i);
	switch(instr->GetType()) {
	case LOAD_INT_VAR:
	case STOR_INT_VAR:
	case LOAD_FLOAT_VAR:
	case STOR_FLOAT_VAR:
	  values.insert(pair<long long, StackInstr*>(instr->GetOperand(), instr));
	  break;
	}
      }
      
      long long index = 0;
      long long last_id = -1;
      multimap<long long, StackInstr*>::iterator value;
      for(value = values.begin(); value != values.end(); value++) {
	long long id = (*value).first;
	StackInstr* instr = (*value).second;
	// instance reference
	if(instr->GetOperand2() == INST) {
	  instr->SetOperand3(instr->GetOperand());
	}
	// local reference
	else {
	  if(last_id != id) {
	    if(instr->GetType() == LOAD_INT_VAR || 
	       instr->GetType() == STOR_INT_VAR) {
	      index -= sizeof(long long);
	    }
	    else {
	      index -= sizeof(DBL_LIT);
	    }
	  }
	  instr->SetOperand3(index);
	  last_id = id;
#ifdef _DEBUG
	  cout << "  stack index: " << instr->GetOperand() << "; jit index: "
	       << instr->GetOperand3() << endl;
#endif
	}
      }
    }
    
  public: 
    static void Initialize(StackProgram* p);
    
    JitCompilerAmd64() {
      var_count = 0;
    }
    
    ~JitCompilerAmd64() {
      while(!working_stack.empty()) {
        RegInstr* instr = working_stack.top();
        working_stack.pop();
	if(instr) {
	  delete instr;
	  instr = NULL;
	}
      }      
      
      while(!aval_regs.empty()) {
        RegisterHolder* holder = aval_regs.front();
        aval_regs.pop_front();
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
    }

    /********************************
     * Executes machine code. Code is
     * compiled on first call to JIT
     * compiler and then cached.
     ********************************/
    void Execute(void* inst, long long cls_id, long long mthd_id, 
		 long long* op_stack, long long& stack_pos) {
      mthd = prgm->GetClass(cls_id)->GetMethod(mthd_id);
      // compile
      if(!mthd->GetNativeCode()) {
	code_buf_max = 128;
	param_count = mthd->GetParamCount();
	code = (BYTE*)valloc(code_buf_max);
	floats = (DBL_LIT*)valloc(MAX_DBLS * sizeof(DBL_LIT));
	
	floats_index = instr_index = code_index = instr_count = params_offset = 0;
	aval_regs.push_front(new RegisterHolder(R15));
	aval_regs.push_front(new RegisterHolder(R14));
	aval_regs.push_front(new RegisterHolder(R13));
	aval_regs.push_front(new RegisterHolder(R11));
	aval_regs.push_front(new RegisterHolder(R10));
	aval_regs.push_front(new RegisterHolder(RBX));
	aval_regs.push_front(new RegisterHolder(RAX));
	aval_regs.push_front(new RegisterHolder(R9));
	aval_regs.push_front(new RegisterHolder(R8));
	aval_regs.push_front(new RegisterHolder(RCX));
	aval_regs.push_front(new RegisterHolder(RDX));
	aval_regs.push_front(new RegisterHolder(RSI));
	aval_regs.push_front(new RegisterHolder(RDI));
	aval_xregs.push_front(new RegisterHolder(XMM7));
	aval_xregs.push_front(new RegisterHolder(XMM6));
	aval_xregs.push_front(new RegisterHolder(XMM5));
	aval_xregs.push_front(new RegisterHolder(XMM4)); 
	aval_xregs.push_front(new RegisterHolder(XMM3));
	aval_xregs.push_front(new RegisterHolder(XMM2)); 
	aval_xregs.push_front(new RegisterHolder(XMM1));
	aval_xregs.push_front(new RegisterHolder(XMM0));   
#ifdef _DEBUG
	cout << "Compiling code..." << endl;
#endif
	// process offsets
	ProcessIndices();
	// setup
	Prolog();
	// number of required local variables plus local varibles
	// in user code; 16-byte alignment for Mac
	long long stack_space = (var_count + 5) * sizeof(long long);
	while(stack_space % 16 != 0) {
	  stack_space += sizeof(long long);
	}
	
	sub_imm_reg(stack_space, RSP);
	// method information
	move_imm_mem(cls_id, CLS_ID, RBP);
	move_imm_mem(mthd_id, MTHD_ID, RBP);
	move_imm_mem((long long)inst, INSTANCE, RBP);
	// translate parameters
	ProcessParameters(param_count);
	// tranlsate program
	ProcessInstructions();
	// teardown
	Epilog();
	// store compiled code
	mthd->SetNativeCode(new NativeCode(code, code_index, floats));
#ifdef _DEBUG
	cout << "Machine code size: actual: " << code_index 
	     << " bytes, buffer: " << code_buf_max << " bytes" << endl;
#endif	
	// show content
	map<long long, StackInstr*>::iterator iter;
	for(iter = jump_table.begin(); iter != jump_table.end(); iter++) {
	  StackInstr* instr = iter->second;
	  long long src_offset = iter->first;
	  long long dest_index = mthd->GetLabelIndex(instr->GetOperand());
	  long long dest_offset = 
	    mthd->GetInstruction(dest_index)->GetOffset();	  
	  long long offset = dest_offset - src_offset - 4;	  
	  memcpy(&code[src_offset], &offset, 4); 
#ifdef _DEBUG
	  cout << "jump update: src: " << src_offset 
	       << " dest: " << dest_offset << endl;
#endif
	}
#ifdef _DEBUG
	cout << "---------------------------" << endl;
#endif
      }
      // already compiled
      else {
#ifdef _DEBUG
	cout << "Pre-compiled code..." << endl;
#endif
	NativeCode* native_code = mthd->GetNativeCode();
	code = native_code->GetCode();
	code_index = native_code->GetSize();
	floats = native_code->GetFloats();
      }
      // execute
      ExecuteMachineCode(cls_id, mthd_id, inst, 
			 code, code_index, 
			 op_stack, stack_pos);
    }
  };
}

#endif
