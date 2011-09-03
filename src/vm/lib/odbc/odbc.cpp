#include "odbc.h"

using namespace std;

extern "C" {
  // initialize odbc environment
  void load_lib() {
    if(!env) {
      SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);
      SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (void*)SQL_OV_ODBC3, 0);
    }
  }
  
  // free odbc environment
  void unload_lib() {
    if(env) {
      SQLFreeHandle(SQL_HANDLE_ENV, env);
    }
  }
  
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
  
  void odbc_disconnect(VMContext& context) {
    SQLHDBC conn = (SQLHDBC)APITools_GetIntValue(context, 0);    
    if(conn) {
      SQLDisconnect(conn);
      SQLFreeHandle(SQL_HANDLE_DBC, conn);
      
#ifdef _DEBUG
      cout << "## disconnect ###" << endl;
#endif
    }
  }
  
  void odbc_update_statement(VMContext& context) {
    SQLHDBC conn = (SQLHDBC)APITools_GetIntValue(context, 1);
    const char* sql = APITools_GetStringValue(context, 2);

#ifdef _DEBUG
    cout << "## update: conn=" << conn << ", stmt=" << sql << "  ###" << endl;
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
  
  void odbc_select_statement(VMContext& context) {
    SQLHDBC conn = (SQLHDBC)APITools_GetIntValue(context, 2);
    const char* sql = APITools_GetStringValue(context, 3);
    
#ifdef _DEBUG
    cout << "## select: conn=" << conn << ", stmt=" << sql << "  ###" << endl;
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
	  vector<ColumnDescription> descriptions;
	  // get column information
	  vector<const char*>* column_names = new vector<const char*>;
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
	      column_names->push_back((const char*)description.column_name);
	      cout << "  name=" << description.column_name << ", type=" << description.type << endl;
	    }
	  }
	  // execute query
	  status = SQLExecute(stmt); 
	  if(SQL_OK) {
	    APITools_SetIntValue(context, 0, (long)stmt);
	    APITools_SetIntValue(context, 1, (long)column_names);
#ifdef _DEBUG
    cout << "## select: OK ###" << endl;
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

  void odbc_result_close(VMContext& context) {
    SQLHSTMT stmt = (SQLHDBC)APITools_GetIntValue(context, 0);
    if(stmt) {
      SQLFreeStmt(stmt, SQL_CLOSE);
    }
    
    vector<const char*>* names = (vector<const char*>*)APITools_GetIntValue(context, 1);
    if(names) {
      delete names;
      names = NULL;
    }
    
#ifdef _DEBUG
      cout << "## closed statement ###" << endl;
#endif
  }
}
