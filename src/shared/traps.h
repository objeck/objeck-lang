/***************************************************************************
 *
 *
 * Copyright (c) 2008-2011, Randy Hollines
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

#ifndef __TRAPS_H__
#define __TRAPS_H__

namespace instructions {
  // system directive codes
  typedef enum _Traps {
    // core operations
    LOAD_CLS_INST_ID = -4000,
    LOAD_CLS_BY_INST,
    LOAD_CLS_BY_NAME,
    INVOKE_MTHD,
    LOAD_INST_UID,
    LOAD_ARY_SIZE,
    CPY_CHAR_STR_ARY,
    CPY_CHAR_STR_ARYS,
    CPY_INT_STR_ARY,
    CPY_FLOAT_STR_ARY,
    // time
    SYS_TIME,
    GMT_TIME,
		DATE_TIME_SET_1,
		DATE_TIME_SET_2,
    TIMER_START,
    TIMER_END,
    // standard i/o
    STD_IN_STRING,
    STD_OUT_BOOL,
    STD_OUT_BYTE,
    STD_OUT_CHAR,
    STD_OUT_INT,
    STD_OUT_FLOAT,
    STD_OUT_CHAR_ARY,
    // file i/o
    FILE_OPEN_READ,
    FILE_OPEN_WRITE,
    FILE_OPEN_READ_WRITE,
    FILE_CLOSE,
    FILE_IN_BYTE,
    FILE_OUT_BYTE,
    FILE_IN_BYTE_ARY,
    FILE_OUT_BYTE_ARY,
    FILE_IN_STRING,
    FILE_OUT_STRING,
    // file operations
    FILE_IS_OPEN,
    FILE_EXISTS,
    FILE_SIZE,
    FILE_REWIND,
    FILE_SEEK,
    FILE_EOF,
    FILE_DELETE,
    FILE_RENAME,
    // directory operations
    DIR_CREATE,
    DIR_EXISTS,
    DIR_LIST,
    // socket i/o
    SOCK_TCP_CONNECT,
    SOCK_TCP_BIND,
    SOCK_TCP_LISTEN,
    SOCK_TCP_ACCEPT,
    SOCK_TCP_IS_CONNECTED,
    SOCK_TCP_CLOSE,
    SOCK_TCP_IN_BYTE,
    SOCK_TCP_IN_BYTE_ARY,
    SOCK_TCP_OUT_BYTE,
    SOCK_TCP_OUT_BYTE_ARY,
    SOCK_TCP_IN_STRING,
    SOCK_TCP_OUT_STRING,
    SOCK_TCP_HOST_NAME,
    // serialization
    SERL_INT,
    SERL_FLOAT,
    SERL_OBJ_INST,
    SERL_BYTE_ARY,
    SERL_INT_ARY,
    SERL_FLOAT_ARY,
    DESERL_INT,
    DESERL_FLOAT,
    DESERL_OBJ_INST,
    DESERL_BYTE_ARY,
    DESERL_INT_ARY,
    DESERL_FLOAT_ARY,
    // platform
    PLTFRM,
    EXIT,
  } 
  Traps;
}

#endif
