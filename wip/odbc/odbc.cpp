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
  char pass[] = "";
  char sql[] = "SELECT * FROM world.city WHERE name = ?";

  SQLHDBC conn;
  SQLRETURN status = SQLAllocHandle(SQL_HANDLE_DBC, env, &conn);
  status = SQLConnect(conn, (SQLCHAR*)&ds, SQL_NTS, (SQLCHAR*)user, SQL_NTS, (SQLCHAR*)pass, SQL_NTS);
  if (SQL_OK) {
    SQLHSTMT stmt = NULL;
    status = SQLAllocStmt(conn, &stmt);
    if (SQL_OK) {
      status = SQLPrepare(stmt, (SQLCHAR*)&sql, SQL_NTS);
      if (SQL_OK) {
        SQLSMALLINT columns;
        status = SQLNumResultCols(stmt, &columns);
        if (SQL_OK) {
          vector<ColumnDescription> descriptions;
          // get column information
          if (columns > 0) {
            for (SQLSMALLINT i = 1; i <= columns; i++) {
              ColumnDescription description;
              status = SQLDescribeCol(stmt, i, (SQLCHAR*)&description.column_name, COL_NAME_MAX,
                &description.column_name_size, &description.type,
                &description.column_size, &description.decimal_length,
                &description.nullable);
              if (SQL_FAIL) {
                SQLFreeStmt(stmt, SQL_CLOSE);
                return NULL;
              }
              cout << "name=" << description.column_name << ", type=" << description.type << endl;
            }
          }

          char str_value[] = "Mazar-e-Sharif";
          SQLRETURN status = SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR,
            0, 0, str_value, strlen(str_value), NULL);
          if (SQL_FAIL) {
            SQLFreeStmt(stmt, SQL_CLOSE);
            return NULL;
          }

          cout << "-- " << SQL_SUCCESS << ", " << SQL_SUCCESS_WITH_INFO << endl;

          // execute query
          status = SQLExecute(stmt);
          if (SQL_OK) {
            cout << "OK" << endl;
          }
          else {
            cout << "-bad execute-" << endl;
          }

          status = SQLFetch(stmt);
          if (SQL_FAIL) {
            SQLFreeStmt(stmt, SQL_CLOSE);
            return NULL;
          }

          SQLLEN is_null;
          char value[VARCHAR_MAX];
          status = SQLGetData(stmt, 2, SQL_C_CHAR, &value, VARCHAR_MAX, &is_null);
          if (SQL_FAIL) {
            SQLFreeStmt(stmt, SQL_CLOSE);
            return NULL;
          }

          cout << "-- value=" << value << endl;
        }
        SQLFreeStmt(stmt, SQL_CLOSE);
      }
      else {
        cout << "-bad statement-" << endl;
      }
    }
    else {
      cout << "-bad statement-" << endl;
    }
    SQLDisconnect(conn);
    SQLFreeHandle(SQL_HANDLE_DBC, conn);
  }
  else {
    cout << "-bad conn-" << endl;
  }

  // clean up
  SQLFreeHandle(SQL_HANDLE_ENV, env);

/*
	cout << sizeof(SQLSMALLINT) << endl;
	cout << sizeof(SQLUINTEGER) << endl;
	cout << sizeof(SQL_TIMESTAMP_STRUCT) << endl;
	cout << sizeof(SQL_TIME_STRUCT) << endl;
*/

  return 0;
}
