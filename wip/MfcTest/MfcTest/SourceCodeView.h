#pragma once


// SourceCodeView view


template<> UINT AFXAPI HashKey( CString& key );

class SourceCodeView : public CRichEditView
{
	DECLARE_DYNCREATE(SourceCodeView)
  CMap<CString, CString&, int, int&> mTokenColors;

  void ParseLine(TCHAR* buffer, int offset, int numRead);

protected:
	SourceCodeView();           // protected constructor used by dynamic creation
	virtual ~SourceCodeView();

public:
#ifdef _DEBUG
	virtual void AssertValid() const;
#ifndef _WIN32_WCE
	virtual void Dump(CDumpContext& dc) const;
#endif
#endif

protected:
  DECLARE_MESSAGE_MAP()
public:
  afx_msg void OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags);
  afx_msg void OnEditPaste();
};


