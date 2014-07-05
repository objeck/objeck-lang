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
#include "wx/stc/stc.h"  // styled text control
#include <wx/fdrepdlg.h>

//! application headers
#include "prefs.h"       // preferences
// #include "dialogs.h"       // preferences

//----------------------------------------------------------------------------
//! Edit
class Edit: public wxStyledTextCtrl {
    friend class EditProperties;
    friend class EditPrint;

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
    // common
    void OnSize( wxSizeEvent &event );
    // edit
    void OnEditRedo (wxCommandEvent &event);
    void OnEditUndo (wxCommandEvent &event);
    void OnEditClear (wxCommandEvent &event);
    void OnEditCut (wxCommandEvent &event);
    void OnEditCopy (wxCommandEvent &event);
    void OnEditPaste (wxCommandEvent &event);
    // find
    void OnFind (wxCommandEvent &event);
    void OnFindNext (wxCommandEvent &event);
    void OnReplace (wxCommandEvent &event);
    void OnReplaceNext (wxCommandEvent &event);
    void OnFindDialog(wxFindDialogEvent& event);

    void OnBraceMatch (wxCommandEvent &event);
    void OnGoto (wxCommandEvent &event);
    void OnEditIndentInc (wxCommandEvent &event);
    void OnEditIndentRed (wxCommandEvent &event);
    void OnEditSelectAll (wxCommandEvent &event);
    void OnEditSelectLine (wxCommandEvent &event);
    //! view
    void OnHilightLang (wxCommandEvent &event);
    void OnDisplayEOL (wxCommandEvent &event);
    void OnIndentGuide (wxCommandEvent &event);
    void OnLineNumber (wxCommandEvent &event);
    void OnLongLineOn (wxCommandEvent &event);
    void OnWhiteSpace (wxCommandEvent &event);
    void OnFoldToggle (wxCommandEvent &event);
    void OnSetOverType (wxCommandEvent &event);
    void OnSetReadOnly (wxCommandEvent &event);
    void OnWrapmodeOn (wxCommandEvent &event);
    void OnUseCharset (wxCommandEvent &event);
    //! extra
    void OnChangeCase (wxCommandEvent &event);
    void OnConvertEOL (wxCommandEvent &event);
    // stc
    void OnMarginClick (wxStyledTextEvent &event);
    void OnCharAdded  (wxStyledTextEvent &event);
    void OnKey  (wxStyledTextEvent &event);

    //! language/lexer
    wxString DeterminePrefs (const wxString &filename);
    bool InitializePrefs (const wxString &filename);
    bool UserSettings (const wxString &filename);
    LanguageInfo const* GetLanguageInfo () {return m_language;};

    //! load/save file
    bool LoadFile ();
    bool LoadFile (const wxString &filename);
    bool SaveFile ();
    bool SaveFile (const wxString &filename);
    bool Modified ();
    wxString GetFilename () {return m_filename;};
    void SetFilename (const wxString &filename) {m_filename = filename;};

private:
    // file
    wxString m_filename;

    // lanugage properties
    LanguageInfo const* m_language;

    // margin variables
    int m_LineNrID;
    int m_LineNrMargin;
    int m_FoldingID;
    int m_FoldingMargin;
    int m_DividerID;

    wxFindReplaceData m_findData;
    wxFindReplaceDialog *m_dlgFind, *m_dlgReplace;
    
    int FindLine(wxFindDialogEvent& event) {
    	const wxString find = event.GetFindString();
    	
		const long minPos = GetCurrentPos();
		const long maxPos = GetLastPosition();
		const int flags = event.GetFlags();
		
		if(flags & wxFR_DOWN) {
		  return FindText(minPos, maxPos, find, flags);
		}
		
		return FindText(minPos - find.size(), 0, find, flags);
	}
	
    DECLARE_EVENT_TABLE()
};

#endif // _EDIT_H_
