/***************************************************************************
 * ODBC support for Objeck
 *
 * Copyright (c) 2011-2012, Randy Hollines
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

#ifndef __ODBC_H__
#define __ODBC_H__

#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>
#include "../../vm/lib_api.h"
#include "../../shared/sys.h"

#define SQL_OK status == SQL_SUCCESS || status == SQL_SUCCESS_WITH_INFO
#define SQL_FAIL status != SQL_SUCCESS && status != SQL_SUCCESS_WITH_INFO
#define COL_NAME_MAX 64
#define VARCHAR_MAX 1024

extern "C" {
  static SQLHENV env;

  typedef struct _ColumnDescription {
    SQLCHAR column_name[COL_NAME_MAX];
    SQLSMALLINT column_name_size;
    SQLSMALLINT type;
    SQLULEN column_size;
    SQLSMALLINT decimal_length;
    SQLSMALLINT nullable;
  } ColumnDescription;

  void ShowError(SQLSMALLINT type, SQLHSTMT hstmt) {
    SQLCHAR SqlState[6];
    SQLCHAR Msg[SQL_MAX_MESSAGE_LENGTH];
    SQLINTEGER NativeError;
    SQLSMALLINT MsgLen;
    SQLRETURN result;

    SQLSMALLINT i = 1;
    while((result = SQLGetDiagRec(SQL_HANDLE_DBC, hstmt, i, SqlState, &NativeError, Msg, sizeof(Msg), &MsgLen)) != SQL_NO_DATA) {
      std::cout << NativeError << " - " << Msg << std::endl;
      i++;
    }
  }
}

#endif
