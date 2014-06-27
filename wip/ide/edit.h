//////////////////////////////////////////////////////////////////////////////
// File:        edit.h
// Purpose:     STC test module
// Maintainer:  Wyo
// Created:     2003-09-01
// Copyright:   (c) wxGuide
// Licence:     wxWindows licence
//////////////////////////////////////////////////////////////////////////////

#ifndef _EDIT_H_
#define _EDIT_H_

//----------------------------------------------------------------------------
// informations
//----------------------------------------------------------------------------


//----------------------------------------------------------------------------
// headers
//----------------------------------------------------------------------------

//! wxWidgets headers

//! wxWidgets/contrib headers
#include "wx/stc/stc.h"
#include "wx/aui/aui.h"
#include "wx/hashmap.h"
#include "wx/stack.h"

//! application headers
#include "prefs.h"       // preferences

#define PAGE_MAX_CLEAN_UP 5

//============================================================================
// declarations
//============================================================================

class Edit;
class EditProperties;

//----------------------------------------------------------------------------
//! Notebook
class Notebook : public wxAuiNotebook {
  WX_DECLARE_STRING_HASH_MAP(Edit*, Pages);
  Pages pages;

  

public:
  Notebook(wxWindow *parent, wxWindowID id = wxID_ANY, const wxPoint &pos = wxDefaultPosition,
    const wxSize &size = wxDefaultSize, long style = wxAUI_NB_DEFAULT_STYLE);

  ~Notebook();

  void OpenFile(wxString& fn);

  // event handlers
  void OnEdit(wxCommandEvent &event);
  void OnPageClose(wxAuiNotebookEvent& event);
  void OnPageChanged(wxAuiNotebookEvent& event);
  
  DECLARE_EVENT_TABLE()
};

//----------------------------------------------------------------------------
//! Edit
class Edit : public wxStyledTextCtrl {
    friend class EditProperties;

    enum
    {
      margin_id_lineno,
      margin_id_fold,
      MARGIN_FOLD
    };

    wxString m_filename;
    LanguageInfo const* m_language;
    int m_LineNrID;
    int m_LineNrMargin;
    int m_FoldingID;
    int m_FoldingMargin;
    int m_DividerID;

    DECLARE_EVENT_TABLE()

public:
    //! constructor
    Edit (wxWindow *parent, wxWindowID id = wxID_ANY,
          const wxPoint &pos = wxDefaultPosition,
          const wxSize &size = wxDefaultSize,
          long style =
#ifndef __WXMAC__
          wxSUNKEN_BORDER|
#endif
          wxVSCROLL
         );

    //! destructor
    ~Edit ();

    // event handlers
    void OnSize(wxSizeEvent &event);
    // edit
    void OnEditRedo(wxCommandEvent &event);
    void OnEditUndo(wxCommandEvent &event);
    void OnEditClear(wxCommandEvent &event);
    void OnEditCut(wxCommandEvent &event);
    void OnEditCopy(wxCommandEvent &event);
    void OnEditPaste(wxCommandEvent &event);
    // find
    void OnFind(wxCommandEvent &event);
    void OnFindNext(wxCommandEvent &event);
    void OnReplace(wxCommandEvent &event);
    void OnReplaceNext(wxCommandEvent &event);
    void OnBraceMatch(wxCommandEvent &event);
    void OnGoto(wxCommandEvent &event);
    void OnEditIndentInc(wxCommandEvent &event);
    void OnEditIndentRed(wxCommandEvent &event);
    void OnEditSelectAll(wxCommandEvent &event);
    void OnEditSelectLine(wxCommandEvent &event);
    //! view
    void OnHilightLang(wxCommandEvent &event);
    void OnDisplayEOL(wxCommandEvent &event);
    void OnIndentGuide(wxCommandEvent &event);
    void OnLineNumber(wxCommandEvent &event);
    void OnLongLineOn(wxCommandEvent &event);
    void OnWhiteSpace(wxCommandEvent &event);
    void OnFoldToggle(wxCommandEvent &event);
    void OnSetOverType(wxCommandEvent &event);
    void OnSetReadOnly(wxCommandEvent &event);
    void OnWrapmodeOn(wxCommandEvent &event);
    void OnUseCharset(wxCommandEvent &event);
    // annotations
    void OnAnnotationAdd(wxCommandEvent& event);
    void OnAnnotationRemove(wxCommandEvent& event);
    void OnAnnotationClear(wxCommandEvent& event);
    void OnAnnotationStyle(wxCommandEvent& event);
    //! extra
    void OnChangeCase(wxCommandEvent &event);
    void OnConvertEOL(wxCommandEvent &event);
    // stc
    void OnMarginClick(wxStyledTextEvent &event);
    void OnCharAdded(wxStyledTextEvent &event);
    void OnKey(wxStyledTextEvent &event);
    
    //! load/save file
    bool LoadFile();
    bool LoadFile(const wxString &filename);
    bool SaveFile();
    bool SaveFile(const wxString &filename);
    bool Modified();
    wxString GetFilename() { return m_filename; };
    void SetFilename(const wxString &filename) { m_filename = filename; };

    //! language/lexer
    wxString DeterminePrefs (const wxString &filename);
    bool InitializePrefs (const wxString &filename);
    bool UserSettings (const wxString &filename);
    LanguageInfo const* GetLanguageInfo () {return m_language;};
};

//----------------------------------------------------------------------------
//! EditProperties
class EditProperties: public wxDialog {

public:

    //! constructor
    EditProperties (Edit *edit, long style = 0);

private:

};

#endif // _EDIT_H_
