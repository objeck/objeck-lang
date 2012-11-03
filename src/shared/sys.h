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

#ifndef __SYS_H__
#define __SYS_H__

// define windows type
#ifdef _WIN32
#include <stdint.h>
#endif

#ifdef _MINGW
#define BYTE_VALUE unsigned char
#define CHAR_VALUE char
#define INT_VALUE int
#define FLOAT_VALUE double
#else
#define BYTE_VALUE uint8_t
#define CHAR_VALUE int8_t
#define INT_VALUE int32_t
#define FLOAT_VALUE double
#endif

namespace instructions {
  // vm types
  typedef enum _MemoryType {
    NIL_TYPE = -1000,
    BYTE_ARY_TYPE,
    INT_TYPE,
    FLOAT_TYPE,
    FUNC_TYPE
  } 
  MemoryType;

  // garbage types
  typedef enum _ParamType {
    INT_PARM = -1500,
    FLOAT_PARM,
    BYTE_ARY_PARM,
    INT_ARY_PARM,
    FLOAT_ARY_PARM,
    OBJ_PARM,
    OBJ_ARY_PARM,
    FUNC_PARM,
  } 
  ParamType;
}

#endif
