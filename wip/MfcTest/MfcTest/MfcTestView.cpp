// MfcTestView.cpp : implementation of the CMfcTestView class
//

#include "stdafx.h"
#include "MfcTest.h"

#include "MfcTestDoc.h"
#include "MfcTestView.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// CMfcTestView

IMPLEMENT_DYNCREATE(CMfcTestView, CView)

BEGIN_MESSAGE_MAP(CMfcTestView, CView)
	// Standard printing commands
	ON_COMMAND(ID_FILE_PRINT, &CView::OnFilePrint)
	ON_COMMAND(ID_FILE_PRINT_DIRECT, &CView::OnFilePrint)
	ON_COMMAND(ID_FILE_PRINT_PREVIEW, &CView::OnFilePrintPreview)
END_MESSAGE_MAP()

// CMfcTestView construction/destruction

CMfcTestView::CMfcTestView()
{
	// TODO: add construction code here

}

CMfcTestView::~CMfcTestView()
{
}

BOOL CMfcTestView::PreCreateWindow(CREATESTRUCT& cs)
{
	// TODO: Modify the Window class or styles here by modifying
	//  the CREATESTRUCT cs

	return CView::PreCreateWindow(cs);
}

// CMfcTestView drawing

void CMfcTestView::OnDraw(CDC* /*pDC*/)
{
	CMfcTestDoc* pDoc = GetDocument();
	ASSERT_VALID(pDoc);
	if (!pDoc)
		return;

	// TODO: add draw code for native data here
}


// CMfcTestView printing

BOOL CMfcTestView::OnPreparePrinting(CPrintInfo* pInfo)
{
	// default preparation
	return DoPreparePrinting(pInfo);
}

void CMfcTestView::OnBeginPrinting(CDC* /*pDC*/, CPrintInfo* /*pInfo*/)
{
	// TODO: add extra initialization before printing
}

void CMfcTestView::OnEndPrinting(CDC* /*pDC*/, CPrintInfo* /*pInfo*/)
{
	// TODO: add cleanup after printing
}


// CMfcTestView diagnostics

#ifdef _DEBUG
void CMfcTestView::AssertValid() const
{
	CView::AssertValid();
}

void CMfcTestView::Dump(CDumpContext& dc) const
{
	CView::Dump(dc);
}

CMfcTestDoc* CMfcTestView::GetDocument() const // non-debug version is inline
{
	ASSERT(m_pDocument->IsKindOf(RUNTIME_CLASS(CMfcTestDoc)));
	return (CMfcTestDoc*)m_pDocument;
}
#endif //_DEBUG


// CMfcTestView message handlers
