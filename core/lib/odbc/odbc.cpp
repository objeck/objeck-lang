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

extern "C" {
  SQLLEN sql_null;
  
  //
  // initialize odbc environment
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void load_lib() 
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
    wcout << L"### connect: " << L"ds=" << wds << L", username=" 
          << wusername << L", password=" << wpassword << L" ###" << std::endl;
#endif
		
    const std::string ds = UnicodeToBytes(wds);
    const std::string username = UnicodeToBytes(wusername);
    const std::string password = UnicodeToBytes(wpassword);
		
    SQLRETURN status = SQLAllocHandle(SQL_HANDLE_DBC, env, &conn);
    if(SQL_FAIL) {
      SQLFreeHandle(SQL_HANDLE_DBC, conn);
      APITools_SetIntValue(context, 0, 0);
      return;
    }
		
    status = SQLConnect(conn, (SQLCHAR*)ds.c_str(), SQL_NTS, (SQLCHAR*)username.c_str(), 
                        SQL_NTS, (SQLCHAR*)password.c_str(), SQL_NTS);     
    if(SQL_FAIL) {
      SQLFreeHandle(SQL_HANDLE_DBC, conn);
      conn = NULL;
      APITools_SetIntValue(context, 0, 0);
      return;
    }
		
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
      wcout << L"### disconnect ###" << std::endl;
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
    wcout << L"### update: conn=" << conn << L", stmt=" << wsql << L"  ###" << std::endl;
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
    status = SQLExecDirect(stmt, (SQLCHAR*)sql.c_str(), SQL_NTS);
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

#ifdef _DEBUG
    wcout << L"### stmt_select_update: stmt=" << stmt << L" ###" << std::endl;
#endif
    if(stmt) {
      SQLExecute(stmt);
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
    wcout << L"### select: conn=" << conn << L", stmt=" << wsql << L"  ###" << std::endl;
#endif    

    if(!conn || wsql.size() < 1) {
      APITools_SetIntValue(context, 0, 0);
      return;
    }
		
    SQLHSTMT stmt = NULL;
    SQLRETURN status = SQLAllocHandle(SQL_HANDLE_STMT, conn, &stmt);
    if(SQL_FAIL) {
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
      // ShowError(SQL_HANDLE_STMT, stmt);
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
      // ShowError(SQL_HANDLE_STMT, stmt);
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
        // ShowError(SQL_HANDLE_STMT, stmt);
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
      wcout << L"  name=" << wcolumn_name << L", type=" << description.type << L", index=" << i << std::endl;
#endif
    }
		
    // execute query
    status = SQLExecute(stmt); 
    if(SQL_FAIL) {
      // ShowError(SQL_HANDLE_STMT, stmt);
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
    wcout << L"### select OK: stmt=" << stmt << L" ###" << std::endl;
#endif  
  }		
	
  //
  // executes a select statement
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_pepare_statement(VMContext& context) 
  {
    SQLHDBC conn = (SQLHDBC)APITools_GetIntValue(context, 3);
    const std::wstring wsql(APITools_GetStringValue(context, 4));
		
#ifdef _DEBUG
    wcout << L"### select: conn=" << conn << L", stmt=" << wsql << L"  ###" << std::endl;
#endif    
		
    if(!conn || wsql.size() < 1) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetIntValue(context, 1, 0);
      APITools_SetIntValue(context, 2, 0);
      return;
    }
		
    SQLHSTMT stmt = NULL;
    SQLRETURN status = SQLAllocHandle(SQL_HANDLE_STMT, conn, &stmt);
    if(SQL_FAIL) {
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
      // ShowError(SQL_HANDLE_STMT, stmt);
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
      // ShowError(SQL_HANDLE_STMT, stmt);
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
        // ShowError(SQL_HANDLE_STMT, stmt);
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
      wcout << L"  name=" << wcolumn_name << L", type=" << description.type << L", index=" << i << std::endl;
#endif
    }
		
    // return statement
    APITools_SetIntValue(context, 0, (size_t)stmt);
    APITools_SetIntValue(context, 1, (size_t)column_names);
    APITools_SetIntValue(context, 2, (size_t)exec_data);
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
      APITools_SetIntValue(context, 0, 0);
      return;
    }
		
    SQLRETURN status = SQLFetch(stmt);
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
    std::map<size_t, std::pair<void*, SQLLEN> >* exec_data = (std::map<size_t, std::pair<void*, SQLLEN> >*)APITools_GetIntValue(context, 2);
    std::map<const std::wstring, int>* column_names = (std::map<const std::wstring, int>*)APITools_GetIntValue(context, 3);
		
#ifdef _DEBUG
    wcout << L"### stmt_update: stmt=" << stmt << L" ###" << std::endl;
#endif

    SQLRETURN status = SQLExecute(stmt);
    PTR param_id;
    
    if(status == SQL_NEED_DATA) {
      status = SQLParamData(stmt, &param_id);
      do {
        std::map<size_t, std::pair<void*, SQLLEN> >::iterator found = exec_data->find((size_t)param_id);
        if(found != exec_data->end()) {
          status = SQLPutData(stmt, found->second.first, found->second.second);
          if(SQL_FAIL) {
            // ShowError(SQL_HANDLE_STMT, stmt);
            SQLFreeStmt(stmt, SQL_UNBIND);
            SQLFreeHandle(SQL_HANDLE_STMT, stmt);
            APITools_SetIntValue(context, 0, -1);
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
        }
        else {
          // ShowError(SQL_HANDLE_STMT, stmt);
          SQLFreeStmt(stmt, SQL_UNBIND);
          SQLFreeHandle(SQL_HANDLE_STMT, stmt);
          APITools_SetIntValue(context, 0, -1);
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
        status = SQLParamData(stmt, &param_id);
      } 
      while(status == SQL_NEED_DATA);
    }
    
    if(SQL_FAIL) {
      // ShowError(SQL_HANDLE_STMT, stmt);
      SQLFreeStmt(stmt, SQL_UNBIND);
      SQLFreeHandle(SQL_HANDLE_STMT, stmt);
      APITools_SetIntValue(context, 0, -1);
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
		
    SQLLEN count;
    status = SQLRowCount(stmt, &count);
    if(SQL_FAIL) {
      // ShowError(SQL_HANDLE_STMT, stmt);
      SQLFreeStmt(stmt, SQL_UNBIND);
      SQLFreeHandle(SQL_HANDLE_STMT, stmt);
      APITools_SetIntValue(context, 0, -1);
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
		
    APITools_SetIntValue(context, 0, count);
    if(exec_data) {
      delete exec_data;
      exec_data = NULL;
    }
    if(column_names) {
      delete column_names;
      column_names = NULL;
    }
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
    wcout << L"### set_smallint: stmt=" << stmt << L", column=" << i << L", value=<NULL> ###" << std::endl;
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
    size_t* value = APITools_GetIntAddress(context, 1);
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);

#ifdef _DEBUG
    wcout << L"### set_smallint: stmt=" << stmt << L", column=" << i 
          << L", value=" << *value << L" ###" << std::endl;
#endif  

    SQLRETURN status = SQLBindParameter(stmt, i, SQL_PARAM_INPUT, SQL_C_SSHORT, 
                                        SQL_SMALLINT, 0, 0, value, 0, NULL);
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
  void odbc_stmt_set_bit_null(VMContext& context) 
  {
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 1);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 2);

#ifdef _DEBUG
    wcout << L"### set_bit: stmt=" << stmt << L", column=" << i << L", value=<NULL> ###" << std::endl;
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
    size_t* value = APITools_GetIntAddress(context, 1);
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);

#ifdef _DEBUG
    wcout << L"### set_bit: stmt=" << stmt << L", column=" << i 
          << L", value=" << *value << L" ###" << std::endl;
#endif  

    SQLRETURN status = SQLBindParameter(stmt, i, SQL_PARAM_INPUT, SQL_C_BIT, 
                                        SQL_BIT, 0, 0, value, 0, NULL);
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
  void odbc_stmt_set_int_null(VMContext& context) 
  {
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 1);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 2);

#ifdef _DEBUG
    wcout << L"### set_int: stmt=" << stmt << L", column=" << i << L", value=<NULL> ###" << std::endl;
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
    size_t* value = APITools_GetIntAddress(context, 1);
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);

#ifdef _DEBUG
    wcout << L"### set_int: stmt=" << stmt << L", column=" << i 
          << L", value=" << *value << L" ###" << std::endl;
#endif  

    SQLRETURN status = SQLBindParameter(stmt, i, SQL_PARAM_INPUT, SQL_C_LONG, 
                                        SQL_INTEGER, 0, 0, value, 0, NULL);
    if(SQL_OK) { 
      APITools_SetIntValue(context, 0, 1);
    }
    else {
      APITools_SetIntValue(context, 0, 0);
    }
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
    wcout << L"### set_double: stmt=" << stmt << L", column=" << i 
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
    size_t* value = APITools_GetFloatAddress(context, 1);
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);

#ifdef _DEBUG
    wcout << L"### set_double: stmt=" << stmt << L", column=" << i 
          << L", value=" << *value << L" ###" << std::endl;
#endif  

    SQLRETURN status = SQLBindParameter(stmt, i, SQL_PARAM_INPUT, SQL_C_DOUBLE, 
                                        SQL_DOUBLE, 0, 0, value, 0, NULL);
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
  void odbc_stmt_set_real_null(VMContext& context) 
  {
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 1);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 2);

#ifdef _DEBUG
    wcout << L"### set_real: stmt=" << stmt << L", column=" << i 
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
    size_t* value = APITools_GetFloatAddress(context, 1);
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);

#ifdef _DEBUG
    wcout << L"### set_real: stmt=" << stmt << L", column=" << i 
          << L", value=" << *value << L" ###" << std::endl;
#endif  

    SQLRETURN status = SQLBindParameter(stmt, i, SQL_PARAM_INPUT, SQL_C_FLOAT,
                                        SQL_REAL, 0, 0, value, 0, NULL);
    if(SQL_OK) { 
      APITools_SetIntValue(context, 0, 1);
    }
    else {
      APITools_SetIntValue(context, 0, 0);
    }
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
    wcout << L"### get_int_by_id: stmt=" << stmt << L", column=" << i 
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
      wcout << L"  " << value << std::endl;
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
    wcout << L"### get_int_by_name: stmt=" << stmt << L", column=" << i 
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
      wcout << L"  " << value << std::endl;
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
  void odbc_result_get_smallint_by_id(VMContext& context) 
  {
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    std::map<const std::wstring, int>* names = (std::map<const std::wstring, int>*)APITools_GetIntValue(context, 4);

#ifdef _DEBUG
    wcout << L"### get_smallint_by_id: stmt=" << stmt << L", column=" << i << L", max=" << (long)(names->size() + 1) << L" ###" << std::endl;
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
      wcout << L"  " << value << std::endl;
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
    wcout << L"### get_smallint_by_name: stmt=" << stmt << L", column=" << i << L", max=" << (long)(names->size() + 1) << L" ###" << std::endl;
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
      wcout << L"  " << value << std::endl;
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
    wcout << L"### get_bit_by_id: stmt=" << stmt << L", column=" << i 
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
      wcout << L"  " << value << std::endl;
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
    wcout << L"### get_bit_by_name: stmt=" << stmt << L", column=" << i 
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
      wcout << L"  " << value << std::endl;
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
    wcout << L"### get_double_by_id: stmt=" << stmt << L", column=" << i << L", max=" 
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
      wcout << L"  " << value << std::endl;
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
    wcout << L"### get_double_by_name: stmt=" << stmt << L", column=" << i << L", max=" 
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
      wcout << L"  " << value << std::endl;
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
    wcout << L"### get_real_by_id: stmt=" << stmt << L", column=" << i << L", max=" 
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
      wcout << L"  " << value << std::endl;
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
    wcout << L"### get_real_by_name: stmt=" << stmt << L", column=" << i << L", max=" 
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
      wcout << L"  " << value << std::endl;
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
    wcout << L"### set_varchar: stmt=" << stmt << L", column=" << i 
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
    char* value = (char*)APITools_GetByteArray(byte_array);
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
		
#ifdef _DEBUG
    wcout << L"### set_varchar: stmt=" << stmt << L", column=" << i 
          << L", value=" << value << L" ###" << std::endl;
#endif  
		
    SQLRETURN status = SQLBindParameter(stmt, i, SQL_PARAM_INPUT, SQL_C_CHAR, 
                                        SQL_LONGVARCHAR, 0, 0, (SQLPOINTER)value, strlen(value), NULL);
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
  void odbc_stmt_set_blob_null(VMContext& context) 
  {
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 1);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 2);
		
#ifdef _DEBUG
    wcout << L"### set_blob: stmt=" << stmt << L", column=" << i 
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
    char* value = (char*)APITools_GetByteArray(byte_array);
    const long value_size = APITools_GetArraySize(byte_array);
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
		
#ifdef _DEBUG
    wcout << L"### set_blob: stmt=" << stmt << L", column=" << i 
          << L", value=" << value << L" ###" << std::endl;
#endif  
    
    SQLRETURN status = SQLBindParameter(stmt, i, SQL_PARAM_INPUT, SQL_C_BINARY, SQL_LONGVARBINARY, 
                                        value_size, 0, (SQLPOINTER)value, value_size, 0);
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
  void odbc_stmt_set_bytes(VMContext& context) 
  {
    size_t* byte_array = (size_t*)APITools_GetIntValue(context, 1);
    char* value = (char*)APITools_GetByteArray(byte_array);
    SQLLEN* value_size = (SQLLEN*)APITools_GetIntAddress(context, 2);
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 3);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 4);
    std::map<int, std::pair<void*, SQLLEN> >* exec_data = (std::map<int, std::pair<void*, SQLLEN> >*)APITools_GetIntValue(context, 5);
    
#ifdef _DEBUG
    wcout << L"### set_bytes: stmt=" << stmt << L", column=" << i 
          << L", value=" << value << L" ###" << std::endl;
#endif
    
    std::pair<void*, SQLLEN> data(value, *value_size);
    *value_size = SQL_LEN_DATA_AT_EXEC(*value_size);
    SQLRETURN status = SQLBindParameter(stmt, i, SQL_PARAM_INPUT, SQL_C_BINARY, SQL_LONGVARBINARY, 
                                        *value_size, 0, (SQLPOINTER)i, 0, value_size);
    exec_data->insert(std::pair<int, std::pair<void*, SQLLEN> >(i, data));
    if(SQL_OK) { 
      APITools_SetIntValue(context, 0, 1);
    }
    else {
      APITools_SetIntValue(context, 0, 0);
    }
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
    wcout << L"### set_timestamp: stmt=" << stmt << L", column=" << i << L", value=<NULL> ###" << std::endl;
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

#ifdef _DEBUG
    wcout << L"### set_timestamp: stmt=" << stmt << L", column=" << i 
          << L", value=" << value[1] << L"-" << value[2] << L"-" << value[2] << L" " 
          << value[4] << L":" <<  value[5] << L":" <<  value[5] << value[6] << L"." 
          << value[7] << L" ###" << std::endl;
#endif

    SQL_TIMESTAMP_STRUCT time_stamp;    
    time_stamp.year = (SQLUSMALLINT)value[1];
    time_stamp.month = (SQLUSMALLINT)value[2];
    time_stamp.day = (SQLUSMALLINT)value[3];
    time_stamp.hour = (SQLUSMALLINT)value[4];
    time_stamp.minute = (SQLUSMALLINT)value[5];
    time_stamp.second = (SQLUSMALLINT)value[6];
    time_stamp.fraction = (SQLUSMALLINT)value[7];

    size_t* data = (size_t*)value[0];
    data += 3;
    memcpy(data, &time_stamp, sizeof(time_stamp));

    SQLRETURN status = SQLBindParameter(stmt, i, SQL_PARAM_INPUT, SQL_C_TYPE_TIMESTAMP, 
                                        SQL_TYPE_TIMESTAMP, 0, 0, data, sizeof(time_stamp), NULL);
    if(SQL_OK) { 
      APITools_SetIntValue(context, 0, 1);
    }
    else {
      APITools_SetIntValue(context, 0, 0);
    }
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
    wcout << L"### set_date: stmt=" << stmt << L", column=" << i 
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

#ifdef _DEBUG
    wcout << L"### set_date: stmt=" << stmt << L", column=" << i 
          << L", value=" << value << L" ###" << std::endl;
#endif

    SQL_DATE_STRUCT time_stamp;    
    time_stamp.year = (SQLUSMALLINT)value[1];
    time_stamp.month = (SQLUSMALLINT)value[2];
    time_stamp.day = (SQLUSMALLINT)value[3];

    size_t* data = (size_t*)value[0];
    data += 3;
    memcpy(data, &time_stamp, sizeof(time_stamp));

    SQLRETURN status = SQLBindParameter(stmt, i, SQL_PARAM_INPUT, SQL_C_TYPE_DATE, 
                                        SQL_TYPE_DATE, 0, 0, data, sizeof(time_stamp), NULL);
    if(SQL_OK) { 
      APITools_SetIntValue(context, 0, 1);
    }
    else {
      APITools_SetIntValue(context, 0, 0);
    }
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
    wcout << L"### get_varchar_by_id: stmt=" << stmt << L", column=" << i 
          << L", max=" << (long)(names->size() + 1) << L" ###" << std::endl;
#endif  

    if(!stmt || !names || i < 1 || i > (long)(names->size() + 1)) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetObjectValue(context, 1, 0);
      return;
    }

    SQLLEN is_null;
    char value[VARCHAR_MAX];
    SQLRETURN status = SQLGetData(stmt, i, SQL_C_CHAR, &value, VARCHAR_MAX, &is_null);
    if(SQL_OK) {
      APITools_SetIntValue(context, 0, is_null == SQL_NULL_DATA);
      const std::wstring out = BytesToUnicode(value);
      APITools_SetStringValue(context, 1, out);
#ifdef _DEBUG
      wcout << L"  " << value << std::endl;
#endif
    }
    else {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetObjectValue(context, 1, 0);
    }
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
    char* buffer = (char*)APITools_GetByteArray(byte_array);
    const long buffer_size = APITools_GetArraySize(byte_array);
		
    SQLUSMALLINT i = (SQLUSMALLINT)APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    std::map<const std::wstring, int>* names = (std::map<const std::wstring, int>*)APITools_GetIntValue(context, 4);

#ifdef _DEBUG
    wcout << L"### get_blob_by_id: stmt=" << stmt << L", column=" << i 
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
  // gets a blob from a result set
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_result_get_blob_by_name(VMContext& context) 
  {

    size_t* byte_array = (size_t*)APITools_GetIntValue(context, 1);
    char* buffer = (char*)APITools_GetByteArray(byte_array);
    const long buffer_size = APITools_GetArraySize(byte_array);

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
    wcout << L"### get_blob_by_name: stmt=" << stmt << L", column=" << i 
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
    wcout << L"### get_varchar_by_name: stmt=" << stmt << L", column=" << i 
          << L", max=" << (long)(names->size() + 1) << L" ###" << std::endl;
#endif  

    if(!stmt || !names || i < 1 || i > (long)(names->size() + 1)) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetObjectValue(context, 1, 0);
      return;
    }

    SQLLEN is_null;
    char value[VARCHAR_MAX];
    SQLRETURN status = SQLGetData(stmt, i, SQL_C_CHAR, &value, 
                                  VARCHAR_MAX, &is_null);
    if(SQL_OK) {
      APITools_SetIntValue(context, 0, is_null == SQL_NULL_DATA);
      const std::wstring out = BytesToUnicode(value);
      APITools_SetStringValue(context, 1, out);
#ifdef _DEBUG
      wcout << L"  " << value << std::endl;
#endif
    }
    else {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetObjectValue(context, 1, 0);
    }
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
    wcout << L"### get_timestamp_by_id: stmt=" << stmt << L", column=" 
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
      size_t* ts_obj = context.alloc_obj(L"ODBC.Timestamp", (size_t*)context.op_stack, *context.stack_pos, false);
      ts_obj[1] = value.year;
      ts_obj[2] = value.month;
      ts_obj[3] = value.day;
      ts_obj[4] = value.hour;
      ts_obj[5] = value.minute;
      ts_obj[6] = value.second;
      ts_obj[7] = value.fraction;

#ifdef _DEBUG
      wcout << L"  " << value.year << std::endl;
      wcout << L"  " << value.month << std::endl;
      wcout << L"  " << value.day << std::endl;
      wcout << L"  " << value.hour << std::endl;
      wcout << L"  " << value.minute << std::endl;
      wcout << L"  " << value.second << std::endl;
      wcout << L"  " << value.fraction << std::endl;
      wcout << L"  is_null=" << (is_null == SQL_NULL_DATA) << std::endl;
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
    wcout << L"### get_timestamp_by_name: stmt=" << stmt << L", column=" 
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
      size_t* ts_obj = context.alloc_obj(L"ODBC.Timestamp", (size_t*)context.op_stack, *context.stack_pos, false);
      ts_obj[1] = value.year;
      ts_obj[2] = value.month;
      ts_obj[3] = value.day;
      ts_obj[4] = value.hour;
      ts_obj[5] = value.minute;
      ts_obj[6] = value.second;
      ts_obj[7] = value.fraction;

#ifdef _DEBUG
      wcout << L"  " << value.year << std::endl;
      wcout << L"  " << value.month << std::endl;
      wcout << L"  " << value.day << std::endl;
      wcout << L"  " << value.hour << std::endl;
      wcout << L"  " << value.minute << std::endl;
      wcout << L"  " << value.second << std::endl;
      wcout << L"  " << value.fraction << std::endl;
      wcout << L"  is_null=" << (is_null == SQL_NULL_DATA) << std::endl;
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
    wcout << L"### get_date_by_id: stmt=" << stmt << L", column=" << i 
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
      size_t* ts_obj = context.alloc_obj(L"ODBC.Date", (size_t*)context.op_stack, *context.stack_pos, false);
      ts_obj[1] = value.year;
      ts_obj[2] = value.month;
      ts_obj[3] = value.day;

#ifdef _DEBUG
      wcout << L"  " << value.year << std::endl;
      wcout << L"  " << value.month << std::endl;
      wcout << L"  " << value.day << std::endl;
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
    wcout << L"### get_date_by_name: stmt=" << stmt << L", column=" << i 
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
      size_t* ts_obj = context.alloc_obj(L"ODBC.Date", (size_t*)context.op_stack, *context.stack_pos, false);
      ts_obj[1] = value.year;
      ts_obj[2] = value.month;
      ts_obj[3] = value.day;

#ifdef _DEBUG
      wcout << L"  " << value.year << std::endl;
      wcout << L"  " << value.month << std::endl;
      wcout << L"  " << value.day << std::endl;
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
		
#ifdef _DEBUG
    wcout << L"### closed result set ###" << std::endl;
#endif
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
      SQLFreeStmt(stmt, SQL_UNBIND);
      SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    }
		
#ifdef _DEBUG
    wcout << L"### closed prepared statement ###" << std::endl;
#endif
  }
}
