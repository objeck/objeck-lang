/***************************************************************************
 * Diagnostics support for Objeck
 *
 * Copyright (c) 2021, Randy Hollines
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
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ***************************************************************************/

#ifndef __TYPE_H__
#define __TYPE_H__

#ifdef _WIN32
#include <windows.h>
#endif

#include "../../vm/lib_api.h"
#include "../../shared/sys.h"
#include "../../compiler/tree.h"

// result codes
enum ResultType {
  // other
  TYPE_UNKN = 0,
  // severity
  TYPE_ERROR = 101,
  TYPE_WARN = 102,
  TYPE_INFO = 103,
  // symbols
  TYPE_FILE = 201,
  TYPE_NAMESPACE = 203,
  TYPE_CLASS = 205,
  TYPE_ENUM = 210,
  TYPE_METHOD = 206,
  TYPE_VARIABLE = 213,
};

// result positions
enum ResultPosition {
  POS_NAME = 0,
  POS_CODE = 1,
  POS_TYPE = 2,
  POS_CHILDREN = 3,
  POS_DESC = 4,
  POS_START_LINE = 5,
  POS_START_POS = 6,
  POS_END_LINE = 7,
  POS_END_POS = 8
};

extern "C" {
  size_t* FormatErrors(VMContext& context, vector<wstring> error_strings);
  void GetTypeName(frontend::Type* type, wstring &output);
}

#endif
