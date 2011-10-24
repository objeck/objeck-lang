#ifndef __ODBC_H__
#define __ODBC_H__

// #include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <stdio.h>
#include <sql.h>
#include <sqlext.h>

#define SQL_OK status == SQL_SUCCESS || status == SQL_SUCCESS_WITH_INFO
#define SQL_FAIL status != SQL_SUCCESS && status != SQL_SUCCESS_WITH_INFO
#define COL_NAME_MAX 64
#define VARCHAR_MAX 1024

namespace odbc {
  using namespace std;
  
  typedef struct _ColumnDescription {
    SQLCHAR column_name[COL_NAME_MAX];
    SQLSMALLINT column_name_size;
    SQLSMALLINT type;
    SQLULEN column_size;
    SQLSMALLINT decimal_length;
    SQLSMALLINT nullable;
  } ColumnDescription;
  
  class Parameter {
  };
  
  class ResultSet {
    SQLHSTMT stmt;
    vector<ColumnDescription> descriptions;
    map<const char*, unsigned int> column_name_ids;
    
  public:
    ResultSet(SQLHSTMT s, vector<ColumnDescription> &d) {
      stmt = s;
      stmt = NULL;
      descriptions = d;
      
      for(unsigned int i = 0; i < descriptions.size(); i++) {
	column_name_ids.insert(pair<const char*, unsigned int>((const char*)descriptions[i].column_name, i));
      }
    }
    
    ~ResultSet() {
      if(stmt) {
	SQLFreeStmt(stmt, SQL_CLOSE);
      }     	
    }
    
    bool IsGood() {
      return stmt != NULL;
    }
    
    bool Next() {
      if(!stmt) {
	return false;
      }
      
      SQLRETURN status = SQLFetch(stmt);
      if(SQL_OK) {
	return true;
      }

      return false;
    }
    
    // TODO: pass status code, use for NULL
    long GetLong(int i) {
      if(i < 1 || i > descriptions.size()) {
	return 0;
      }

      SQLSMALLINT type = descriptions[i - 1].type;
      if(type != SQL_INTEGER) {
	return 0;
      }
      
      SQLLEN is_null;
      long value;
      SQLRETURN status = SQLGetData(stmt, i, SQL_C_SLONG, 
				    &value, 0, &is_null);
      if(SQL_OK) {
	return value;
      }

      return 0;
    }

    // TODO: pass status code, use for NULL
    string GetString(int i) {
      if(i < 1 || i > descriptions.size()) {
	return "";
      }

      SQLSMALLINT type = descriptions[i - 1].type;
      if(type != SQL_VARCHAR) {
	return "";
      }
      
      SQLLEN is_null;
      char value[VARCHAR_MAX];
      SQLRETURN status = SQLGetData(stmt, i, SQL_C_CHAR, &value, 
				    VARCHAR_MAX, &is_null);
      if(SQL_OK) {
	return value;
      }

      return "";
    }
  };
  
  class ODBCClient {
    SQLHDBC conn;
    ResultSet* result;
    
  public:
    ODBCClient(SQLHENV env, string ds, string username, string password) {
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

      result = NULL;
    }
  
    ~ODBCClient() {
      if(conn) {
	SQLDisconnect(conn);
	SQLFreeHandle(SQL_HANDLE_DBC, conn);
      }

      if(result) {
	delete result;
	result = NULL;
      }
    }
    
    int ExecuteUpdate(string sql) {
      if(!conn || result) {
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
	    SQLFreeStmt(stmt, SQL_CLOSE);
	    return count;
	  }
	}
      }
      
      if(stmt) {
	SQLFreeStmt(stmt, SQL_CLOSE);
      }
      return -1;
    }
    
    void ReleaseResult() {
      delete result;
      result = NULL;
    }
    
    ResultSet* ExecuteSelect(string &sql) {
      if(!conn || result) {
	return NULL;
      }
      
      SQLHSTMT stmt = NULL;
      SQLRETURN status = SQLAllocStmt(conn, &stmt);
      if(SQL_OK) {
	status = SQLPrepare(stmt, (SQLCHAR*)sql.c_str(), SQL_NTS);
	if(SQL_OK) {
	  SQLSMALLINT columns;
	  status = SQLNumResultCols(stmt, &columns);
	  if(SQL_OK) {
	    vector<ColumnDescription> descriptions;
	    // get column information
	    if(columns > 0) {
	      for(SQLSMALLINT i = 1; i <= columns; i++) {
		ColumnDescription description;
		status = SQLDescribeCol(stmt, i, (SQLCHAR*)&description.column_name, COL_NAME_MAX, 
					&description.column_name_size, &description.type, 
					&description.column_size, &description.decimal_length, 
					&description.nullable);
		if(SQL_FAIL) {
		  SQLFreeStmt(stmt, SQL_CLOSE);
		  return NULL;
		}
cout << "name=" << description.column_name << ", type=" << description.type << endl;
		descriptions.push_back(description);
	      }
	    }
	    // execute query
	    status = SQLExecute(stmt); 
	    if(SQL_OK) {
	      result = new ResultSet(stmt, descriptions);
	      return result;
	    }
	  } 
	}
      }
      
      if(stmt) {
	SQLFreeStmt(stmt, SQL_CLOSE);
      }      
      return NULL;
    }
  
    ResultSet* ExecuteSelect(string &sql, vector<Parameter*> &params) {
      return NULL;
    }
  }; 
}

#endif
