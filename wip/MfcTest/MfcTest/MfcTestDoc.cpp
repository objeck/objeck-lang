// MfcTestDoc.cpp : implementation of the CMfcTestDoc class
//

#include "stdafx.h"
#include "MfcTest.h"

#include "MfcTestDoc.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// CMfcTestDoc

IMPLEMENT_DYNCREATE(CMfcTestDoc, CDocument)

BEGIN_MESSAGE_MAP(CMfcTestDoc, CDocument)
END_MESSAGE_MAP()


// CMfcTestDoc construction/destruction

CMfcTestDoc::CMfcTestDoc()
{
	// TODO: add one-time construction code here

}

CMfcTestDoc::~CMfcTestDoc()
{
}

BOOL CMfcTestDoc::OnNewDocument()
{
	if (!CDocument::OnNewDocument())
		return FALSE;

	// TODO: add reinitialization code here
	// (SDI documents will reuse this document)

	return TRUE;
}




// CMfcTestDoc serialization

void CMfcTestDoc::Serialize(CArchive& ar)
{
	if (ar.IsStoring())
	{
		// TODO: add storing code here
	}
	else
	{
		// TODO: add loading code here
	}
}


// CMfcTestDoc diagnostics

#ifdef _DEBUG
void CMfcTestDoc::AssertValid() const
{
	CDocument::AssertValid();
}

void CMfcTestDoc::Dump(CDumpContext& dc) const
{
	CDocument::Dump(dc);
}
#endif //_DEBUG


// CMfcTestDoc commands
