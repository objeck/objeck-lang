// MfcTestView.h : interface of the CMfcTestView class
//


#pragma once


class CMfcTestView : public CView
{
protected: // create from serialization only
	CMfcTestView();
	DECLARE_DYNCREATE(CMfcTestView)

// Attributes
public:
	CMfcTestDoc* GetDocument() const;

// Operations
public:

// Overrides
public:
	virtual void OnDraw(CDC* pDC);  // overridden to draw this view
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
protected:
	virtual BOOL OnPreparePrinting(CPrintInfo* pInfo);
	virtual void OnBeginPrinting(CDC* pDC, CPrintInfo* pInfo);
	virtual void OnEndPrinting(CDC* pDC, CPrintInfo* pInfo);

// Implementation
public:
	virtual ~CMfcTestView();
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

protected:

// Generated message map functions
protected:
	DECLARE_MESSAGE_MAP()
};

#ifndef _DEBUG  // debug version in MfcTestView.cpp
inline CMfcTestDoc* CMfcTestView::GetDocument() const
   { return reinterpret_cast<CMfcTestDoc*>(m_pDocument); }
#endif

