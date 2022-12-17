/***************************************************************************
 * Copyright (c) 2008-2013, Randy Hollines
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

#ifndef __INSTRS_H__
#define __INSTRS_H__

namespace instructions {
  // vm instructions
  enum InstructionType {
    // loads operations
    LOAD_INT_LIT  = 0,
    LOAD_CHAR_LIT,
    LOAD_FLOAT_LIT,
    LOAD_INT_VAR,
    LOAD_LOCL_INT_VAR, // only used by the VM
    LOAD_CLS_INST_INT_VAR, // only used by the VM
    LOAD_FLOAT_VAR,
    LOAD_FUNC_VAR,
    LOAD_CLS_MEM,
    LOAD_INST_MEM,
    // stores operations
    STOR_INT_VAR,
    STOR_LOCL_INT_VAR, // only used by the VM
    STOR_CLS_INST_INT_VAR, // only used by the VM
    STOR_FLOAT_VAR,
    STOR_FUNC_VAR,
    // copy operations
    COPY_INT_VAR,
    COPY_LOCL_INT_VAR, // only used by the VM
    COPY_CLS_INST_INT_VAR, // only used by the VM
    COPY_FLOAT_VAR,
    COPY_FUNC_VAR,
    // array operations
    LOAD_BYTE_ARY_ELM,
    LOAD_CHAR_ARY_ELM,
    LOAD_INT_ARY_ELM,
    LOAD_FLOAT_ARY_ELM,
    STOR_BYTE_ARY_ELM,
    STOR_CHAR_ARY_ELM,
    STOR_INT_ARY_ELM,
    STOR_FLOAT_ARY_ELM,
    LOAD_ARY_SIZE,
    // logical operations
    EQL_INT,
    NEQL_INT,
    LES_INT,
    GTR_INT,
    LES_EQL_INT,
    GTR_EQL_INT,
    EQL_FLOAT,
    NEQL_FLOAT,
    LES_FLOAT,
    GTR_FLOAT,
    LES_EQL_FLOAT,
    GTR_EQL_FLOAT,
    // mathematical operations
    AND_INT,
    OR_INT,
    ADD_INT,
    SUB_INT,
    MUL_INT,
    DIV_INT,
    MOD_INT,
    BIT_AND_INT,
    BIT_OR_INT,
    BIT_XOR_INT,
    SHL_INT,
    SHR_INT,
    ADD_FLOAT,
    SUB_FLOAT,
    MUL_FLOAT,
    DIV_FLOAT,
    FLOR_FLOAT,
    CEIL_FLOAT,
    SIN_FLOAT,
    COS_FLOAT,
    TAN_FLOAT,
    ASIN_FLOAT,
    ACOS_FLOAT,
    ATAN_FLOAT,
    ATAN2_FLOAT,
    MOD_FLOAT,
    LOG_FLOAT,
    POW_FLOAT,
    SQRT_FLOAT,
    RAND_FLOAT,
    // conversions
    I2F,
    F2I,
    S2I,
    S2F,
    I2S,
    F2S,
    // control
    MTHD_CALL,
    DYN_MTHD_CALL,
    JMP,
    LBL,
    RTRN,
    // memory operations
    NEW_BYTE_ARY,
    NEW_CHAR_ARY,
    NEW_INT_ARY,
    NEW_FLOAT_ARY,
    NEW_OBJ_INST,
    NEW_FUNC_INST,
    CPY_BYTE_ARY,
    CPY_CHAR_ARY,
    CPY_INT_ARY,
    CPY_FLOAT_ARY,
    // casting & type check
    OBJ_INST_CAST,
    OBJ_TYPE_OF,
    // external OS traps
    TRAP,
    TRAP_RTRN,
    SET_SIGNAL,
    RAISE_SIGNAL,
    // shared libraries
    DLL_LOAD,
    DLL_UNLOAD,
    DLL_FUNC_CALL,
    // stack ops
    SWAP_INT,
    POP_INT,
    POP_FLOAT,
    // thread directives
    ASYNC_MTHD_CALL,
    THREAD_JOIN,
    THREAD_SLEEP,
    THREAD_MUTEX,
    CRITICAL_START,
    CRITICAL_END,
    // library directives
    LIB_OBJ_TYPE_OF,
    LIB_NEW_OBJ_INST,
    LIB_MTHD_CALL,
    LIB_OBJ_INST_CAST,
    LIB_FUNC_DEF,
    // system directives
    END_STMTS,
  };

  // memory reference context, used for
  // loading and storing variables
  enum MemoryContext {
    CLS = -3500,
    INST,
    LOCL
  };
}

#endif
