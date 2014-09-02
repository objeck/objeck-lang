// MfcTest.h : main header file for the MfcTest application
//
#pragma once

#ifndef __AFXWIN_H__
	#error "include 'stdafx.h' before including this file for PCH"
#endif

#include "resource.h"       // main symbols


// CMfcTestApp:
// See MfcTest.cpp for the implementation of this class
//

class CMfcTestApp : public CWinApp
{
public:
	CMfcTestApp();


// Overrides
public:
	virtual BOOL InitInstance();

// Implementation
	afx_msg void OnAppAbout();
	DECLARE_MESSAGE_MAP()
};

extern CMfcTestApp theApp;