#pragma once


// BuildView view

class BuildView : public CListView
{
	DECLARE_DYNCREATE(BuildView)

protected:
	BuildView();           // protected constructor used by dynamic creation
	virtual ~BuildView();

public:
#ifdef _DEBUG
	virtual void AssertValid() const;
#ifndef _WIN32_WCE
	virtual void Dump(CDumpContext& dc) const;
#endif
#endif

protected:
	DECLARE_MESSAGE_MAP()
};


