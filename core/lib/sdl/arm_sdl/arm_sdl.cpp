#include "arm_sdl.h"

/*
	To test the library, include "arm_sdl.h" from an application project
	and call arm_sdlTest().
	
	Do not forget to add the library to Project Dependencies in Visual Studio.
*/

static int s_Test = 0;

int arm_sdlTest()
{
	return ++s_Test;
}