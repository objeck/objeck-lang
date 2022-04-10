/***************************************************************************
 * JIT compiler for 32-bit x86 architectures (Windows and Linux).
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

#define MAX_DBLS 256
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
      instr = nullptr;
    }  

    RegInstr(StackInstr* si, double* da) {
      type = IMM_FLOAT;
      operand = (long)da;
      holder = nullptr;
      instr = nullptr;
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
#ifdef _DEBUG_JIT
        assert(false);
#endif
        break;
      }
      instr = si;
      holder = nullptr;
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
      buffer = nullptr;
    }

    inline bool CanAddCode(int32_t size) {
      if(available - size > 0) {
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
    PageManager();

    ~PageManager();

    unsigned char* GetPage(unsigned char* code, int32_t size);
  };
  
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
    vector<int32_t> div_by_zero_offsets;    // code -4
    int32_t local_space;
    StackMethod* method;
    int32_t instr_count;
    unsigned char* code;
    int32_t code_index;
    int32_t epilog_index;
    double* float_consts;     
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
    void ProcessFloatOperation(StackInstr* instruction);
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
        code_buf_max *= 2;
        code = (unsigned char*)realloc(code, code_buf_max); 
        if(!code) {
          wcerr << L"Unable to allocate memory!" << endl;
          exit(1);
        }
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
    unsigned ModRM(Register eff_adr, Register mod_rm);

    // Returns the name of a register
    wstring GetRegisterName(Register reg);

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
    void RegisterEncode3(unsigned char& code, int32_t offset, Register reg);

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
     * Check for divide by 0
     **********************************/
    inline void CheckDivideByZero(Register reg) {
      // is zero
      cmp_imm_reg(0, reg);
      AddMachineCode(0x0f);
      AddMachineCode(0x84);
      div_by_zero_offsets.push_back(code_index);
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
     * Gets an available register from
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
#ifdef _DEBUG_JIT
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

#ifdef _DEBUG_JIT
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
#ifdef _DEBUG_JIT
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
#ifdef _DEBUG_JIT
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

    // float functions
    void fld_mem(int32_t offset, Register src);
    void fstp_mem(int32_t offset, Register src);
    void fsin();
    void fcos();
    void ftan();
    void fsqrt();
    
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
    RegisterHolder* call_xfunc(double (*func_ptr)(double), RegInstr* left);
    RegisterHolder* call_xfunc2(double (*func_ptr)(double, double), RegInstr* left);
    
    // generates a conditional jump
    bool cond_jmp(InstructionType type);
    void loop(int32_t offset);

    inline static int32_t PopInt(size_t* op_stack, int32_t *stack_pos) {
      int32_t value = op_stack[--(*stack_pos)];
#ifdef _DEBUG_JIT
      wcout << L"\t[pop_i: value=" << (int32_t*)value << L"(" << value << L")]" << L"; pos=" << (*stack_pos) << endl;
#endif

      return value;
    }

    inline static void PushInt(size_t* op_stack, int32_t *stack_pos, int32_t value) {
      op_stack[(*stack_pos)++] = value;
#ifdef _DEBUG_JIT
      wcout << L"\t[push_i: value=" << (int32_t*)value << L"(" << value << L")]" << L"; pos=" << (*stack_pos) << endl;
#endif
    }

    inline static FLOAT_VALUE PopFloat(size_t* op_stack, int32_t* stack_pos) {
      (*stack_pos) -= 2;
      
#ifdef _DEBUG_JIT
      FLOAT_VALUE v = *((FLOAT_VALUE*)(&op_stack[(*stack_pos)]));
      wcout << L"  [pop_f: stack_pos=" << (*stack_pos) << L"; value=" << L"]; pos=" << (*stack_pos) << endl;
      return v;
#endif
      
      return *((FLOAT_VALUE*)(&op_stack[(*stack_pos)]));
    }
    
    inline static void PushFloat(const FLOAT_VALUE v, size_t* op_stack, int32_t* stack_pos) {
#ifdef _DEBUG_JIT
      wcout << L"  [push_f: stack_pos=" << (*stack_pos) << L"; value=" << v
            << L"]; call_pos=" << (*stack_pos) << endl;
#endif
      *((FLOAT_VALUE*)(&op_stack[(*stack_pos)])) = v;
      (*stack_pos) += 2;
    }
    
    // Process call backs from ASM code
    static void JitStackCallback(const int32_t instr_id, StackInstr* instr, const int32_t cls_id, 
                              const int32_t mthd_id, int32_t* inst, size_t* op_stack, int32_t *stack_pos, 
                              StackFrame** call_stack, long* call_stack_pos, const int32_t ip); 
    
    // Calculates array element offset. 
    // Note: this code must match up 
    // with the interpreter's 'ArrayIndex'
    // method. Bounds checks are not done on
    // JIT code.
    RegisterHolder* ArrayIndex(StackInstr* instr, MemoryType type);

    // Calculates the indices for
    // memory references.
    void ProcessIndices();

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
          instr = nullptr;
        }
      }      

      while(!aval_regs.empty()) {
        RegisterHolder* holder = aval_regs.back();
        aval_regs.pop_back();
        if(holder) {
          delete holder;
          holder = nullptr;
        }
      }

      while(!aval_xregs.empty()) {
        RegisterHolder* holder = aval_xregs.back();
        aval_xregs.pop_back();
        if(holder) {
          delete holder;
          holder = nullptr;
        }
      }

      while(!used_regs.empty()) {
        RegisterHolder* holder = used_regs.front();
        if(holder) {
          delete holder;
          holder = nullptr;
        }
        // next
        used_regs.pop_front();
      }
      used_regs.clear();

      while(!used_xregs.empty()) {
        RegisterHolder* holder = used_xregs.front();
        if(holder) {
          delete holder;
          holder = nullptr;
        }
        // next
        used_xregs.pop_front();
      }
      used_xregs.clear();

      while(!aux_regs.empty()) {
        RegisterHolder* holder = aux_regs.top();
        if(holder) {
          delete holder;
          holder = nullptr;
        }
        aux_regs.pop();
      }
    }

    //
    // Compiles stack code into IA-32 machine code
    //
    bool Compile(StackMethod* cm);
  };    

  /********************************
   * Prototype for JIT function
   ********************************/
  typedef int32_t(*jit_fun_ptr)(int32_t cls_id, int32_t mthd_id, size_t* cls_mem, size_t* inst, size_t* op_stack, 
                                long* stack_pos, StackFrame** call_stack, long* call_stack_pos, size_t** jit_mem, long* offset);

  /********************************
   * JitExecutor class
   ********************************/
  class JitExecutor {
    static StackProgram* program;

  public:
    static void Initialize(StackProgram* p);

    // Executes machine code
    long Execute(StackMethod* method, size_t* inst, size_t* op_stack, long* stack_pos,
                 StackFrame** call_stack, long* call_stack_pos, StackFrame* frame);
  };
}
#endif
