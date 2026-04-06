/***************************************************************************
 * Copyright (c) 2020-2023, Randy Hollines
 * All rights reserved.
***************************************************************************/

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Wininet.lib")

#pragma once

#include "targetver.h"
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers

// Windows Header Files
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#ifdef _MSYS2
#include <shlobj.h>
#else
#include <shlobj_core.h>
#endif

#include <shlwapi.h>
#include <string>
#include <iostream>
#include <wininet.h>
#include <tlhelp32.h>
#include <process.h>
#include "../../../shared/sys.h"
#include "../../../shared/version.h"

// C RunTime Header Files
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>