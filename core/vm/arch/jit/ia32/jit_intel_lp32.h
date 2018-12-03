/***************************************************************************
 * JIT compiler for the x86 architecture.
 *
 * Copyright (c) 2008-2018, Randy Hollines
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
 * - Neither the name of the Objeck team nor the names of its 
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

#ifndef __JIT_COMPILER__
#define __JIT_COMPILER__

#ifndef _WIN32
#include "../../memory.h"
#include "../../../arch/posix/posix.h"
#include <sys/mman.h>
#include <errno.h>
#else
#include "../../../arch/win32/win32.h"
#endif

#include "../../../common.h"
#include "../../../interpreter.h"

using namespace std;

namespace Runtime {
  // offsets for Intel (IA-32) addresses
#define CLS_ID 8
#define MTHD_ID 12
#define CLASS_MEM 16
#define INSTANCE_MEM 20
#define OP_STACK 24
#define STACK_POS 28
#define CALL_STACK 32
#define CALL_STACK_POS 36
#define JIT_MEM 40
#define JIT_OFFSET 44
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
#define BUFFER_SIZE 512
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
      case LOAD_CHAR_LIT:
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

    void SetOperand(int32_t o) {
      operand = o;
    }

    int32_t GetOperand() {
      return operand;
    }
  };

  /********************************
   * Manage executable buffers of memory
   ********************************/
  class PageHolder {
    unsigned char* buffer;
    int32_t available, index;

  public:
    PageHolder(int32_t size) {
      index = 0;
      int factor = 1;
      if(size > PAGE_SIZE) {
        factor = size / PAGE_SIZE + 1;
      }
      available = factor *  PAGE_SIZE;
#ifdef _WIN32
      buffer = (unsigned char*)malloc(available);
#else
      if(posix_memalign((void**)&buffer, PAGE_SIZE, available)) {
        wcerr << L"Unable to allocate JIT memory!" << endl;
        exit(1);
      }
      if(mprotect(buffer, available, PROT_READ | PROT_WRITE | PROT_EXEC) < 0) {
        wcerr << L"Unable to mprotect" << endl;
        exit(1);
      }
#endif
    }

    PageHolder() {
      index = 0;
      available = PAGE_SIZE;
#ifdef _WIN32
      buffer = (unsigned char*)malloc(PAGE_SIZE);
#else
      if(posix_memalign((void**)&buffer, PAGE_SIZE, PAGE_SIZE)) {
        wcerr << L"Unable to allocate JIT memory!" << endl;
        exit(1);
      }

      if(mprotect(buffer, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC) < 0) {
        wcerr << L"Unable to mprotect" << endl;
        exit(1);
      }
#endif
    }

    ~PageHolder() {
#ifdef _WIN32
      free(buffer);
#else
      munmap(buffer, PAGE_SIZE);
#endif
      buffer = NULL;
    }

    inline bool CanAddCode(int32_t size) {
      if(available - size >= 0) {
        return true;
      }

      return false;
    }

    inline unsigned char* AddCode(unsigned char* code, int32_t size) {
      unsigned char* temp = buffer + index;
      memcpy(temp, code, size);
      index += size;
      available -= size;

      return temp;
    }
  };

  class PageManager {
    vector<PageHolder*> holders;
    
  public:
    PageManager() {
      for(int i = 0; i < 4; ++i) {
        holders.push_back(new PageHolder(PAGE_SIZE * (i + 1)));
      }
    }

    ~PageManager() {
      while(!holders.empty()) {
        PageHolder* tmp = holders.front();
        holders.erase(holders.begin());
        // delete
        delete tmp;
        tmp = NULL;
      }
    }

    unsigned char* GetPage(unsigned char* code, int32_t size) {
      bool placed = false;

      unsigned char* temp = NULL;
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
  };

  /********************************
   * Prototype for jit function
   ********************************/
  typedef int32_t (*jit_fun_ptr)(int32_t cls_id, int32_t mthd_id, size_t* cls_mem, size_t* inst, size_t* op_stack, 
                                 long* stack_pos, StackFrame** call_stack, long* call_stack_pos, size_t** jit_mem, long* offset);
  
  /********************************
   * JitCompilerIA32 class
   ********************************/
  class JitCompilerIA32 {
    static StackProgram* program;
    static PageManager* page_manager;
    deque<RegInstr*> working_stack;
    vector<RegisterHolder*> aval_regs;
    list<RegisterHolder*> used_regs;
    stack<RegisterHolder*> aux_regs;
    RegisterHolder* reg_eax;
    vector<RegisterHolder*> aval_xregs;
    list<RegisterHolder*> used_xregs;
    unordered_map<int32_t, StackInstr*> jump_table;
    vector<int32_t> deref_offsets;          // -1
    vector<int32_t> bounds_less_offsets;    // -2
    vector<int32_t> bounds_greater_offsets; // -3
    int32_t local_space;
    StackMethod* method;
    int32_t instr_count;
    unsigned char* code;
    int32_t code_index;
    int32_t epilog_index;
    double* floats;     
    int32_t floats_index;
    int32_t instr_index;
    int32_t code_buf_max;
    bool compile_success;
    bool skip_jump;

    // setup and teardown
    void Prolog();
    void Epilog();

    // stack conversion operations
    void ProcessParameters(int32_t count);
    void RegisterRoot();
    void ProcessInstructions();
    void ProcessLoad(StackInstr* instr);
    void ProcessStore(StackInstr* instruction);
    void ProcessCopy(StackInstr* instr);
    RegInstr* ProcessIntFold(long left_imm, long right_imm, InstructionType type);
    void ProcessIntCalculation(StackInstr* instruction);
    void ProcessFloatCalculation(StackInstr* instruction);
    void ProcessReturn(int32_t params = -1);
    void ProcessStackCallback(int32_t instr_id, StackInstr* instr, int32_t &instr_index, int32_t params);
    void ProcessIntCallParameter();
    void ProcessFloatCallParameter(); 
    void ProcessFunctionCallParameter();
    void ProcessReturnParameters(MemoryType type);
    void ProcessLoadByteElement(StackInstr* instr);
    void ProcessLoadCharElement(StackInstr* instr);
    void ProcessStoreByteElement(StackInstr* instr);
    void ProcessStoreCharElement(StackInstr* instr);
    void ProcessLoadIntElement(StackInstr* instr);
    void ProcessStoreIntElement(StackInstr* instr);
    void ProcessLoadFloatElement(StackInstr* instr);
    void ProcessStoreFloatElement(StackInstr* instr);
    void ProcessJump(StackInstr* instr);
    void ProcessFloor(StackInstr* instr);
    void ProcessCeiling(StackInstr* instr);
    void ProcessFloatToInt(StackInstr* instr);
    void ProcessIntToFloat(StackInstr* instr);
    
    // Add byte code to buffer
    inline void AddMachineCode(unsigned char b) {
      if(code_index == code_buf_max) {
        code = (unsigned char*)realloc(code, code_buf_max * 2); 
        if(!code) {
          wcerr << L"Unable to allocate memory!" << endl;
          exit(1);
        }
        code_buf_max *= 2;
      }
      code[code_index++] = b;
    }

    // Encodes and writes out a 32-bit integer value
    inline void AddImm(int32_t imm) {
      unsigned char buffer[sizeof(int32_t)];
      ByteEncode32(buffer, imm);
      for(int i = 0; i < (int)sizeof(int32_t); ++i) {
        AddMachineCode(buffer[i]);
      }
    }

    // Encodes and writes out a 16-bit integer value
    inline void AddImm16(int16_t imm) {
      unsigned char buffer[sizeof(int16_t)];
      ByteEncode16(buffer, imm);
      for(int i = 0; i < (int)sizeof(int16_t); ++i) {
        AddMachineCode(buffer[i]);
      }
    }

    // Caculates the IA-32 MOD R/M
    // offset
    inline unsigned ModRM(Register eff_adr, Register mod_rm) {
      unsigned byte = 0;

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
        wcerr << L">>> invalid register reference <<<" << endl;
        exit(1);
        break;
      }

      return byte;
    }

    // Returns the name of a register
    wstring GetRegisterName(Register reg) {
      switch(reg) {
      case EAX:
        return L"eax";

      case EBX:
        return L"ebx";

      case ECX:
        return L"ecx";

      case EDX:
        return L"edx";

      case EDI:
        return L"edi";

      case ESI:
        return L"esi";

      case EBP:
        return L"ebp";

      case ESP:
        return L"esp";

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
      }

      return L"unknown";
    }

    // Encodes a byte array with a 32-bit value
    inline void ByteEncode32(unsigned char buffer[], int32_t value) {
      memcpy(buffer, &value, sizeof(int32_t));
    }

    // Encodes a byte array with a 16-bit value
    inline void ByteEncode16(unsigned char buffer[], int16_t value) {
      memcpy(buffer, &value, sizeof(int16_t));
    }

    // Encodes an array with the 
    // binary ID of a register
    inline void RegisterEncode3(unsigned char& code, int32_t offset, Register reg) {
#ifdef _DEBUG
      assert(offset == 2 || offset == 5);
#endif

      unsigned char reg_id;
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

      default:
        wcerr << L"internal error" << endl;
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
      // is zero
      cmp_imm_reg(0, reg);
      AddMachineCode(0x0f);
      AddMachineCode(0x84);
      deref_offsets.push_back(code_index);
      AddImm(0);
      // jump to exit
    }

    /***********************************
     * Checks array bounds
     **********************************/
    inline void CheckArrayBounds(Register reg, Register max_reg) {
      // less than zero
      cmp_imm_reg(0, reg);
      AddMachineCode(0x0f);
      AddMachineCode(0x8c);
      bounds_less_offsets.push_back(code_index);
      AddImm(0);
      // jump to exit

      // greater than max
      cmp_reg_reg(max_reg, reg);
      AddMachineCode(0x0f);
      AddMachineCode(0x8d);
      bounds_greater_offsets.push_back(code_index);
      AddImm(0);
      // jump to exit
    }

    /***********************************
     * Gets an avaiable register from
     ***********************************/
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
          wcout << L">>> No general registers avaiable! <<<" << endl;
#endif
          aux_regs.push(reg_eax);
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
      wcout << L"\t * allocating " << GetRegisterName(holder->GetRegister())
            << L" *" << endl;
#endif

      return holder;
    }

    // Returns a register to the pool
    void ReleaseRegister(RegisterHolder* h) {
#ifdef _VERBOSE
      wcout << L"\t * releasing " << GetRegisterName(h->GetRegister())
            << L" *" << endl;
#endif

#ifdef _DEBUG
      assert(h->GetRegister() < XMM0);
      for(size_t i  = 0; i < aval_regs.size(); ++i) {
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

    // Gets an avaiable register from
    // the pool of registers
    RegisterHolder* GetXmmRegister() {
      RegisterHolder* holder;
      if(aval_xregs.empty()) {
        compile_success = false;
#ifdef _DEBUG
        wcout << L">>> No XMM registers avaiable! <<<" << endl;
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
      wcout << L"\t * allocating " << GetRegisterName(holder->GetRegister())
            << L" *" << endl;
#endif

      return holder;
    }

    // Returns a register to the pool
    void ReleaseXmmRegister(RegisterHolder* h) {
#ifdef _DEBUG
      assert(h->GetRegister() >= XMM0);
      for(size_t i = 0; i < aval_xregs.size(); ++i) {
        assert(h != aval_xregs[i]);
      }
#endif

#ifdef _VERBOSE
      wcout << L"\t * releasing: " << GetRegisterName(h->GetRegister())
            << L" * " << endl;
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
    void move_reg_mem16(Register src, int32_t offset, Register dest);
    void move_mem16_reg(int32_t offset, Register src, Register dest);
    void move_imm_mem16(int32_t imm, int32_t offset, Register dest);
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
    void xor_imm_reg(int32_t imm, Register reg);
    void xor_reg_reg(Register src, Register dest);
    void xor_mem_reg(int32_t offset, Register src, Register dest);

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
    void shl_reg_reg(Register src, Register dest);
    void shl_mem_reg(int32_t offset, Register src, Register dest);
    void shl_imm_reg(int32_t value, Register dest);

    void shr_reg_reg(Register src, Register dest);
    void shr_mem_reg(int32_t offset, Register src, Register dest);
    void shr_imm_reg(int32_t value, Register dest);

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

    // generates a conditional jump
    bool cond_jmp(InstructionType type);
    void loop(int32_t offset);

    inline static int32_t PopInt(size_t* op_stack, int32_t *stack_pos) {
      int32_t value = op_stack[--(*stack_pos)];
#ifdef _DEBUG
      wcout << L"\t[pop_i: value=" << (int32_t*)value << L"(" << value << L")]" << L"; pos=" << (*stack_pos) << endl;
#endif

      return value;
    }

    inline static void PushInt(size_t* op_stack, int32_t *stack_pos, int32_t value) {
      op_stack[(*stack_pos)++] = value;
#ifdef _DEBUG
      wcout << L"\t[push_i: value=" << (int32_t*)value << L"(" << value << L")]" << L"; pos=" << (*stack_pos) << endl;
#endif
    }

    // Process call backs from ASM code
    static void StackCallback(const int32_t instr_id, StackInstr* instr, const int32_t cls_id, 
                              const int32_t mthd_id, int32_t* inst, size_t* op_stack, int32_t *stack_pos, 
                              StackFrame** call_stack, long* call_stack_pos, const int32_t ip) {
#ifdef _DEBUG
      wcout << L"Stack Call: instr=" << instr_id
            << L", oper_1=" << instr->GetOperand() << L", oper_2=" << instr->GetOperand2() 
            << L", oper_3=" << instr->GetOperand3() << L", self=" << inst << L"(" << (size_t)inst << L"), stack=" 
            << op_stack << L", stack_addr=" << stack_pos << L", stack_pos=" << (*stack_pos) << endl;
#endif
      switch(instr_id) {
      case MTHD_CALL:
      case DYN_MTHD_CALL: {
#ifdef _DEBUG
        wcout << L"jit oper: MTHD_CALL: cls=" << instr->GetOperand() << L", mthd=" << instr->GetOperand2() << endl;
#endif
        StackInterpreter intpr(call_stack, call_stack_pos);
        intpr.Execute(op_stack, (long*)stack_pos, ip, program->GetClass(cls_id)->GetMethod(mthd_id), (size_t*)inst, true);
      }
        break;

      case LOAD_ARY_SIZE: {
        size_t* array = (size_t*)PopInt(op_stack, stack_pos);
        if(!array) {
          wcerr << L"Atempting to dereference a 'Nil' memory instance" << endl;
          wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
          exit(1);
        }
        PushInt(op_stack, stack_pos, array[2]);
      }
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

        size++;
        int32_t* mem = (int32_t*)MemoryManager::AllocateArray(size + ((dim + 2) * sizeof(int32_t)), BYTE_ARY_TYPE, op_stack, *stack_pos);
        mem[0] = size - 1;
        mem[1] = dim;
        memcpy(mem + 2, indices, dim * sizeof(int32_t));
        PushInt(op_stack, stack_pos, (int32_t)mem);

#ifdef _DEBUG
        wcout << L"jit oper: NEW_BYTE_ARY: dim=" << dim << L"; size=" << size 
              << L"; index=" << (*stack_pos) << L"; mem=" << mem << endl;
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
        int32_t* mem = (int32_t*)MemoryManager::AllocateArray(size + ((dim + 2) * sizeof(int32_t)), 
                                                              CHAR_ARY_TYPE, op_stack, *stack_pos);
        mem[0] = size - 1;
        mem[1] = dim;
        memcpy(mem + 2, indices, dim * sizeof(int32_t));
        PushInt(op_stack, stack_pos, (int32_t)mem);

#ifdef _DEBUG
        wcout << L"jit oper: NEW_CHAR_ARY: dim=" << dim << L"; size=" << size 
              << L"; index=" << (*stack_pos) << L"; mem=" << mem << endl;
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

        int32_t* mem = (int32_t*)MemoryManager::AllocateArray(size + dim + 2, INT_TYPE, op_stack, *stack_pos);
#ifdef _DEBUG
        wcout << L"jit oper: NEW_INT_ARY: dim=" << dim << L"; size=" << size 
              << L"; index=" << (*stack_pos) << L"; mem=" << mem << endl;
#endif
        mem[0] = size;
        mem[1] = dim;
        memcpy(mem + 2, indices, dim * sizeof(int32_t));
        PushInt(op_stack, stack_pos, (int32_t)mem);
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

        size *= 2;
        int32_t* mem = (int32_t*)MemoryManager::AllocateArray(size + dim + 2, INT_TYPE, op_stack, *stack_pos);
#ifdef _DEBUG
        wcout << L"jit oper: NEW_FLOAT_ARY: dim=" << dim << L"; size=" << size 
              << L"; index=" << (*stack_pos) << L"; mem=" << mem << endl; 
#endif
        mem[0] = size / 2;
        mem[1] = dim;
        memcpy(mem + 2, indices, dim * sizeof(int32_t));
        PushInt(op_stack, stack_pos, (int32_t)mem);
      }
        break;

      case NEW_OBJ_INST: {
#ifdef _DEBUG
        wcout << L"jit oper: NEW_OBJ_INST: id=" << instr->GetOperand() << endl; 
#endif
        int32_t* mem = (int32_t*)MemoryManager::AllocateObject(instr->GetOperand(), 
                                                               op_stack, *stack_pos);
        PushInt(op_stack, stack_pos, (int32_t)mem);
      }
        break;

      case OBJ_TYPE_OF: {
        size_t* mem = (size_t*)PopInt(op_stack, stack_pos);
        size_t* result = MemoryManager::ValidObjectCast(mem, instr->GetOperand(),
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
        int32_t* mem = (int32_t*)PopInt(op_stack, stack_pos);
        int32_t to_id = instr->GetOperand();
#ifdef _DEBUG
        wcout << L"jit oper: OBJ_INST_CAST: from=" << mem << L", to=" << to_id << endl; 
#endif	
        int32_t result = (int32_t)MemoryManager::ValidObjectCast((size_t*)mem, to_id, 
                                                                 program->GetHierarchy(), program->GetInterfaces());
        if(!result && mem) {
          StackClass* to_cls = MemoryManager::GetClass((size_t*)mem);	  
          wcerr << L">>> Invalid object cast: '" << (to_cls ? to_cls->GetName() : L"?" )  
                << L"' to '" << program->GetClass(to_id)->GetName() << L"' <<<" << endl;
          wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
          exit(1);
        }
        PushInt(op_stack, stack_pos, result);
      }
        break;

        //----------- threads -----------

      case THREAD_JOIN: {
        int32_t* instance = inst;
        if(!instance) {
          wcerr << L"Atempting to dereference a 'Nil' memory instance" << endl;
          wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
          exit(1);
        }
#ifdef _WIN32
        HANDLE vm_thread = (HANDLE)instance[0];
        if(WaitForSingleObject(vm_thread, INFINITE) != WAIT_OBJECT_0) {
          wcerr << L"Unable to join thread!" << endl;
          exit(-1);
        }
#else
        void* status;
        pthread_t vm_thread = (pthread_t)instance[0];      
        if(pthread_join(vm_thread, &status)) {
          wcerr << L"Unable to join thread!" << endl;
          exit(-1);
        }
#endif
      }
        break;

      case THREAD_SLEEP:
#ifdef _WIN32
        Sleep(PopInt(op_stack, stack_pos));
#else
        usleep(PopInt(op_stack, stack_pos)* 1000);
#endif
        break;

      case THREAD_MUTEX: {
        int32_t* instance = inst;
        if(!instance) {
          wcerr << L"Atempting to dereference a 'Nil' memory instance" << endl;
          wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
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
        int32_t* instance = (int32_t*)PopInt(op_stack, stack_pos);
        if(!instance) {
          wcerr << L"Atempting to dereference a 'Nil' memory instance" << endl;
          wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
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
        int32_t* instance = (int32_t*)PopInt(op_stack, stack_pos);
        if(!instance) {
          wcerr << L"Atempting to dereference a 'Nil' memory instance" << endl;
          wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
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
        long length = PopInt(op_stack, stack_pos);;
        const long src_offset = PopInt(op_stack, stack_pos);;
        size_t* src_array = (size_t*)PopInt(op_stack, stack_pos);;
        const long dest_offset = PopInt(op_stack, stack_pos);;
        size_t* dest_array = (size_t*)PopInt(op_stack, stack_pos);;      

        if(!src_array || !dest_array) {
          wcerr << L">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
          wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
          exit(1);
        }

        const long src_array_len = src_array[2];
        const long dest_array_len = dest_array[2];
        if(length > 0 && src_offset + length <= src_array_len && dest_offset + length <= dest_array_len) {
          unsigned char* src_array_ptr = (unsigned char*)(src_array + 3);
          unsigned char* dest_array_ptr = (unsigned char*)(dest_array + 3);
          memcpy(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length);
          PushInt(op_stack, stack_pos, 1);
        }
        else {
          PushInt(op_stack, stack_pos, 0);
        }
      }
        break;

      case CPY_CHAR_ARY: {
        long length = PopInt(op_stack, stack_pos);;
        const long src_offset = PopInt(op_stack, stack_pos);;
        size_t* src_array = (size_t*)PopInt(op_stack, stack_pos);;
        const long dest_offset = PopInt(op_stack, stack_pos);;
        size_t* dest_array = (size_t*)PopInt(op_stack, stack_pos);;      

        if(!src_array || !dest_array) {
          wcerr << L">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
          wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
          exit(1);
        }

        const long src_array_len = src_array[2];
        const long dest_array_len = dest_array[2];

        if(length > 0 && src_offset + length <= src_array_len && dest_offset + length <= dest_array_len) {
          wchar_t* src_array_ptr = (wchar_t*)(src_array + 3);
          wchar_t* dest_array_ptr = (wchar_t*)(dest_array + 3);
          memcpy(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length * sizeof(wchar_t));
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
        size_t* src_array = (size_t*)PopInt(op_stack, stack_pos);;
        const long dest_offset = PopInt(op_stack, stack_pos);;
        size_t* dest_array = (size_t*)PopInt(op_stack, stack_pos);;      

        if(!src_array || !dest_array) {
          wcerr << L">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
          wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
          exit(1);
        }

        const long src_array_len = src_array[0];
        const long dest_array_len = dest_array[0];
        if(length > 0 && src_offset + length <= src_array_len && dest_offset + length <= dest_array_len) {
          size_t* src_array_ptr = src_array + 3;
          size_t* dest_array_ptr = dest_array + 3;
          memcpy(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length * sizeof(size_t));
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
        size_t* src_array = (size_t*)PopInt(op_stack, stack_pos);;
        const long dest_offset = PopInt(op_stack, stack_pos);;
        size_t* dest_array = (size_t*)PopInt(op_stack, stack_pos);;      

        if(!src_array || !dest_array) {
          wcerr << L">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
          wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
          exit(1);
        }

        const long src_array_len = src_array[0];
        const long dest_array_len = dest_array[0];
        if(length > 0 && src_offset + length <= src_array_len && dest_offset + length <= dest_array_len) {
          size_t* src_array_ptr = src_array + 3;
          size_t* dest_array_ptr = dest_array + 3;
          memcpy(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length * sizeof(FLOAT_VALUE));
          PushInt(op_stack, stack_pos, 1);
        }
        else {
          PushInt(op_stack, stack_pos, 0);
        }
      }
        break;

      case TRAP:
      case TRAP_RTRN: {
        size_t* stack = (size_t*)op_stack;
        long* pos = (long*)stack_pos;
        if(!TrapProcessor::ProcessTrap(program, (size_t*)inst, stack, pos, NULL)) {
          wcerr << L"  JIT compiled machine code..." << endl;
          exit(1);
        }
      }
        break;
	
#ifdef _DEBUG
      default:
        wcerr << L"Unknown callback!" << endl;
        break;
	
        wcout << L"  ending stack: pos=" << (*stack_pos) << endl;
#endif
      }
    } 
    
    // Calculates array element offset. 
    // Note: this code must match up 
    // with the interpreter's 'ArrayIndex'
    // method. Bounds checks are not done on
    // JIT code.
    RegisterHolder* ArrayIndex(StackInstr* instr, MemoryType type) {
      RegInstr* holder = working_stack.front();
      working_stack.pop_front();

      RegisterHolder* array_holder;
      switch(holder->GetType()) {
      case IMM_INT:
        wcerr << L">>> trying to index a constant! <<<" << endl;
        exit(1);
        break;

      case REG_INT:
        array_holder = holder->GetRegister();
        break;

      case MEM_INT:
        array_holder = GetRegister();
        move_mem_reg(holder->GetOperand(), EBP, array_holder->GetRegister());
        break;

      default:
        wcerr << L"internal error" << endl;
        exit(1);
        break;
      }
      CheckNilDereference(array_holder->GetRegister());

      /* Algorithm:
         int32_t index = PopInt();
         const int32_t dim = instr->GetOperand();

         for(int i = 1; i < dim; ++i) {
           index *= array[i];
           index += PopInt();
         }
      */

      delete holder;
      holder = NULL;

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
        move_mem_reg(holder->GetOperand(), EBP, index_holder->GetRegister());
        break;

      default:
        wcerr << L"internal error" << endl;
        exit(1);
        break;
      }

      const int32_t dim = instr->GetOperand();
      for(int i = 1; i < dim; ++i) {
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
        case IMM_INT:
          add_imm_reg(holder->GetOperand(), index_holder->GetRegister());
          break;

        case REG_INT:
          add_reg_reg(holder->GetRegister()->GetRegister(), 
                      index_holder->GetRegister());
          break;

        case MEM_INT:
          add_mem_reg(holder->GetOperand(), EBP, index_holder->GetRegister());
          break;

        default:
          wcerr << L"internal error" << endl;
          exit(1);
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

#ifdef _WIN32
      case CHAR_ARY_TYPE:
        shl_imm_reg(1, index_holder->GetRegister());
        shl_imm_reg(1, bounds_holder->GetRegister());
        break;
#else
      case CHAR_ARY_TYPE:
#endif
      case INT_TYPE:
        shl_imm_reg(2, index_holder->GetRegister());
        shl_imm_reg(2, bounds_holder->GetRegister());
        break;

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
      add_imm_reg((instr->GetOperand() + 2) * sizeof(int32_t), index_holder->GetRegister());
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
      wcout << L"Calculating indices for variables..." << endl;
#endif
      multimap<int32_t, StackInstr*> values;
      for(int32_t i = 0; i < method->GetInstructionCount(); ++i) {
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
          values.insert(pair<int32_t, StackInstr*>(instr->GetOperand(), instr));
          break;

        default:
          break;
        }
      }

      int32_t index = TMP_REG_5;
      int32_t last_id = -1;
      multimap<int32_t, StackInstr*>::iterator value;
      for(value = values.begin(); value != values.end(); ++value) {
        int32_t id = value->first;
        StackInstr* instr = (*value).second;
        // instance reference
        if(instr->GetOperand2() == INST || instr->GetOperand2() == CLS) {
          // note: all instance variables are allocted in 4-byte blocks,
          // for floats the assembler allocates 2 4-byte blocks
          instr->SetOperand3(instr->GetOperand() * sizeof(int32_t));
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
              index -= sizeof(int32_t);
            }
            else if(instr->GetType() == LOAD_FUNC_VAR || 
                    instr->GetType() == STOR_FUNC_VAR) {
              index -= sizeof(int32_t) * 2;
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
          wcout << L"native memory: index=" << instr->GetOperand() << L"; jit index="
                << instr->GetOperand3() << endl;
        }
        else {
          wcout << L"native stack: index=" << instr->GetOperand() << L"; jit index="
                << instr->GetOperand3() << endl;
        }
#endif
      }
      local_space = -(index + TMP_REG_5);

#ifdef _DEBUG
      wcout << L"Local space required: " << (local_space + 8) << L" byte(s)" << endl;
#endif
    }

  public: 
    static void Initialize(StackProgram* p);

    JitCompilerIA32() {
    }

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

    //
    // Compiles stack code into IA-32 machine code
    //
    inline bool Compile(StackMethod* cm) {
      compile_success = true;

      if(!cm->GetNativeCode()) {
        skip_jump = false;
        method = cm;

        int32_t cls_id = method->GetClass()->GetId();
        int32_t mthd_id = method->GetId();
#ifdef _DEBUG
        wcout << L"---------- Compiling Native Code: method_id=" << cls_id << L"," 
              << mthd_id << L"; mthd_name='" << method->GetName() << L"'; params=" 
              << method->GetParamCount() << L" ----------" << endl;
#endif

        code_buf_max = BUFFER_SIZE;
        code = (unsigned char*)malloc(code_buf_max);
#ifdef _WIN32
        floats = new double[MAX_DBLS];
#else
        if(posix_memalign((void**)&floats, PAGE_SIZE, sizeof(double) * MAX_DBLS)) {
          wcerr << L"Unable to allocate JIT memory!" << endl;
          exit(1);
        }
#endif

        floats_index = instr_index = code_index = epilog_index = instr_count = 0;
        // general use registers
        reg_eax = new RegisterHolder(EAX);
        aval_regs.push_back(new RegisterHolder(EDX));
        aval_regs.push_back(new RegisterHolder(ECX));
        aval_regs.push_back(new RegisterHolder(EBX));
        aval_regs.push_back(reg_eax);
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
        wcout << L"Compiling code for IA-32 architecture..." << endl;
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
        ProcessParameters(method->GetParamCount());
        // tranlsate program
        ProcessInstructions();
        if(!compile_success) {
          return false;
        }

        // show content
        unordered_map<int32_t, StackInstr*>::iterator iter;
        for(iter = jump_table.begin(); iter != jump_table.end(); ++iter) {
          StackInstr* instr = iter->second;
          const int32_t src_offset = iter->first;
          const int32_t dest_index = method->GetLabelIndex(instr->GetOperand()) + 1;
          const int32_t dest_offset = method->GetInstruction(dest_index)->GetOffset();
          const int32_t offset = dest_offset - src_offset - 4;
          memcpy(&code[src_offset], &offset, 4); 
#ifdef _DEBUG
          wcout << L"jump update: src=" << src_offset << L"; dest=" << dest_offset << endl;
#endif
        }

        for(size_t i = 0; i < deref_offsets.size(); ++i) {
          const int32_t index = deref_offsets[i];
          int32_t offset = epilog_index - index + 1;
          memcpy(&code[index], &offset, 4);
        }

        for(size_t i = 0; i < bounds_less_offsets.size(); ++i) {
          const int32_t index = bounds_less_offsets[i];
          int32_t offset = epilog_index - index + 11;
          memcpy(&code[index], &offset, 4);
        }
            
        for(size_t i = 0; i < bounds_greater_offsets.size(); ++i) {
          const int32_t index = bounds_greater_offsets[i];
          int32_t offset = epilog_index - index + 21;
          memcpy(&code[index], &offset, 4);
        }

#ifdef _DEBUG
        wcout << L"Caching JIT code: actual=" << code_index << L", buffer=" << code_buf_max << L" byte(s)" << endl;
#endif
        // store compiled code
        method->SetNativeCode(new NativeCode(page_manager->GetPage(code, code_index), code_index, floats));
        free(code);
        code = NULL;

        compile_success = true;
      }

      return compile_success;
    }
  };    

  /********************************
   * JitExecutor class
   ********************************/
  class JitExecutor {
    static StackProgram* program;
    StackMethod* method;
    unsigned char* code;
    int32_t code_index; 
    double* floats;

    int32_t ExecuteMachineCode(int32_t cls_id, int32_t mthd_id, size_t* inst, unsigned char* code, 
                               const int32_t code_size, size_t* op_stack, long* stack_pos,
                               StackFrame** call_stack, long* call_stack_pos, StackFrame* frame);

  public:
    static void Initialize(StackProgram* p);

    JitExecutor() {
    }

    ~JitExecutor() {
    }    
    
    // Executes machine code
    inline long Execute(StackMethod* cm, size_t* inst, size_t* op_stack, long* stack_pos, StackFrame** call_stack, long* call_stack_pos, StackFrame* frame) {
      method = cm;
      int32_t cls_id = method->GetClass()->GetId();
      int32_t mthd_id = method->GetId();

#ifdef _DEBUG
      wcout << L"=== MTHD_CALL (native): id=" << cls_id << L"," << mthd_id 
            << L"; name='" << method->GetName() << L"'; self=" << inst << L"(" << (size_t)inst 
            << L"); stack=" << op_stack << L"; stack_pos=" << (*stack_pos) << L"; params=" 
            << method->GetParamCount() << L" ===" << endl;
      assert((*stack_pos) >= method->GetParamCount());
#endif
      
      NativeCode* native_code = method->GetNativeCode();
      code = native_code->GetCode();
      code_index = native_code->GetSize();
      floats = native_code->GetFloats();
      
      // execute
      return ExecuteMachineCode(cls_id, mthd_id, inst, code, code_index, op_stack, stack_pos, call_stack, call_stack_pos, frame);
    }
  };
}
#endif
