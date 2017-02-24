#include "odbc.h"

using namespace std;
using namespace odbc;

int main() {
  // setup environment
  SQLHENV env;
  SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);
  SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (void*)SQL_OV_ODBC3, 0);

  char ds[] = "test";
  char user[] = "root";
  char pass[] = "helloworld";
  char sql[] = "insert into test.users2 values (6, 'fgf', 'west', 'foo')";

  SQLHDBC conn;
  SQLRETURN status = SQLAllocHandle(SQL_HANDLE_DBC, env, &conn);
  status = SQLConnect(conn, (SQLCHAR*)&ds, SQL_NTS, (SQLCHAR*)user, SQL_NTS, (SQLCHAR*)pass, SQL_NTS);
	if(SQL_FAIL) {
		cout << "- connect fail -" << endl;
		exit(1);
	}

	SQLHSTMT stmt;
	status = SQLAllocHandle(SQL_HANDLE_STMT, conn, &stmt);
	if(SQL_FAIL) {
		cout << "- statement fail -" << endl;
		exit(1);
	}

	/*
  char str_value[] = "foo";
	status = SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR,
																			0, 0, str_value, strlen(str_value), NULL);
	if(SQL_FAIL) {
		cout << "- bind fail -" << endl;
		exit(1);
	}
	*/

	status = SQLPrepare(stmt, (SQLCHAR*)sql, SQL_NTS);
	if (SQL_FAIL) {
		cout << "- prepare fail -" << endl;
		exit(1);
	}
	
	// execute query
	status = SQLExecute(stmt);
	if(SQL_FAIL) {
		cout << "- execute fail -" << endl;
		exit(1);
	}

	SQLDisconnect(conn);
	SQLFreeStmt(stmt, SQL_CLOSE);
	SQLFreeHandle(SQL_HANDLE_DBC, conn);
  SQLFreeHandle(SQL_HANDLE_ENV, env);
	
  return 0;
}
