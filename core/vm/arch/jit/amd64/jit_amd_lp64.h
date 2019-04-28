/***************************************************************************
 * JIT compiler for x86-64 architectures (Windows, macOS and Linux).
 *
 * Copyright (c) 2008-2018 Randy Hollines
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
#ifndef __JIT_COMPILER__
#define __JIT_COMPILER__

#ifdef _WIN64
#include "../../../arch/win32/win32.h"
#else
#include "../../posix/posix.h"
#include <sys/mman.h>
#include <errno.h>
#endif
#include "../../../common.h"
#include "../../../interpreter.h"

using namespace std;

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

#define MAX_DBLS 64
#define BUFFER_SIZE 512

#ifndef I386_PGBYTES
#define PAGE_SIZE 4096
#endif

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

    inline Register GetRegister() {
      return reg;
    }
  };

  /********************************
   * RegInstr class
   ********************************/
  class RegInstr {
    RegType type;
    long operand;
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
      instr = NULL;
    }

    RegInstr(StackInstr* si, double* da) {
      type = IMM_FLOAT;
#ifdef _WIN64    
      operand2 = (size_t)da;
#else
      operand = (long)da;
#endif    
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

    void SetOperand(long o) {
      operand = o;
    }

    long GetOperand() {
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
      available = factor * PAGE_SIZE;
    
#ifdef _WIN64    
      buffer = (unsigned char*)VirtualAlloc(NULL, available, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
      if(!buffer) {
        wcerr << L"Unable to allocate JIT memory!" << endl;
        exit(1);
      }
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
    
#ifdef _WIN64    
      buffer = (unsigned char*)VirtualAlloc(NULL, PAGE_SIZE, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
      if(!buffer) {
        wcerr << L"Unable to allocate JIT memory!" << endl;
        exit(1);
      }
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
#ifdef _WIN64  
      VirtualFree(buffer, NULL, MEM_RELEASE);
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
  typedef long(*jit_fun_ptr)(long cls_id, long mthd_id, size_t* cls_mem, size_t* inst, size_t* op_stack, long *stack_pos,
                             StackFrame** call_stack, long* call_stack_pos, size_t** jit_mem, long* offset);

  /********************************
   * JitCompilerIA64 class
   ********************************/
  class JitCompilerIA64 {
    static StackProgram* program;
    static PageManager* page_manager;
    deque<RegInstr*> working_stack;
    vector<RegisterHolder*> aval_regs;
    list<RegisterHolder*> used_regs;
    stack<RegisterHolder*> aux_regs;
    RegisterHolder* rax_reg;
    vector<RegisterHolder*> aval_xregs;
    list<RegisterHolder*> used_xregs;
    unordered_map<long, StackInstr*> jump_table; // jump addresses
    vector<long> nil_deref_offsets;      // code -1
    vector<long> bounds_less_offsets;    // code -2
    vector<long> bounds_greater_offsets; // code -3
    long local_space, org_local_space;
    StackMethod* method;
    long instr_count;
    unsigned char* code;
    long code_index;
    long epilog_index;
    double* floats;
    long floats_index;
    long instr_index;
    long code_buf_max;
    bool compile_success;
    bool skip_jump;

    // setup and teardown
    void Prolog();
    void Epilog();

    // stack conversion operations
    void ProcessParameters(long count);
    void RegisterRoot();
    void ProcessInstructions();
    void ProcessLoad(StackInstr* instr);
    void ProcessStore(StackInstr* instruction);
    void ProcessCopy(StackInstr* instr);
    RegInstr* ProcessIntFold(long left_imm, long right_imm, InstructionType type);
    void ProcessIntCalculation(StackInstr* instruction);
    void ProcessFloatCalculation(StackInstr* instruction);
    void ProcessFloatOperation(StackInstr* instruction);
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
    void ProcessFloor(StackInstr* instr);
    void ProcessCeiling(StackInstr* instr);
    void ProcessFloatToInt(StackInstr* instr);
    void ProcessIntToFloat(StackInstr* instr);

    /********************************
     * Add byte code to buffer
     ********************************/
    void AddMachineCode(unsigned char b) {
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

    /********************************
     * Encodes and writes out 32-bit
     * integer values; note sizeof(int)
     ********************************/
    inline void AddImm(int imm) {
      unsigned char buffer[sizeof(int)];
      ByteEncode32(buffer, imm);
      for(size_t i = 0; i < sizeof(int); ++i) {
        AddMachineCode(buffer[i]);
      }
    }

    /********************************
    * Encodes and writes out a 16-bit 
    * integer value
    ********************************/
    inline void AddImm16(int16_t imm) {
      unsigned char buffer[sizeof(int16_t)];
      ByteEncode16(buffer, imm);
      for(size_t i = 0; i < sizeof(int16_t); ++i) {
        AddMachineCode(buffer[i]);
      }
    }

    /********************************
     * Encodes and writes out 64-bit
     * integer values
     ********************************/
#ifdef _WIN64   
    inline void AddImm64(size_t imm) {
      unsigned char buffer[sizeof(size_t)];
      ByteEncode64(buffer, imm);
      for(int i = 0; i < sizeof(size_t); ++i) {
        AddMachineCode(buffer[i]);
      }
  }  
#else
    inline void AddImm64(long imm) {
      unsigned char buffer[sizeof(long)];
      ByteEncode64(buffer, imm);
      for(size_t i = 0; i < sizeof(long); ++i) {
        AddMachineCode(buffer[i]);
      }    
    }
#endif

    /********************************
     * Encoding for AMD64 "B" bits
     ********************************/
    inline unsigned char B(Register b) {
      if((b > RSP && b < XMM0) || b > XMM7) {
        return 0x49;
      }

      return 0x48;
    }

    /********************************
     * Encoding for AMD64 "XB" bits
     ********************************/
    inline unsigned char XB(Register b) {
      if((b > RSP && b < XMM0) || b > XMM7) {
        return 0x4b;
      }

      return 0x4a;
    }

    /********************************
     * Encoding for AMD64 "XB" bits
     ********************************/
    inline unsigned char XB32(Register b) {
      if((b > RSP && b < XMM0) || b > XMM7) {
        return 0x66;
      }

      return 0x67;
    }

    /********************************
     * Encoding for AMD64 "RXB" bits
     ********************************/
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

    /********************************
     * Encoding for AMD64 "RXB" bits
     ********************************/
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

    /********************************
     * Encoding for AMD64 "ROB" bits
     ********************************/
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

    /********************************
     * Caculates the AMD64 MOD R/M
     * offset
     ********************************/
    inline unsigned char ModRM(Register eff_adr, Register mod_rm) {
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
          wcerr << L"internal error" << endl;
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
          wcerr << L"invalid register reference" << endl;
          exit(1);
          break;

        default:
          wcerr << L"internal error" << endl;
          exit(1);
          break;
      }

      return byte;
    }

    /********************************
     * Returns the name of a register
     ********************************/
    wstring GetRegisterName(Register reg) {
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

    /********************************
     * Encodes a byte array with a
     * 32-bit value
     ********************************/
    inline void ByteEncode32(unsigned char buffer[], int value) {
      memcpy(buffer, &value, sizeof(int));
    }

    /********************************
    * Encodes a byte array with a 
    * 16-bit value
    ********************************/
    inline void ByteEncode16(unsigned char buffer[], int16_t value) {
      memcpy(buffer, &value, sizeof(int16_t));
    }

    /********************************
     * Encodes a byte array with a
     * 64-bit value
     ********************************/
#ifdef _WIN64
    inline void ByteEncode64(unsigned char buffer[], size_t value) {
      memcpy(buffer, &value, sizeof(size_t));
    }
#else
  inline void ByteEncode64(unsigned char buffer[], long value) {
      memcpy(buffer, &value, sizeof(long));
    }
#endif      

    /********************************
     * Encodes an array with the
     * binary ID of a register
     ********************************/
    inline void RegisterEncode3(unsigned char& code, long offset, Register reg) {
#ifdef _DEBUG
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
      cmp_imm_reg(0, reg);
#ifdef _DEBUG
      wcout << L"  " << (++instr_count) << L": [je <err>]" << endl;
#endif
      // jump not equal
      AddMachineCode(0x0f);
      AddMachineCode(0x84);
      nil_deref_offsets.push_back(code_index);
      AddImm(0);
      // jump to exit
    }
  
    /***********************************
     * Checks array bounds
     **********************************/
    inline void CheckArrayBounds(Register reg, Register max_reg) {

      // less than zero
      cmp_imm_reg(0, reg);
#ifdef _DEBUG
      wcout << L"  " << (++instr_count) << L": [jl <err>]" << endl;
#endif
      // jump not equal
      AddMachineCode(0x0f);
      AddMachineCode(0x8c);
      bounds_less_offsets.push_back(code_index);
      AddImm(0);
      // jump to exit

      // greater than max
      cmp_reg_reg(max_reg, reg);
#ifdef _DEBUG
      wcout << L"  " << (++instr_count) << L": [jge <err>]" << endl;
#endif
      // jump not equal
      AddMachineCode(0x0f);
      AddMachineCode(0x8d);
      bounds_greater_offsets.push_back(code_index);
      AddImm(0);
      // jump to exit
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
          wcout << L">>> No general registers avaiable! <<<" << endl;
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
      wcout << L"\t * releasing: " << GetRegisterName(h->GetRegister()) << L" * " << endl;
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
    void move_reg_mem16(Register src, int32_t offset, Register dest);
    void move_mem16_reg(int32_t offset, Register src, Register dest);
    void move_imm_mem16(int32_t imm, int32_t offset, Register dest);
    void move_reg_mem32(Register src, long offset, Register dest);
    void move_mem32_reg(long offset, Register src, Register dest);
    void move_reg_reg(Register src, Register dest);
    void move_reg_mem(Register src, long offset, Register dest);
    void move_mem_reg(long offset, Register src, Register dest);
    void move_mem_reg32(int32_t offset, Register src, Register dest);
    void move_imm_memx(RegInstr* instr, long offset, Register dest);
    void move_imm_mem(long imm, long offset, Register dest);
#ifdef _WIN64  
    void move_imm_reg(size_t imm, Register reg);
#else
    void move_imm_reg(long imm, Register reg);
#endif  
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

    // float functions
    void fld_mem(int32_t offset, Register src);
    void fstp_mem(int32_t offset, Register src);
    void fsin();
    void fcos();
    void ftan();
    void fsqrt();

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
    void shl_mem_reg(long offset, Register src, Register dest);
    void shl_imm_reg(long value, Register dest);
    void shr_reg_reg(Register src, Register dest);
    void shr_mem_reg(long offset, Register src, Register dest);
    void shr_imm_reg(long value, Register dest);

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
    RegisterHolder* call_xfunc(double(*func_ptr)(double), RegInstr* left);
    RegisterHolder* call_xfunc2(double(*func_ptr)(double, double), RegInstr* left);

    // generates a conditional jump
    bool cond_jmp(InstructionType type);
    void loop(long offset);

    static size_t PopInt(size_t* op_stack, long *stack_pos) {
      const size_t value = op_stack[--(*stack_pos)];
#ifdef _DEBUG
      wcout << L"\t[pop_i: value=" << (size_t*)value << L"(" << value << L")]" << L"; pos=" << (*stack_pos) << endl;
#endif

      return value;
    }

    static void PushInt(size_t* op_stack, long *stack_pos, size_t value) {
      op_stack[(*stack_pos)++] = value;
#ifdef _DEBUG
      wcout << L"\t[push_i: value=" << (size_t*)value << L"(" << value << L")]" << L"; pos=" << (*stack_pos) << endl;
#endif
    }

    static void StackCallback(const long instr_id, StackInstr* instr, const long cls_id,
                              const long mthd_id, size_t* inst, size_t* op_stack, long *stack_pos, 
                              StackFrame** call_stack, long* call_stack_pos, const long ip) {
#ifdef _DEBUG
      wcout << L"Stack Call: instr=" << instr_id
        << L", oper_1=" << instr->GetOperand() << L", oper_2=" << instr->GetOperand2()
        << L", oper_3=" << instr->GetOperand3() << L", self=" << inst << L"("
        << (size_t)inst << L"), stack=" << op_stack << L", stack_addr=" << stack_pos
        << L", stack_pos=" << (*stack_pos) << endl;
#endif
      switch(instr_id) {
        case MTHD_CALL:
        case DYN_MTHD_CALL: {
          
#ifdef _DEBUG
          StackMethod* called = program->GetClass(instr->GetOperand())->GetMethod(instr->GetOperand2());
          wcout << L"jit oper: MTHD_CALL: mthd=" << called->GetName() << endl;
#endif
          StackInterpreter intpr(call_stack, call_stack_pos);
          intpr.Execute(op_stack, stack_pos, ip, program->GetClass(cls_id)->GetMethod(mthd_id), inst, true);
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

          // NULL terminated string workaround
          size++;
          size_t* mem = MemoryManager::AllocateArray((long)(size + ((dim + 2) * sizeof(size_t))), BYTE_ARY_TYPE, op_stack, *stack_pos);
          mem[0] = size;
          mem[1] = dim;
          memcpy(mem + 2, indices, dim * sizeof(size_t));
          PushInt(op_stack, stack_pos, (size_t)mem);

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
          size_t* mem = (size_t*)MemoryManager::AllocateArray((long)(size + ((dim + 2) * sizeof(size_t))), CHAR_ARY_TYPE, op_stack, *stack_pos);
          mem[0] = size - 1;
          mem[1] = dim;
          memcpy(mem + 2, indices, dim * sizeof(size_t));
          PushInt(op_stack, stack_pos, (size_t)mem);

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

          size_t* mem = (size_t*)MemoryManager::AllocateArray((long)(size + dim + 2), INT_TYPE, op_stack, *stack_pos);
#ifdef _DEBUG
          wcout << L"jit oper: NEW_INT_ARY: dim=" << dim << L"; size=" << size
            << L"; index=" << (*stack_pos) << L"; mem=" << mem << endl;
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

          size *= 2;
          size_t* mem = MemoryManager::AllocateArray((long)(size + dim + 2), INT_TYPE, op_stack, *stack_pos);
#ifdef _DEBUG
          wcout << L"jit oper: NEW_FLOAT_ARY: dim=" << dim << L"; size=" << size
            << L"; index=" << (*stack_pos) << L"; mem=" << mem << endl;
#endif
          mem[0] = size / 2;
          mem[1] = dim;
          memcpy(mem + 2, indices, dim * sizeof(size_t));
          PushInt(op_stack, stack_pos, (size_t)mem);
        }
        break;

        case NEW_OBJ_INST:{
#ifdef _DEBUG
          StackClass* klass = program->GetClass(instr->GetOperand());
          wcout << L"jit oper: NEW_OBJ_INST: class=" << klass->GetName() << endl;
#endif
          size_t* mem = MemoryManager::AllocateObject(instr->GetOperand(), op_stack, *stack_pos);
          PushInt(op_stack, stack_pos, (size_t)mem);
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
#ifdef _DEBUG
          wcout << L"jit oper: OBJ_INST_CAST: from=" << mem << L", to=" << to_id << endl;
#endif  
          size_t result = (size_t)MemoryManager::ValidObjectCast(mem, to_id, program->GetHierarchy(), program->GetInterfaces());
          if(!result && mem) {
            StackClass* to_cls = MemoryManager::GetClass(mem);
            wcerr << L">>> Invalid object cast: '" << (to_cls ? to_cls->GetName() : L"?")
              << L"' to '" << program->GetClass(to_id)->GetName() << L"' <<<" << endl;
            wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
            exit(1);
          }
          PushInt(op_stack, stack_pos, result);
        }
        break;

        //----------- threads -----------

        case THREAD_JOIN: {
          size_t* instance = inst;
          if(!instance) {
            wcerr << L"Atempting to dereference a 'Nil' memory instance" << endl;
            wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
            exit(1);
          }

#ifdef _WIN64
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
#ifdef _WIN64    
          Sleep((DWORD)PopInt(op_stack, stack_pos));
#else
          usleep(PopInt(op_stack, stack_pos));
#endif      
          break;

        case THREAD_MUTEX: {
          size_t* instance = inst;
          if(!instance) {
            wcerr << L"Atempting to dereference a 'Nil' memory instance" << endl;
            wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
            exit(1);
          }
#ifdef _WIN64      
          InitializeCriticalSection((CRITICAL_SECTION*)&instance[1]);
#else
          pthread_mutex_init((pthread_mutex_t*)&instance[1], NULL);
#endif    
        }
        break;

        case CRITICAL_START: {
          size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
          if(!instance) {
            wcerr << L"Atempting to dereference a 'Nil' memory instance" << endl;
            wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
            exit(1);
          }
#ifdef _WIN64      
          EnterCriticalSection((CRITICAL_SECTION*)&instance[1]);
#else
          pthread_mutex_lock((pthread_mutex_t*)&instance[1]);
#endif      
        }
        break;

        case CRITICAL_END: {
          size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
          if(!instance) {
            wcerr << L"Atempting to dereference a 'Nil' memory instance" << endl;
            wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
            exit(1);
          }
#ifdef _WIN64      
          LeaveCriticalSection((CRITICAL_SECTION*)&instance[1]);
#else
          pthread_mutex_unlock((pthread_mutex_t*)&instance[1]);
#endif      
        }
        break;

        // ---------------- memory copy ----------------
        case CPY_BYTE_ARY: {
          long length = (long)PopInt(op_stack, stack_pos);;
          const long src_offset = (long)PopInt(op_stack, stack_pos);;
          size_t* src_array = (size_t*)PopInt(op_stack, stack_pos);;
          const long dest_offset = (long)PopInt(op_stack, stack_pos);;
          size_t* dest_array = (size_t*)PopInt(op_stack, stack_pos);;

          if(!src_array || !dest_array) {
            wcerr << L">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
            wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
            exit(1);
          }

          const long src_array_len = (long)src_array[2];
          const long dest_array_len = (long)dest_array[2];
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

        case CPY_CHAR_ARY: {
          long length = (long)PopInt(op_stack, stack_pos);;
          const long src_offset = (long)PopInt(op_stack, stack_pos);;
          size_t* src_array = (size_t*)PopInt(op_stack, stack_pos);;
          const long dest_offset = (long)PopInt(op_stack, stack_pos);;
          size_t* dest_array = (size_t*)PopInt(op_stack, stack_pos);;

          if(!src_array || !dest_array) {
            wcerr << L">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
            wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
            exit(1);
          }

          const long src_array_len = (long)src_array[2];
          const long dest_array_len = (long)dest_array[2];

          if(length > 0 && src_offset + length <= src_array_len && dest_offset + length <= dest_array_len) {
            const wchar_t* src_array_ptr = (wchar_t*)(src_array + 3);
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
          long length = (long)PopInt(op_stack, stack_pos);;
          const long src_offset = (long)PopInt(op_stack, stack_pos);;
          size_t* src_array = (size_t*)PopInt(op_stack, stack_pos);;
          const long dest_offset = (long)PopInt(op_stack, stack_pos);;
          size_t* dest_array = (size_t*)PopInt(op_stack, stack_pos);;

          if(!src_array || !dest_array) {
            wcerr << L">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
            wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
            exit(1);
          }

          const long src_array_len = (long)src_array[0];
          const long dest_array_len = (long)dest_array[0];
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
          long length = (long)PopInt(op_stack, stack_pos);;
          const long src_offset = (long)PopInt(op_stack, stack_pos);;
          size_t* src_array = (size_t*)PopInt(op_stack, stack_pos);;
          const long dest_offset = (long)PopInt(op_stack, stack_pos);;
          size_t* dest_array = (size_t*)PopInt(op_stack, stack_pos);;

          if(!src_array || !dest_array) {
            wcerr << L">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
            wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
            exit(1);
          }

          const long src_array_len = (long)src_array[0];
          const long dest_array_len = (long)dest_array[0];
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
        case TRAP_RTRN:
          if(!TrapProcessor::ProcessTrap(program, inst, op_stack, stack_pos, NULL)) {
            wcerr << L"  JIT compiled machine code..." << endl;
            exit(1);
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
    // method.
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
          move_mem_reg((long)holder->GetOperand(), RBP, array_holder->GetRegister());
          break;

        default:
          wcerr << L"internal error" << endl;
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
          move_imm_reg((long)holder->GetOperand(), index_holder->GetRegister());
          break;

        case REG_INT:
          index_holder = holder->GetRegister();
          break;

        case MEM_INT:
          index_holder = GetRegister();
          move_mem_reg((long)holder->GetOperand(), RBP, index_holder->GetRegister());
          break;

        default:
          wcerr << L"internal error" << endl;
          exit(1);
          break;
      }

      const long dim = instr->GetOperand();
      for(int i = 1; i < dim; ++i) {
        // index *= array[i];
        mul_mem_reg((i + 2) * sizeof(size_t), array_holder->GetRegister(),
                    index_holder->GetRegister());
        if(holder) {
          delete holder;
          holder = NULL;
        }

        holder = working_stack.front();
        working_stack.pop_front();
        switch(holder->GetType()) {
          case IMM_INT:
            add_imm_reg((long)holder->GetOperand(), index_holder->GetRegister());
            break;

          case REG_INT:
            add_reg_reg(holder->GetRegister()->GetRegister(),
                        index_holder->GetRegister());
            break;

          case MEM_INT:
            add_mem_reg((long)holder->GetOperand(), RBP, index_holder->GetRegister());
            break;

          default:
            break;
        }
      }

      // bounds check
      RegisterHolder* bounds_holder = GetRegister();
#ifdef _WIN64    
      move_mem_reg32(0, array_holder->GetRegister(), bounds_holder->GetRegister());
#else
      move_mem_reg(0, array_holder->GetRegister(), bounds_holder->GetRegister()); 
#endif    

      // ajust indices
      switch(type) {
        case BYTE_ARY_TYPE:
          break;

        case CHAR_ARY_TYPE:
#ifdef _WIN64    
          shl_imm_reg(1, index_holder->GetRegister());
          shl_imm_reg(1, bounds_holder->GetRegister());
#else
          shl_imm_reg(2, index_holder->GetRegister());
          shl_imm_reg(2, bounds_holder->GetRegister());
#endif      
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
      add_imm_reg((instr->GetOperand() + 2) * sizeof(size_t), index_holder->GetRegister());
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
      multimap<long, StackInstr*> values;
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
          instr->SetOperand3(instr->GetOperand() * sizeof(size_t));
        }
        // local reference
        else {
          // note: all local variables are allocted in 4 or 8 bytes ` 
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
      org_local_space = local_space = -(index + TMP_REG_5);

#ifdef _DEBUG
      wcout << L"Local space required: " << (local_space + 16) << L" byte(s)" << endl;
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
        wcout << L"---------- Compiling Native Code: method_id=" << cls_id << L","
          << mthd_id << L"; mthd_name='" << method->GetName() << L"'; params="
          << method->GetParamCount() << L" ----------" << endl;
#endif  
        // code buffer memory
        code_buf_max = BUFFER_SIZE;
        code = (unsigned char*)malloc(code_buf_max);

        // floats memory
#ifdef _WIN64

        floats = (double*)VirtualAlloc(NULL, sizeof(double) * MAX_DBLS, MEM_COMMIT, PAGE_READWRITE);
        if(!floats) {
          wcerr << L"Unable to allocate JIT memory for floats!" << endl;
          exit(1);
        }
#else
        if(posix_memalign((void**)&floats, PAGE_SIZE, sizeof(double) * MAX_DBLS)) {
          wcerr << L"Unable to reallocate JIT memory!" << endl;
          exit(1);
        }
#endif    
        floats_index = instr_index = code_index = epilog_index = instr_count = 0;

        rax_reg = new RegisterHolder(RAX);
#ifdef _WIN64
        // general use registers
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
#ifdef _DEBUG
        wcout << L"Compiling code for Windows AMD64 architecture..." << endl;
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
        wcout << L"Compiling code for Posix AMD64 architecture..." << endl;
#endif
#endif

        // process offsets
        ProcessIndices();

        // setup
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
        unordered_map<long, StackInstr*>::iterator iter;
        for(iter = jump_table.begin(); iter != jump_table.end(); ++iter) {
          StackInstr* instr = iter->second;
          const long src_offset = iter->first;
          const long dest_index = method->GetLabelIndex(instr->GetOperand()) + 1;
          const long dest_offset = method->GetInstruction(dest_index)->GetOffset();
          const long offset = dest_offset - src_offset - 4; // 64-bit jump offset
          memcpy(&code[src_offset], &offset, 4);
#ifdef _DEBUG
          wcout << L"jump update: src=" << src_offset
            << L"; dest=" << dest_offset << endl;
#endif
        }

        for(size_t i = 0; i < nil_deref_offsets.size(); ++i) {
          const long index = nil_deref_offsets[i];
          long offset = epilog_index - index + 1;
          memcpy(&code[index], &offset, 4);
        }

        for(size_t i = 0; i < bounds_less_offsets.size(); ++i) {
          const long index = bounds_less_offsets[i];
          long offset = epilog_index - index + 16;
          memcpy(&code[index], &offset, 4);
        }

        for(size_t i = 0; i < bounds_greater_offsets.size(); ++i) {
          const long index = bounds_greater_offsets[i];
          long offset = epilog_index - index + 31;
          memcpy(&code[index], &offset, 4);
        }

#ifdef _DEBUG
        wcout << L"Caching JIT code: actual=" << code_index
          << L", buffer=" << code_buf_max << L" byte(s)" << endl;
#endif
        // store compiled code
        method->SetNativeCode(new NativeCode(page_manager->GetPage(code, code_index), code_index, floats));
        
        free(code);
        code = NULL;

#ifdef _TIMING
        wcout << L"JIT compiling: method='" << method->GetName() << L"', time="
          << (double)(clock() - start) / CLOCKS_PER_SEC << L" second(s)." << endl;
#endif

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
    long code_index;
    double* floats;

    long ExecuteMachineCode(long cls_id, long mthd_id, size_t* inst, unsigned char* code, const long code_size, 
                            size_t* op_stack, long *stack_pos, StackFrame** call_stack, long* call_stack_pos, StackFrame* frame);

  public:
    static void Initialize(StackProgram* p);

    JitExecutor() {
    }

    ~JitExecutor() {
    }

    // Executes machine code
    long Execute(StackMethod* cm, size_t* inst, size_t* op_stack, long* stack_pos,
                 StackFrame** call_stack, long* call_stack_pos, StackFrame* frame) {
      method = cm;
      long cls_id = method->GetClass()->GetId();
      long mthd_id = method->GetId();

      NativeCode* native_code = method->GetNativeCode();
      code = native_code->GetCode();
      code_index = native_code->GetSize();
      floats = native_code->GetFloats();

#ifdef _DEBUG
      wcout << L"=== MTHD_CALL (native): id=" << cls_id << L"," << mthd_id
        << L"; name='" << method->GetName() << L"'; self=" << inst << L"(" << (size_t)inst
        << L"); stack=" << op_stack << L"; stack_pos=" << (*stack_pos) << L"; params="
        << method->GetParamCount() << L"; code=" << (size_t*)code << L"; code_index="
        << code_index << L" ===" << endl;
      assert((*stack_pos) >= method->GetParamCount());
#endif

      // execute
      return ExecuteMachineCode(cls_id, mthd_id, inst, code, code_index, op_stack,  stack_pos, call_stack, call_stack_pos, frame);
    }
  };
}
#endif
