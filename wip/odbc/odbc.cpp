#include "odbc.h"

using namespace std;
using namespace odbc;

int main() {
  // setup environment
  SQLHENV env;
  SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);
  SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (void*)SQL_OV_ODBC3, 0);
  
  ODBCClient client(env, "test", "root", "");
  string sql = "select * from test.student";
  ResultSet result = client.ExecuteSelect(sql);
  if(result.IsGood()) {
    while(result.Next()) {
      cout << "value=" << result.GetLong(1) << endl;
      cout << "value=" << result.GetString(2) << endl;
    }
  }
  
  // clean up
  SQLFreeHandle(SQL_HANDLE_ENV, env);
  return 0;
}
