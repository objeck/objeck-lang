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

#include "odbc.h"

using namespace std;

extern "C" {
  //
  // initialize odbc environment
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void load_lib() {
    if(!env) {
      SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);
      SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (void*)SQL_OV_ODBC3, 0);
    }
  }
  
  //
  // release odbc resources
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void unload_lib() {
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
  void odbc_connect(VMContext& context) {
    SQLHDBC conn;

    const char* ds = APITools_GetStringValue(context, 1);
    const char* username = APITools_GetStringValue(context, 2);
    const char* password = APITools_GetStringValue(context, 3);
    
#ifdef _DEBUG
    cout << "### connect: " << "ds=" << ds << ", username=" 
	 << username << ", password=" << password << " ###" << endl;
#endif

    SQLRETURN status = SQLAllocHandle(SQL_HANDLE_DBC, env, &conn);
    if(SQL_OK) {
      status = SQLConnect(conn, (SQLCHAR*)ds, SQL_NTS, 
			  (SQLCHAR*)username, SQL_NTS, 
			  (SQLCHAR*)password, SQL_NTS);      
      if(SQL_FAIL) {	
	conn = NULL;
      }
    }
    else {
      conn = NULL;
    }
    
    APITools_SetIntValue(context, 0, (long)conn);
  }
  
  //
  // disconnects from an ODBC data source
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_disconnect(VMContext& context) {
    SQLHDBC conn = (SQLHDBC)APITools_GetIntValue(context, 0);    
    if(conn) {
      SQLDisconnect(conn);
      SQLFreeHandle(SQL_HANDLE_DBC, conn);
      
#ifdef _DEBUG
      cout << "### disconnect ###" << endl;
#endif
    }
  }
  
  //
  // executes and update statement
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_update_statement(VMContext& context) {
    SQLHDBC conn = (SQLHDBC)APITools_GetIntValue(context, 1);
    const char* sql = APITools_GetStringValue(context, 2);

#ifdef _DEBUG
    cout << "### update: conn=" << conn << ", stmt=" << sql << "  ###" << endl;
#endif    
    
    if(!conn || !sql) {
      APITools_SetIntValue(context, 0, -1);
      return;
    }
    
    SQLHSTMT stmt = NULL;
    SQLRETURN status = SQLAllocStmt(conn, &stmt);
    if(SQL_OK) {
      status = SQLExecDirect(stmt, (SQLCHAR*)sql, SQL_NTS);
      if(SQL_OK) {
	SQLLEN count;
	status = SQLRowCount(stmt, &count);
	if(SQL_OK) {
	  SQLFreeStmt(stmt, SQL_CLOSE);
	  APITools_SetIntValue(context, 0, count);
	  return;
	}
      }
    }
    
    if(stmt) {
      SQLFreeStmt(stmt, SQL_CLOSE);
    }
    APITools_SetIntValue(context, 0, -1);
  }

  //
  // executes a prepared select statement
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_stmt_select_statement(VMContext& context) {
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 0);

#ifdef _DEBUG
    cout << "### stmt_select_update: stmt=" << stmt << " ###" << endl;
#endif
    
    SQLExecute(stmt);
  }
  
  //
  // executes a select statement
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_select_statement(VMContext& context) {
    SQLHDBC conn = (SQLHDBC)APITools_GetIntValue(context, 2);
    const char* sql = APITools_GetStringValue(context, 3);
    
#ifdef _DEBUG
    cout << "### select: conn=" << conn << ", stmt=" << sql << "  ###" << endl;
#endif    
    
    if(!conn || !sql) {
      APITools_SetIntValue(context, 0, 0);
      return;
    }
    
    SQLHSTMT stmt = NULL;
    SQLRETURN status = SQLAllocStmt(conn, &stmt);
    if(SQL_OK) {
      status = SQLPrepare(stmt, (SQLCHAR*)sql, SQL_NTS);
      if(SQL_OK) {
	SQLSMALLINT columns;
	status = SQLNumResultCols(stmt, &columns);
	if(SQL_OK) {
	  // get column information
	  map<const string, int>* column_names = new map<const string, int>;
	  if(columns > 0) {
	    for(SQLSMALLINT i = 1; i <= columns; i++) {
	      ColumnDescription description;
	      status = SQLDescribeCol(stmt, i, (SQLCHAR*)&description.column_name, COL_NAME_MAX, 
				      &description.column_name_size, &description.type, 
				      &description.column_size, &description.decimal_length, 
				      &description.nullable);
	      if(SQL_FAIL) {
		SQLFreeStmt(stmt, SQL_CLOSE);
		APITools_SetIntValue(context, 0, 0);
		return;
	      }
	      column_names->insert(pair<string, int>((const char*)description.column_name, i));
#ifdef _DEBUG
	      cout << "  name=" << description.column_name << ", type=" << description.type << endl;
#endif
	    }
	  }
	  // execute query
	  status = SQLExecute(stmt); 
	  if(SQL_OK) {
	    APITools_SetIntValue(context, 0, (long)stmt);
	    APITools_SetIntValue(context, 1, (long)column_names);
#ifdef _DEBUG
	    cout << "### select OK: stmt=" << stmt << " ###" << endl;
#endif  
	    return;
	  }
	} 
      }
    }
      
    if(stmt) {
      SQLFreeStmt(stmt, SQL_CLOSE);
    }      
    APITools_SetIntValue(context, 0, 0);
  }

  //
  // executes a select statement
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_pepare_statement(VMContext& context) {
    SQLHDBC conn = (SQLHDBC)APITools_GetIntValue(context, 2);
    const char* sql = APITools_GetStringValue(context, 3);
    
#ifdef _DEBUG
    cout << "### select: conn=" << conn << ", stmt=" << sql << "  ###" << endl;
#endif    
    
    if(!conn || !sql) {
      APITools_SetIntValue(context, 0, 0);
      return;
    }
    
    SQLHSTMT stmt = NULL;
    SQLRETURN status = SQLAllocStmt(conn, &stmt);
    if(SQL_OK) {
      status = SQLPrepare(stmt, (SQLCHAR*)sql, SQL_NTS);
      if(SQL_OK) {
	SQLSMALLINT columns;
	status = SQLNumResultCols(stmt, &columns);
	if(SQL_OK) {
	  // get column information
	  map<const string, int>* column_names = new map<const string, int>;
	  if(columns > 0) {
	    for(SQLSMALLINT i = 1; i <= columns; i++) {
	      ColumnDescription description;
	      status = SQLDescribeCol(stmt, i, (SQLCHAR*)&description.column_name, COL_NAME_MAX, 
				      &description.column_name_size, &description.type, 
				      &description.column_size, &description.decimal_length, 
				      &description.nullable);
	      if(SQL_FAIL) {
		SQLFreeStmt(stmt, SQL_CLOSE);
		APITools_SetIntValue(context, 0, 0);
		return;
	      }
	      column_names->insert(pair<string, int>((const char*)description.column_name, i));
#ifdef _DEBUG
	      cout << "  name=" << description.column_name << ", type=" << description.type << endl;
#endif
	    }
	  }
	  // return statement
	  APITools_SetIntValue(context, 0, (long)stmt);
	  APITools_SetIntValue(context, 1, (long)column_names);
	  return;
	} 
      }
    }
      
    if(stmt) {
      SQLFreeStmt(stmt, SQL_CLOSE);
    }      
    APITools_SetIntValue(context, 0, 0);
  }
  
  //
  // fetches the next row in a resultset
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_result_next(VMContext& context) {
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 1);
    if(!stmt) {
      APITools_SetIntValue(context, 0, 0);
      return;
    }
      
    SQLRETURN status = SQLFetch(stmt);
    if(SQL_OK) {
      APITools_SetIntValue(context, 0, 1);
      return;
    }

    APITools_SetIntValue(context, 0, 0);
  }

  //
  // updates a prepared statement
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_stmt_update(VMContext& context) {
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 1);

#ifdef _DEBUG
    cout << "### stmt_update: stmt=" << stmt << " ###" << endl;
#endif
    
    SQLRETURN status = SQLExecute(stmt);
    if(SQL_OK) {
      SQLLEN count;
      status = SQLRowCount(stmt, &count);
      if(SQL_OK) {
	APITools_SetIntValue(context, 0, count);
	return;
      }
    }
    
    APITools_SetIntValue(context, 0, -1);
  }
  
  //
  // set a small int from a prepared statement
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_stmt_set_smallint(VMContext& context) {
    long* value = APITools_GetIntAddress(context, 1);
    long i = APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    
#ifdef _DEBUG
    cout << "### set_smallint: stmt=" << stmt << ", column=" << i 
	 << ", value=" << *value << " ###" << endl;
#endif  
    
    SQLRETURN status = SQLBindParameter(stmt, i, SQL_PARAM_INPUT, SQL_C_SSHORT, 
					SQL_SMALLINT, 0, 0, value, 0, NULL);
    if(SQL_OK) { 
      APITools_SetIntValue(context, 0, 1);
      return;
    }
    
    APITools_SetIntValue(context, 0, 0);
  }

  //
  // set an int from a prepared statement
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_stmt_set_bit(VMContext& context) {
    long* value = APITools_GetIntAddress(context, 1);
    long i = APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    
#ifdef _DEBUG
    cout << "### set_bit: stmt=" << stmt << ", column=" << i 
	 << ", value=" << *value << " ###" << endl;
#endif  
    
    SQLRETURN status = SQLBindParameter(stmt, i, SQL_PARAM_INPUT, SQL_C_BIT, 
					SQL_BIT, 0, 0, value, 0, NULL);
    if(SQL_OK) { 
      APITools_SetIntValue(context, 0, 1);
      return;
    }
    
    APITools_SetIntValue(context, 0, 0);
  }
  
  //
  // set an int from a prepared statement
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_stmt_set_int(VMContext& context) {
    long* value = APITools_GetIntAddress(context, 1);
    long i = APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    
#ifdef _DEBUG
    cout << "### set_int: stmt=" << stmt << ", column=" << i 
	 << ", value=" << *value << " ###" << endl;
#endif  
    
    SQLRETURN status = SQLBindParameter(stmt, i, SQL_PARAM_INPUT, SQL_C_LONG, 
					SQL_INTEGER, 0, 0, value, 0, NULL);
    if(SQL_OK) { 
      APITools_SetIntValue(context, 0, 1);
      return;
    }
    
    APITools_SetIntValue(context, 0, 0);
  }

  //
  // set an double from a prepared statement
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_stmt_set_double(VMContext& context) {
    long* value = APITools_GetFloatAddress(context, 1);
    long i = APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    
#ifdef _DEBUG
    cout << "### set_double: stmt=" << stmt << ", column=" << i 
	 << ", value=" << *value << " ###" << endl;
#endif  
    
    SQLRETURN status = SQLBindParameter(stmt, i, SQL_PARAM_INPUT, SQL_C_DOUBLE, 
					SQL_DOUBLE, 0, 0, value, 0, NULL);
    if(SQL_OK) { 
      APITools_SetIntValue(context, 0, 1);
      return;
    }
    
    APITools_SetIntValue(context, 0, 0);
  }

  //
  // set an double from a prepared statement
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_stmt_set_real(VMContext& context) {
    long* value = APITools_GetFloatAddress(context, 1);
    long i = APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    
#ifdef _DEBUG
    cout << "### set_real: stmt=" << stmt << ", column=" << i 
	 << ", value=" << *value << " ###" << endl;
#endif  
    
    SQLRETURN status = SQLBindParameter(stmt, i, SQL_PARAM_INPUT, SQL_C_FLOAT,
					SQL_REAL, 0, 0, value, 0, NULL);
    if(SQL_OK) { 
      APITools_SetIntValue(context, 0, 1);
      return;
    }
    
    APITools_SetIntValue(context, 0, 0);
  }
  
  //
  // gets an int from a result set
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_result_get_int_by_id(VMContext& context) {
    SQLUSMALLINT i = APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    map<const string, int>* names = (map<const string, int>*)APITools_GetIntValue(context, 4);
    
#ifdef _DEBUG
    cout << "### get_int_by_id: stmt=" << stmt << ", column=" << i 
	 << ", max=" << (long)names->size() << " ###" << endl;
#endif  
    
    if(!stmt || !names || i < 1 || i > (long)names->size()) {
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
      cout << "  " << value << endl;
#endif
      return;
    }
    
    APITools_SetIntValue(context, 0, 0);
    APITools_SetIntValue(context, 1, 0);
  }



  //
  // gets an int from a result set
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_result_get_smallint_by_id(VMContext& context) {
    SQLUSMALLINT i = APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    map<const string, int>* names = (map<const string, int>*)APITools_GetIntValue(context, 4);
    
#ifdef _DEBUG
    cout << "### get_smallint_by_id: stmt=" << stmt << ", column=" << i << ", max=" << (long)names->size() << " ###" << endl;
#endif  
    
    if(!stmt || !names || i < 1 || i > (long)names->size()) {
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
      cout << "  " << value << endl;
#endif
      return;
    }
    
    APITools_SetIntValue(context, 0, 0);
    APITools_SetIntValue(context, 1, 0);
  }
  
  //
  // gets a bit from a result set
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_result_get_bit_by_id(VMContext& context) {
    SQLUSMALLINT i = APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    map<const string, int>* names = (map<const string, int>*)APITools_GetIntValue(context, 4);
    
#ifdef _DEBUG
    cout << "### get_bit_by_id: stmt=" << stmt << ", column=" << i 
	 << ", max=" << (long)names->size() << " ###" << endl;
#endif  
    
    if(!stmt || !names || i < 1 || i > (long)names->size()) {
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
      cout << "  " << value << endl;
#endif
      return;
    }
    
    APITools_SetIntValue(context, 0, 0);
    APITools_SetIntValue(context, 1, 0);
  }
  
  //
  // gets an double from a result set
  //
#ifdef _WIN32
  __declspec(dllexport)  
#endif
  void odbc_result_get_double_by_id(VMContext& context) {
    SQLUSMALLINT i = APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    map<const string, int>* names = (map<const string, int>*)APITools_GetIntValue(context, 4);
    
#ifdef _DEBUG
    cout << "### get_double_by_id: stmt=" << stmt << ", column=" << i << ", max=" 
	 << (long)names->size() << " ###" << endl;
#endif  
    
    if(!stmt || !names || i < 1 || i > (long)names->size()) {
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
      cout << "  " << value << endl;
#endif
      return;
    }
    
    APITools_SetIntValue(context, 0, 0);
    APITools_SetFloatValue(context, 1, 0.0);
  }

  //
  // gets an double from a result set
  //
#ifdef _WIN32
  __declspec(dllexport)  
#endif
  void odbc_result_get_real_by_id(VMContext& context) {
    SQLUSMALLINT i = APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    map<const string, int>* names = (map<const string, int>*)APITools_GetIntValue(context, 4);
    
#ifdef _DEBUG
    cout << "### get_real_by_id: stmt=" << stmt << ", column=" << i << ", max=" 
	 << (long)names->size() << " ###" << endl;
#endif  
    
    if(!stmt || !names || i < 1 || i > (long)names->size()) {
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
      cout << "  " << value << endl;
#endif
      return;
    }
    
    APITools_SetIntValue(context, 0, 0);
    APITools_SetFloatValue(context, 1, 0.0);
  }
  
  //
  // set a string for a prepared statement
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_stmt_set_varchar(VMContext& context) {
    char* value = APITools_GetStringValue(context, 1);
    long i = APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    
#ifdef _DEBUG
    cout << "### set_varchar: stmt=" << stmt << ", column=" << i 
	 << ", value=" << value << " ###" << endl;
#endif  
    
    SQLRETURN status = SQLBindParameter(stmt, i, SQL_PARAM_INPUT, SQL_C_CHAR, 
					SQL_CHAR, 0, 0, value, strlen(value), NULL);
    if(SQL_OK) { 
      APITools_SetIntValue(context, 0, 1);
      return;
    }
    
    APITools_SetIntValue(context, 0, 0);
  }
  
  //
  // set a timestamp for a prepared statement
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_stmt_set_timestamp(VMContext& context) {
    long* value = APITools_GetObjectValue(context, 1);
    long i = APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    
#ifdef _DEBUG
    cout << "### set_timestamp: stmt=" << stmt << ", column=" << i 
	 << ", value=" << value[1] << "-" << value[2] << "-" << value[2] << " " 
	 << value[4] << ":" <<  value[5] << ":" <<  value[5]  value[6] << "." 
	 << value[7] << " ###" << endl;
#endif
    
    SQL_TIMESTAMP_STRUCT time_stamp;    
    time_stamp.year = value[1];
    time_stamp.month = value[2];
    time_stamp.day = value[3];
    time_stamp.hour = value[4];
    time_stamp.minute = value[5];
    time_stamp.second = value[6];
    time_stamp.fraction = value[7];
    
    long* data = (long*)value[0];
    data += 3;
    memcpy(data, &time_stamp, sizeof(time_stamp));
    
    SQLRETURN status = SQLBindParameter(stmt, i, SQL_PARAM_INPUT, SQL_C_TYPE_TIMESTAMP, 
					SQL_TYPE_TIMESTAMP, 0, 0, data, sizeof(time_stamp), NULL);
    if(SQL_OK) { 
      APITools_SetIntValue(context, 0, 1);
      return;
    }
    
    APITools_SetIntValue(context, 0, 0);
  }
  
  //
  // set a date for a prepared statement
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_stmt_set_date(VMContext& context) {
    long* value = APITools_GetObjectValue(context, 1);
    long i = APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    
#ifdef _DEBUG
    cout << "### set_date: stmt=" << stmt << ", column=" << i 
	 << ", value=" << value << " ###" << endl;
#endif
    
    SQL_DATE_STRUCT time_stamp;    
    time_stamp.year = value[1];
    time_stamp.month = value[2];
    time_stamp.day = value[3];
    
    long* data = (long*)value[0];
    data += 3;
    memcpy(data, &time_stamp, sizeof(time_stamp));
    
    SQLRETURN status = SQLBindParameter(stmt, i, SQL_PARAM_INPUT, SQL_C_TYPE_DATE, 
					SQL_TYPE_DATE, 0, 0, data, sizeof(time_stamp), NULL);
    if(SQL_OK) { 
      APITools_SetIntValue(context, 0, 1);
      return;
    }
    
    APITools_SetIntValue(context, 0, 0);
  }
    
  //
  // gets a string from a result set
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_result_get_varchar_by_id(VMContext& context) {
    long i = APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    map<const string, int>* names = (map<const string, int>*)APITools_GetIntValue(context, 4);
    
#ifdef _DEBUG
    cout << "### get_varchar_by_id: stmt=" << stmt << ", column=" << i 
	 << ", max=" << (long)names->size() << " ###" << endl;
#endif  
    
    if(!stmt || !names || i < 1 || i > (long)names->size()) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetObjectValue(context, 1, NULL);
      return;
    }

    SQLLEN is_null;
    char value[VARCHAR_MAX];
    SQLRETURN status = SQLGetData(stmt, i, SQL_C_CHAR, &value, 
				  VARCHAR_MAX, &is_null);
    if(SQL_OK) {
      APITools_SetIntValue(context, 0, is_null == SQL_NULL_DATA);
      APITools_SetStringValue(context, 1, value);
#ifdef _DEBUG
      cout << "  " << value << endl;
#endif
      return;
    }
    
    APITools_SetIntValue(context, 0, 0);
    APITools_SetObjectValue(context, 1, NULL);
  }

  //
  // gets a string from a result set
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_result_get_varchar_by_name(VMContext& context) {
    const char* name = APITools_GetStringValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    map<const string, int>* names = (map<const string, int>*)APITools_GetIntValue(context, 4);

    map<const string, int>::iterator result = names->find(name);
    if(result == names->end()) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetObjectValue(context, 1, NULL);
      return;
    }
    long i = result->second;
    
#ifdef _DEBUG
    cout << "### get_varchar_by_id: stmt=" << stmt << ", column=" << i 
	 << ", max=" << (long)names->size() << " ###" << endl;
#endif  
    
    if(!stmt || !names || i < 1 || i > (long)names->size()) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetObjectValue(context, 1, NULL);
      return;
    }

    SQLLEN is_null;
    char value[VARCHAR_MAX];
    SQLRETURN status = SQLGetData(stmt, i, SQL_C_CHAR, &value, 
				  VARCHAR_MAX, &is_null);
    if(SQL_OK) {
      APITools_SetIntValue(context, 0, is_null == SQL_NULL_DATA);
      APITools_SetStringValue(context, 1, value);
#ifdef _DEBUG
      cout << "  " << value << endl;
#endif
      return;
    }
    
    APITools_SetIntValue(context, 0, 0);
    APITools_SetObjectValue(context, 1, NULL);
  }

  //
  // gets a timestamp from a result set
  //  
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_result_get_timestamp_by_id(VMContext& context) {
    long i = APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    map<const string, int>* names = (map<const string, int>*)APITools_GetIntValue(context, 4);
    
#ifdef _DEBUG
    cout << "### get_timestamp_by_id: stmt=" << stmt << ", column=" 
	 << i << ", max=" << (long)names->size() << " ###" << endl;
#endif  
    
    if(!stmt || !names || i < 1 || i > (long)names->size()) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetObjectValue(context, 1, NULL);
      return;
    }

    SQLLEN is_null;
    TIMESTAMP_STRUCT value;
    SQLRETURN status = SQLGetData(stmt, i, SQL_C_TYPE_TIMESTAMP, &value, 
				  sizeof(TIMESTAMP_STRUCT), &is_null);
    if(SQL_OK) {
      APITools_SetIntValue(context, 0, is_null == SQL_NULL_DATA);
      long* ts_obj = context.alloc_obj("ODBC.Timestamp", (long*)context.op_stack, *context.stack_pos);
      ts_obj[0] = value.year;
      ts_obj[1] = value.month;
      ts_obj[2] = value.day;
      ts_obj[3] = value.hour;
      ts_obj[4] = value.minute;
      ts_obj[5] = value.second;
      ts_obj[6] = value.fraction;
      
#ifdef _DEBUG
      cout << "  " << value.year << endl;
      cout << "  " << value.month << endl;
      cout << "  " << value.day << endl;
      cout << "  " << value.hour << endl;
      cout << "  " << value.minute << endl;
      cout << "  " << value.second << endl;
      cout << "  " << value.fraction << endl;
#endif
      
      // set values
      APITools_SetIntValue(context, 0, 0);
      APITools_SetObjectValue(context, 1, ts_obj);
      return;
    }
    
    APITools_SetIntValue(context, 0, 0);
    APITools_SetObjectValue(context, 1, NULL);
  }

  //
  // gets a date from a result set
  //  
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_result_get_date_by_id(VMContext& context) {
    long i = APITools_GetIntValue(context, 2);
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 3);
    map<const string, int>* names = (map<const string, int>*)APITools_GetIntValue(context, 4);
    
#ifdef _DEBUG
    cout << "### get_date_by_id: stmt=" << stmt << ", column=" << i 
	 << ", max=" << (long)names->size() << " ###" << endl;
#endif  
    
    if(!stmt || !names || i < 1 || i > (long)names->size()) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetObjectValue(context, 1, NULL);
      return;
    }

    SQLLEN is_null;
    DATE_STRUCT value;
    SQLRETURN status = SQLGetData(stmt, i, SQL_C_TYPE_DATE, &value, 
				  sizeof(DATE_STRUCT), &is_null);
    if(SQL_OK) {
      APITools_SetIntValue(context, 0, is_null == SQL_NULL_DATA);
      long* ts_obj = context.alloc_obj("ODBC.Date", (long*)context.op_stack, *context.stack_pos);
      ts_obj[0] = value.year;
      ts_obj[1] = value.month;
      ts_obj[2] = value.day;
      
#ifdef _DEBUG
      cout << "  " << value.year << endl;
      cout << "  " << value.month << endl;
      cout << "  " << value.day << endl;
#endif
      
      // set values
      APITools_SetIntValue(context, 0, 0);
      APITools_SetObjectValue(context, 1, ts_obj);
      return;
    }
    
    APITools_SetIntValue(context, 0, 0);
    APITools_SetObjectValue(context, 1, NULL);
  }
  
  //
  // closes a result set
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void odbc_result_close(VMContext& context) {
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 0);
    if(stmt) {
      SQLFreeStmt(stmt, SQL_CLOSE);
    }
    
    map<const string, int>* names = (map<const string, int>*)APITools_GetIntValue(context, 1);
    if(names) {
      delete names;
      names = NULL;
    }
    
#ifdef _DEBUG
    cout << "### closed statement ###" << endl;
#endif
  }
}
