// BuildView.cpp : implementation file
//

#include "stdafx.h"
#include "MfcTest.h"
#include "BuildView.h"


// BuildView

IMPLEMENT_DYNCREATE(BuildView, CListView)

BuildView::BuildView()
{

}

BuildView::~BuildView()
{
}

BEGIN_MESSAGE_MAP(BuildView, CListView)
END_MESSAGE_MAP()


// BuildView diagnostics

#ifdef _DEBUG
void BuildView::AssertValid() const
{
	CListView::AssertValid();
}

#ifndef _WIN32_WCE
void BuildView::Dump(CDumpContext& dc) const
{
	CListView::Dump(dc);
}
#endif
#endif //_DEBUG


// BuildView message handlers
