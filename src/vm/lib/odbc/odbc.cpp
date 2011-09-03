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
    
    /*
    int size = APITools_GetArgumentCount(context);
    cout << size << endl;
    cout << APITools_GetIntValue(context, 1) << endl;
    cout << APITools_GetFloatValue(context, 2) << endl;
    APITools_SetFloatValue(context, 2, 13.5);
    APITools_SetIntValue(context, 0, 20);
    
    cout << "---0---" << endl;
    
//    APITools_PushInt(context, 13);
    APITools_PushFloat(context, 1112.11);
    APITools_CallMethod(context, NULL, "System.$Float:PrintLine:f,");
    
    cout << "---1---" << endl;
    */
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
  }
}
