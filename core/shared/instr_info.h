/***************************************************************************
 * The bytecode instruction set, described once.
 *
 * Copyright (c) 2026, Randy Hollines
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

#pragma once

#include "instrs.h"
#include <cstdint>
#include <cstddef>

//
// WHY THIS FILE EXISTS
// --------------------
// What an opcode carries and does used to be written out by hand in a dozen
// places -- the writer (compiler/emit.cpp), the .obl reader (compiler/linker.cpp),
// the .obe loader (vm/loader.cpp), the optimizer's label and slot passes, the
// inliner -- and nothing checked that they agreed. This is the one description.
// Phase 1 (this file) only DESCRIBES: no code is driven by it, and no byte that
// is emitted or read changes. tools/cicd/check_instr_table.py reads it, and the
// hand-written sites, and fails when they disagree. docs/INSTRUCTION_SET.md
// has the rest, including how to add an opcode.
//
// Every row is written from the code, not from memory: the operands from
// IntermediateInstruction::Write, Loader::LoadStatements and
// Library::LoadStatements; the stack effect from the interpreter handlers
// (interpreter.cpp, dispatch.cpp). The table is in enum order, and the
// static_asserts at the bottom fail the build when a row is missing, extra or
// out of place, so adding an opcode to instrs.h without a row does not compile.
//
// READING A ROW
// -------------
//   ROW(NAME, op1, op2, op3, wide, pops, pushes, flags, "note")
//
//   op1 op2 op3  the roles of IntermediateInstruction::operand, operand2 and
//                operand3 (StackInstr's GetOperand, GetOperand2, GetOperand3).
//                Role::R_NONE: not used. Role::R_RUNTIME: used by the VM only -- set
//                after load, never in a file.
//   wide         the field that does not fit those three: operand7 (I64, the
//                literal an Int is), operand4 (F64), operand5 (STR), operand5 and
//                operand6 (STR2).
//   pops pushes  operand-stack WORDS. A Float is one word, a function value two
//                (function id word on top). STACK_VAR means the count depends on
//                something other than the opcode; the note says what.
//   flags        the IFLAG_ bits below.
//
// The file layout follows from the roles: the used operands in operand,
// operand2, operand3 order (a Char as a wchar, the rest as 32-bit ints), then the
// wide field. Wire() below derives it; the checker compares it with what the
// writer and the two readers actually do.
//
namespace instructions {
  namespace info {

    // What an operand IS. Phase 2 drives the inliner's shifting and the label
    // passes from these, so the role must say what the optimizer needs to know.
    enum class Role : uint8_t {
      R_NONE,       // not used
      R_RUNTIME,    // set by the VM after load; not in the file
      R_IMM,        // an immediate integer (a select's base value)
      R_CHAR,       // an immediate character (written as a wchar, not an int)
      R_LABEL,      // a reference to a label id (a jump target)
      R_LABEL_DEF,  // the definition of a label id (LBL)
      R_SLOT,       // a variable slot: a local, instance or class memory id
      R_CTX,        // the memory context of a slot: CLS, INST or LOCL
      R_CLASS,      // a class id
      R_METHOD,     // a method id
      R_COUNT,      // a count: dimensions, trap words, parameters, table range
      R_FLAG,       // a flag or a state word (is_native, a try's cond)
      R_COND,       // a jump's condition: < 0 unconditional, else the value popped
      R_TYPE,       // a return type code (NIL_TYPE, INT_TYPE, ...)
      R_SIZE,       // a size in bytes
    };

    // The field beyond operand, operand2 and operand3.
    enum class Wide : uint8_t {
      W_NONE,
      W_I64,   // operand7: an Int literal (operand mirrors its low bits)
      W_F64,   // operand4: a Float literal
      W_STR,   // operand5: a class name
      W_STR2,  // operand5 and operand6: a class name and a method name
    };

    // Words popped or pushed that the opcode alone does not decide.
    constexpr int8_t STACK_VAR = -1;

    constexpr uint16_t IFLAG_JUMP        = 1 << 0;   // names a jump target (JMP, TRY_START, JMP_TABLE and its slots)
    constexpr uint16_t IFLAG_TERMINATOR  = 1 << 1;   // control never falls through to the next instruction
    constexpr uint16_t IFLAG_CALL        = 1 << 2;   // enters another method
    constexpr uint16_t IFLAG_MAY_TRAP    = 1 << 3;   // can raise a runtime error (nil, bounds, divide by zero, ...)
    constexpr uint16_t IFLAG_SIDE_EFFECT = 1 << 4;   // changes something beyond the operand stack's own words
    constexpr uint16_t IFLAG_ALLOCATES   = 1 << 5;   // allocates on the managed heap: a collection can run
    constexpr uint16_t IFLAG_CTX_ADDR    = 1 << 6;   // pops one more word, the base address, unless operand2 is LOCL
    constexpr uint16_t IFLAG_VM_ONLY     = 1 << 7;   // made by the loader; never in a file
    constexpr uint16_t IFLAG_LIB_ONLY    = 1 << 8;   // in a .obl only; the linker resolves it to another opcode
    constexpr uint16_t IFLAG_PSEUDO      = 1 << 9;   // never an instruction: an id used as a trap id, or a marker
    constexpr uint16_t IFLAG_RESERVED    = 1 << 10;  // in the enum, but nothing emits, reads or runs it

    struct InstrInfo {
      InstructionType type;
      const char* name;
      Role op1;
      Role op2;
      Role op3;
      Wide wide;
      int8_t pops;
      int8_t pushes;
      uint16_t flags;
      const char* note;

      constexpr bool Has(uint16_t flag) const { return (flags & flag) != 0; }
      constexpr bool IsJump() const { return Has(IFLAG_JUMP); }
      constexpr bool IsTerminator() const { return Has(IFLAG_TERMINATOR); }
      constexpr bool MayTrap() const { return Has(IFLAG_MAY_TRAP); }
      constexpr bool HasSideEffect() const { return Has(IFLAG_SIDE_EFFECT); }

      // an operand names a label, whether it defines it or jumps to it
      constexpr bool CarriesLabel() const {
        return op1 == Role::R_LABEL || op1 == Role::R_LABEL_DEF || op2 == Role::R_LABEL || op2 == Role::R_LABEL_DEF ||
               op3 == Role::R_LABEL || op3 == Role::R_LABEL_DEF;
      }

      // an operand is a variable slot, which the inliner must shift
      constexpr bool CarriesSlot() const {
        return op1 == Role::R_SLOT || op2 == Role::R_SLOT || op3 == Role::R_SLOT;
      }

      // does an operand written to a file: not NONE, not RUNTIME
      static constexpr bool InFile(Role r) { return r != Role::R_NONE && r != Role::R_RUNTIME; }
    };

#define OBJK_ROW(NAME, o1, o2, o3, wide, pops, pushes, flags, note) \
    { instructions::NAME, #NAME, Role::R_##o1, Role::R_##o2, Role::R_##o3, Wide::W_##wide, pops, pushes, flags, note }

    // One row per enumerator of InstructionType, in enum order.
    constexpr InstrInfo kInstrInfo[] = {   // C++14: no inline variables (the native-library projects build below C++17)
      // ---- loads ----
      OBJK_ROW(LOAD_INT_LIT,          NONE, NONE, NONE, I64, 0, 1, 0, "push an Int literal (operand7)"),
      OBJK_ROW(LOAD_CHAR_LIT,         CHAR, NONE, NONE, NONE, 0, 1, 0, "push a Char literal"),
      OBJK_ROW(LOAD_FLOAT_LIT,        NONE, NONE, NONE, F64, 0, 1, 0, "push a Float literal (operand4)"),
      OBJK_ROW(LOAD_INT_VAR,          SLOT, CTX,  NONE, NONE, 0, 1, IFLAG_CTX_ADDR | IFLAG_MAY_TRAP, "load a one-word variable; LOCL reads the frame, else the base address is popped (a Nil base traps); the loader rewrites it to LOAD_LOCL_INT_VAR or LOAD_CLS_INST_INT_VAR"),
      OBJK_ROW(LOAD_LOCL_INT_VAR,     SLOT, CTX,  NONE, NONE, 0, 1, IFLAG_VM_ONLY, "LOAD_INT_VAR with a LOCL context"),
      OBJK_ROW(LOAD_CLS_INST_INT_VAR, SLOT, CTX,  NONE, NONE, 1, 1, IFLAG_VM_ONLY | IFLAG_MAY_TRAP, "LOAD_INT_VAR with a CLS or INST context: pops the base address"),
      OBJK_ROW(LOAD_FLOAT_VAR,        SLOT, CTX,  NONE, NONE, 0, 1, IFLAG_CTX_ADDR | IFLAG_MAY_TRAP, "load a Float variable (one word)"),
      OBJK_ROW(LOAD_FUNC_VAR,         SLOT, CTX,  NONE, NONE, 0, 2, IFLAG_CTX_ADDR | IFLAG_MAY_TRAP, "load a function variable: two words at slot and slot + 1"),
      OBJK_ROW(LOAD_CLS_MEM,          NONE, NONE, NONE, NONE, 0, 1, 0, "push the current class's static memory base"),
      OBJK_ROW(LOAD_INST_MEM,         NONE, NONE, NONE, NONE, 0, 1, 0, "push 'self': local slot 0 of the frame"),
      // ---- stores ----
      OBJK_ROW(STOR_INT_VAR,          SLOT, CTX,  NONE, NONE, 1, 0, IFLAG_CTX_ADDR | IFLAG_MAY_TRAP | IFLAG_SIDE_EFFECT, "store a one-word variable; the base address, when not LOCL, is on top of the value"),
      OBJK_ROW(STOR_LOCL_INT_VAR,     SLOT, CTX,  NONE, NONE, 1, 0, IFLAG_VM_ONLY | IFLAG_SIDE_EFFECT, "STOR_INT_VAR with a LOCL context"),
      OBJK_ROW(STOR_CLS_INST_INT_VAR, SLOT, CTX,  NONE, NONE, 2, 0, IFLAG_VM_ONLY | IFLAG_MAY_TRAP | IFLAG_SIDE_EFFECT, "STOR_INT_VAR with a CLS or INST context: base address, then value"),
      OBJK_ROW(STOR_FLOAT_VAR,        SLOT, CTX,  NONE, NONE, 1, 0, IFLAG_CTX_ADDR | IFLAG_MAY_TRAP | IFLAG_SIDE_EFFECT, "store a Float variable"),
      OBJK_ROW(STOR_FUNC_VAR,         SLOT, CTX,  NONE, NONE, 2, 0, IFLAG_CTX_ADDR | IFLAG_MAY_TRAP | IFLAG_SIDE_EFFECT, "store a function variable: two words"),
      // ---- copies: a store that leaves the value on the stack ----
      OBJK_ROW(COPY_INT_VAR,          SLOT, CTX,  NONE, NONE, 1, 1, IFLAG_CTX_ADDR | IFLAG_MAY_TRAP | IFLAG_SIDE_EFFECT, "store the top word without popping it; made by the optimizer (InstructionReplacement)"),
      OBJK_ROW(COPY_LOCL_INT_VAR,     SLOT, CTX,  NONE, NONE, 1, 1, IFLAG_VM_ONLY | IFLAG_SIDE_EFFECT, "COPY_INT_VAR with a LOCL context"),
      OBJK_ROW(COPY_CLS_INST_INT_VAR, SLOT, CTX,  NONE, NONE, 2, 1, IFLAG_VM_ONLY | IFLAG_MAY_TRAP | IFLAG_SIDE_EFFECT, "COPY_INT_VAR with a CLS or INST context"),
      OBJK_ROW(COPY_FLOAT_VAR,        SLOT, CTX,  NONE, NONE, 1, 1, IFLAG_CTX_ADDR | IFLAG_MAY_TRAP | IFLAG_SIDE_EFFECT, "store the top Float without popping it"),
      OBJK_ROW(COPY_FUNC_VAR,         SLOT, CTX,  NONE, NONE, 2, 2, IFLAG_RESERVED | IFLAG_CTX_ADDR | IFLAG_SIDE_EFFECT, "nothing creates it and neither reader nor the interpreter handles it (dispatch.cpp has a placeholder); emit.cpp Write and the inliner still list it"),
      // ---- array elements: operand is the dimension count ----
      OBJK_ROW(LOAD_BYTE_ARY_ELM,     COUNT, CTX, NONE, NONE, STACK_VAR, 1, IFLAG_MAY_TRAP, "pops the array reference and operand index words"),
      OBJK_ROW(LOAD_CHAR_ARY_ELM,     COUNT, CTX, NONE, NONE, STACK_VAR, 1, IFLAG_MAY_TRAP, "pops the array reference and operand index words"),
      OBJK_ROW(LOAD_INT_ARY_ELM,      COUNT, CTX, NONE, NONE, STACK_VAR, 1, IFLAG_MAY_TRAP, "pops the array reference and operand index words"),
      OBJK_ROW(LOAD_FLOAT_ARY_ELM,    COUNT, CTX, NONE, NONE, STACK_VAR, 1, IFLAG_MAY_TRAP, "pops the array reference and operand index words"),
      OBJK_ROW(STOR_BYTE_ARY_ELM,     COUNT, CTX, NONE, NONE, STACK_VAR, 0, IFLAG_MAY_TRAP | IFLAG_SIDE_EFFECT, "pops the array reference, operand index words, then the value"),
      OBJK_ROW(STOR_CHAR_ARY_ELM,     COUNT, CTX, NONE, NONE, STACK_VAR, 0, IFLAG_MAY_TRAP | IFLAG_SIDE_EFFECT, "pops the array reference, operand index words, then the value"),
      OBJK_ROW(STOR_INT_ARY_ELM,      COUNT, CTX, NONE, NONE, STACK_VAR, 0, IFLAG_MAY_TRAP | IFLAG_SIDE_EFFECT, "pops the array reference, operand index words, then the value; write-barriered"),
      OBJK_ROW(STOR_FLOAT_ARY_ELM,    COUNT, CTX, NONE, NONE, STACK_VAR, 0, IFLAG_MAY_TRAP | IFLAG_SIDE_EFFECT, "pops the array reference, operand index words, then the value"),
      OBJK_ROW(LOAD_ARY_SIZE,         NONE, NONE, NONE, NONE, 1, 1, IFLAG_MAY_TRAP, "array reference -> element count"),
      // ---- limits ----
      OBJK_ROW(NAN_INT,               NONE, NONE, NONE, NONE, 0, 1, 0, "push NaN (as a Float word)"),
      OBJK_ROW(INF_INT,               NONE, NONE, NONE, NONE, 0, 1, 0, "push +infinity (as a Float word)"),
      OBJK_ROW(NEG_INF_INT,           NONE, NONE, NONE, NONE, 0, 1, 0, "push -infinity (as a Float word)"),
      OBJK_ROW(NAN_FLOAT,             NONE, NONE, NONE, NONE, 0, 1, 0, "push NaN"),
      OBJK_ROW(INF_FLOAT,             NONE, NONE, NONE, NONE, 0, 1, 0, "push +infinity"),
      OBJK_ROW(NEG_INF_FLOAT,         NONE, NONE, NONE, NONE, 0, 1, 0, "push -infinity"),
      // ---- comparisons ----
      OBJK_ROW(EQL_INT,               NONE, NONE, NONE, NONE, 2, 1, 0, ""),
      OBJK_ROW(NEQL_INT,              NONE, NONE, NONE, NONE, 2, 1, 0, ""),
      OBJK_ROW(LES_INT,               NONE, NONE, NONE, NONE, 2, 1, 0, ""),
      OBJK_ROW(GTR_INT,               NONE, NONE, NONE, NONE, 2, 1, 0, ""),
      OBJK_ROW(LES_EQL_INT,           NONE, NONE, NONE, NONE, 2, 1, 0, ""),
      OBJK_ROW(GTR_EQL_INT,           NONE, NONE, NONE, NONE, 2, 1, 0, ""),
      OBJK_ROW(EQL_FLOAT,             NONE, NONE, NONE, NONE, 2, 1, 0, "two Floats -> an Int"),
      OBJK_ROW(NEQL_FLOAT,            NONE, NONE, NONE, NONE, 2, 1, 0, "two Floats -> an Int"),
      OBJK_ROW(LES_FLOAT,             NONE, NONE, NONE, NONE, 2, 1, 0, "two Floats -> an Int"),
      OBJK_ROW(GTR_FLOAT,             NONE, NONE, NONE, NONE, 2, 1, 0, "two Floats -> an Int"),
      OBJK_ROW(LES_EQL_FLOAT,         NONE, NONE, NONE, NONE, 2, 1, 0, "two Floats -> an Int"),
      OBJK_ROW(GTR_EQL_FLOAT,         NONE, NONE, NONE, NONE, 2, 1, 0, "two Floats -> an Int"),
      // ---- integer operations ----
      OBJK_ROW(AND_INT,               NONE, NONE, NONE, NONE, 2, 1, 0, "logical and"),
      OBJK_ROW(OR_INT,                NONE, NONE, NONE, NONE, 2, 1, 0, "logical or"),
      OBJK_ROW(ADD_INT,               NONE, NONE, NONE, NONE, 2, 1, 0, ""),
      OBJK_ROW(SUB_INT,               NONE, NONE, NONE, NONE, 2, 1, 0, ""),
      OBJK_ROW(MUL_INT,               NONE, NONE, NONE, NONE, 2, 1, 0, ""),
      OBJK_ROW(DIV_INT,               NONE, NONE, NONE, NONE, 2, 1, IFLAG_MAY_TRAP, "divide by zero"),
      OBJK_ROW(MOD_INT,               NONE, NONE, NONE, NONE, 2, 1, IFLAG_MAY_TRAP, "divide by zero"),
      OBJK_ROW(BIT_AND_INT,           NONE, NONE, NONE, NONE, 2, 1, 0, ""),
      OBJK_ROW(BIT_OR_INT,            NONE, NONE, NONE, NONE, 2, 1, 0, ""),
      OBJK_ROW(BIT_XOR_INT,           NONE, NONE, NONE, NONE, 2, 1, 0, ""),
      OBJK_ROW(BIT_NOT_INT,           NONE, NONE, NONE, NONE, 1, 1, 0, ""),
      OBJK_ROW(SHL_INT,               NONE, NONE, NONE, NONE, 2, 1, 0, "shift count is masked"),
      OBJK_ROW(SHR_INT,               NONE, NONE, NONE, NONE, 2, 1, 0, "shift count is masked"),
      // ---- float operations ----
      OBJK_ROW(ADD_FLOAT,             NONE, NONE, NONE, NONE, 2, 1, 0, ""),
      OBJK_ROW(SUB_FLOAT,             NONE, NONE, NONE, NONE, 2, 1, 0, ""),
      OBJK_ROW(MUL_FLOAT,             NONE, NONE, NONE, NONE, 2, 1, 0, ""),
      OBJK_ROW(DIV_FLOAT,             NONE, NONE, NONE, NONE, 2, 1, IFLAG_MAY_TRAP, "divide by zero raises, as DIV_INT does (#1178)"),
      OBJK_ROW(FLOR_FLOAT,            NONE, NONE, NONE, NONE, 1, 1, 0, ""),
      OBJK_ROW(CEIL_FLOAT,            NONE, NONE, NONE, NONE, 1, 1, 0, ""),
      OBJK_ROW(TRUNC_FLOAT,           NONE, NONE, NONE, NONE, 1, 1, 0, ""),
      OBJK_ROW(SIN_FLOAT,             NONE, NONE, NONE, NONE, 1, 1, 0, ""),
      OBJK_ROW(COS_FLOAT,             NONE, NONE, NONE, NONE, 1, 1, 0, ""),
      OBJK_ROW(TAN_FLOAT,             NONE, NONE, NONE, NONE, 1, 1, 0, ""),
      OBJK_ROW(ASIN_FLOAT,            NONE, NONE, NONE, NONE, 1, 1, 0, ""),
      OBJK_ROW(ACOS_FLOAT,            NONE, NONE, NONE, NONE, 1, 1, 0, ""),
      OBJK_ROW(ATAN_FLOAT,            NONE, NONE, NONE, NONE, 1, 1, 0, ""),
      OBJK_ROW(LOG2_FLOAT,            NONE, NONE, NONE, NONE, 1, 1, 0, ""),
      OBJK_ROW(CBRT_FLOAT,            NONE, NONE, NONE, NONE, 1, 1, 0, ""),
      OBJK_ROW(COSH_FLOAT,            NONE, NONE, NONE, NONE, 1, 1, 0, ""),
      OBJK_ROW(SINH_FLOAT,            NONE, NONE, NONE, NONE, 1, 1, 0, ""),
      OBJK_ROW(TANH_FLOAT,            NONE, NONE, NONE, NONE, 1, 1, 0, ""),
      OBJK_ROW(ATAN2_FLOAT,           NONE, NONE, NONE, NONE, 2, 1, 0, ""),
      OBJK_ROW(ACOSH_FLOAT,           NONE, NONE, NONE, NONE, 1, 1, 0, ""),
      OBJK_ROW(ASINH_FLOAT,           NONE, NONE, NONE, NONE, 1, 1, 0, ""),
      OBJK_ROW(ATANH_FLOAT,           NONE, NONE, NONE, NONE, 1, 1, 0, ""),
      OBJK_ROW(MOD_FLOAT,             NONE, NONE, NONE, NONE, 2, 1, 0, "fmod"),
      OBJK_ROW(LOG_FLOAT,             NONE, NONE, NONE, NONE, 1, 1, 0, ""),
      OBJK_ROW(ROUND_FLOAT,           NONE, NONE, NONE, NONE, 1, 1, 0, ""),
      OBJK_ROW(EXP_FLOAT,             NONE, NONE, NONE, NONE, 1, 1, 0, ""),
      OBJK_ROW(LOG10_FLOAT,           NONE, NONE, NONE, NONE, 1, 1, 0, ""),
      OBJK_ROW(POW_FLOAT,             NONE, NONE, NONE, NONE, 2, 1, 0, ""),
      OBJK_ROW(SQRT_FLOAT,            NONE, NONE, NONE, NONE, 1, 1, 0, ""),
      OBJK_ROW(GAMMA_FLOAT,           NONE, NONE, NONE, NONE, 1, 1, 0, ""),
      OBJK_ROW(RAND_FLOAT,            NONE, NONE, NONE, NONE, 0, 1, IFLAG_SIDE_EFFECT, "advances the generator"),
      // ---- conversions ----
      OBJK_ROW(I2F,                   NONE, NONE, NONE, NONE, 1, 1, 0, ""),
      OBJK_ROW(F2I,                   NONE, NONE, NONE, NONE, 1, 1, 0, ""),
      OBJK_ROW(S2I,                   NONE, NONE, NONE, NONE, 2, 1, IFLAG_MAY_TRAP, "string reference on top, then the base; a Nil string traps"),
      OBJK_ROW(S2F,                   NONE, NONE, NONE, NONE, 1, 1, IFLAG_MAY_TRAP, "string reference; a Nil string traps"),
      OBJK_ROW(I2S,                   NONE, NONE, NONE, NONE, 3, 0, IFLAG_SIDE_EFFECT, "writes into the caller's Char[]: buffer on top, then the base, then the value; nothing is pushed"),
      OBJK_ROW(F2S,                   NONE, NONE, NONE, NONE, 2, 0, IFLAG_SIDE_EFFECT, "writes into the caller's Char[]: buffer on top, then the value; nothing is pushed"),
      // ---- control ----
      OBJK_ROW(MTHD_CALL,             CLASS, METHOD, FLAG, NONE, STACK_VAR, STACK_VAR, IFLAG_CALL | IFLAG_MAY_TRAP | IFLAG_SIDE_EFFECT | IFLAG_ALLOCATES, "pops the instance and the callee's parameter words, pushes its return words (0, 1 or 2); operand3 is is_native in a file, and the VM reuses it as the auto-JIT state"),
      OBJK_ROW(DYN_MTHD_CALL,         COUNT, TYPE, RUNTIME, NONE, STACK_VAR, STACK_VAR, IFLAG_CALL | IFLAG_MAY_TRAP | IFLAG_SIDE_EFFECT | IFLAG_ALLOCATES, "the target (class << 16 | method) is popped, then the instance and parameters; pushes by operand2; operand3 is the VM's JIT state"),
      OBJK_ROW(MTHD_CALL_JIT,         CLASS, METHOD, FLAG, NONE, STACK_VAR, STACK_VAR, IFLAG_VM_ONLY | IFLAG_CALL | IFLAG_MAY_TRAP | IFLAG_SIDE_EFFECT | IFLAG_ALLOCATES, "MTHD_CALL patched to call native code"),
      OBJK_ROW(DYN_MTHD_CALL_JIT,     COUNT, TYPE, RUNTIME, NONE, STACK_VAR, STACK_VAR, IFLAG_VM_ONLY | IFLAG_CALL | IFLAG_MAY_TRAP | IFLAG_SIDE_EFFECT | IFLAG_ALLOCATES, "DYN_MTHD_CALL patched to call native code"),
      OBJK_ROW(JMP,                   LABEL, COND, NONE, NONE, STACK_VAR, 0, IFLAG_JUMP, "pops one word when operand2 >= 0 and jumps if it equals operand2; operand2 < 0 jumps always (then it ends the block). The writer's label is an instruction index after JumpToLocation"),
      OBJK_ROW(LBL,                   LABEL_DEF, NONE, NONE, NONE, 0, 0, 0, "defines a label; the loader keeps it as a no-op so indexes stay put"),
      OBJK_ROW(RTRN,                  NONE, NONE, NONE, NONE, 0, 0, IFLAG_TERMINATOR, "returns to the caller"),
      // ---- memory ----
      OBJK_ROW(NEW_BYTE_ARY,          COUNT, NONE, NONE, NONE, STACK_VAR, 1, IFLAG_ALLOCATES | IFLAG_MAY_TRAP, "pops operand dimension sizes, pushes the array"),
      OBJK_ROW(NEW_CHAR_ARY,          COUNT, NONE, NONE, NONE, STACK_VAR, 1, IFLAG_ALLOCATES | IFLAG_MAY_TRAP, "pops operand dimension sizes, pushes the array"),
      OBJK_ROW(NEW_INT_ARY,           COUNT, NONE, NONE, NONE, STACK_VAR, 1, IFLAG_ALLOCATES | IFLAG_MAY_TRAP, "an Int[]: never scanned by the collector"),
      OBJK_ROW(NEW_FLOAT_ARY,         COUNT, NONE, NONE, NONE, STACK_VAR, 1, IFLAG_ALLOCATES | IFLAG_MAY_TRAP, "pops operand dimension sizes, pushes the array"),
      OBJK_ROW(NEW_OBJ_INST,          CLASS, NONE, NONE, NONE, 0, 1, IFLAG_ALLOCATES, "pushes a new instance of the class"),
      OBJK_ROW(NEW_FUNC_INST,         SIZE, NONE, NONE, NONE, 0, 1, IFLAG_ALLOCATES, "allocates operand bytes for a function value's closure memory"),
      OBJK_ROW(CPY_BYTE_ARY,          NONE, NONE, NONE, NONE, 5, 1, IFLAG_MAY_TRAP | IFLAG_SIDE_EFFECT, "length, source offset, source, destination offset, destination -> a success flag"),
      OBJK_ROW(CPY_CHAR_ARY,          NONE, NONE, NONE, NONE, 5, 1, IFLAG_MAY_TRAP | IFLAG_SIDE_EFFECT, "length, source offset, source, destination offset, destination -> a success flag"),
      OBJK_ROW(CPY_INT_ARY,           NONE, NONE, NONE, NONE, 5, 1, IFLAG_MAY_TRAP | IFLAG_SIDE_EFFECT, "length, source offset, source, destination offset, destination -> a success flag"),
      OBJK_ROW(CPY_FLOAT_ARY,         NONE, NONE, NONE, NONE, 5, 1, IFLAG_MAY_TRAP | IFLAG_SIDE_EFFECT, "length, source offset, source, destination offset, destination -> a success flag"),
      OBJK_ROW(ZERO_BYTE_ARY,         NONE, NONE, NONE, NONE, 1, 0, IFLAG_MAY_TRAP | IFLAG_SIDE_EFFECT, "array reference"),
      OBJK_ROW(ZERO_CHAR_ARY,         NONE, NONE, NONE, NONE, 1, 0, IFLAG_MAY_TRAP | IFLAG_SIDE_EFFECT, "array reference"),
      OBJK_ROW(ZERO_INT_ARY,          NONE, NONE, NONE, NONE, 1, 0, IFLAG_MAY_TRAP | IFLAG_SIDE_EFFECT, "array reference"),
      OBJK_ROW(ZERO_FLOAT_ARY,        NONE, NONE, NONE, NONE, 1, 0, IFLAG_MAY_TRAP | IFLAG_SIDE_EFFECT, "array reference"),
      // ---- casts and type checks ----
      OBJK_ROW(OBJ_INST_CAST,         CLASS, NONE, NONE, NONE, 1, 1, IFLAG_MAY_TRAP, "an invalid cast raises; Nil passes through"),
      OBJK_ROW(OBJ_TYPE_OF,           CLASS, NONE, NONE, NONE, 1, 1, IFLAG_MAY_TRAP, "a Nil operand raises"),
      // ---- traps ----
      OBJK_ROW(TRAP,                  COUNT, NONE, NONE, NONE, STACK_VAR, STACK_VAR, IFLAG_MAY_TRAP | IFLAG_SIDE_EFFECT | IFLAG_ALLOCATES, "operand words incl. the trap id (a LOAD_INT_LIT just ahead, which the loader verifies) are popped; the results depend on the trap id"),
      OBJK_ROW(TRAP_RTRN,             COUNT, NONE, NONE, NONE, STACK_VAR, STACK_VAR, IFLAG_MAY_TRAP | IFLAG_SIDE_EFFECT | IFLAG_ALLOCATES, "a TRAP whose trap returns a value; the results depend on the trap id"),
      OBJK_ROW(SET_SIGNAL,            NONE, NONE, NONE, NONE, 0, 0, IFLAG_PSEUDO, "an id the compiler pushes ahead of TRAP; never an instruction"),
      OBJK_ROW(RAISE_SIGNAL,          NONE, NONE, NONE, NONE, 0, 0, IFLAG_PSEUDO, "an id the compiler pushes ahead of TRAP; never an instruction"),
      // ---- shared libraries ----
      OBJK_ROW(EXT_LIB_LOAD,          NONE, NONE, NONE, NONE, 0, 0, IFLAG_MAY_TRAP | IFLAG_SIDE_EFFECT, "reads its arguments from the frame, not the stack"),
      OBJK_ROW(EXT_LIB_UNLOAD,        NONE, NONE, NONE, NONE, 0, 0, IFLAG_MAY_TRAP | IFLAG_SIDE_EFFECT, "reads its arguments from the frame, not the stack"),
      OBJK_ROW(EXT_LIB_FUNC_CALL,     NONE, NONE, NONE, NONE, 0, STACK_VAR, IFLAG_CALL | IFLAG_MAY_TRAP | IFLAG_SIDE_EFFECT | IFLAG_ALLOCATES, "runs a native function, which leaves its results on the operand stack itself"),
      // ---- stack ----
      OBJK_ROW(SWAP_INT,              NONE, NONE, NONE, NONE, 2, 2, 0, ""),
      OBJK_ROW(POP_INT,               NONE, NONE, NONE, NONE, 1, 0, 0, ""),
      OBJK_ROW(POP_FLOAT,             NONE, NONE, NONE, NONE, 1, 0, 0, ""),
      // ---- threads ----
      OBJK_ROW(ASYNC_MTHD_CALL,       CLASS, METHOD, FLAG, NONE, 0, 0, IFLAG_CALL | IFLAG_MAY_TRAP | IFLAG_SIDE_EFFECT | IFLAG_ALLOCATES, "starts the Thread's Run(Base) from the frame; the three operands are written but the VM ignores them"),
      OBJK_ROW(THREAD_JOIN,           NONE, NONE, NONE, NONE, 0, 0, IFLAG_MAY_TRAP | IFLAG_SIDE_EFFECT, "joins the thread in the frame's self"),
      OBJK_ROW(THREAD_SLEEP,          NONE, NONE, NONE, NONE, 1, 0, IFLAG_SIDE_EFFECT, "milliseconds"),
      OBJK_ROW(THREAD_MUTEX,          NONE, NONE, NONE, NONE, 0, 0, IFLAG_MAY_TRAP | IFLAG_SIDE_EFFECT, "creates the mutex for the frame's self"),
      OBJK_ROW(CRITICAL_START,        NONE, NONE, NONE, NONE, 1, 0, IFLAG_MAY_TRAP | IFLAG_SIDE_EFFECT, "locks the instance's mutex"),
      OBJK_ROW(CRITICAL_END,          NONE, NONE, NONE, NONE, 1, 0, IFLAG_MAY_TRAP | IFLAG_SIDE_EFFECT, "unlocks the instance's mutex"),
      // ---- library directives: only in a .obl; the linker rewrites each ----
      OBJK_ROW(LIB_OBJ_TYPE_OF,       NONE, NONE, NONE, STR, 1, 1, IFLAG_LIB_ONLY | IFLAG_MAY_TRAP, "resolved to OBJ_TYPE_OF"),
      OBJK_ROW(LIB_NEW_OBJ_INST,      NONE, NONE, NONE, STR, 0, 1, IFLAG_LIB_ONLY | IFLAG_ALLOCATES, "resolved to NEW_OBJ_INST"),
      OBJK_ROW(LIB_MTHD_CALL,         NONE, NONE, FLAG, STR2, STACK_VAR, STACK_VAR, IFLAG_LIB_ONLY | IFLAG_CALL | IFLAG_MAY_TRAP | IFLAG_SIDE_EFFECT | IFLAG_ALLOCATES, "resolved to MTHD_CALL; operand3 is is_native"),
      OBJK_ROW(LIB_OBJ_INST_CAST,     NONE, NONE, NONE, STR, 1, 1, IFLAG_LIB_ONLY | IFLAG_MAY_TRAP, "resolved to OBJ_INST_CAST"),
      OBJK_ROW(LIB_FUNC_DEF,          NONE, NONE, NONE, STR2, 0, 1, IFLAG_LIB_ONLY, "resolved to LOAD_INT_LIT of the function value"),
      // ---- try ----
      OBJK_ROW(TRY_START,             LABEL, FLAG, NONE, NONE, 0, 0, IFLAG_JUMP | IFLAG_SIDE_EFFECT, "pushes an error handler at the label; operand2 is written and read but the VM ignores it"),
      OBJK_ROW(TRY_END,               NONE, NONE, NONE, NONE, 0, 0, IFLAG_SIDE_EFFECT, "pops the error handler"),
      // ---- select jump table ----
      OBJK_ROW(JMP_TABLE,             IMM, COUNT, LABEL, NONE, 1, 0, IFLAG_JUMP | IFLAG_TERMINATOR, "pops the selector; operand is the base, operand2 the slot count, operand3 the DEFAULT label (not operand: the label is in the third operand). 'range' JMP_TABLE_SLOTs follow"),
      OBJK_ROW(JMP_TABLE_SLOT,        LABEL, NONE, NONE, NONE, 0, 0, IFLAG_JUMP, "a table entry; read by JMP_TABLE, never executed"),
      // ---- appended after the jump table so the opcodes above keep their numbers ----
      OBJK_ROW(NEW_OBJ_ARY,           COUNT, NONE, NONE, NONE, STACK_VAR, 1, IFLAG_ALLOCATES | IFLAG_MAY_TRAP, "an array of references: scanned and relocated by the collector"),
      // ---- marker ----
      OBJK_ROW(END_STMTS,             NONE, NONE, NONE, NONE, 0, 0, IFLAG_PSEUDO, "the enum's last value; never an instruction"),
    };

#undef OBJK_ROW

    constexpr size_t kInstrCount = sizeof(kInstrInfo) / sizeof(kInstrInfo[0]);

    // The two checks below loop inside a constexpr function, which needs C++14. The native
    // libraries include this header from projects that build at C++11 (the Linux build scripts
    // of core/lib/crypto and others), where a loop in a constexpr body is an error. The VM and
    // the compiler build at C++17 or later and always run the checks, so a missing row still
    // fails the build there; the libraries just skip the assertions.
#if (__cplusplus >= 201402L) || (defined(_MSVC_LANG) && _MSVC_LANG >= 201402L)
    // Exactly one row per enumerator, in enum order: a new opcode needs its row
    // here, in the same place, or this file does not compile.
    constexpr bool TableMatchesEnum() {
      for(size_t i = 0; i < kInstrCount; ++i) {
        if(static_cast<size_t>(kInstrInfo[i].type) != i) {
          return false;
        }
      }
      return true;
    }

    static_assert(kInstrCount == static_cast<size_t>(instructions::END_STMTS) + 1,
                  "instr_info.h: one row per InstructionType enumerator (a row is missing or extra)");
    static_assert(TableMatchesEnum(),
                  "instr_info.h: the rows must be in the order of the InstructionType enumerators");
#endif

#if (__cplusplus >= 201402L) || (defined(_MSVC_LANG) && _MSVC_LANG >= 201402L)
    // Rules that hold for every row, so a new row cannot contradict itself.
    constexpr bool RowsAreConsistent() {
      for(size_t i = 0; i < kInstrCount; ++i) {
        const InstrInfo& row = kInstrInfo[i];
        // a jump names a label; a label reference or definition is a jump or LBL
        if(row.IsJump() && !row.CarriesLabel()) {
          return false;
        }
        // a slot comes with its memory context
        if(row.CarriesSlot() && row.op2 != Role::R_CTX) {
          return false;
        }
        // the base address is only popped for a variable access
        if(row.Has(IFLAG_CTX_ADDR) && !row.CarriesSlot()) {
          return false;
        }
        // nothing is in a file that is made by the VM, or is not an instruction
        if(row.Has(IFLAG_VM_ONLY) && row.Has(IFLAG_LIB_ONLY)) {
          return false;
        }
        // a wide field is never beside a role in the same slot it would shadow
        if(row.wide != Wide::W_NONE && row.Has(IFLAG_VM_ONLY)) {
          return false;
        }
      }
      return true;
    }

    static_assert(RowsAreConsistent(), "instr_info.h: a row contradicts the rules in RowsAreConsistent");
#endif

    inline constexpr const InstrInfo& Info(InstructionType type) {
      return kInstrInfo[static_cast<size_t>(type)];
    }

  }
}
