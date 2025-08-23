/************************************************************************
 * JIT compiler for x86-64 architectures (Windows, macOS and Linux).
 *
 * Copyright (c) 2025, Randy Hollines
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
 *************************************************************************/

#pragma once

#include "../jit_common.h"

namespace Runtime {
#ifdef _WIN64
  // offsets for Win64
#define CLS_ID 16
#define MTHD_ID 24
#define CLASS_MEM 32
#define INSTANCE_MEM 40
#define OP_STACK 48
#define STACK_POS 56
#define CALL_STACK 64
#define CALL_STACK_POS 72
#define JIT_MEM 80
#define JIT_OFFSET 88
  // float temps
#define TMP_XMM_0 -8
#define TMP_XMM_1 -16
#define TMP_XMM_2 -24
  // integer temps
#define TMP_REG_0 -32
#define TMP_REG_1 -40
#define TMP_REG_2 -48
#define TMP_REG_3 -56
#define TMP_REG_4 -64
#define TMP_REG_5 -72
#define RED_ZONE -80  
#else
  // offset for Posix 64-bit
#define CLS_ID -8
#define MTHD_ID -16
#define CLASS_MEM -24
#define INSTANCE_MEM -32
#define OP_STACK -40
#define STACK_POS -48
#define CALL_STACK 16
#define CALL_STACK_POS 24
#define JIT_MEM 32
#define JIT_OFFSET 40
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
#endif

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

  /**
   * RegisterHolder class
   */
  class RegisterHolder {
    Register reg;

  public:
    RegisterHolder(Register r) {
      reg = r;
    }

    ~RegisterHolder() {
    }

    inline Register GetRegister() {
      return reg;
    }
  };

  /**
   * RegInstr class
   */
  class RegInstr {
    RegType type;
    int64_t operand;
#ifdef _WIN64
    size_t operand2;
#endif  
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

    RegInstr(double* da) {
      type = IMM_FLOAT;
#ifdef _WIN64    
      operand2 = (size_t)da;
#else
      operand = (int64_t)da;
#endif    
      holder = nullptr;
      instr = nullptr;
    }

    RegInstr(RegType t, int64_t o) {
      type = t;
      operand = o;
    }

    RegInstr(StackInstr* si);

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

    void SetOperand(int64_t o) {
      operand = o;
    }

    int64_t GetOperand() {
      return operand;
    }

#ifdef _WIN64
    void SetOperand2(size_t o2) {
      operand2 = o2;
    }

    size_t GetOperand2() {
      return operand2;
    }
#endif  
  };

  /**
   * Manage executable buffers of memory
   */
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
      available = factor * PAGE_SIZE;
    
#ifdef _WIN64    
      buffer = (unsigned char*)VirtualAlloc(nullptr, available, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
      if(!buffer) {
        std::wcerr << L"Unable to allocate JIT memory!" << std::endl;
        exit(1);
      }
#else
      if(posix_memalign((void**)&buffer, PAGE_SIZE, available)) {
        std::wcerr << L"Unable to allocate JIT memory!" << std::endl;
        exit(1);
      }
      if(mprotect(buffer, available, PROT_READ | PROT_WRITE | PROT_EXEC) < 0) {
        std::wcerr << L"Unable to mprotect" << std::endl;
        exit(1);
      }
#endif    
    }

    PageHolder();

    ~PageHolder() {
#ifdef _WIN64  
      VirtualFree(buffer, 0, MEM_RELEASE);
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

  /**
   * Manage executable buffers of memory
   */
  class PageManager {
    std::vector<PageHolder*> holders;

  public:
    PageManager();

    ~PageManager();

    unsigned char* GetPage(unsigned char* code, int32_t size);
  };
  
  /**
   * JIT compiler class for AMD64
   */
  class JitAmd64 : public JitCompiler {
    static PageManager* page_manager;
    std::deque<RegInstr*> working_stack;
    std::vector<RegisterHolder*> aval_regs;
    std::list<RegisterHolder*> used_regs;
    std::stack<RegisterHolder*> aux_regs;
    RegisterHolder* rax_reg;
    std::vector<RegisterHolder*> aval_xregs;
    std::list<RegisterHolder*> used_xregs;
    std::unordered_map<long, StackInstr*> jump_table; // jump addresses
    std::vector<long> nil_deref_offsets;      // code -1
    std::vector<long> bounds_less_offsets;    // code -2
    std::vector<long> bounds_greater_offsets; // code -3
    std::vector<long> div_by_zero_offsets;    // code -4
    long local_space, org_local_space;
    StackMethod* method;
    long instr_count;
    unsigned char* code;
    long code_index;
    long epilog_index;
    double* float_consts;
    long floats_index;
    long instr_index;
    long code_buf_max;
    bool compile_success;
    bool skip_jump;

    // setup and tear down
    void Prolog();
    void Epilog();

    // stack conversion operations
    void ProcessParameters(long count);
    void RegisterRoot();
    void ProcessInstructions();
    void ProcessNot(StackInstr* instr);
    void ProcessLoad(StackInstr* instr);
    void ProcessStore(StackInstr* instruction);
    void ProcessCopy(StackInstr* instr);
    RegInstr* ProcessIntFold(int64_t left_imm, int64_t right_imm, InstructionType type);
    void ProcessIntCalculation(StackInstr* instruction);
    void ProcessFloatCalculation(StackInstr* instruction);
    
    void ProcessFloatOperation(StackInstr* instruction);
    void ProcessFloatSquareRoot(StackInstr* instruction);
    void ProcessFloatRound(StackInstr* instruction, wchar_t mode);

    void ProcessReturn(long params = -1);
    void ProcessStackCallback(long instr_id, StackInstr* instr, long &instr_index, long params);
    void ProcessFunctionCallParameter();
    void ProcessIntCallParameter();
    void ProcessFloatCallParameter();
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
    void ProcessFloatToInt(StackInstr* instr);
    void ProcessIntToFloat(StackInstr* instr);

    /**
     * Add byte code to buffer
     */
    void AddMachineCode(unsigned char b) {
      if(code_index == code_buf_max) {
        code = (unsigned char*)realloc(code, code_buf_max * 2);
        if(!code) {
          std::wcerr << L"Unable to allocate memory!" << std::endl;
          exit(1);
        }

        code_buf_max *= 2;
      }
      code[code_index++] = b;
    }

    /**
     * Encodes and writes out 32-bit integer values; note sizeof(int)
     */
    inline void AddImm(int imm) {
      unsigned char buffer[sizeof(int)];
      ByteEncode32(buffer, imm);
      for(size_t i = 0; i < sizeof(int); ++i) {
        AddMachineCode(buffer[i]);
      }
    }

    /**
    * Encodes and writes out a 16-bit integer value
    */
    inline void AddImm16(int16_t imm) {
      unsigned char buffer[sizeof(int16_t)];
      ByteEncode16(buffer, imm);
      for(size_t i = 0; i < sizeof(int16_t); ++i) {
        AddMachineCode(buffer[i]);
      }
    }

    /**
     * Encodes and writes out 64-bit integer values
     */
#ifdef _WIN64   
    inline void AddImm64(size_t imm) {
      unsigned char buffer[sizeof(size_t)];
      ByteEncode64(buffer, imm);
      for(size_t i = 0; i < sizeof(size_t); ++i) {
        AddMachineCode(buffer[i]);
      }
  }  
#else
    inline void AddImm64(long imm) {
      unsigned char buffer[sizeof(int64_t)];
      ByteEncode64(buffer, imm);
      for(size_t i = 0; i < sizeof(int64_t); ++i) {
        AddMachineCode(buffer[i]);
      }    
    }
#endif

    /**
     * Encoding for AMD64 "REX.W" bits
     */
    inline unsigned char REXW(Register reg) {
      switch(reg) {
      case RAX:
      case XMM0:
      case R8:
      case XMM8:
        return 0xd0;

      case RBX:
      case XMM3:
      case R11:
      case XMM11:
        return 0xd0 + 0x3;

      case RCX:
      case XMM1:
      case R9:
      case XMM9:
        return 0xd0 + 0x1;

      case RDX:
      case XMM2:
      case R10:
      case XMM10:
        return 0xd0 + 0x2;

      case RDI:
      case XMM7:
      case R15:
      case XMM15:
        return 0xd0 + 0x7;

      case RSI:
      case XMM6:
      case R14:
      case XMM14:
        return 0xd0 + 0x6;

      case RSP:
      case XMM4:
      case R12:
      case XMM12:
        return 0xd0 + 0x4;

      case RBP:
      case XMM5:
      case R13:
      case XMM13:
        return 0xd0 + 0x5;

      default:
        std::wcerr << L"internal error" << std::endl;
        exit(1);
        break;
      }

      return 0;
    }

    /**
     * Encoding for AMD64 "B" bits
     */
    inline unsigned char B(Register b) {
      if((b > RSP && b < XMM0) || b > XMM7) {
        return 0x49;
      }

      return 0x48;
    }

    /**
     * Encoding for AMD64 "XB" bits
     */
    inline unsigned char XB(Register b) {
      if((b > RSP && b < XMM0) || b > XMM7) {
        return 0x4b;
      }

      return 0x4a;
    }

    /**
     * Encoding for AMD64 "XB" bits
     */
    inline unsigned char XB32(Register b) {
      if((b > RSP && b < XMM0) || b > XMM7) {
        return 0x66;
      }

      return 0x67;
    }

    /**
     * Encoding for AMD64 "RXB" bits
     */
    inline unsigned char RXB(Register r, Register b) {
      unsigned char value = 0x4a;
      if((r > RSP && r < XMM0) || r > XMM7) {
        value += 0x4;
      }

      if((b > RSP && b < XMM0) || b > XMM7) {
        value += 0x1;
      }

      return value;
    }

    /**
     * Encoding for AMD64 "RXB" bits
     */
    inline unsigned char RXB32(Register r, Register b) {
      unsigned char value = 0x42;
      if((r > RSP && r < XMM0) || r > XMM7) {
        value += 0x4;
      }

      if((b > RSP && b < XMM0) || b > XMM7) {
        value += 0x1;
      }

      return value;
    }

    /**
     * Encoding for AMD64 "ROB" bits
     */
    inline unsigned char ROB(Register r, Register b) {
      unsigned char value = 0x48;
      if((r > RSP && r < XMM0) || r > XMM7) {
        value += 0x4;
      }

      if((b > RSP && b < XMM0) || b > XMM7) {
        value += 0x1;
      }

      return value;
    }

    /**
     * Calculates the AMD64 MOD R/M offset
     */
    unsigned char ModRM(Register eff_adr, Register mod_rm);

    /**
     * Returns the name of a register
     */
    std::wstring GetRegisterName(Register reg);

    /**
     * Encodes a byte array with a 32-bit value
     */
    inline void ByteEncode32(unsigned char buffer[], int value) {
      memcpy(buffer, &value, sizeof(int));
    }

    /**
     * Encodes a byte array with a 16-bit value
     */
    inline void ByteEncode16(unsigned char buffer[], int16_t value) {
      memcpy(buffer, &value, sizeof(int16_t));
    }

    /**
     * Encodes a byte array with a 64-bit value
     */
#ifdef _WIN64
    inline void ByteEncode64(unsigned char buffer[], size_t value) {
      memcpy(buffer, &value, sizeof(size_t));
    }
#else
  inline void ByteEncode64(unsigned char buffer[], long value) {
      memcpy(buffer, &value, sizeof(int64_t));
    }
#endif      

    /**
     * Encodes an array with the binary ID of a register
     */
    void RegisterEncode3(unsigned char& code, long offset, Register reg);

    /**
     * Check for 'Nil' dereferencing
     */
    inline void CheckNilDereference(Register reg) {
      cmp_imm_reg(0, reg);
#ifdef _DEBUG_JIT
      std::wcout << L"  " << (++instr_count) << L": [je <err>]" << std::endl;
#endif
      // jump not equal
      AddMachineCode(0x0f);
      AddMachineCode(0x84);
      nil_deref_offsets.push_back(code_index);
      AddImm(0);
      // jump to exit
    }

    /**
     * Check for divide by 0
     */
    inline void CheckDivideByZero(Register reg) {
      if(reg < XMM0) {
        cmp_imm_reg(0, reg);
      }
      else {
        cmp_imm_xreg((size_t)&float_consts[0], reg);
      }
#ifdef _DEBUG_JIT
      std::wcout << L"  " << (++instr_count) << L": [je <err>]" << std::endl;
#endif
      // jump not equal
      AddMachineCode(0x0f);
      AddMachineCode(0x84);
      div_by_zero_offsets.push_back(code_index);
      AddImm(0);
      // jump to exit
    }

    /**
     * Check for divide by 0
     */
    inline void CheckDivideByZero(int32_t offset, Register src) {
      cmp_imm_mem(offset, src, 0);
#ifdef _DEBUG_JIT
      std::wcout << L"  " << (++instr_count) << L": [je <err>]" << std::endl;
#endif
      // jump not equal
      AddMachineCode(0x0f);
      AddMachineCode(0x84);
      div_by_zero_offsets.push_back(code_index);
      AddImm(0);
      // jump to exit
    }
  
    /**
     * Checks array bounds
     */
    inline void CheckArrayBounds(Register reg, Register max_reg) {

      // less than zero
      cmp_imm_reg(0, reg);
#ifdef _DEBUG_JIT
      std::wcout << L"  " << (++instr_count) << L": [jl <err>]" << std::endl;
#endif
      // jump not equal
      AddMachineCode(0x0f);
      AddMachineCode(0x8c);
      bounds_less_offsets.push_back(code_index);
      AddImm(0);
      // jump to exit

      // greater than max
      cmp_reg_reg(max_reg, reg);
#ifdef _DEBUG_JIT
      std::wcout << L"  " << (++instr_count) << L": [jge <err>]" << std::endl;
#endif
      // jump not equal
      AddMachineCode(0x0f);
      AddMachineCode(0x8d);
      bounds_greater_offsets.push_back(code_index);
      AddImm(0);
      // jump to exit
    }

    // Gets an available register from the pool of registers
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
          std::wcout << L">>> No general registers avaiable! <<<" << std::endl;
#endif
          aux_regs.push(rax_reg);
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
      std::wcout << L"\t * allocating " << GetRegisterName(holder->GetRegister())
        << L" *" << std::endl;
#endif

      return holder;
    }

    // Returns a register to the pool
    void ReleaseRegister(RegisterHolder* h) {
#ifdef _VERBOSE
      std::wcout << L"\t * releasing " << GetRegisterName(h->GetRegister())
        << L" *" << std::endl;
#endif

#ifdef _DEBUG_JIT
      assert(h->GetRegister() < XMM0);
      for(size_t i = 0; i < aval_regs.size(); ++i) {
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

    // Gets an available register from the pool of registers
    RegisterHolder* GetXmmRegister() {
      RegisterHolder* holder;
      if(aval_xregs.empty()) {
        compile_success = false;
#ifdef _DEBUG_JIT
        std::wcout << L">>> No XMM registers avaiable! <<<" << std::endl;
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
      std::wcout << L"\t * allocating " << GetRegisterName(holder->GetRegister())
        << L" *" << std::endl;
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
      std::wcout << L"\t * releasing: " << GetRegisterName(h->GetRegister()) << L" * " << std::endl;
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
    void move_imm_mem8(int8_t imm, long offset, Register dest);
    void move_reg_mem16(Register src, int32_t offset, Register dest);
    void move_mem16_reg(int32_t offset, Register src, Register dest);
    void move_imm_mem16(int16_t imm, int32_t offset, Register dest);
    void move_imm_mem32(int32_t imm, long offset, Register dest);
    void move_reg_mem32(Register src, long offset, Register dest);
    void move_mem32_reg(long offset, Register src, Register dest);
    void move_reg_reg(Register src, Register dest);
    void move_reg_mem(Register src, long offset, Register dest);
    void move_mem_reg(long offset, Register src, Register dest);
    void move_mem_reg32(long offset, Register src, Register dest);
    void move_imm_memx(RegInstr* instr, long offset, Register dest);
    void move_imm_mem(int64_t imm, long offset, Register dest);
#ifdef _WIN64  
    void move_imm_reg(int64_t imm, Register reg);
#else
    void move_imm_reg(long imm, Register reg);
#endif  
    void move_imm_xreg(RegInstr* instr, Register reg);
    void move_mem_xreg(long offset, Register src, Register dest);
    void move_xreg_mem(Register src, long offset, Register dest);
    void move_xreg_xreg(Register src, Register dest);

    // math instructions
    void math_imm_reg(int64_t imm, Register reg, InstructionType type);
    void math_imm_xreg(RegInstr* instr, Register reg, InstructionType type);
    void math_reg_reg(Register src, Register dest, InstructionType type);
    void math_xreg_xreg(Register src, Register dest, InstructionType type);
    void math_mem_reg(long offset, Register reg, InstructionType type);
    void math_mem_xreg(long offset, Register reg, InstructionType type);

    // logical
    void and_imm_reg(int64_t imm, Register reg);
    void and_reg_reg(Register src, Register dest);
    void and_mem_reg(long offset, Register src, Register dest);
    void not_reg(Register reg);

    void or_imm_reg(int64_t imm, Register reg);
    void or_reg_reg(Register src, Register dest);
    void or_mem_reg(long offset, Register src, Register dest);
    void xor_imm_reg(int64_t imm, Register reg);
    void xor_reg_reg(Register src, Register dest);
    void xor_mem_reg(long offset, Register src, Register dest);

    // float functions
    void fld_mem(int32_t offset, Register src);
    void fstp_mem(int32_t offset, Register src);
    void fsin();
    void fcos();
    void ftan();
    void fsqrt();
    void fround();
    void flog();
    void flog10();

    // add instructions
    void add_imm_mem(int64_t imm, long offset, Register dest);
    void add_imm_reg(int64_t imm, Register reg);
    void add_imm_xreg(RegInstr* instr, Register reg);
    void add_xreg_xreg(Register src, Register dest);
    void add_mem_reg(long offset, Register src, Register dest);
    void add_mem_xreg(long offset, Register src, Register dest);
    void add_reg_reg(Register src, Register dest);
    void sqrt_xreg_xreg(Register src, Register dest);
    void round_xreg_xreg(Register src, Register dest, wchar_t mode);

    // sub instructions
    void sub_imm_xreg(RegInstr* instr, Register reg);
    void sub_xreg_xreg(Register src, Register dest);
    void sub_mem_xreg(long offset, Register src, Register dest);
    void sub_imm_reg(int64_t imm, Register reg);
    void sub_imm_mem(int64_t imm, long offset, Register dest);
    void sub_reg_reg(Register src, Register dest);
    void sub_mem_reg(long offset, Register src, Register dest);

    // mul instructions
    void mul_imm_xreg(RegInstr* instr, Register reg);
    void mul_xreg_xreg(Register src, Register dest);
    void mul_mem_xreg(long offset, Register src, Register dest);
    void mul_imm_reg(int64_t imm, Register reg);
    void mul_reg_reg(Register src, Register dest);
    void mul_mem_reg(long offset, Register src, Register dest);

    // div instructions
    void div_imm_xreg(RegInstr* instr, Register reg);
    void div_xreg_xreg(Register src, Register dest);
    void div_mem_xreg(long offset, Register src, Register dest);
    void div_imm_reg(int64_t imm, Register reg, bool is_mod = false);
    void div_reg_reg(Register src, Register dest, bool is_mod = false);
    void div_mem_reg(long offset, Register src, Register dest, bool is_mod = false);

    // compare instructions
    void cmp_reg_reg(Register src, Register dest);
    void cmp_mem_reg(long offset, Register src, Register dest);
    void cmp_imm_reg(int64_t imm, Register reg);
    void cmp_imm_mem(long offset, Register src, int64_t imm);
    void cmp_xreg_xreg(Register src, Register dest);
    void cmp_mem_xreg(long offset, Register src, Register dest);
    void cmp_imm_xreg(size_t addr, Register reg);
    void cmov_reg(Register reg, InstructionType oper);

    // inc/dec instructions
    void dec_reg(Register dest);
    void dec_mem(long offset, Register dest);
    void inc_mem(long offset, Register dest);

    // shift instructions
    void shl_reg_reg(Register src, Register dest);
    void shl_mem_reg(long offset, Register src, Register dest);
    void shl_imm_reg(int64_t value, Register dest);
    void shr_reg_reg(Register src, Register dest);
    void shr_mem_reg(long offset, Register src, Register dest);
    void shr_imm_reg(int64_t value, Register dest);

    // push/pop instructions
    void push_imm(long value);
    void push_reg(Register reg);
    void pop_reg(Register reg);
    void push_mem(long offset, Register src);

    // type conversion instructions
    void cvt_xreg_reg(Register src, Register dest);
    void cvt_imm_reg(RegInstr* instr, Register reg);
    void cvt_mem_reg(long offset, Register src, Register dest);
    void cvt_reg_xreg(Register src, Register dest);
    void cvt_imm_xreg(RegInstr* instr, Register reg);
    void cvt_mem_xreg(long offset, Register src, Register dest);

    // function call instruction
    void call_reg(Register reg);
    RegisterHolder* call_xfunc(double(*func_ptr)(double), RegInstr* left);
    RegisterHolder* call_xfunc2(double(*func_ptr)(double, double), RegInstr* left);

    // generates a conditional jump
    bool cond_jmp(InstructionType type);
    void loop(long offset);

    // Calculates array element offset. 
    // Note: this code must match up with the interpreter's 'ArrayIndex' method.
    RegisterHolder* ArrayIndex(StackInstr* instr, MemoryType type);

    // Calculates the indices for memory references.
    void ProcessIndices();

  public:
    static void Initialize(StackProgram* p);

    JitAmd64() {
    }

    ~JitAmd64() {
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

    /**
     * Compiles stack code into AMD64 machine code
     */
    bool Compile(StackMethod* cm);
  };

  /**
   * Prototype for JIT function
   */
  typedef long(*jit_fun_ptr)(long cls_id, long mthd_id, size_t* cls_mem, size_t* inst, size_t* op_stack, size_t* stack_pos, 
                             StackFrame** call_stack, long* call_stack_pos, size_t** jit_mem, long* offset);

  /**
   * JIT runtime wrapper class
   */
  class JitRuntime {
    static StackProgram* program;
    
  public:
    static void Initialize(StackProgram* p);

    // Executes machine code
    long Execute(StackMethod* method, size_t* inst, size_t* op_stack, size_t* stack_pos, 
                 StackFrame** call_stack, long* call_stack_pos, StackFrame* frame);
  };
}
