/***************************************************************************
 * ODBC support for Objeck
 *
 * Copyright (c) 2011-2015, Randy Hollines
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

#include "odbc.h"
#include <mutex>

extern "C" {

  //
  // Parameter values, owned by the statement they are bound to. ODBC reads a bound
  // parameter's buffer and length indicator when the statement runs, not when it is
  // bound, so both must live as long as the binding. They used to point into the
  // Objeck objects passed to the setter: an IntRef or a FloatRef young enough to be
  // moved or collected before the statement ran, and buffers a caller dropped between
  // runs of a reused statement. Each value is now copied here, keyed by statement
  // handle and position, and freed when the statement is.
  //
  struct BoundParam {
    std::vector<unsigned char> data;
    SQLLEN length;
  };

  static std::mutex bound_params_lock;
  static std::map<SQLHSTMT, std::map<int, BoundParam> > bound_params;

  // copies a value for a position and returns its stable storage
  static BoundParam* keep_param(SQLHSTMT stmt, int i, const void* value, size_t size)
  {
    std::lock_guard<std::mutex> guard(bound_params_lock);
    BoundParam& param = bound_params[stmt][i];
    const unsigned char* bytes = (const unsigned char*)value;
    param.data.assign(bytes, bytes + size);
    // a zero-length value still needs an address to bind
    if(param.data.empty()) {
      param.data.push_back(0);
    }
    param.length = (SQLLEN)size;
    return &param;
  }

  static BoundParam* find_param(SQLHSTMT stmt, int i)
  {
    std::lock_guard<std::mutex> guard(bound_params_lock);
    std::map<SQLHSTMT, std::map<int, BoundParam> >::iterator found = bound_params.find(stmt);
    if(found == bound_params.end()) {
      return nullptr;
    }
    std::map<int, BoundParam>::iterator param = found->second.find(i);
    return param == found->second.end() ? nullptr : &param->second;
  }

  static void drop_params(SQLHSTMT stmt)
  {
    std::lock_guard<std::mutex> guard(bound_params_lock);
    bound_params.erase(stmt);
  }

  // binds a fixed-size value of a C type
  static void bind_value(VMContext& context, SQLHSTMT stmt, SQLUSMALLINT i, SQLSMALLINT c_type, SQLSMALLINT sql_type,
                         const void* value, size_t size)
  {
    BoundParam* param = keep_param(stmt, i, value, size);
    const SQLRETURN status = SQLBindParameter(stmt, i, SQL_PARAM_INPUT, c_type, sql_type, 0, 0,
                                              (SQLPOINTER)param->data.data(), (SQLLEN)size, &param->length);
    APITools_SetIntValue(context, 0, (status == SQL_SUCCESS || status == SQL_SUCCESS_WITH_INFO) ? 1 : 0);
  }
  SQLLEN sql_null;

  // Reads a whole character cell in pieces. One fixed buffer used to cut text
  // longer than VARCHAR_MAX short without a word, and a NULL cell was read back
  // from the uninitialized buffer, since SQLGetData writes nothing for one.
  static bool read_text_cell(SQLHSTMT stmt, SQLUSMALLINT i, std::string& out, bool& is_null)
  {
    out.clear();
    is_null = false;

    char buffer[VARCHAR_MAX];
    while(true) {
      SQLLEN ind = 0;
      const SQLRETURN status = SQLGetData(stmt, i, SQL_C_CHAR, buffer, VARCHAR_MAX, &ind);
      if(status == SQL_NO_DATA) {
        return true;
      }
      if(SQL_FAIL) {
        return false;
      }
      if(ind == SQL_NULL_DATA) {
        is_null = true;
        return true;
      }

      // truncated: a full buffer less its terminator, and more to come
      if(status == SQL_SUCCESS_WITH_INFO && (ind == SQL_NO_TOTAL || ind >= VARCHAR_MAX)) {
        out.append(buffer, VARCHAR_MAX - 1);
        continue;
      }

      out.append(buffer, ind > 0 ? (size_t)ind : 0);
      return true;
    }
  }

  // Reads a whole binary cell in pieces, so its length is known exactly
  static bool read_binary_cell(SQLHSTMT stmt, SQLUSMALLINT i, std::vector<unsigned char>& out, bool& is_null)
  {
    out.clear();
    is_null = false;

    unsigned char buffer[VARCHAR_MAX];
    while(true) {
      SQLLEN ind = 0;
      const SQLRETURN status = SQLGetData(stmt, i, SQL_C_BINARY, buffer, VARCHAR_MAX, &ind);
      if(status == SQL_NO_DATA) {
        return true;
      }
      if(SQL_FAIL) {
        return false;
      }
      if(ind == SQL_NULL_DATA) {
        is_null = true;
        return true;
      }

      if(status == SQL_SUCCESS_WITH_INFO && (ind == SQL_NO_TOTAL || ind > VARCHAR_MAX)) {
        out.insert(out.end(), buffer, buffer + VARCHAR_MAX);
        continue;
      }

      out.insert(out.end(), buffer, buffer + (ind > 0 ? (size_t)ind : 0));
      return true;
    }
  }

  // sets a text result: [0] the null flag, [1] the string (Nil for NULL or an error)
  static void set_text_result(VMContext& context, SQLHSTMT stmt, SQLUSMALLINT i)
  {
    std::string value;
    bool is_null = false;
    if(read_text_cell(stmt, i, value, is_null) && !is_null) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetStringValue(context, 1, BytesToUnicode(value));
    }
    else {
      APITools_SetIntValue(context, 0, is_null ? 1 : 0);
      APITools_SetObjectValue(context, 1, 0);
    }
  }

  // Why the last connect on this thread failed. A failed connect frees its
  // handle, leaving odbc_get_error nothing to read, so Connection->GetLastError()
  // returned "" for the one error a caller cannot diagnose any other way.
  static thread_local std::string last_connect_error;

  // Why the last select or prepare on this thread failed. Those calls free
  // their statement handle on failure, so its diagnostic went with it and a
  // failed Select read as an empty result with no reason given.
  static thread_local std::string last_statement_error;

  static void set_statement_error(SQLSMALLINT handle_type, SQLHANDLE handle)
  {
    SQLCHAR sql_state[6] = {0};
    SQLINTEGER native_error = 0;
    SQLCHAR message[SQL_MAX_MESSAGE_LENGTH] = {0};
    SQLSMALLINT text_len = 0;

    const SQLRETURN result = handle ? SQLGetDiagRec(handle_type, handle, 1, sql_state, &native_error,
                                                    message, sizeof(message), &text_len) : SQL_ERROR;
    if(result == SQL_SUCCESS || result == SQL_SUCCESS_WITH_INFO) {
      char error_msg[SQL_MAX_MESSAGE_LENGTH + 64];
      snprintf(error_msg, sizeof(error_msg), "[%s] %s (%d)", (char*)sql_state, (char*)message, (int)native_error);
      last_statement_error = error_msg;
    }
    else {
      last_statement_error = "the statement failed; the driver gave no diagnostic";
    }
  }

  static void set_connect_error(SQLHDBC conn)
  {
    SQLCHAR sql_state[6] = {0};
    SQLINTEGER native_error = 0;
    SQLCHAR message[SQL_MAX_MESSAGE_LENGTH] = {0};
    SQLSMALLINT text_len = 0;

    const SQLRETURN result = SQLGetDiagRec(SQL_HANDLE_DBC, conn, 1, sql_state, &native_error,
                                           message, sizeof(message), &text_len);
    if(result == SQL_SUCCESS || result == SQL_SUCCESS_WITH_INFO) {
      char error_msg[SQL_MAX_MESSAGE_LENGTH + 64];
      snprintf(error_msg, sizeof(error_msg), "[%s] %s (%d)", (char*)sql_state, (char*)message, (int)native_error);
      last_connect_error = error_msg;
    }
    else {
      last_connect_error = "connection failed; the driver manager gave no diagnostic";
    }
  }

  //
  // initialize odbc environment
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
    void load_lib(VMContext& context)
  {
    if(!env) {
      SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);
      SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (void*)SQL_OV_ODBC3, 0);
      sql_null = SQL_NULL_DATA;
    }
  }

  //
  // release odbc resources
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
    void unload_lib()
  {
    if(env) {
      SQLFreeHandle(SQL_HANDLE_ENV, env);
    }
  }

  //
  // connects to an ODBC data source
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
    void odbc_connect(VMContext& context)
  {
    SQLHDBC conn;

    const std::wstring wds(APITools_GetStringValue(context, 1));
    const std::wstring wusername(APITools_GetStringValue(context, 2));
    const std::wstring wpassword(APITools_GetStringValue(context, 3));

#ifdef _DEBUG
    std::wcout << L"### connect: " << L"ds=" << wds << L", username="
      << wusername << L", password=" << wpassword << L" ###" << std::endl;
#endif

    const std::string ds = UnicodeToBytes(wds);
    const std::string username = UnicodeToBytes(wusername);
    const std::string password = UnicodeToBytes(wpassword);

    SQLRETURN status = SQLAllocHandle(SQL_HANDLE_DBC, env, &conn);
    if(SQL_FAIL) {
      last_connect_error = "could not allocate an ODBC connection handle";
      SQLFreeHandle(SQL_HANDLE_DBC, conn);
      APITools_SetIntValue(context, 0, 0);
      return;
    }

    {
      // a network round trip to the data source; park so a collection on
      // another thread need not wait for it (only C memory is touched)
      APITools_Parked parked(context);
      status = SQLConnect(conn, (SQLCHAR*)ds.c_str(), SQL_NTS, (SQLCHAR*)username.c_str(),
                          SQL_NTS, (SQLCHAR*)password.c_str(), SQL_NTS);
    }
    if(SQL_FAIL) {
      set_connect_error(conn);
      SQLFreeHandle(SQL_HANDLE_DBC, conn);
      conn = NULL;
      APITools_SetIntValue(context, 0, 0);
      return;
    }

    last_connect_error.clear();
    APITools_SetIntValue(context, 0, (size_t)conn);
  }

  //
  // connects to an ODBC data source using a connection string
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
    void odbc_connect_string(VMContext& context)
  {
    SQLHDBC conn;

    const std::wstring wconn_str(APITools_GetStringValue(context, 1));

#ifdef _DEBUG
    std::wcout << L"### connect_string: " << wconn_str << L" ###" << std::endl;
#endif

    const std::string conn_str = UnicodeToBytes(wconn_str);

    SQLRETURN status = SQLAllocHandle(SQL_HANDLE_DBC, env, &conn);
    if(SQL_FAIL) {
      last_connect_error = "could not allocate an ODBC connection handle";
      SQLFreeHandle(SQL_HANDLE_DBC, conn);
      APITools_SetIntValue(context, 0, 0);
      return;
    }

    SQLCHAR out_conn_str[1024];
    SQLSMALLINT out_conn_str_len;
    {
      APITools_Parked parked(context);
      status = SQLDriverConnect(conn, NULL, (SQLCHAR*)conn_str.c_str(), SQL_NTS,
                                out_conn_str, sizeof(out_conn_str), &out_conn_str_len,
                                SQL_DRIVER_NOPROMPT);
    }
    if(SQL_FAIL) {
      set_connect_error(conn);
      SQLFreeHandle(SQL_HANDLE_DBC, conn);
      conn = NULL;
      APITools_SetIntValue(context, 0, 0);
      return;
    }

    last_connect_error.clear();
    APITools_SetIntValue(context, 0, (size_t)conn);
  }

  //
  // disconnects from an ODBC data source
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
    void odbc_disconnect(VMContext& context)
  {
    SQLHDBC conn = (SQLHDBC)APITools_GetIntValue(context, 0);
    if(conn) {
      SQLDisconnect(conn);
      SQLFreeHandle(SQL_HANDLE_DBC, conn);

#ifdef _DEBUG
      std::wcout << L"### disconnect ###" << std::endl;
#endif
    }
  }

  //
  // executes and update statement
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
    void odbc_update_statement(VMContext& context)
  {
    SQLHDBC conn = (SQLHDBC)APITools_GetIntValue(context, 1);
    const std::wstring wsql(APITools_GetStringValue(context, 2));

#ifdef _DEBUG
    std::wcout << L"### update: conn=" << conn << L", stmt=" << wsql << L"  ###" << std::endl;
#endif    

    if(!conn || wsql.size() < 1) {
      APITools_SetIntValue(context, 0, -1);
      return;
    }

    SQLHSTMT stmt = NULL;
    SQLRETURN status = SQLAllocHandle(SQL_HANDLE_STMT, conn, &stmt);
    if(SQL_FAIL) {
      if(stmt) {
        SQLFreeStmt(stmt, SQL_UNBIND);
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
      };
      APITools_SetIntValue(context, 0, -1);
      return;
    }

    const std::string sql = UnicodeToBytes(wsql);
    {
      // the statement runs on the data source; park for it (sql is C memory)
      APITools_Parked parked(context);
      status = SQLExecDirect(stmt, (SQLCHAR*)sql.c_str(), SQL_NTS);
    }
    if(SQL_FAIL) {
      // ShowError(SQL_HANDLE_STMT, stmt);
      SQLFreeStmt(stmt, SQL_UNBIND);
      SQLFreeHandle(SQL_HANDLE_STMT, stmt);
      APITools_SetIntValue(context, 0, -1);
      return;
    }

    SQLLEN count;
    status = SQLRowCount(stmt, &count);
    if(SQL_FAIL) {
      // ShowError(SQL_HANDLE_STMT, stmt);
      SQLFreeStmt(stmt, SQL_UNBIND);
      APITools_SetIntValue(context, 0, -1);
      return;
    }

    SQLFreeStmt(stmt, SQL_UNBIND);
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    APITools_SetIntValue(context, 0, count);
  }

  //
  // executes a prepared select statement
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
    void odbc_stmt_select_statement(VMContext& context)
  {

    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 0);
    last_statement_error.clear();
    if(!stmt) {
      APITools_SetIntValue(context, 1, 0);
      return;
    }

    // a cursor left open by the last run must be closed before the next
    SQLFreeStmt(stmt, SQL_CLOSE);

    SQLRETURN status;
    {
      // Parameters are copies the statement owns (keep_param), so they stay valid
      // while parked.
      APITools_Parked parked(context);
      status = SQLExecute(stmt);
    }

    // [1], when the caller passes it, says whether the statement ran; a library
    // built before it existed passes one argument and is told nothing
    if(SQL_FAIL) {
      set_statement_error(SQL_HANDLE_STMT, stmt);
      APITools_SetIntValue(context, 1, 0);
    }
    else {
      APITools_SetIntValue(context, 1, 1);
    }
  }

  //
  // executes a select statement
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
    void odbc_select_statement(VMContext& context)
  {
    SQLHDBC conn = (SQLHDBC)APITools_GetIntValue(context, 3);
    const std::wstring wsql(APITools_GetStringValue(context, 4));

#ifdef _DEBUG
    std::wcout << L"### select: conn=" << conn << L", stmt=" << wsql << L"  ###" << std::endl;
#endif    

    last_statement_error.clear();
    if(!conn || wsql.size() < 1) {
      APITools_SetIntValue(context, 0, 0);
      return;
    }

    SQLHSTMT stmt = NULL;
    SQLRETURN status = SQLAllocHandle(SQL_HANDLE_STMT, conn, &stmt);
    if(SQL_FAIL) {
      set_statement_error(SQL_HANDLE_DBC, conn);
      // ShowError(SQL_HANDLE_STMT, stmt);
      SQLFreeStmt(stmt, SQL_UNBIND);
      SQLFreeHandle(SQL_HANDLE_STMT, stmt);
      APITools_SetIntValue(context, 0, 0);
      APITools_SetIntValue(context, 1, 0);
      APITools_SetIntValue(context, 2, 0);
      return;
    }

    const std::string sql = UnicodeToBytes(wsql);
    status = SQLPrepare(stmt, (SQLCHAR*)sql.c_str(), SQL_NTS);
    if(SQL_FAIL) {
      set_statement_error(SQL_HANDLE_STMT, stmt);
      SQLFreeStmt(stmt, SQL_UNBIND);
      SQLFreeHandle(SQL_HANDLE_STMT, stmt);
      APITools_SetIntValue(context, 0, 0);
      APITools_SetIntValue(context, 1, 0);
      APITools_SetIntValue(context, 2, 0);
      return;
    }

    SQLSMALLINT columns;
    status = SQLNumResultCols(stmt, &columns);
    if(SQL_FAIL) {
      set_statement_error(SQL_HANDLE_STMT, stmt);
      SQLFreeStmt(stmt, SQL_UNBIND);
      SQLFreeHandle(SQL_HANDLE_STMT, stmt);
      APITools_SetIntValue(context, 0, 0);
      APITools_SetIntValue(context, 1, 0);
      APITools_SetIntValue(context, 2, 0);
      return;
    }

    std::map<const std::wstring, int>* column_names = new std::map<const std::wstring, int>;
    std::map<int, std::pair<void*, SQLLEN> >* exec_data = new std::map<int, std::pair<void*, SQLLEN> >;
    for(SQLSMALLINT i = 1; i <= columns; i++) {
      ColumnDescription description;
      status = SQLDescribeCol(stmt, i, (SQLCHAR*)&description.column_name, COL_NAME_MAX,
                              &description.column_name_size, &description.type,
                              &description.column_size, &description.decimal_length,
                              &description.nullable);
      if(SQL_FAIL) {
        set_statement_error(SQL_HANDLE_STMT, stmt);
        SQLFreeStmt(stmt, SQL_UNBIND);
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        APITools_SetIntValue(context, 0, 0);
        APITools_SetIntValue(context, 1, 0);
        APITools_SetIntValue(context, 2, 0);

        if(exec_data) {
          delete exec_data;
          exec_data = NULL;
        }

        if(column_names) {
          delete column_names;
          column_names = NULL;
        }

        return;
      }

      const std::string column_name((const char*)description.column_name);
      const std::wstring wcolumn_name(column_name.begin(), column_name.end());
      column_names->insert(std::pair<std::wstring, int>(wcolumn_name, i));
#ifdef _DEBUG
      std::wcout << L"  name=" << wcolumn_name << L", type=" << description.type << L", index=" << i << std::endl;
#endif
    }

    // execute query
    {
      APITools_Parked parked(context);
      status = SQLExecute(stmt);
    }
    if(SQL_FAIL) {
      set_statement_error(SQL_HANDLE_STMT, stmt);
      SQLFreeStmt(stmt, SQL_UNBIND);
      SQLFreeHandle(SQL_HANDLE_STMT, stmt);
      APITools_SetIntValue(context, 0, 0);
      APITools_SetIntValue(context, 1, 0);
      APITools_SetIntValue(context, 2, 0);

      if(exec_data) {
        delete exec_data;
        exec_data = NULL;
      }

      if(column_names) {
        delete column_names;
        column_names = NULL;
      }

      return;
    }

    APITools_SetIntValue(context, 0, (size_t)stmt);
    APITools_SetIntValue(context, 1, (size_t)column_names);
    APITools_SetIntValue(context, 2, (size_t)exec_data);
#ifdef _DEBUG
    std::wcout << L"### select OK: stmt=" << stmt << L" ###" << std::endl;
#endif  
  }

  //
  // executes a select statement
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
    void odbc_prepare_statement(VMContext& context)
  {
    SQLHDBC conn = (SQLHDBC)APITools_GetIntValue(context, 3);
    const std::wstring wsql(APITools_GetStringValue(context, 4));

#ifdef _DEBUG
    std::wcout << L"### select: conn=" << conn << L", stmt=" << wsql << L"  ###" << std::endl;
#endif    

    last_statement_error.clear();
    if(!conn || wsql.size() < 1) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetIntValue(context, 1, 0);
      APITools_SetIntValue(context, 2, 0);
      return;
    }

    SQLHSTMT stmt = NULL;
    SQLRETURN status = SQLAllocHandle(SQL_HANDLE_STMT, conn, &stmt);
    if(SQL_FAIL) {
      set_statement_error(SQL_HANDLE_DBC, conn);
      // ShowError(SQL_HANDLE_STMT, stmt);
      SQLFreeStmt(stmt, SQL_UNBIND);
      SQLFreeHandle(SQL_HANDLE_STMT, stmt);
      APITools_SetIntValue(context, 0, 0);
      APITools_SetIntValue(context, 1, 0);
      APITools_SetIntValue(context, 2, 0);
      return;
    }

    const std::string sql = UnicodeToBytes(wsql);
    status = SQLPrepare(stmt, (SQLCHAR*)sql.c_str(), SQL_NTS);
    if(SQL_FAIL) {
      set_statement_error(SQL_HANDLE_STMT, stmt);
      SQLFreeStmt(stmt, SQL_UNBIND);
      SQLFreeHandle(SQL_HANDLE_STMT, stmt);
      APITools_SetIntValue(context, 0, 0);
      APITools_SetIntValue(context, 1, 0);
      APITools_SetIntValue(context, 2, 0);
      return;
    }

    SQLSMALLINT columns;
    status = SQLNumResultCols(stmt, &columns);
    if(SQL_FAIL) {
      set_statement_error(SQL_HANDLE_STMT, stmt);
      SQLFreeStmt(stmt, SQL_UNBIND);
      SQLFreeHandle(SQL_HANDLE_STMT, stmt);
      APITools_SetIntValue(context, 0, 0);
      APITools_SetIntValue(context, 1, 0);
      APITools_SetIntValue(context, 2, 0);
      return;
    }

    // map execution data and get column information
    std::map<int, std::pair<void*, SQLLEN> >* exec_data = new std::map<int, std::pair<void*, SQLLEN> >;
    std::map<const std::wstring, int>* column_names = new std::map<const std::wstring, int>;
    for(SQLSMALLINT i = 1; i <= columns; i++) {
      ColumnDescription description;
      status = SQLDescribeCol(stmt, i, (SQLCHAR*)&description.column_name, COL_NAME_MAX,
                              &description.column_name_size, &description.type,
                              &description.column_size, &description.decimal_length,
                              &description.nullable);
      if(SQL_FAIL) {
        set_statement_error(SQL_HANDLE_STMT, stmt);
        SQLFreeStmt(stmt, SQL_UNBIND);
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        APITools_SetIntValue(context, 0, 0);
        APITools_SetIntValue(context, 1, 0);
        APITools_SetIntValue(context, 2, 0);

        if(exec_data) {
          delete exec_data;
          exec_data = NULL;
        }

        if(column_names) {
          delete column_names;
          column_names = NULL;
        }

        return;
      }

      const std::string column_name((const char*)description.column_name);
      const std::wstring wcolumn_name(column_name.begin(), column_name.end());
      column_names->insert(std::pair<std::wstring, int>(wcolumn_name, i));
#ifdef _DEBUG
      std::wcout << L"  name=" << wcolumn_name << L", type=" << description.type << L", index=" << i << std::endl;
#endif
    }

    // return statement
    APITools_SetIntValue(context, 0, (size_t)stmt);
    APITools_SetIntValue(context, 1, (size_t)column_names);
    APITools_SetIntValue(context, 2, (size_t)exec_data);
  }

  //
  // get result set metadata
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void odbc_result_metadata(VMContext& context)
  {
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 1);
    if(!stmt) {
      SQLFreeStmt(stmt, SQL_UNBIND);
      SQLFreeHandle(SQL_HANDLE_STMT, stmt);
      APITools_SetIntValue(context, 0, 0);
      return;
    }

    SQLSMALLINT columns;
    SQLRETURN status = SQLNumResultCols(stmt, &columns);
    if(SQL_FAIL && columns > 0) {
      // ShowError(SQL_HANDLE_STMT, stmt);
      SQLFreeStmt(stmt, SQL_UNBIND);
      SQLFreeHandle(SQL_HANDLE_STMT, stmt);
      APITools_SetIntValue(context, 0, 0);
      return;
    }

    size_t* metadata_array = APITools_MakeIntArray(context, columns);
    size_t* metadata_array_ptr = metadata_array + 3;

    for(SQLSMALLINT i = 0; i < columns; ++i) {
      char column_name[SMALL_BUFFER_MAX];
      SQLSMALLINT column_name_length;
      SQLSMALLINT column_type;
      SQLULEN column_size;
      SQLSMALLINT column_decimal_digits;
      SQLSMALLINT column_nullable;

      status = SQLDescribeCol(stmt, i + 1, (SQLCHAR*)column_name, SMALL_BUFFER_MAX, &column_name_length,
                              &column_type, &column_size, &column_decimal_digits, &column_nullable);
      if(SQL_FAIL) {
        SQLFreeStmt(stmt, SQL_UNBIND);
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        APITools_SetIntValue(context, 0, 0);
        return;
      }

      size_t* column_obj = APITools_CreateObject(context, L"Database.ODBC.ColumnInfo");
      column_obj[0] = column_type;
      column_obj[1] = (size_t)APITools_CreateStringObject(context, BytesToUnicode(column_name));
      column_obj[2] = column_size;
      column_obj[3] = column_decimal_digits;
      column_obj[4] = column_nullable ? 1 : 0;

      metadata_array_ptr[i] = (size_t)column_obj;
    }

    size_t* resultset_metadata_obj = APITools_CreateObject(context, L"Database.ODBC.ColumnInfoRef");
    resultset_metadata_obj[0] = (size_t)metadata_array;

    APITools_SetObjectValue(context, 0, resultset_metadata_obj);
  }

  //
  // get number of results
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void odbc_result_num_columns(VMContext& context)
  {
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 1);
    if(!stmt) {
      SQLFreeStmt(stmt, SQL_UNBIND);
      SQLFreeHandle(SQL_HANDLE_STMT, stmt);
      APITools_SetIntValue(context, 0, 0);
      return;
    }

    SQLSMALLINT columns;
    SQLRETURN status = SQLNumResultCols(stmt, &columns);
    if(SQL_OK) {
      APITools_SetIntValue(context, 0, columns);
    }
    else {
      SQLFreeStmt(stmt, SQL_UNBIND);
      SQLFreeHandle(SQL_HANDLE_STMT, stmt);
      APITools_SetIntValue(context, 0, -1);
    }
  }

  //
  // fetches the next row in a resultset
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_result_next(VMContext& context) 
  {
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 1);
    if(!stmt) {
      SQLFreeStmt(stmt, SQL_UNBIND);
      SQLFreeHandle(SQL_HANDLE_STMT, stmt);
      APITools_SetIntValue(context, 0, 0);
      return;
    }
		
    SQLRETURN status;
    {
      // a fetch can wait on the data source for the next block of rows
      APITools_Parked parked(context);
      status = SQLFetch(stmt);
    }
    if(SQL_OK) {
      APITools_SetIntValue(context, 0, 1);
    }
    else {
      APITools_SetIntValue(context, 0, 0);
    }
  }

  //
  // updates a prepared statement
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_stmt_update(VMContext& context) 
  {

    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 1);
    if(!stmt) {
      APITools_SetIntValue(context, 0, -1);
      return;
    }

    // The statement, its column names and its execution data belong to the
    // ParameterStatement, which runs it as often as it likes and frees them all in
    // Close. This used to free the two maps after every run, and the statement too
    // on a failure, while the caller kept their addresses: a second run read freed
    // memory, and Close freed the statement twice.
    SQLFreeStmt(stmt, SQL_CLOSE);

    SQLRETURN status;
    {
      APITools_Parked parked(context);
      status = SQLExecute(stmt);
    }

    while(status == SQL_NEED_DATA) {
      PTR param_id = nullptr;
      status = SQLParamData(stmt, &param_id);
      if(status != SQL_NEED_DATA) {
        break;
      }

      // set_bytes binds the position as the token
      BoundParam* param = find_param(stmt, (int)(size_t)param_id);
      if(!param) {
        SQLCancel(stmt);
        APITools_SetIntValue(context, 0, -1);
        return;
      }

      const SQLLEN size = SQL_LEN_DATA_AT_EXEC(0) - param->length;
      status = SQLPutData(stmt, (SQLPOINTER)param->data.data(), size);
      if(SQL_FAIL) {
        SQLCancel(stmt);
        APITools_SetIntValue(context, 0, -1);
        return;
      }
      status = SQL_NEED_DATA;
    }

    if(SQL_FAIL && status != SQL_NO_DATA) {
      APITools_SetIntValue(context, 0, -1);
      return;
    }

    SQLLEN count = 0;
    status = SQLRowCount(stmt, &count);
    if(SQL_FAIL) {
      APITools_SetIntValue(context, 0, -1);
      return;
    }

    APITools_SetIntValue(context, 0, count);
  }

  //
  // set a small int from a prepared statement
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_stmt_set_smallint_null(VMContext& context) 
  {
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 1);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 2);

#ifdef _DEBUG
    std::wcout << L"### set_smallint: stmt=" << stmt << L", column=" << i << L", value=<NULL> ###" << std::endl;
#endif  

    SQLRETURN status = SQLBindParameter(stmt, i, SQL_PARAM_INPUT, SQL_C_SSHORT, 
                                        SQL_SMALLINT, 0, 0, NULL, 0, &sql_null);
    if(SQL_OK) { 
      APITools_SetIntValue(context, 0, 1);
    }
    else {
      APITools_SetIntValue(context, 0, 0);
    }
  }
  
  //
  // set a small int from a prepared statement
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_stmt_set_smallint(VMContext& context) 
  {

    const SQLSMALLINT value = (SQLSMALLINT)APITools_GetIntValue(context, 1);
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    bind_value(context, stmt, i, SQL_C_SSHORT, SQL_SMALLINT, &value, sizeof(value));
  }
	
  //
  // set an int from a prepared statement
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_stmt_set_bit_null(VMContext& context) 
  {
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 1);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 2);

#ifdef _DEBUG
    std::wcout << L"### set_bit: stmt=" << stmt << L", column=" << i << L", value=<NULL> ###" << std::endl;
#endif  

    SQLRETURN status = SQLBindParameter(stmt, i, SQL_PARAM_INPUT, SQL_C_BIT, 
                                        SQL_BIT, 0, 0, NULL, 0, &sql_null);
    if(SQL_OK) { 
      APITools_SetIntValue(context, 0, 1);
    }
    else {
      APITools_SetIntValue(context, 0, 0);
    }
  }

  //
  // set an int from a prepared statement
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_stmt_set_bit(VMContext& context) 
  {

    const unsigned char value = APITools_GetIntValue(context, 1) ? 1 : 0;
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    bind_value(context, stmt, i, SQL_C_BIT, SQL_BIT, &value, sizeof(value));
  }

  //
  // set an int from a prepared statement
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_stmt_set_int_null(VMContext& context) 
  {
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 1);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 2);

#ifdef _DEBUG
    std::wcout << L"### set_int: stmt=" << stmt << L", column=" << i << L", value=<NULL> ###" << std::endl;
#endif  

    SQLRETURN status = SQLBindParameter(stmt, i, SQL_PARAM_INPUT, SQL_C_LONG, 
                                        SQL_INTEGER, 0, 0, NULL, 0, &sql_null);
    if(SQL_OK) { 
      APITools_SetIntValue(context, 0, 1);
    }
    else {
      APITools_SetIntValue(context, 0, 0);
    }
  }

  //
  // set an int from a prepared statement
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_stmt_set_int(VMContext& context) 
  {

    const SQLINTEGER value = (SQLINTEGER)APITools_GetIntValue(context, 1);
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    bind_value(context, stmt, i, SQL_C_LONG, SQL_INTEGER, &value, sizeof(value));
  }

  //
  // set a bigint from a prepared statement to null
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void odbc_stmt_set_bigint_null(VMContext& context)
  {
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 1);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 2);

#ifdef _DEBUG
    std::wcout << L"### set_bigint: stmt=" << stmt << L", column=" << i << L", value=<NULL> ###" << std::endl;
#endif

    SQLRETURN status = SQLBindParameter(stmt, i, SQL_PARAM_INPUT, SQL_C_SBIGINT,
                                        SQL_BIGINT, 0, 0, NULL, 0, &sql_null);
    if(SQL_OK) {
      APITools_SetIntValue(context, 0, 1);
    }
    else {
      APITools_SetIntValue(context, 0, 0);
    }
  }

  //
  // set a bigint from a prepared statement
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void odbc_stmt_set_bigint(VMContext& context)
  {

    const SQLBIGINT value = (SQLBIGINT)APITools_GetIntValue(context, 1);
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    bind_value(context, stmt, i, SQL_C_SBIGINT, SQL_BIGINT, &value, sizeof(value));
  }

  //
  // set an double from a prepared statement to null
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void odbc_stmt_set_double_null(VMContext& context)
  {
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 1);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 2);

#ifdef _DEBUG
    std::wcout << L"### set_double: stmt=" << stmt << L", column=" << i 
          << L", value=<NULL> ###" << std::endl;
#endif  

    SQLRETURN status = SQLBindParameter(stmt, i, SQL_PARAM_INPUT, SQL_C_DOUBLE, 
                                        SQL_DOUBLE, 0, 0, NULL, 0, &sql_null);
    if(SQL_OK) { 
      APITools_SetIntValue(context, 0, 1);
    }
    else {
      APITools_SetIntValue(context, 0, 0);
    }
  }

  //
  // set an double from a prepared statement
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_stmt_set_double(VMContext& context) 
  {

    const SQLDOUBLE value = APITools_GetFloatValue(context, 1);
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    bind_value(context, stmt, i, SQL_C_DOUBLE, SQL_DOUBLE, &value, sizeof(value));
  }

  //
  // set an double from a prepared statement
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_stmt_set_real_null(VMContext& context) 
  {
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 1);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 2);

#ifdef _DEBUG
    std::wcout << L"### set_real: stmt=" << stmt << L", column=" << i 
          << L", value=<NULL> ###" << std::endl;
#endif  

    SQLRETURN status = SQLBindParameter(stmt, i, SQL_PARAM_INPUT, SQL_C_FLOAT,
                                        SQL_REAL, 0, 0, NULL, 0, &sql_null);
    if(SQL_OK) { 
      APITools_SetIntValue(context, 0, 1);
    }
    else {
      APITools_SetIntValue(context, 0, 0);
    }
  }

  //
  // set an double from a prepared statement
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_stmt_set_real(VMContext& context) 
  {

    const SQLREAL value = (SQLREAL)APITools_GetFloatValue(context, 1);
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    bind_value(context, stmt, i, SQL_C_FLOAT, SQL_REAL, &value, sizeof(value));
  }

  //
  // gets an int from a result set
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_result_get_int_by_id(VMContext& context) 
  {
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    std::map<const std::wstring, int>* names = (std::map<const std::wstring, int>*)APITools_GetIntValue(context, 4);

#ifdef _DEBUG
    std::wcout << L"### get_int_by_id: stmt=" << stmt << L", column=" << i 
          << L", max=" << (long)(names->size() + 1) << L" ###" << std::endl;
#endif  

    if(!stmt || !names || i < 1 || i > (long)(names->size() + 1)) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetIntValue(context, 1, 0);
      return;
    }

    SQLLEN is_null;
    int value;
    SQLRETURN status = SQLGetData(stmt, i, SQL_C_LONG, &value, 0, &is_null);
    if(SQL_OK) {
      APITools_SetIntValue(context, 0, is_null == SQL_NULL_DATA);
      APITools_SetIntValue(context, 1, value);
#ifdef _DEBUG
      std::wcout << L"  " << value << std::endl;
#endif
    }
    else {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetIntValue(context, 1, 0);
    }
  }
  
  //
  // gets an int from a result set
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_result_get_int_by_name(VMContext& context) 
  {
    const wchar_t* name = APITools_GetStringValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    std::map<const std::wstring, int>* names = (std::map<const std::wstring, int>*)APITools_GetIntValue(context, 4);

    std::map<const std::wstring, int>::iterator result = names->find(name);
    if(result == names->end()) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetIntValue(context, 1, 0);
      return;
    }
    SQLUSMALLINT i = (SQLUSMALLINT)result->second;
    
#ifdef _DEBUG
    std::wcout << L"### get_int_by_name: stmt=" << stmt << L", column=" << i 
          << L", max=" << (long)(names->size() + 1) << L" ###" << std::endl;
#endif  

    if(!stmt || !names || i < 1 || i > (long)(names->size() + 1)) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetIntValue(context, 1, 0);
      return;
    }

    SQLLEN is_null;
    int value;
    SQLRETURN status = SQLGetData(stmt, i, SQL_C_LONG, &value, 0, &is_null);
    if(SQL_OK) {
      APITools_SetIntValue(context, 0, is_null == SQL_NULL_DATA);
      APITools_SetIntValue(context, 1, value);
#ifdef _DEBUG
      std::wcout << L"  " << value << std::endl;
#endif
    }
    else {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetIntValue(context, 1, 0);
    }
  }
  
  //
  // gets a bigint from a result set
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void odbc_result_get_bigint_by_id(VMContext& context)
  {
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    std::map<const std::wstring, int>* names = (std::map<const std::wstring, int>*)APITools_GetIntValue(context, 4);

#ifdef _DEBUG
    std::wcout << L"### get_bigint_by_id: stmt=" << stmt << L", column=" << i
          << L", max=" << (long)(names->size() + 1) << L" ###" << std::endl;
#endif

    if(!stmt || !names || i < 1 || i > (long)(names->size() + 1)) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetIntValue(context, 1, 0);
      return;
    }

    SQLLEN is_null;
    SQLBIGINT value;
    SQLRETURN status = SQLGetData(stmt, i, SQL_C_SBIGINT, &value, 0, &is_null);
    if(SQL_OK) {
      APITools_SetIntValue(context, 0, is_null == SQL_NULL_DATA);
      APITools_SetIntValue(context, 1, (size_t)value);
#ifdef _DEBUG
      std::wcout << L"  " << value << std::endl;
#endif
    }
    else {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetIntValue(context, 1, 0);
    }
  }

  //
  // gets a bigint from a result set
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void odbc_result_get_bigint_by_name(VMContext& context)
  {
    const wchar_t* name = APITools_GetStringValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    std::map<const std::wstring, int>* names = (std::map<const std::wstring, int>*)APITools_GetIntValue(context, 4);

    std::map<const std::wstring, int>::iterator result = names->find(name);
    if(result == names->end()) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetIntValue(context, 1, 0);
      return;
    }
    SQLUSMALLINT i = (SQLUSMALLINT)result->second;

#ifdef _DEBUG
    std::wcout << L"### get_bigint_by_name: stmt=" << stmt << L", column=" << i
          << L", max=" << (long)(names->size() + 1) << L" ###" << std::endl;
#endif

    if(!stmt || !names || i < 1 || i > (long)(names->size() + 1)) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetIntValue(context, 1, 0);
      return;
    }

    SQLLEN is_null;
    SQLBIGINT value;
    SQLRETURN status = SQLGetData(stmt, i, SQL_C_SBIGINT, &value, 0, &is_null);
    if(SQL_OK) {
      APITools_SetIntValue(context, 0, is_null == SQL_NULL_DATA);
      APITools_SetIntValue(context, 1, (size_t)value);
#ifdef _DEBUG
      std::wcout << L"  " << value << std::endl;
#endif
    }
    else {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetIntValue(context, 1, 0);
    }
  }

  //
  // gets a smallint from a result set
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void odbc_result_get_smallint_by_id(VMContext& context)
  {
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    std::map<const std::wstring, int>* names = (std::map<const std::wstring, int>*)APITools_GetIntValue(context, 4);

#ifdef _DEBUG
    std::wcout << L"### get_smallint_by_id: stmt=" << stmt << L", column=" << i << L", max=" << (long)(names->size() + 1) << L" ###" << std::endl;
#endif  

    if(!stmt || !names || i < 1 || i > (long)(names->size() + 1)) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetIntValue(context, 1, 0);
      return;
    }

    SQLLEN is_null;
    short value;
    SQLRETURN status = SQLGetData(stmt, i, SQL_C_SSHORT, &value, 0, &is_null);
    if(SQL_OK) {
      APITools_SetIntValue(context, 0, is_null == SQL_NULL_DATA);
      APITools_SetIntValue(context, 1, value);
#ifdef _DEBUG
      std::wcout << L"  " << value << std::endl;
#endif
    }
    else {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetIntValue(context, 1, 0);
    }
  }
  
  //
  // gets an int from a result set
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_result_get_smallint_by_name(VMContext& context) 
  {
    const wchar_t* name = APITools_GetStringValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    std::map<const std::wstring, int>* names = (std::map<const std::wstring, int>*)APITools_GetIntValue(context, 4);
    
    std::map<const std::wstring, int>::iterator result = names->find(name);
    if(result == names->end()) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetIntValue(context, 1, 0);
      return;
    }
    SQLUSMALLINT i = (SQLUSMALLINT)result->second;

#ifdef _DEBUG
    std::wcout << L"### get_smallint_by_name: stmt=" << stmt << L", column=" << i << L", max=" << (long)(names->size() + 1) << L" ###" << std::endl;
#endif  

    if(!stmt || !names || i < 1 || i > (long)(names->size() + 1)) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetIntValue(context, 1, 0);
      return;
    }

    SQLLEN is_null;
    short value;
    SQLRETURN status = SQLGetData(stmt, i, SQL_C_SSHORT, &value, 0, &is_null);
    if(SQL_OK) {
      APITools_SetIntValue(context, 0, is_null == SQL_NULL_DATA);
      APITools_SetIntValue(context, 1, value);
#ifdef _DEBUG
      std::wcout << L"  " << value << std::endl;
#endif
    }
    else {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetIntValue(context, 1, 0);
    }
  }
  
  //
  // gets a bit from a result set
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_result_get_bit_by_id(VMContext& context) 
  {
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    std::map<const std::wstring, int>* names = (std::map<const std::wstring, int>*)APITools_GetIntValue(context, 4);

#ifdef _DEBUG
    std::wcout << L"### get_bit_by_id: stmt=" << stmt << L", column=" << i 
          << L", max=" << (long)(names->size() + 1) << L" ###" << std::endl;
#endif  

    if(!stmt || !names || i < 1 || i > (long)(names->size() + 1)) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetIntValue(context, 1, 0);
      return;
    }

    SQLLEN is_null;
    unsigned char value;
    SQLRETURN status = SQLGetData(stmt, i, SQL_C_BIT, &value, 0, &is_null);
    if(SQL_OK) {
      APITools_SetIntValue(context, 0, is_null == SQL_NULL_DATA);
      APITools_SetIntValue(context, 1, value);
#ifdef _DEBUG
      std::wcout << L"  " << value << std::endl;
#endif
    }
    else {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetIntValue(context, 1, 0);
    }
  }

  //
  // gets a bit from a result set
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_result_get_bit_by_name(VMContext& context) 
  {
    const wchar_t* name = APITools_GetStringValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    std::map<const std::wstring, int>* names = (std::map<const std::wstring, int>*)APITools_GetIntValue(context, 4);

    std::map<const std::wstring, int>::iterator result = names->find(name);
    if(result == names->end()) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetIntValue(context, 1, 0);
      return;
    }
    SQLUSMALLINT i = (SQLUSMALLINT)result->second;
    
#ifdef _DEBUG
    std::wcout << L"### get_bit_by_name: stmt=" << stmt << L", column=" << i 
          << L", max=" << (long)(names->size() + 1) << L" ###" << std::endl;
#endif  

    if(!stmt || !names || i < 1 || i > (long)(names->size() + 1)) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetIntValue(context, 1, 0);
      return;
    }

    SQLLEN is_null;
    unsigned char value;
    SQLRETURN status = SQLGetData(stmt, i, SQL_C_BIT, &value, 0, &is_null);
    if(SQL_OK) {
      APITools_SetIntValue(context, 0, is_null == SQL_NULL_DATA);
      APITools_SetIntValue(context, 1, value);
#ifdef _DEBUG
      std::wcout << L"  " << value << std::endl;
#endif
    }
    else {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetIntValue(context, 1, 0);
    }
  }
  
  //
  // gets a double from a result set
  //
#ifdef _WIN32
  __declspec(dllexport)  
#endif
  void odbc_result_get_double_by_id(VMContext& context) 
  {
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    std::map<const std::wstring, int>* names = (std::map<const std::wstring, int>*)APITools_GetIntValue(context, 4);

#ifdef _DEBUG
    std::wcout << L"### get_double_by_id: stmt=" << stmt << L", column=" << i << L", max=" 
          << (long)(names->size() + 1) << L" ###" << std::endl;
#endif  

    if(!stmt || !names || i < 1 || i > (long)(names->size() + 1)) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetFloatValue(context, 1, 0.0);
      return;
    }

    SQLLEN is_null;
    double value;
    SQLRETURN status = SQLGetData(stmt, i, SQL_C_DOUBLE, &value, 0, &is_null);
    if(SQL_OK) {
      APITools_SetIntValue(context, 0, is_null == SQL_NULL_DATA);
      APITools_SetFloatValue(context, 1, value);
#ifdef _DEBUG
      std::wcout << L"  " << value << std::endl;
#endif
    }
    else {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetFloatValue(context, 1, 0.0);
    }
  }

  //
  // gets a double from a result set
  //
#ifdef _WIN32
  __declspec(dllexport)  
#endif
  void odbc_result_get_double_by_name(VMContext& context) 
  {
    const wchar_t* name = APITools_GetStringValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    std::map<const std::wstring, int>* names = (std::map<const std::wstring, int>*)APITools_GetIntValue(context, 4);

    std::map<const std::wstring, int>::iterator result = names->find(name);
    if(result == names->end()) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetFloatValue(context, 1, 0.0);
      return;
    }
    SQLUSMALLINT i = (SQLUSMALLINT)result->second;
    
#ifdef _DEBUG
    std::wcout << L"### get_double_by_name: stmt=" << stmt << L", column=" << i << L", max=" 
          << (long)(names->size() + 1) << L" ###" << std::endl;
#endif  

    if(!stmt || !names || i < 1 || i > (long)(names->size() + 1)) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetFloatValue(context, 1, 0.0);
      return;
    }

    SQLLEN is_null;
    double value;
    SQLRETURN status = SQLGetData(stmt, i, SQL_C_DOUBLE, &value, 0, &is_null);
    if(SQL_OK) {
      APITools_SetIntValue(context, 0, is_null == SQL_NULL_DATA);
      APITools_SetFloatValue(context, 1, value);
#ifdef _DEBUG
      std::wcout << L"  " << value << std::endl;
#endif
    }
    else {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetFloatValue(context, 1, 0.0);
    }
  }
  
  //
  // gets a real from a result set
  //
#ifdef _WIN32
  __declspec(dllexport)  
#endif
  void odbc_result_get_real_by_id(VMContext& context) 
  {
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    std::map<const std::wstring, int>* names = (std::map<const std::wstring, int>*)APITools_GetIntValue(context, 4);

#ifdef _DEBUG
    std::wcout << L"### get_real_by_id: stmt=" << stmt << L", column=" << i << L", max=" 
          << (long)(names->size() + 1) << L" ###" << std::endl;
#endif  

    if(!stmt || !names || i < 1 || i > (long)(names->size() + 1)) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetFloatValue(context, 1, 0.0);
      return;
    }

    SQLLEN is_null;
    float value;
    SQLRETURN status = SQLGetData(stmt, i, SQL_C_FLOAT, &value, 0, &is_null);
    if(SQL_OK) {
      APITools_SetIntValue(context, 0, is_null == SQL_NULL_DATA);
      APITools_SetFloatValue(context, 1, value);
#ifdef _DEBUG
      std::wcout << L"  " << value << std::endl;
#endif
    }
    else {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetFloatValue(context, 1, 0.0);
    }
  }

  //
  // gets a real from a result set
  //
#ifdef _WIN32
  __declspec(dllexport)  
#endif
  void odbc_result_get_real_by_name(VMContext& context) 
  {
    const wchar_t* name = APITools_GetStringValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    std::map<const std::wstring, int>* names = (std::map<const std::wstring, int>*)APITools_GetIntValue(context, 4);
    
    std::map<const std::wstring, int>::iterator result = names->find(name);
    if(result == names->end()) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetFloatValue(context, 1, 0.0);
      return;
    }
    SQLUSMALLINT i = (SQLUSMALLINT)result->second;
    
#ifdef _DEBUG
    std::wcout << L"### get_real_by_name: stmt=" << stmt << L", column=" << i << L", max=" 
          << (long)(names->size() + 1) << L" ###" << std::endl;
#endif  

    if(!stmt || !names || i < 1 || i > (long)(names->size() + 1)) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetFloatValue(context, 1, 0.0);
      return;
    }

    SQLLEN is_null;
    float value;
    SQLRETURN status = SQLGetData(stmt, i, SQL_C_FLOAT, &value, 0, &is_null);
    if(SQL_OK) {
      APITools_SetIntValue(context, 0, is_null == SQL_NULL_DATA);
      APITools_SetFloatValue(context, 1, value);
#ifdef _DEBUG
      std::wcout << L"  " << value << std::endl;
#endif
    }
    else {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetFloatValue(context, 1, 0.0);
    }
  }
  
  //
  // set a std::string for a prepared statement to null
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_stmt_set_varchar_null(VMContext& context) 
  {
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 1);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 2);
		
#ifdef _DEBUG
    std::wcout << L"### set_varchar: stmt=" << stmt << L", column=" << i 
          << L", value=<NULL> ###" << std::endl;
#endif  
		
    SQLRETURN status = SQLBindParameter(stmt, i, SQL_PARAM_INPUT, SQL_C_CHAR, 
                                        SQL_LONGVARCHAR, 0, 0, NULL, 0, &sql_null);
    if(SQL_OK) { 
      APITools_SetIntValue(context, 0, 1);
    }
    else {
      APITools_SetIntValue(context, 0, 0);
    }
  }

  //
  // set a std::string for a prepared statement
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_stmt_set_varchar(VMContext& context) 
  {

    size_t* byte_array = (size_t*)APITools_GetIntValue(context, 1);
    const char* value = (const char*)APITools_GetArray(byte_array);
    const size_t value_size = value ? strlen(value) : 0;
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);

    BoundParam* param = keep_param(stmt, i, value, value_size);
    const SQLRETURN status = SQLBindParameter(stmt, i, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_LONGVARCHAR, value_size > 0 ? value_size : 1, 0,
                                              (SQLPOINTER)param->data.data(), (SQLLEN)value_size, &param->length);
    APITools_SetIntValue(context, 0, SQL_OK ? 1 : 0);
  }

  //
  // set a binary std::string for a prepared statement
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_stmt_set_blob_null(VMContext& context) 
  {
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 1);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 2);
		
#ifdef _DEBUG
    std::wcout << L"### set_blob: stmt=" << stmt << L", column=" << i 
          << L", value=<NULL> ###" << std::endl;
#endif  
    
    SQLRETURN status = SQLBindParameter(stmt, i, SQL_PARAM_INPUT, SQL_C_BINARY, SQL_LONGVARBINARY, 
                                        0, 0, NULL, 0, &sql_null);
    if(SQL_OK) { 
      APITools_SetIntValue(context, 0, 1);
    }
    else {
      APITools_SetIntValue(context, 0, 0);
    }
  }

  //
  // set a binary std::string for a prepared statement
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_stmt_set_blob(VMContext& context) 
  {

    size_t* byte_array = (size_t*)APITools_GetIntValue(context, 1);
    const char* value = (const char*)APITools_GetArray(byte_array);
    const size_t value_size = APITools_GetArraySize(byte_array);
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);

    // the length goes in an indicator: bound without one, ODBC reads binary data as
    // NUL-terminated and stores a blob only up to its first zero byte
    BoundParam* param = keep_param(stmt, i, value, value_size);
    const SQLRETURN status = SQLBindParameter(stmt, i, SQL_PARAM_INPUT, SQL_C_BINARY, SQL_LONGVARBINARY, value_size > 0 ? value_size : 1, 0,
                                              (SQLPOINTER)param->data.data(), (SQLLEN)value_size, &param->length);
    APITools_SetIntValue(context, 0, SQL_OK ? 1 : 0);
  }

  //
  // set a binary std::string for a prepared statement
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_stmt_set_bytes(VMContext& context) 
  {

    size_t* byte_array = (size_t*)APITools_GetIntValue(context, 1);
    const char* value = (const char*)APITools_GetArray(byte_array);
    size_t value_size = (size_t)APITools_GetIntValue(context, 2);
    const size_t array_size = APITools_GetArraySize(byte_array);
    if(value_size > array_size) {
      value_size = array_size;
    }
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 3);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 4);

    BoundParam* param = keep_param(stmt, i, value, value_size);
    param->length = SQL_LEN_DATA_AT_EXEC((SQLLEN)value_size);
    const SQLRETURN status = SQLBindParameter(stmt, i, SQL_PARAM_INPUT, SQL_C_BINARY, SQL_LONGVARBINARY, value_size > 0 ? value_size : 1, 0,
                                              (SQLPOINTER)(size_t)i, 0, &param->length);
    APITools_SetIntValue(context, 0, SQL_OK ? 1 : 0);
  }
  
  //
  // set a timestamp for a prepared statement to null
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_stmt_set_timestamp_null(VMContext& context) 
  {
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 1);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 2);

#ifdef _DEBUG
    std::wcout << L"### set_timestamp: stmt=" << stmt << L", column=" << i << L", value=<NULL> ###" << std::endl;
#endif

    SQLRETURN status = SQLBindParameter(stmt, i, SQL_PARAM_INPUT, SQL_C_TYPE_TIMESTAMP, 
                                        SQL_TYPE_TIMESTAMP, 0, 0, NULL, 0, &sql_null);
    if(SQL_OK) { 
      APITools_SetIntValue(context, 0, 1);
    }
    else {
      APITools_SetIntValue(context, 0, 0);
    }
  }
  
  //
  // set a timestamp for a prepared statement
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_stmt_set_timestamp(VMContext& context) 
  {

    size_t* value = APITools_GetObjectValue(context, 1);
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    if(!value) {
      APITools_SetIntValue(context, 0, 0);
      return;
    }

    SQL_TIMESTAMP_STRUCT time_stamp;
    time_stamp.year = (SQLSMALLINT)value[1];
    time_stamp.month = (SQLUSMALLINT)value[2];
    time_stamp.day = (SQLUSMALLINT)value[3];
    time_stamp.hour = (SQLUSMALLINT)value[4];
    time_stamp.minute = (SQLUSMALLINT)value[5];
    time_stamp.second = (SQLUSMALLINT)value[6];
    time_stamp.fraction = (SQLUINTEGER)value[7];
    bind_value(context, stmt, i, SQL_C_TYPE_TIMESTAMP, SQL_TYPE_TIMESTAMP, &time_stamp, sizeof(time_stamp));
  }

  //
  // set a date for a prepared statement to null
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_stmt_set_date_null(VMContext& context) 
  {
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 1);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 2);

#ifdef _DEBUG
    std::wcout << L"### set_date: stmt=" << stmt << L", column=" << i 
          << L", value=<NULL> ###" << std::endl;
#endif
    
    SQLRETURN status = SQLBindParameter(stmt, i, SQL_PARAM_INPUT, SQL_C_TYPE_DATE, 
                                        SQL_TYPE_DATE, 0, 0, NULL, 0, &sql_null);
    if(SQL_OK) { 
      APITools_SetIntValue(context, 0, 1);
    }
    else {
      APITools_SetIntValue(context, 0, 0);
    }
  }
  
  //
  // set a date for a prepared statement
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_stmt_set_date(VMContext& context) 
  {

    size_t* value = APITools_GetObjectValue(context, 1);
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    if(!value) {
      APITools_SetIntValue(context, 0, 0);
      return;
    }

    SQL_DATE_STRUCT date;
    date.year = (SQLSMALLINT)value[1];
    date.month = (SQLUSMALLINT)value[2];
    date.day = (SQLUSMALLINT)value[3];
    bind_value(context, stmt, i, SQL_C_TYPE_DATE, SQL_TYPE_DATE, &date, sizeof(date));
  }

  //
  // gets a std::string from a result set
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_result_get_varchar_by_id(VMContext& context) 
  {
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    std::map<const std::wstring, int>* names = (std::map<const std::wstring, int>*)APITools_GetIntValue(context, 4);

#ifdef _DEBUG
    std::wcout << L"### get_varchar_by_id: stmt=" << stmt << L", column=" << i 
          << L", max=" << (long)(names->size() + 1) << L" ###" << std::endl;
#endif  

    if(!stmt || !names || i < 1 || i > (long)(names->size() + 1)) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetObjectValue(context, 1, 0);
      return;
    }

    set_text_result(context, stmt, i);
  }
	
  //
  // gets a blob from a result set
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_result_get_blob_by_id(VMContext& context) 
  {
    size_t* byte_array = (size_t*)APITools_GetIntValue(context, 1);
    char* buffer = (char*)APITools_GetArray(byte_array);
    const size_t buffer_size = APITools_GetArraySize(byte_array);
		
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    std::map<const std::wstring, int>* names = (std::map<const std::wstring, int>*)APITools_GetIntValue(context, 4);

#ifdef _DEBUG
    std::wcout << L"### get_blob_by_id: stmt=" << stmt << L", column=" << i 
          << L", max=" << buffer_size << L" ###" << std::endl;
#endif  

    if(!stmt || !names || i < 1 || i > names->size() + 1) {
      APITools_SetIntValue(context, 0, 0);
      return;
    }

    SQLLEN is_null;
    SQLRETURN status = SQLGetData(stmt, i, SQL_C_BINARY, buffer, buffer_size, &is_null);
    if(SQL_OK) {
      APITools_SetIntValue(context, 0, is_null == SQL_NULL_DATA);
    }
    else {
      APITools_SetIntValue(context, 0, 0);
    }
  }

  //
  // gets a blob from a result set
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_result_get_blob_by_name(VMContext& context) 
  {

    size_t* byte_array = (size_t*)APITools_GetIntValue(context, 1);
    char* buffer = (char*)APITools_GetArray(byte_array);
    const size_t buffer_size = APITools_GetArraySize(byte_array);

    const wchar_t* name = APITools_GetStringValue(context, 2);		
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    std::map<const std::wstring, int>* names = (std::map<const std::wstring, int>*)APITools_GetIntValue(context, 4);
    
    std::map<const std::wstring, int>::iterator result = names->find(name);
    if(result == names->end()) {
      APITools_SetIntValue(context, 0, 0);
      return;
    }
    SQLUSMALLINT i = (SQLUSMALLINT)result->second;
    
#ifdef _DEBUG
    std::wcout << L"### get_blob_by_name: stmt=" << stmt << L", column=" << i 
          << L", max=" << buffer_size << L" ###" << std::endl;
#endif  

    if(!stmt || !names || i < 1 || i > (long)(names->size() + 1)) {
      APITools_SetIntValue(context, 0, 0);
      return;
    }

    SQLLEN is_null;
    SQLRETURN status = SQLGetData(stmt, i, SQL_C_BINARY, buffer, buffer_size, &is_null);
    if(SQL_OK) {
      APITools_SetIntValue(context, 0, is_null == SQL_NULL_DATA);
    }
    else {
      APITools_SetIntValue(context, 0, 0);
    }
  }

  //
  // gets a whole binary value from a result set as a new Byte[], however long:
  // [0] the null flag, [1] a ByteArrayRef that receives the bytes (Nil for NULL
  // or an error), [2] the column, [3] the statement, [4] the column names
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void odbc_result_get_bytes_by_id(VMContext& context)
  {
    size_t* holder = APITools_GetObjectValue(context, 1);
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    std::map<const std::wstring, int>* names = (std::map<const std::wstring, int>*)APITools_GetIntValue(context, 4);

    APITools_SetIntValue(context, 0, 0);
    if(!holder || !stmt || !names || i < 1 || i > (long)(names->size() + 1)) {
      return;
    }
    holder[0] = 0;

    std::vector<unsigned char> value;
    bool is_null = false;
    if(!read_binary_cell(stmt, i, value, is_null)) {
      return;
    }
    if(is_null) {
      APITools_SetIntValue(context, 0, 1);
      return;
    }

    size_t* byte_array = APITools_MakeByteArray(context, value.size());
    if(!value.empty()) {
      memcpy(reinterpret_cast<unsigned char*>(byte_array + 3), value.data(), value.size());
    }
    holder[0] = (size_t)byte_array;
  }
  
  //
  // gets a std::string from a result set
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_result_get_varchar_by_name(VMContext& context) 
  {
    const wchar_t* name = APITools_GetStringValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    std::map<const std::wstring, int>* names = (std::map<const std::wstring, int>*)APITools_GetIntValue(context, 4);

    std::map<const std::wstring, int>::iterator result = names->find(name);
    if(result == names->end()) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetObjectValue(context, 1, 0);
      return;
    }
    SQLUSMALLINT i = (SQLUSMALLINT)result->second;

#ifdef _DEBUG
    std::wcout << L"### get_varchar_by_name: stmt=" << stmt << L", column=" << i 
          << L", max=" << (long)(names->size() + 1) << L" ###" << std::endl;
#endif  

    if(!stmt || !names || i < 1 || i > (long)(names->size() + 1)) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetObjectValue(context, 1, 0);
      return;
    }

    set_text_result(context, stmt, i);
  }

  //
  // gets a timestamp from a result set
  //  
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_result_get_timestamp_by_id(VMContext& context) 
  {
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    std::map<const std::wstring, int>* names = (std::map<const std::wstring, int>*)APITools_GetIntValue(context, 4);

#ifdef _DEBUG
    std::wcout << L"### get_timestamp_by_id: stmt=" << stmt << L", column=" 
          << i << L", max=" << (long)(names->size() + 1) << L" ###" << std::endl;
#endif  

    if(!stmt || !names || i < 1 || i > (long)(names->size() + 1)) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetObjectValue(context, 1, 0);
      return;
    }

    SQLLEN is_null;
    TIMESTAMP_STRUCT value;
    SQLRETURN status = SQLGetData(stmt, i, SQL_C_TYPE_TIMESTAMP, &value, 
                                  sizeof(TIMESTAMP_STRUCT), &is_null);
    if(SQL_OK) {
      size_t* ts_obj = APITools_CreateObject(context, L"Database.ODBC.Timestamp");
      ts_obj[1] = value.year;
      ts_obj[2] = value.month;
      ts_obj[3] = value.day;
      ts_obj[4] = value.hour;
      ts_obj[5] = value.minute;
      ts_obj[6] = value.second;
      ts_obj[7] = value.fraction;

#ifdef _DEBUG
      std::wcout << L"  " << value.year << std::endl;
      std::wcout << L"  " << value.month << std::endl;
      std::wcout << L"  " << value.day << std::endl;
      std::wcout << L"  " << value.hour << std::endl;
      std::wcout << L"  " << value.minute << std::endl;
      std::wcout << L"  " << value.second << std::endl;
      std::wcout << L"  " << value.fraction << std::endl;
      std::wcout << L"  is_null=" << (is_null == SQL_NULL_DATA) << std::endl;
#endif
      // set values
      APITools_SetIntValue(context, 0, (is_null == SQL_NULL_DATA) ? 1 : 0);
      APITools_SetObjectValue(context, 1, ts_obj);
    }
    else {
      APITools_SetObjectValue(context, 0, 0);
      APITools_SetObjectValue(context, 1, 0);
    }
  }
  
  //
  // gets a timestamp from a result set
  //  
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_result_get_timestamp_by_name(VMContext& context) 
  {
    const wchar_t* name = APITools_GetStringValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    std::map<const std::wstring, int>* names = (std::map<const std::wstring, int>*)APITools_GetIntValue(context, 4);
    
    std::map<const std::wstring, int>::iterator result = names->find(name);
    if(result == names->end()) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetObjectValue(context, 1, 0);
      return;
    }
    SQLUSMALLINT i = (SQLUSMALLINT)result->second;
    
#ifdef _DEBUG
    std::wcout << L"### get_timestamp_by_name: stmt=" << stmt << L", column=" 
      << i << L", max=" << (long)(names->size() + 1) << L" ###" << std::endl;
#endif  

    if(!stmt || !names || i < 1 || i > (long)(names->size() + 1)) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetObjectValue(context, 1, 0);
      return;
    }

    SQLLEN is_null;
    TIMESTAMP_STRUCT value;
    SQLRETURN status = SQLGetData(stmt, i, SQL_C_TYPE_TIMESTAMP, &value, 
                                  sizeof(TIMESTAMP_STRUCT), &is_null);
    if(SQL_OK) {
      size_t* ts_obj = APITools_CreateObject(context, L"Database.ODBC.Timestamp");
      ts_obj[1] = value.year;
      ts_obj[2] = value.month;
      ts_obj[3] = value.day;
      ts_obj[4] = value.hour;
      ts_obj[5] = value.minute;
      ts_obj[6] = value.second;
      ts_obj[7] = value.fraction;

#ifdef _DEBUG
      std::wcout << L"  " << value.year << std::endl;
      std::wcout << L"  " << value.month << std::endl;
      std::wcout << L"  " << value.day << std::endl;
      std::wcout << L"  " << value.hour << std::endl;
      std::wcout << L"  " << value.minute << std::endl;
      std::wcout << L"  " << value.second << std::endl;
      std::wcout << L"  " << value.fraction << std::endl;
      std::wcout << L"  is_null=" << (is_null == SQL_NULL_DATA) << std::endl;
#endif
      // set values
      APITools_SetIntValue(context, 0, (is_null == SQL_NULL_DATA) ? 1 : 0);
      APITools_SetObjectValue(context, 1, ts_obj);
    }
    else {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetObjectValue(context, 1, 0);
    }
  }
  
  //
  // gets a date from a result set
  //  
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_result_get_date_by_id(VMContext& context) 
  {
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    std::map<const std::wstring, int>* names = (std::map<const std::wstring, int>*)APITools_GetIntValue(context, 4);

#ifdef _DEBUG
    std::wcout << L"### get_date_by_id: stmt=" << stmt << L", column=" << i 
          << L", max=" << (long)(names->size() + 1) << L" ###" << std::endl;
#endif  

    if(!stmt || !names || i < 1 || i > (long)(names->size() + 1)) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetObjectValue(context, 1, 0);
      return;
    }

    SQLLEN is_null;
    DATE_STRUCT value;
    SQLRETURN status = SQLGetData(stmt, i, SQL_C_TYPE_DATE, &value, sizeof(DATE_STRUCT), &is_null);
    if(SQL_OK) {
      size_t* ts_obj = APITools_CreateObject(context, L"Database.ODBC.Date");
      ts_obj[1] = value.year;
      ts_obj[2] = value.month;
      ts_obj[3] = value.day;

#ifdef _DEBUG
      std::wcout << L"  " << value.year << std::endl;
      std::wcout << L"  " << value.month << std::endl;
      std::wcout << L"  " << value.day << std::endl;
#endif

      // set values
      APITools_SetIntValue(context, 0, (is_null == SQL_NULL_DATA) ? 1 : 0);
      APITools_SetObjectValue(context, 1, ts_obj);
    }
    else {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetObjectValue(context, 1, 0);
    }
  }

  //
  // gets a date from a result set
  //  
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_result_get_date_by_name(VMContext& context) 
  {
    const wchar_t* name = APITools_GetStringValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    std::map<const std::wstring, int>* names = (std::map<const std::wstring, int>*)APITools_GetIntValue(context, 4);

    std::map<const std::wstring, int>::iterator result = names->find(name);
    if(result == names->end()) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetObjectValue(context, 1, 0);
      return;
    }
    SQLUSMALLINT i = (SQLUSMALLINT)result->second;
    
#ifdef _DEBUG
    std::wcout << L"### get_date_by_name: stmt=" << stmt << L", column=" << i 
          << L", max=" << (long)(names->size() + 1) << L" ###" << std::endl;
#endif  

    if(!stmt || !names || i < 1 || i > (long)(names->size() + 1)) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetObjectValue(context, 1, 0);
      return;
    }

    SQLLEN is_null;
    DATE_STRUCT value;
    SQLRETURN status = SQLGetData(stmt, i, SQL_C_TYPE_DATE, &value, sizeof(DATE_STRUCT), &is_null);
    if(SQL_OK) {
      size_t* ts_obj = APITools_CreateObject(context, L"Database.ODBC.Date");
      ts_obj[1] = value.year;
      ts_obj[2] = value.month;
      ts_obj[3] = value.day;

#ifdef _DEBUG
      std::wcout << L"  " << value.year << std::endl;
      std::wcout << L"  " << value.month << std::endl;
      std::wcout << L"  " << value.day << std::endl;
#endif

      // set values
      APITools_SetIntValue(context, 0, (is_null == SQL_NULL_DATA) ? 1 : 0);
      APITools_SetObjectValue(context, 1, ts_obj);
    }
    else {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetObjectValue(context, 1, 0);
    }
  }


  //
  // sets autocommit mode on a connection
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void odbc_set_autocommit(VMContext& context)
  {
    SQLHDBC conn = (SQLHDBC)APITools_GetIntValue(context, 0);
    size_t enable = APITools_GetIntValue(context, 1);

#ifdef _DEBUG
    std::wcout << L"### set_autocommit: conn=" << conn << L", enable=" << enable << L" ###" << std::endl;
#endif

    if(!conn) {
      APITools_SetIntValue(context, 0, 0);
      return;
    }

    SQLRETURN status = SQLSetConnectAttr(conn, SQL_ATTR_AUTOCOMMIT,
                                          enable ? (SQLPOINTER)SQL_AUTOCOMMIT_ON : (SQLPOINTER)SQL_AUTOCOMMIT_OFF, 0);
    if(SQL_OK) {
      APITools_SetIntValue(context, 0, 1);
    }
    else {
      APITools_SetIntValue(context, 0, 0);
    }
  }

  //
  // commits the current transaction
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void odbc_commit(VMContext& context)
  {
    SQLHDBC conn = (SQLHDBC)APITools_GetIntValue(context, 0);

#ifdef _DEBUG
    std::wcout << L"### commit: conn=" << conn << L" ###" << std::endl;
#endif

    if(!conn) {
      APITools_SetIntValue(context, 0, 0);
      return;
    }

    SQLRETURN status = SQLEndTran(SQL_HANDLE_DBC, conn, SQL_COMMIT);
    if(SQL_OK) {
      APITools_SetIntValue(context, 0, 1);
    }
    else {
      APITools_SetIntValue(context, 0, 0);
    }
  }

  //
  // rolls back the current transaction
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void odbc_rollback(VMContext& context)
  {
    SQLHDBC conn = (SQLHDBC)APITools_GetIntValue(context, 0);

#ifdef _DEBUG
    std::wcout << L"### rollback: conn=" << conn << L" ###" << std::endl;
#endif

    if(!conn) {
      APITools_SetIntValue(context, 0, 0);
      return;
    }

    SQLRETURN status = SQLEndTran(SQL_HANDLE_DBC, conn, SQL_ROLLBACK);
    if(SQL_OK) {
      APITools_SetIntValue(context, 0, 1);
    }
    else {
      APITools_SetIntValue(context, 0, 0);
    }
  }

  //
  // gets the last ODBC error for a given handle
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void odbc_get_error(VMContext& context)
  {
    SQLSMALLINT handle_type_id = (SQLSMALLINT)APITools_GetIntValue(context, 1);
    SQLHANDLE handle = (SQLHANDLE)APITools_GetIntValue(context, 2);

#ifdef _DEBUG
    std::wcout << L"### get_error: type=" << handle_type_id << L", handle=" << handle << L" ###" << std::endl;
#endif

    SQLSMALLINT sql_handle_type;
    switch(handle_type_id) {
      case 1:
        sql_handle_type = SQL_HANDLE_ENV;
        break;
      case 2:
        sql_handle_type = SQL_HANDLE_DBC;
        break;
      case 3:
        sql_handle_type = SQL_HANDLE_STMT;
        break;
      default:
        sql_handle_type = SQL_HANDLE_DBC;
        break;
    }

    if(!handle) {
      // A connection that never opened, or a statement that failed, has no
      // handle: report why.
      if(handle_type_id == 2) {
        APITools_SetStringValue(context, 0, BytesToUnicode(last_connect_error));
      }
      else if(handle_type_id == 3) {
        // a select or prepare that failed has no statement left: report why
        APITools_SetStringValue(context, 0, BytesToUnicode(last_statement_error));
      }
      else {
        APITools_SetStringValue(context, 0, std::wstring());
      }
      return;
    }

    SQLCHAR sql_state[6];
    SQLINTEGER native_error;
    SQLCHAR message[SQL_MAX_MESSAGE_LENGTH];
    SQLSMALLINT text_len;

    SQLRETURN result = SQLGetDiagRec(sql_handle_type, handle, 1, sql_state, &native_error,
                                      message, sizeof(message), &text_len);
    if(result == SQL_SUCCESS || result == SQL_SUCCESS_WITH_INFO) {
      char error_msg[SQL_MAX_MESSAGE_LENGTH + 64];
      snprintf(error_msg, sizeof(error_msg), "[%s] %s (%d)", (char*)sql_state, (char*)message, (int)native_error);
      const std::wstring werror = BytesToUnicode(error_msg);
      APITools_SetStringValue(context, 0, werror);
    }
    else {
      APITools_SetStringValue(context, 0, L"");
    }
  }

  //
  // gets table names from a connection
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void odbc_get_tables(VMContext& context)
  {
    SQLHDBC conn = (SQLHDBC)APITools_GetIntValue(context, 1);

#ifdef _DEBUG
    std::wcout << L"### get_tables: conn=" << conn << L" ###" << std::endl;
#endif

    if(!conn) {
      APITools_SetStringValue(context, 0, L"");
      return;
    }

    SQLHSTMT stmt = NULL;
    SQLRETURN status = SQLAllocHandle(SQL_HANDLE_STMT, conn, &stmt);
    if(SQL_FAIL) {
      if(stmt) {
        SQLFreeStmt(stmt, SQL_UNBIND);
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
      }
      APITools_SetStringValue(context, 0, L"");
      return;
    }

    {
      APITools_Parked parked(context);
      status = SQLTables(stmt, NULL, 0, NULL, 0, NULL, 0, (SQLCHAR*)"TABLE", SQL_NTS);
    }
    if(SQL_FAIL) {
      SQLFreeStmt(stmt, SQL_UNBIND);
      SQLFreeHandle(SQL_HANDLE_STMT, stmt);
      APITools_SetStringValue(context, 0, L"");
      return;
    }

    std::wstring result_str;
    char table_name[VARCHAR_MAX];
    SQLLEN indicator;
    bool first = true;

    while(SQLFetch(stmt) == SQL_SUCCESS) {
      status = SQLGetData(stmt, 3, SQL_C_CHAR, table_name, sizeof(table_name), &indicator);
      if(status == SQL_SUCCESS || status == SQL_SUCCESS_WITH_INFO) {
        if(indicator != SQL_NULL_DATA) {
          if(!first) {
            result_str += L",";
          }
          result_str += BytesToUnicode(table_name);
          first = false;
        }
      }
    }

    SQLFreeStmt(stmt, SQL_UNBIND);
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    APITools_SetStringValue(context, 0, result_str);
  }

  //
  // gets column info for a table
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void odbc_get_columns(VMContext& context)
  {
    SQLHDBC conn = (SQLHDBC)APITools_GetIntValue(context, 1);
    const std::wstring wtable(APITools_GetStringValue(context, 2));

#ifdef _DEBUG
    std::wcout << L"### get_columns: conn=" << conn << L", table=" << wtable << L" ###" << std::endl;
#endif

    if(!conn || wtable.size() < 1) {
      APITools_SetStringValue(context, 0, L"");
      return;
    }

    const std::string table_name = UnicodeToBytes(wtable);

    SQLHSTMT stmt = NULL;
    SQLRETURN status = SQLAllocHandle(SQL_HANDLE_STMT, conn, &stmt);
    if(SQL_FAIL) {
      if(stmt) {
        SQLFreeStmt(stmt, SQL_UNBIND);
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
      }
      APITools_SetStringValue(context, 0, L"");
      return;
    }

    {
      APITools_Parked parked(context);
      status = SQLColumns(stmt, NULL, 0, NULL, 0, (SQLCHAR*)table_name.c_str(), SQL_NTS, NULL, 0);
    }
    if(SQL_FAIL) {
      SQLFreeStmt(stmt, SQL_UNBIND);
      SQLFreeHandle(SQL_HANDLE_STMT, stmt);
      APITools_SetStringValue(context, 0, L"");
      return;
    }

    std::wstring result_str;
    char col_name[VARCHAR_MAX];
    char col_type[VARCHAR_MAX];
    SQLLEN name_indicator;
    SQLLEN type_indicator;
    bool first = true;

    while(SQLFetch(stmt) == SQL_SUCCESS) {
      SQLRETURN name_status = SQLGetData(stmt, 4, SQL_C_CHAR, col_name, sizeof(col_name), &name_indicator);
      SQLRETURN type_status = SQLGetData(stmt, 6, SQL_C_CHAR, col_type, sizeof(col_type), &type_indicator);

      if((name_status == SQL_SUCCESS || name_status == SQL_SUCCESS_WITH_INFO) &&
         (type_status == SQL_SUCCESS || type_status == SQL_SUCCESS_WITH_INFO)) {
        if(name_indicator != SQL_NULL_DATA) {
          if(!first) {
            result_str += L",";
          }
          result_str += BytesToUnicode(col_name);
          result_str += L":";
          if(type_indicator != SQL_NULL_DATA) {
            result_str += BytesToUnicode(col_type);
          }
          else {
            result_str += L"UNKNOWN";
          }
          first = false;
        }
      }
    }

    SQLFreeStmt(stmt, SQL_UNBIND);
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    APITools_SetStringValue(context, 0, result_str);
  }

  //
  // closes a result set
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void odbc_result_close(VMContext& context)
  {
    std::map<const std::wstring, int>* column_names = (std::map<const std::wstring, int>*)APITools_GetIntValue(context, 0);
    if(column_names) {
      delete column_names;
      column_names = NULL;
    }

    std::map<int, std::pair<void*, SQLLEN> >* exec_data = (std::map<int, std::pair<void*, SQLLEN> >*)APITools_GetIntValue(context, 1);
    if(exec_data) {
      delete exec_data;
      exec_data = NULL;
    }

    SQLHSTMT stmt = (SQLHSTMT)APITools_GetIntValue(context, 2);
    if(stmt) {
      drop_params(stmt);
      SQLFreeStmt(stmt, SQL_UNBIND);
      SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    }

#ifdef _DEBUG
    std::wcout << L"### closed result set ###" << std::endl;
#endif
  }

  //
  // closes the cursor of a prepared statement's result set, leaving the statement,
  // which its ParameterStatement owns, ready to run again
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void odbc_result_close_cursor(VMContext& context)
  {
    SQLHSTMT stmt = (SQLHSTMT)APITools_GetIntValue(context, 0);
    if(stmt) {
      SQLFreeStmt(stmt, SQL_CLOSE);
    }
  }
	
  //
  // closes prepared statement
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_stmt_close(VMContext& context) 
  {

    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 0);
    if(stmt) {
      drop_params(stmt);
      SQLFreeStmt(stmt, SQL_UNBIND);
      SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    }

    // [1] and [2], when the caller passes them, are the statement's column names and
    // execution data, which it owns; a library built before they were passed leaks
    // them rather than freeing what it may still use
    if(context.data_array && context.data_array[0] > 2) {
      std::map<const std::wstring, int>* column_names = (std::map<const std::wstring, int>*)APITools_GetIntValue(context, 1);
      if(column_names) {
        delete column_names;
      }
      std::map<int, std::pair<void*, SQLLEN> >* exec_data = (std::map<int, std::pair<void*, SQLLEN> >*)APITools_GetIntValue(context, 2);
      if(exec_data) {
        delete exec_data;
      }
    }
  }
}
