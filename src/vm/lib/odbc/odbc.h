#ifndef __ODBC_H__
#define __ODBC_H__

#include <sql.h>
#include <sqlext.h>
#include "../../../vm/lib_api.h"

#define SQL_OK status == SQL_SUCCESS || status == SQL_SUCCESS_WITH_INFO
#define SQL_FAIL status != SQL_SUCCESS && status != SQL_SUCCESS_WITH_INFO
#define COL_NAME_MAX 64
#define VARCHAR_MAX 1024

static SQLHENV env;

#endif
