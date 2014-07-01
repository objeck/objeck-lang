//////////////////////////////////////////////////////////////////////////////
// Original authors:  Wyo, John Labenski, Otto Wyss
// Copyright: (c) wxGuide, (c) John Labenski, Otto Wyss
// Modified by: Randy Hollines
// Licence: wxWindows licence
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
#include "wx/fdrepdlg.h"
#include "wx/hashmap.h"
#include "wx/stack.h"

//! application headers
#include "prefs.h"       // preferences

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

  void ClosePage(Edit* edit);
  
public:
  Notebook(wxWindow *parent, wxWindowID id = wxID_ANY, const wxPoint &pos = wxDefaultPosition,
    const wxSize &size = wxDefaultSize, long style = wxAUI_NB_DEFAULT_STYLE);
  ~Notebook();

  Edit* GetEdit();
  void OpenFile(wxString& fn);
  void CloseAll();

  // event handlers
  void OnEdit(wxCommandEvent &event);
  void OnPageClose(wxAuiNotebookEvent& event);
  void OnDisplayEOL(wxCommandEvent &event);
  void OnIndentGuide(wxCommandEvent &event);
  void OnLineNumber(wxCommandEvent &event);
  void OnLongLineOn(wxCommandEvent &event);
  void OnWhiteSpace(wxCommandEvent &event);
  
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
    wxFindReplaceDialog* m_findReplace;
    wxFindReplaceData m_FindData;
    int m_foundStart;
    bool m_modified;

    bool FindText(int &found_start, int &found_end, bool find_next);
    void ReplaceText(const wxString &find_string);

    static wxString DecodeFindDialogEventFlags(int flags)
    {
      wxString str;
      str << (flags & wxFR_DOWN ? wxT("down") : wxT("up")) << wxT(", ")
        << (flags & wxFR_WHOLEWORD ? wxT("whole words only, ") : wxT(""))
        << (flags & wxFR_MATCHCASE ? wxT("") : wxT("not "))
        << wxT("case sensitive");

      return str;
    }

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

    void ShowEOL() {
      SetViewEOL(!GetViewEOL());
    }

    void ShowIndentGuide() {
      SetIndentationGuides(!GetIndentationGuides());
    }

    void ShowLineNumbers() {
      SetMarginWidth(m_LineNrID, GetMarginWidth(m_LineNrID) == 0 ? m_LineNrMargin : 0);
    }

    void ShowLongLines() {
      SetEdgeMode(GetEdgeMode() == 0 ? wxSTC_EDGE_LINE : wxSTC_EDGE_NONE);
    }

    void ShowWhiteSpace() {
      SetViewWhiteSpace(GetViewWhiteSpace() == 0 ? wxSTC_WS_VISIBLEALWAYS : wxSTC_WS_INVISIBLE);
    }

    void FoldToggle() {
      ToggleFold(GetFoldParent(GetCurrentLine()));
    }

    void OverType() {
      SetOvertype(!GetOvertype());
    }

    void ReadOnly() {
      SetReadOnly(!GetReadOnly());
    }

    void WrapmodeOn() {
      SetWrapMode(GetWrapMode() == 0 ? wxSTC_WRAP_WORD : wxSTC_WRAP_NONE);
    }

    // event handlers
    void OnSize(wxSizeEvent &event);
    // edit
    void OnEditRedo(wxCommandEvent &event);
    void OnEditUndo(wxCommandEvent &event);
    void OnEditClear(wxCommandEvent &event);
    void OnEditCut(wxCommandEvent &event);
    void OnEditCopy(wxCommandEvent &event);
    void OnEditPaste(wxCommandEvent &event);
    void OnBraceMatch(wxCommandEvent &event);
    void OnGoto(wxCommandEvent &event);
    void OnEditIndentInc(wxCommandEvent &event);
    void OnEditIndentRed(wxCommandEvent &event);
    void OnEditSelectAll(wxCommandEvent &event);
    void OnEditSelectLine(wxCommandEvent &event);
    // find & replace
    void OnFindReplace(wxCommandEvent &event);
    void OnFindReplaceDialog(wxFindDialogEvent& event);
    //! view
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
    void OnModified(wxStyledTextEvent &event);
    
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
