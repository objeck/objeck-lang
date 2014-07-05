//////////////////////////////////////////////////////////////////////////////
// File:        dialog.h
// Purpose:     STC test module
// Maintainer:  Hollines
// Created:     2014
// Copyright:   (c) R. Hollines
// Licence:     
//////////////////////////////////////////////////////////////////////////////

#ifndef _DIALOG_H_
#define _DIALOG_H_

//! wxWidgets headers

//! wxWidgets/contrib headers
#include "wx/stc/stc.h"  // styled text control

//! application headers
#include "prefs.h"       // preferences
#include "edit.h"       // preferences

#if wxUSE_PRINTING_ARCHITECTURE

//----------------------------------------------------------------------------
//! EditPrint
class EditPrint : public wxPrintout {

public:

  //! constructor
  EditPrint(Edit *edit, const wxChar *title = wxT(""));

  //! event handlers
  bool OnPrintPage(int page);
  bool OnBeginDocument(int startPage, int endPage);

  //! print functions
  bool HasPage(int page);
  void GetPageInfo(int *minPage, int *maxPage, int *selPageFrom, int *selPageTo);

private:
  Edit *m_edit;
  int m_printed;
  wxRect m_pageRect;
  wxRect m_printRect;

  bool PrintScaling(wxDC *dc);
};

#endif // wxUSE_PRINTING_ARCHITECTURE

//----------------------------------------------------------------------------
//! EditProperties
class EditProperties : public wxDialog {

public:

  //! constructor
  EditProperties(Edit *edit, long style = 0);

private:

};

#endif