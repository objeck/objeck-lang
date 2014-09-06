// MfcTestDoc.h : interface of the CMfcTestDoc class
//


#pragma once


class CMfcTestDoc : public CDocument
{
protected: // create from serialization only
	CMfcTestDoc();
	DECLARE_DYNCREATE(CMfcTestDoc)

// Attributes
public:

// Operations
public:

// Overrides
public:
	virtual BOOL OnNewDocument();
	virtual void Serialize(CArchive& ar);

// Implementation
public:
	virtual ~CMfcTestDoc();
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

protected:

// Generated message map functions
protected:
	DECLARE_MESSAGE_MAP()
};


