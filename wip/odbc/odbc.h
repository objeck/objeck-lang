#ifndef __ODBC_H__
#define __ODBC_H__

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <stdio.h>
#include <sql.h>
#include <sqlext.h>

#define SQL_OK status == SQL_SUCCESS || status == SQL_SUCCESS_WITH_INFO
#define SQL_FAIL status != SQL_SUCCESS && status != SQL_SUCCESS_WITH_INFO

namespace odbc {
  using namespace std;
  
  class Parameter {
  };
  
  class ResultSet {
  };
  
  class ODBCClient {
    SQLHDBC conn;
  
  public:
    ODBCClient(SQLHENV env, string &ds, string &username, string &password) {
      SQLRETURN status = SQLAllocHandle(SQL_HANDLE_DBC, env, &conn);
      if(SQL_OK) {
	status = SQLConnect(conn, (SQLCHAR*)ds.c_str(), SQL_NTS, 
			    (SQLCHAR*)username.c_str(), SQL_NTS, 
			    (SQLCHAR*)password.c_str(), SQL_NTS);      
	if(SQL_FAIL) {	
	  conn = NULL;
	}
      }
      else {
	conn = NULL;
      }
    }
  
    ~ODBCClient() {
      if(conn) {
	SQLDisconnect(conn);
	SQLFreeHandle(SQL_HANDLE_DBC, conn);
      }
    }
  
    int ExecuteUpdate(string &sql) {
      if(!conn) {
	return -1;
      }
    
      SQLHSTMT stmt = NULL;
      SQLRETURN status = SQLAllocStmt(conn, &stmt);
      if(SQL_OK) {
	status = SQLExecDirect(stmt, (SQLCHAR*)sql.c_str(), SQL_NTS);
	if(SQL_OK) {
	  SQLLEN count;
	  status = SQLRowCount(stmt, &count);
	  if(SQL_OK) {
	    return count;
	  }
	}
      }
    
      return -1;
    }
  
    ResultSet* ExecuteSelect(string &sql) {
      return NULL;
    }
  
    ResultSet* ExecuteSelect(string &sql, vector<Parameter*> &params) {
      return NULL;
    }
  }; 
}

#endif
