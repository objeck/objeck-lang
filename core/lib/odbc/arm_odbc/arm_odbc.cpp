#include "arm_odbc.h"

/*
	To test the library, include "arm_odbc.h" from an application project
	and call arm_odbcTest().
	
	Do not forget to add the library to Project Dependencies in Visual Studio.
*/

static int s_Test = 0;

int arm_odbcTest()
{
	return ++s_Test;
}