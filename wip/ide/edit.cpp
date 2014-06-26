//////////////////////////////////////////////////////////////////////////////
// File:        contrib/samples/stc/edit.cpp
// Purpose:     STC test module
// Maintainer:  Wyo
// Created:     2003-09-01
// Copyright:   (c) wxGuide
// Licence:     wxWindows licence
//////////////////////////////////////////////////////////////////////////////

//----------------------------------------------------------------------------
// informations
//----------------------------------------------------------------------------


//----------------------------------------------------------------------------
// headers
//----------------------------------------------------------------------------

// For compilers that support precompilation, includes "wx/wx.h".
#include "wx/wxprec.h"

#ifdef __BORLANDC__
    #pragma hdrstop
#endif

// for all others, include the necessary headers (this file is usually all you
// need because it includes almost all 'standard' wxWidgets headers)
#ifndef WX_PRECOMP
    #include "wx/wx.h"
    #include "wx/textdlg.h"
#endif

//! wxWidgets headers
#include "wx/file.h"     // raw file io support
#include "wx/filename.h" // filename support

//! application headers
#include "defsext.h"     // additional definitions

#include "edit.h"        // edit module


//----------------------------------------------------------------------------
// resources
//----------------------------------------------------------------------------


//============================================================================
// declarations
//============================================================================

// The (uniform) style used for the annotations.
const int ANNOTATION_STYLE = wxSTC_STYLE_LASTPREDEFINED + 1;

//============================================================================
// implementation
//============================================================================

//----------------------------------------------------------------------------
// Notebook
//----------------------------------------------------------------------------

BEGIN_EVENT_TABLE(Notebook, wxAuiNotebook)
    // pages
    EVT_AUINOTEBOOK_PAGE_CLOSE(wxID_ANY, OnPageClose)
    EVT_AUINOTEBOOK_PAGE_CHANGED(wxID_ANY, OnPageChanged)
    // edit
    EVT_MENU(wxID_CLEAR, Notebook::OnEditClear)
    EVT_MENU(wxID_CUT, Notebook::OnEditCut)
    EVT_MENU(wxID_COPY, Notebook::OnEditCopy)
    EVT_MENU(wxID_PASTE, Notebook::OnEditPaste)
    EVT_MENU(myID_INDENTINC, Notebook::OnEditIndentInc)
    EVT_MENU(myID_INDENTRED, Notebook::OnEditIndentRed)
    EVT_MENU(wxID_SELECTALL, Notebook::OnEditSelectAll)
    
    EVT_MENU(wxID_REDO, Notebook::OnEditRedo)
    EVT_MENU(wxID_UNDO, Notebook::OnEditUndo)
    // find
    EVT_MENU(wxID_FIND, Notebook::OnFind)
    EVT_MENU(myID_FINDNEXT, Notebook::OnFindNext)
    EVT_MENU(myID_REPLACE, Notebook::OnReplace)
    EVT_MENU(myID_REPLACENEXT, Notebook::OnReplaceNext)
    EVT_MENU(myID_GOTO, Notebook::OnGoto)
    // view
    EVT_MENU_RANGE(myID_HILIGHTFIRST, myID_HILIGHTLAST, Notebook::OnHilightLang)
    EVT_MENU(myID_DISPLAYEOL, Notebook::OnDisplayEOL)
    EVT_MENU(myID_INDENTGUIDE, Notebook::OnIndentGuide)
    EVT_MENU(myID_LONGLINEON, Notebook::OnLongLineOn)
    EVT_MENU(myID_WHITESPACE, Notebook::OnWhiteSpace)
    EVT_MENU(myID_FOLDTOGGLE, Notebook::OnFoldToggle)
    EVT_MENU(myID_OVERTYPE, Notebook::OnSetOverType)
    EVT_MENU(myID_READONLY, Notebook::OnSetReadOnly)
    EVT_MENU(myID_WRAPMODEON, Notebook::OnWrapmodeOn)
    EVT_MENU(myID_CHARSETANSI, Notebook::OnUseCharset)
    EVT_MENU(myID_CHARSETMAC, Notebook::OnUseCharset)
    // annotations
    EVT_MENU(myID_ANNOTATION_ADD, Notebook::OnAnnotationAdd)
    EVT_MENU(myID_ANNOTATION_REMOVE, Notebook::OnAnnotationRemove)
    EVT_MENU(myID_ANNOTATION_CLEAR, Notebook::OnAnnotationClear)
    EVT_MENU(myID_ANNOTATION_STYLE_HIDDEN, Notebook::OnAnnotationStyle)
    EVT_MENU(myID_ANNOTATION_STYLE_STANDARD, Notebook::OnAnnotationStyle)
    EVT_MENU(myID_ANNOTATION_STYLE_BOXED, Notebook::OnAnnotationStyle)
    // stc
    EVT_STC_MARGINCLICK(wxID_ANY, Notebook::OnMarginClick)
    EVT_STC_CHARADDED(wxID_ANY, Notebook::OnCharAdded)
    EVT_STC_KEY(wxID_ANY, Notebook::OnKey)
    // extra
    EVT_MENU(myID_CHANGELOWER, Notebook::OnChangeCase)
    EVT_MENU(myID_CHANGEUPPER, Notebook::OnChangeCase)
    EVT_MENU(myID_CONVERTCR, Notebook::OnConvertEOL)
    EVT_MENU(myID_CONVERTCRLF, Notebook::OnConvertEOL)
    EVT_MENU(myID_CONVERTLF, Notebook::OnConvertEOL)
END_EVENT_TABLE()

Notebook::Notebook(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size, long style) : 
  wxAuiNotebook(parent, id, pos, size, style)
{

}

Notebook::~Notebook()
{

}

void Notebook::OnPageClose(wxAuiNotebookEvent& event)
{
  Edit* edit = static_cast<Edit*>(GetPage(GetSelection()));
  if (edit) {
    if (edit->Modified()) {
      if (wxMessageBox(_("Text is not saved, save before closing?"), _("Close"),
        wxYES_NO | wxICON_QUESTION) == wxYES) {
        edit->SaveFile();
        if (edit->Modified()) {
          wxMessageBox(_("Text could not be saved!"), _("Close abort"),
            wxOK | wxICON_EXCLAMATION);
          return;
        }
      }
    }
    pages.erase(edit->GetFilename());
    edit->SetFilename(wxEmptyString);
    edit->ClearAll();
    edit->SetSavePoint();
  }
}

void Notebook::OnPageChanged(wxAuiNotebookEvent& event)
{
  int type = event.GetEventType();
}

void Notebook::OpenFile(wxString& fn)
{
  wxFileName w(fn); 
  w.Normalize(); 
  wxString filename = w.GetFullPath();

  // set page focus
  Pages::iterator found = pages.find(filename);
  if (found != pages.end()) {
    int page_index = GetPageIndex(found->second);
    if (page_index != wxNOT_FOUND) {
      SetSelection(page_index);
    }
  }
  // add new page and set focus
  else {
    Edit* edit = new Edit(this);    
    edit->LoadFile(filename);
    edit->SelectNone();
    const wxString page_name = w.GetName() + wxT('.') + w.GetExt();
    AddPage(edit, page_name);
    pages[filename] = edit;

    int page_index = GetPageIndex(edit);
    if (page_index != wxNOT_FOUND) {
      SetSelection(page_index);
    }
  }
}

//----------------------------------------------------------------------------
// common event handlers
void Notebook::OnConvertEOL(wxCommandEvent &event) {
  Edit* edit = static_cast<Edit*>(GetCurrentPage());
  if (!edit) {
    return;
  }

  int eolMode = edit->GetEOLMode();
    switch (event.GetId()) {
        case myID_CONVERTCR: { eolMode = wxSTC_EOL_CR; break;}
        case myID_CONVERTCRLF: { eolMode = wxSTC_EOL_CRLF; break;}
        case myID_CONVERTLF: { eolMode = wxSTC_EOL_LF; break;}
    }
    edit->ConvertEOLs(eolMode);
    edit->SetEOLMode(eolMode);
}



// edit event handlers
void Notebook::OnEditRedo(wxCommandEvent &WXUNUSED(event)) {
  Edit* edit = static_cast<Edit*>(GetCurrentPage());
  if (!edit) {
    return;
  }

  if (!edit->CanRedo()) return;
  edit->Redo();
}

void Notebook::OnEditUndo(wxCommandEvent &WXUNUSED(event)) {
  Edit* edit = static_cast<Edit*>(GetCurrentPage());
  if (!edit) {
    return;
  }

  if (!edit->CanUndo()) return;
  edit->Undo();
}

void Notebook::OnEditClear(wxCommandEvent &WXUNUSED(event)) {
  Edit* edit = static_cast<Edit*>(GetCurrentPage());
  if (!edit) {
    return;
  }

  if (edit->GetReadOnly()) return;
  edit->Clear();
}

void Notebook::OnKey(wxStyledTextEvent &WXUNUSED(event))
{
  wxMessageBox("OnKey");
}

void Notebook::OnEditCut(wxCommandEvent &WXUNUSED(event)) {
  Edit* edit = static_cast<Edit*>(GetCurrentPage());
  if (!edit) {
    return;
  }

  if (edit->GetReadOnly() || (edit->GetSelectionEnd() - edit->GetSelectionStart() <= 0)) return;
  edit->Cut();
}

void Notebook::OnEditCopy(wxCommandEvent &WXUNUSED(event)) {
  Edit* edit = static_cast<Edit*>(GetCurrentPage());
  if (!edit) {
    return;
  }

  if (edit->GetSelectionEnd() - edit->GetSelectionStart() <= 0) return;
  edit->Copy();
}

void Notebook::OnEditPaste(wxCommandEvent &WXUNUSED(event)) {
  Edit* edit = static_cast<Edit*>(GetCurrentPage());
  if (!edit) {
    return;
  }

  if (!edit->CanPaste()) return;
  edit->Paste();
}

void Notebook::OnFind(wxCommandEvent &WXUNUSED(event)) {
  Edit* edit = static_cast<Edit*>(GetCurrentPage());
  if (!edit) {
    return;
  }

}

void Notebook::OnFindNext(wxCommandEvent &WXUNUSED(event)) {
  Edit* edit = static_cast<Edit*>(GetCurrentPage());
  if (!edit) {
    return;
  }

}

void Notebook::OnReplace(wxCommandEvent &WXUNUSED(event)) {
  Edit* edit = static_cast<Edit*>(GetCurrentPage());
  if (!edit) {
    return;
  }

}

void Notebook::OnReplaceNext(wxCommandEvent &WXUNUSED(event)) {
  Edit* edit = static_cast<Edit*>(GetCurrentPage());
  if (!edit) {
    return;
  }

}

void Notebook::OnGoto(wxCommandEvent &WXUNUSED(event)) {
  Edit* edit = static_cast<Edit*>(GetCurrentPage());
  if (!edit) {
    return;
  }

}

void Notebook::OnEditIndentInc(wxCommandEvent &WXUNUSED(event)) {
  Edit* edit = static_cast<Edit*>(GetCurrentPage());
  if (!edit) {
    return;
  }

  edit->CmdKeyExecute(wxSTC_CMD_TAB);
}

void Notebook::OnEditIndentRed(wxCommandEvent &WXUNUSED(event)) {
  Edit* edit = static_cast<Edit*>(GetCurrentPage());
  if (!edit) {
    return;
  }

  edit->CmdKeyExecute(wxSTC_CMD_DELETEBACK);
}

void Notebook::OnEditSelectAll(wxCommandEvent &WXUNUSED(event)) {
  Edit* edit = static_cast<Edit*>(GetCurrentPage());
  if (!edit) {
    return;
  }

  edit->SetSelection(0, edit->GetTextLength());
}

void Notebook::OnHilightLang(wxCommandEvent &event) {
  Edit* edit = static_cast<Edit*>(GetCurrentPage());
  if (!edit) {
    return;
  }

  edit->InitializePrefs(g_LanguagePrefs[event.GetId() - myID_HILIGHTFIRST].name);
}

void Notebook::OnDisplayEOL(wxCommandEvent &WXUNUSED(event)) {
  Edit* edit = static_cast<Edit*>(GetCurrentPage());
  if (!edit) {
    return;
  }

  edit->SetViewEOL(!edit->GetViewEOL());
}

void Notebook::OnIndentGuide(wxCommandEvent &WXUNUSED(event)) {
  Edit* edit = static_cast<Edit*>(GetCurrentPage());
  if (!edit) {
    return;
  }

  edit->SetIndentationGuides(!edit->GetIndentationGuides());
}

void Notebook::OnLongLineOn(wxCommandEvent &WXUNUSED(event)) 
{
  Edit* edit = static_cast<Edit*>(GetCurrentPage());
  if (!edit) {
    return;
  }

  edit->SetEdgeMode(edit->GetEdgeMode() == 0 ? wxSTC_EDGE_LINE : wxSTC_EDGE_NONE);
}

void Notebook::OnWhiteSpace(wxCommandEvent &WXUNUSED(event)) 
{
  Edit* edit = static_cast<Edit*>(GetCurrentPage());
  if (!edit) {
    return;
  }

  edit->SetViewWhiteSpace(edit->GetViewWhiteSpace() == 0 ?
  wxSTC_WS_VISIBLEALWAYS : wxSTC_WS_INVISIBLE);
}

void Notebook::OnFoldToggle(wxCommandEvent &WXUNUSED(event)) 
{
  Edit* edit = static_cast<Edit*>(GetCurrentPage());
  if (!edit) {
    return;
  }

  edit->ToggleFold(edit->GetFoldParent(edit->GetCurrentLine()));
}

void Notebook::OnSetOverType(wxCommandEvent &WXUNUSED(event)) 
{
  Edit* edit = static_cast<Edit*>(GetCurrentPage());
  if (!edit) {
    return;
  }

  edit->SetOvertype(!edit->GetOvertype());
}

void Notebook::OnSetReadOnly(wxCommandEvent &WXUNUSED(event)) 
{
  Edit* edit = static_cast<Edit*>(GetCurrentPage());
  if (!edit) {
    return;
  }

  edit->SetReadOnly(!edit->GetReadOnly());
}

void Notebook::OnWrapmodeOn(wxCommandEvent &WXUNUSED(event)) 
{
  Edit* edit = static_cast<Edit*>(GetCurrentPage());
  if (!edit) {
    return;
  }

  edit->SetWrapMode(edit->GetWrapMode() == 0 ? wxSTC_WRAP_WORD : wxSTC_WRAP_NONE);
}

void Notebook::OnUseCharset(wxCommandEvent &event) 
{
  Edit* edit = static_cast<Edit*>(GetCurrentPage());
  if (!edit) {
    return;
  }

  int Nr;
  int charset = edit->GetCodePage();
  switch (event.GetId()) {
  case myID_CHARSETANSI: {charset = wxSTC_CHARSET_ANSI; break; }
  case myID_CHARSETMAC: {charset = wxSTC_CHARSET_ANSI; break; }
  }
  for (Nr = 0; Nr < wxSTC_STYLE_LASTPREDEFINED; Nr++) {
    edit->StyleSetCharacterSet(Nr, charset);
  }
  edit->SetCodePage(charset);
}

void Notebook::OnAnnotationAdd(wxCommandEvent& WXUNUSED(event))
{
  Edit* edit = static_cast<Edit*>(GetCurrentPage());
  if (!edit) {
    return;
  }

  const int line = edit->GetCurrentLine();

  wxString ann = edit->AnnotationGetText(line);
  ann = wxGetTextFromUser
    (
    wxString::Format("Enter annotation for the line %d", line),
    "Edit annotation",
    ann,
    this
    );
  if (ann.empty())
    return;

  edit->AnnotationSetText(line, ann);
  edit->AnnotationSetStyle(line, ANNOTATION_STYLE);

  // Scintilla doesn't update the scroll width for annotations, even with
  // scroll width tracking on, so do it manually.
  const int width = edit->GetScrollWidth();

  // NB: The following adjustments are only needed when using
  //     wxSTC_ANNOTATION_BOXED annotations style, but we apply them always
  //     in order to make things simpler and not have to redo the width
  //     calculations when the annotations visibility changes. In a real
  //     program you'd either just stick to a fixed annotations visibility or
  //     update the width when it changes.

  // Take into account the fact that the annotation is shown indented, with
  // the same indent as the line it's attached to.
  int indent = edit->GetLineIndentation(line);

  // This is just a hack to account for the width of the box, there doesn't
  // seem to be any way to get it directly from Scintilla.
  indent += 3;

  const int widthAnn = edit->TextWidth(ANNOTATION_STYLE, ann + wxString(indent, ' '));

  if (widthAnn > width)
    edit->SetScrollWidth(widthAnn);
}

void Notebook::OnAnnotationRemove(wxCommandEvent& WXUNUSED(event))
{
  Edit* edit = static_cast<Edit*>(GetCurrentPage());
  if (!edit) {
    return;
  }

  edit->AnnotationSetText(edit->GetCurrentLine(), wxString());
}

void Notebook::OnAnnotationClear(wxCommandEvent& WXUNUSED(event))
{
  Edit* edit = static_cast<Edit*>(GetCurrentPage());
  if (!edit) {
    return;
  }

  edit->AnnotationClearAll();
}

void Notebook::OnAnnotationStyle(wxCommandEvent& event)
{
  Edit* edit = static_cast<Edit*>(GetCurrentPage());
  if (!edit) {
    return;
  }

  int style = 0;
  switch (event.GetId()) {
  case myID_ANNOTATION_STYLE_HIDDEN:
    style = wxSTC_ANNOTATION_HIDDEN;
    break;

  case myID_ANNOTATION_STYLE_STANDARD:
    style = wxSTC_ANNOTATION_STANDARD;
    break;

  case myID_ANNOTATION_STYLE_BOXED:
    style = wxSTC_ANNOTATION_BOXED;
    break;
  }

  edit->AnnotationSetVisible(style);
}

void Notebook::OnChangeCase(wxCommandEvent &event) {
  Edit* edit = static_cast<Edit*>(GetCurrentPage());
  if (!edit) {
    return;
  }

  switch (event.GetId()) {
  case myID_CHANGELOWER: {
    edit->CmdKeyExecute(wxSTC_CMD_LOWERCASE);
    break;
  }
  case myID_CHANGEUPPER: {
    edit->CmdKeyExecute(wxSTC_CMD_UPPERCASE);
    break;
  }
  }
}

//! misc
void Notebook::OnMarginClick(wxStyledTextEvent &event) {
  Edit* edit = static_cast<Edit*>(GetCurrentPage());
  if (!edit) {
    return;
  }
  
  if (event.GetMargin() == 2) {
    int lineClick = edit->LineFromPosition(event.GetPosition());
    int levelClick = edit->GetFoldLevel(lineClick);
    if ((levelClick & wxSTC_FOLDLEVELHEADERFLAG) > 0) {
      edit->ToggleFold(lineClick);
    }
  }
}

void Notebook::OnCharAdded(wxStyledTextEvent &event) {
  Edit* edit = static_cast<Edit*>(GetCurrentPage());
  if (!edit) {
    return;
  }

  char chr = (char)event.GetKey();
  int currentLine = edit->GetCurrentLine();
  // Change this if support for mac files with \r is needed
  if (chr == '\n') {
    int lineInd = 0;
    if (currentLine > 0) {
      lineInd = edit->GetLineIndentation(currentLine - 1);
    }
    if (lineInd == 0) return;
    edit->SetLineIndentation(currentLine, lineInd);
    edit->GotoPos(edit->PositionFromLine(currentLine) + lineInd);
  }
}

//----------------------------------------------------------------------------
// Edit
//----------------------------------------------------------------------------

BEGIN_EVENT_TABLE (Edit, wxStyledTextCtrl)
    // common
    EVT_SIZE(Edit::OnSize)
    EVT_MENU(myID_SELECTLINE, Edit::OnEditSelectLine)
    EVT_MENU(myID_LINENUMBER, Edit::OnLineNumber)
    EVT_MENU(myID_BRACEMATCH, Edit::OnBraceMatch)
END_EVENT_TABLE()

Edit::Edit (wxWindow *parent, wxWindowID id,
            const wxPoint &pos,
            const wxSize &size,
            long style)
    : wxStyledTextCtrl (parent, id, pos, size, style) {

    m_filename = wxEmptyString;

    m_LineNrID = 0;
    m_DividerID = 1;
    m_FoldingID = 2;

    // initialize language
    m_language = NULL;

    // Use all the bits in the style byte as styles, not indicators.
    SetStyleBits(8);
    
    // default font for all styles
    SetViewEOL (g_CommonPrefs.displayEOLEnable);
    SetIndentationGuides (g_CommonPrefs.indentGuideEnable);
    SetEdgeMode (g_CommonPrefs.longLineOnEnable?
                 wxSTC_EDGE_LINE: wxSTC_EDGE_NONE);
    SetViewWhiteSpace (g_CommonPrefs.whiteSpaceEnable?
                       wxSTC_WS_VISIBLEALWAYS: wxSTC_WS_INVISIBLE);
    SetOvertype (g_CommonPrefs.overTypeInitial);
    SetReadOnly (g_CommonPrefs.readOnlyInitial);
    SetWrapMode (g_CommonPrefs.wrapModeInitial?
                 wxSTC_WRAP_WORD: wxSTC_WRAP_NONE);
    wxFont font (10, wxMODERN, wxNORMAL, wxNORMAL);
    StyleSetFont (wxSTC_STYLE_DEFAULT, font);
    StyleSetForeground (wxSTC_STYLE_DEFAULT, *wxBLACK);
    StyleSetBackground (wxSTC_STYLE_DEFAULT, *wxWHITE);
    StyleSetForeground (wxSTC_STYLE_LINENUMBER, wxColour (wxT("DARK GREY")));
    StyleSetBackground (wxSTC_STYLE_LINENUMBER, *wxWHITE);
    StyleSetForeground(wxSTC_STYLE_INDENTGUIDE, wxColour (wxT("DARK GREY")));
    InitializePrefs (DEFAULT_LANGUAGE);

    // set visibility
    SetVisiblePolicy (wxSTC_VISIBLE_STRICT|wxSTC_VISIBLE_SLOP, 1);
    SetXCaretPolicy (wxSTC_CARET_EVEN|wxSTC_VISIBLE_STRICT|wxSTC_CARET_SLOP, 1);
    SetYCaretPolicy (wxSTC_CARET_EVEN|wxSTC_VISIBLE_STRICT|wxSTC_CARET_SLOP, 1);

    // markers
    // ---- Enable code folding
    SetMarginType(MARGIN_FOLD, wxSTC_MARGIN_SYMBOL);
    SetMarginWidth(MARGIN_FOLD, 15);
    SetMarginMask(MARGIN_FOLD, wxSTC_MASK_FOLDERS);
    StyleSetBackground(MARGIN_FOLD, wxColor(200, 200, 200));
    SetMarginSensitive(MARGIN_FOLD, true);

    // Properties found from http://www.scintilla.org/SciTEDoc.html
    SetProperty(wxT("fold"), wxT("1"));
    SetProperty(wxT("fold.comment"), wxT("1"));
    SetProperty(wxT("fold.compact"), wxT("1"));

    
     SetProperty(wxT("fold"), wxT("1"));
    SetProperty(wxT("fold.comment"), wxT("1"));
    SetProperty(wxT("fold.compact"), wxT("1"));
    SetProperty(wxT("fold.preprocessor"), wxT("1"));
    SetProperty(wxT("fold.html"), wxT("1"));
    SetProperty(wxT("fold.html.preprocessor"), wxT("1"));

    SetMarginType(margin_id_lineno, wxSTC_MARGIN_NUMBER);
    SetMarginWidth(margin_id_lineno, 32);

    MarkerDefine(wxSTC_MARKNUM_FOLDER,        wxSTC_MARK_BOXPLUS, wxT("WHITE"), wxT("GREY"));
    MarkerDefine(wxSTC_MARKNUM_FOLDEROPEN,    wxSTC_MARK_BOXMINUS,  wxT("WHITE"), wxT("GREY"));
    MarkerDefine(wxSTC_MARKNUM_FOLDERSUB,     wxSTC_MARK_VLINE,     wxT("WHITE"), wxT("GREY"));
    MarkerDefine(wxSTC_MARKNUM_FOLDEREND,     wxSTC_MARK_BOXPLUSCONNECTED, wxT("WHITE"), wxT("GREY"));
    MarkerDefine(wxSTC_MARKNUM_FOLDEROPENMID, wxSTC_MARK_BOXMINUSCONNECTED, wxT("WHITE"), wxT("GREY"));
    MarkerDefine(wxSTC_MARKNUM_FOLDERMIDTAIL, wxSTC_MARK_TCORNER,     wxT("WHITE"), wxT("GREY"));
    MarkerDefine(wxSTC_MARKNUM_FOLDERTAIL,    wxSTC_MARK_LCORNER,     wxT("WHITE"), wxT("GREY"));

    SetMarginMask(margin_id_fold, wxSTC_MASK_FOLDERS);
    SetMarginWidth(margin_id_fold, 32);
    SetMarginSensitive(margin_id_fold, true);

    SetFoldFlags(wxSTC_FOLDFLAG_LINEBEFORE_CONTRACTED | wxSTC_FOLDFLAG_LINEAFTER_CONTRACTED);

    SetTabWidth(4);
    SetUseTabs(false);
    SetWrapMode(wxSTC_WRAP_WORD);
    SetWrapVisualFlags(wxSTC_WRAPVISUALFLAG_END);

    // annotations
    AnnotationSetVisible(wxSTC_ANNOTATION_BOXED);

    // miscellaneous
    m_LineNrMargin = TextWidth (wxSTC_STYLE_LINENUMBER, wxT("_999999"));
    m_FoldingMargin = 16;
    CmdKeyClear (wxSTC_KEY_TAB, 0); // this is done by the menu accelerator key
    SetLayoutCache (wxSTC_CACHE_PAGE);

}

Edit::~Edit () {}

//----------------------------------------------------------------------------
// common event handlers
void Edit::OnSize(wxSizeEvent& event) {
  int x = GetClientSize().x +
    (g_CommonPrefs.lineNumberEnable ? m_LineNrMargin : 0) +
    (g_CommonPrefs.foldEnable ? m_FoldingMargin : 0);
  if (x > 0) SetScrollWidth(x);
  event.Skip();
}

void Edit::OnEditSelectLine(wxCommandEvent &WXUNUSED(event)) {
  int lineStart = PositionFromLine(GetCurrentLine());
  int lineEnd = PositionFromLine(GetCurrentLine() + 1);
  SetSelection(lineStart, lineEnd);
}

void Edit::OnLineNumber(wxCommandEvent &WXUNUSED(event))
{
  SetMarginWidth(m_LineNrID, GetMarginWidth(m_LineNrID) == 0 ? m_LineNrMargin : 0);
}

void Edit::OnBraceMatch(wxCommandEvent &WXUNUSED(event)) {
  int min = GetCurrentPos();
  int max = BraceMatch(min);
  if (max > (min + 1)) {
    BraceHighlight(min + 1, max);
    SetSelection(min + 1, max);
  }
  else{
    BraceBadLight(min);
  }
}

//----------------------------------------------------------------------------
// private functions
wxString Edit::DeterminePrefs (const wxString &filename) {

    LanguageInfo const* curInfo;

    // determine language from filepatterns
    int languageNr;
    for (languageNr = 0; languageNr < g_LanguagePrefsSize; languageNr++) {
        curInfo = &g_LanguagePrefs [languageNr];
        wxString filepattern = curInfo->filepattern;
        filepattern.Lower();
        while (!filepattern.empty()) {
            wxString cur = filepattern.BeforeFirst (';');
            if ((cur == filename) ||
                (cur == (filename.BeforeLast ('.') + wxT(".*"))) ||
                (cur == (wxT("*.") + filename.AfterLast ('.')))) {
                return curInfo->name;
            }
            filepattern = filepattern.AfterFirst (';');
        }
    }
    return wxEmptyString;

}

bool Edit::InitializePrefs (const wxString &name) {

    // initialize styles
    StyleClearAll();
    LanguageInfo const* curInfo = NULL;

    // determine language
    bool found = false;
    int languageNr;
    for (languageNr = 0; languageNr < g_LanguagePrefsSize; languageNr++) {
        curInfo = &g_LanguagePrefs [languageNr];
        if (curInfo->name == name) {
            found = true;
            break;
        }
    }
    if (!found) return false;

    // set lexer and language
    SetLexer (curInfo->lexer);
    m_language = curInfo;

    // set margin for line numbers
    SetMarginType (m_LineNrID, wxSTC_MARGIN_NUMBER);
    StyleSetForeground (wxSTC_STYLE_LINENUMBER, wxColour (wxT("DARK GREY")));
    StyleSetBackground (wxSTC_STYLE_LINENUMBER, *wxWHITE);
    SetMarginWidth (m_LineNrID, 0); // start out not visible

    // annotations style
    StyleSetBackground(ANNOTATION_STYLE, wxColour(244, 220, 220));
    StyleSetForeground(ANNOTATION_STYLE, *wxBLACK);
    StyleSetSizeFractional(ANNOTATION_STYLE,
            (StyleGetSizeFractional(wxSTC_STYLE_DEFAULT)*4)/5);

    // default fonts for all styles!
    int Nr;
    for (Nr = 0; Nr < wxSTC_STYLE_LASTPREDEFINED; Nr++) {
        wxFont font (10, wxMODERN, wxNORMAL, wxNORMAL);
        StyleSetFont (Nr, font);
    }

    // set common styles
    StyleSetForeground (wxSTC_STYLE_DEFAULT, wxColour (wxT("DARK GREY")));
    StyleSetForeground (wxSTC_STYLE_INDENTGUIDE, wxColour (wxT("DARK GREY")));

    // initialize settings
    if (g_CommonPrefs.syntaxEnable) {
        int keywordnr = 0;
        for (Nr = 0; Nr < STYLE_TYPES_COUNT; Nr++) {
            if (curInfo->styles[Nr].type == -1) continue;
            const StyleInfo &curType = g_StylePrefs [curInfo->styles[Nr].type];
            wxFont font (curType.fontsize, wxMODERN, wxNORMAL, wxNORMAL, false,
                         curType.fontname);
            StyleSetFont (Nr, font);
            if (curType.foreground) {
                StyleSetForeground (Nr, wxColour (curType.foreground));
            }
            if (curType.background) {
                StyleSetBackground (Nr, wxColour (curType.background));
            }
            StyleSetBold (Nr, (curType.fontstyle & mySTC_STYLE_BOLD) > 0);
            StyleSetItalic (Nr, (curType.fontstyle & mySTC_STYLE_ITALIC) > 0);
            StyleSetUnderline (Nr, (curType.fontstyle & mySTC_STYLE_UNDERL) > 0);
            StyleSetVisible (Nr, (curType.fontstyle & mySTC_STYLE_HIDDEN) == 0);
            StyleSetCase (Nr, curType.lettercase);
            const char *pwords = curInfo->styles[Nr].words;
            if (pwords) {
                SetKeyWords (keywordnr, pwords);
                keywordnr += 1;
            }
        }
    }

    // set margin as unused
    SetMarginType (m_DividerID, wxSTC_MARGIN_SYMBOL);
    SetMarginWidth (m_DividerID, 0);
    SetMarginSensitive (m_DividerID, false);

    // folding
    SetMarginType (m_FoldingID, wxSTC_MARGIN_SYMBOL);
    SetMarginMask (m_FoldingID, wxSTC_MASK_FOLDERS);
    StyleSetBackground (m_FoldingID, *wxWHITE);
    SetMarginWidth (m_FoldingID, 0);
    SetMarginSensitive (m_FoldingID, false);
    if (g_CommonPrefs.foldEnable) {
        SetMarginWidth (m_FoldingID, curInfo->folds != 0? m_FoldingMargin: 0);
        SetMarginSensitive (m_FoldingID, curInfo->folds != 0);
        SetProperty (wxT("fold"), curInfo->folds != 0? wxT("1"): wxT("0"));
        SetProperty (wxT("fold.comment"),
                     (curInfo->folds & mySTC_FOLD_COMMENT) > 0? wxT("1"): wxT("0"));
        SetProperty (wxT("fold.compact"),
                     (curInfo->folds & mySTC_FOLD_COMPACT) > 0? wxT("1"): wxT("0"));
        SetProperty (wxT("fold.preprocessor"),
                     (curInfo->folds & mySTC_FOLD_PREPROC) > 0? wxT("1"): wxT("0"));
        SetProperty (wxT("fold.html"),
                     (curInfo->folds & mySTC_FOLD_HTML) > 0? wxT("1"): wxT("0"));
        SetProperty (wxT("fold.html.preprocessor"),
                     (curInfo->folds & mySTC_FOLD_HTMLPREP) > 0? wxT("1"): wxT("0"));
        SetProperty (wxT("fold.comment.python"),
                     (curInfo->folds & mySTC_FOLD_COMMENTPY) > 0? wxT("1"): wxT("0"));
        SetProperty (wxT("fold.quotes.python"),
                     (curInfo->folds & mySTC_FOLD_QUOTESPY) > 0? wxT("1"): wxT("0"));
    }
    SetFoldFlags (wxSTC_FOLDFLAG_LINEBEFORE_CONTRACTED |
                  wxSTC_FOLDFLAG_LINEAFTER_CONTRACTED);

    // set spaces and indention
    SetTabWidth (4);
    SetUseTabs (false);
    SetTabIndents (true);
    SetBackSpaceUnIndents (true);
    SetIndent (g_CommonPrefs.indentEnable? 4: 0);

    // others
    SetViewEOL (g_CommonPrefs.displayEOLEnable);
    SetIndentationGuides (g_CommonPrefs.indentGuideEnable);
    SetEdgeColumn (80);
    SetEdgeMode (g_CommonPrefs.longLineOnEnable? wxSTC_EDGE_LINE: wxSTC_EDGE_NONE);
    SetViewWhiteSpace (g_CommonPrefs.whiteSpaceEnable?
                       wxSTC_WS_VISIBLEALWAYS: wxSTC_WS_INVISIBLE);
    SetOvertype (g_CommonPrefs.overTypeInitial);
    SetReadOnly (g_CommonPrefs.readOnlyInitial);
    SetWrapMode (g_CommonPrefs.wrapModeInitial?
                 wxSTC_WRAP_WORD: wxSTC_WRAP_NONE);

    return true;
}

bool Edit::LoadFile ()
{
#if wxUSE_FILEDLG
    // get filname
    if (!m_filename) {
        wxFileDialog dlg (this, wxT("Open file"), wxEmptyString, wxEmptyString,
                          wxT("Any file (*)|*"), wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_CHANGE_DIR);
        if (dlg.ShowModal() != wxID_OK) return false;
        m_filename = dlg.GetPath();
    }

    // load file
    return LoadFile (m_filename);
#else
    return false;
#endif // wxUSE_FILEDLG
}

bool Edit::LoadFile (const wxString &filename) {

    // load file in edit and clear undo
    if (!filename.empty()) m_filename = filename;

    wxStyledTextCtrl::LoadFile(m_filename);

    EmptyUndoBuffer();

    // determine lexer language
    wxFileName file (m_filename);
    InitializePrefs(DeterminePrefs(file.GetFullName()));

    return true;
}

bool Edit::SaveFile ()
{
#if wxUSE_FILEDLG
    // return if no change
    if (!Modified()) return true;

    // get filname
    if (!m_filename) {
        wxFileDialog dlg (this, wxT("Save file"), wxEmptyString, wxEmptyString, wxT("Any file (*)|*"),
                          wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dlg.ShowModal() != wxID_OK) return false;
        m_filename = dlg.GetPath();
    }

    // save file
    return SaveFile (m_filename);
#else
    return false;
#endif // wxUSE_FILEDLG
}

bool Edit::SaveFile (const wxString &filename) {

    // return if no change
    if (!Modified()) return true;

//     // save edit in file and clear undo
//     if (!filename.empty()) m_filename = filename;
//     wxFile file (m_filename, wxFile::write);
//     if (!file.IsOpened()) return false;
//     wxString buf = GetText();
//     bool okay = file.Write (buf);
//     file.Close();
//     if (!okay) return false;
//     EmptyUndoBuffer();
//     SetSavePoint();

//     return true;

    return wxStyledTextCtrl::SaveFile(filename);

}

bool Edit::Modified () {

    // return modified state
    return (GetModify() && !GetReadOnly());
}

//----------------------------------------------------------------------------
// EditProperties
//----------------------------------------------------------------------------

EditProperties::EditProperties (Edit *edit,
                                long style)
        : wxDialog (edit, wxID_ANY, wxEmptyString,
                    wxDefaultPosition, wxDefaultSize,
                    style | wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {

    // sets the application title
    SetTitle (_("Properties"));
    wxString text;

    // fullname
    wxBoxSizer *fullname = new wxBoxSizer (wxHORIZONTAL);
    fullname->Add (10, 0);
    fullname->Add (new wxStaticText (this, wxID_ANY, _("Full filename"),
                                     wxDefaultPosition, wxSize(80, wxDefaultCoord)),
                   0, wxALIGN_LEFT|wxALIGN_CENTER_VERTICAL);
    fullname->Add (new wxStaticText (this, wxID_ANY, edit->GetFilename()),
                   0, wxALIGN_LEFT|wxALIGN_CENTER_VERTICAL);

    // text info
    wxGridSizer *textinfo = new wxGridSizer (4, 0, 2);
    textinfo->Add (new wxStaticText (this, wxID_ANY, _("Language"),
                                     wxDefaultPosition, wxSize(80, wxDefaultCoord)),
                   0, wxALIGN_LEFT|wxALIGN_CENTER_VERTICAL|wxLEFT, 4);
    textinfo->Add (new wxStaticText (this, wxID_ANY, edit->m_language->name),
                   0, wxALIGN_LEFT|wxALIGN_CENTER_VERTICAL|wxRIGHT, 4);
    textinfo->Add (new wxStaticText (this, wxID_ANY, _("Lexer-ID: "),
                                     wxDefaultPosition, wxSize(80, wxDefaultCoord)),
                   0, wxALIGN_LEFT|wxALIGN_CENTER_VERTICAL|wxLEFT, 4);
    text = wxString::Format (wxT("%d"), edit->GetLexer());
    textinfo->Add (new wxStaticText (this, wxID_ANY, text),
                   0, wxALIGN_RIGHT|wxALIGN_CENTER_VERTICAL|wxRIGHT, 4);
    wxString EOLtype = wxEmptyString;
    switch (edit->GetEOLMode()) {
        case wxSTC_EOL_CR: {EOLtype = wxT("CR (Unix)"); break; }
        case wxSTC_EOL_CRLF: {EOLtype = wxT("CRLF (Windows)"); break; }
        case wxSTC_EOL_LF: {EOLtype = wxT("CR (Macintosh)"); break; }
    }
    textinfo->Add (new wxStaticText (this, wxID_ANY, _("Line endings"),
                                     wxDefaultPosition, wxSize(80, wxDefaultCoord)),
                   0, wxALIGN_LEFT|wxALIGN_CENTER_VERTICAL|wxLEFT, 4);
    textinfo->Add (new wxStaticText (this, wxID_ANY, EOLtype),
                   0, wxALIGN_LEFT|wxALIGN_CENTER_VERTICAL|wxRIGHT, 4);

    // text info box
    wxStaticBoxSizer *textinfos = new wxStaticBoxSizer (
                     new wxStaticBox (this, wxID_ANY, _("Informations")),
                     wxVERTICAL);
    textinfos->Add (textinfo, 0, wxEXPAND);
    textinfos->Add (0, 6);

    // statistic
    wxGridSizer *statistic = new wxGridSizer (4, 0, 2);
    statistic->Add (new wxStaticText (this, wxID_ANY, _("Total lines"),
                                     wxDefaultPosition, wxSize(80, wxDefaultCoord)),
                    0, wxALIGN_LEFT|wxALIGN_CENTER_VERTICAL|wxLEFT, 4);
    text = wxString::Format (wxT("%d"), edit->GetLineCount());
    statistic->Add (new wxStaticText (this, wxID_ANY, text),
                    0, wxALIGN_RIGHT|wxALIGN_CENTER_VERTICAL|wxRIGHT, 4);
    statistic->Add (new wxStaticText (this, wxID_ANY, _("Total chars"),
                                     wxDefaultPosition, wxSize(80, wxDefaultCoord)),
                    0, wxALIGN_LEFT|wxALIGN_CENTER_VERTICAL|wxLEFT, 4);
    text = wxString::Format (wxT("%d"), edit->GetTextLength());
    statistic->Add (new wxStaticText (this, wxID_ANY, text),
                    0, wxALIGN_RIGHT|wxALIGN_CENTER_VERTICAL|wxRIGHT, 4);
    statistic->Add (new wxStaticText (this, wxID_ANY, _("Current line"),
                                     wxDefaultPosition, wxSize(80, wxDefaultCoord)),
                    0, wxALIGN_LEFT|wxALIGN_CENTER_VERTICAL|wxLEFT, 4);
    text = wxString::Format (wxT("%d"), edit->GetCurrentLine());
    statistic->Add (new wxStaticText (this, wxID_ANY, text),
                    0, wxALIGN_RIGHT|wxALIGN_CENTER_VERTICAL|wxRIGHT, 4);
    statistic->Add (new wxStaticText (this, wxID_ANY, _("Current pos"),
                                     wxDefaultPosition, wxSize(80, wxDefaultCoord)),
                    0, wxALIGN_LEFT|wxALIGN_CENTER_VERTICAL|wxLEFT, 4);
    text = wxString::Format (wxT("%d"), edit->GetCurrentPos());
    statistic->Add (new wxStaticText (this, wxID_ANY, text),
                    0, wxALIGN_RIGHT|wxALIGN_CENTER_VERTICAL|wxRIGHT, 4);

    // char/line statistics
    wxStaticBoxSizer *statistics = new wxStaticBoxSizer (
                     new wxStaticBox (this, wxID_ANY, _("Statistics")),
                     wxVERTICAL);
    statistics->Add (statistic, 0, wxEXPAND);
    statistics->Add (0, 6);

    // total pane
    wxBoxSizer *totalpane = new wxBoxSizer (wxVERTICAL);
    totalpane->Add (fullname, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);
    totalpane->Add (0, 6);
    totalpane->Add (textinfos, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
    totalpane->Add (0, 10);
    totalpane->Add (statistics, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
    totalpane->Add (0, 6);
    wxButton *okButton = new wxButton (this, wxID_OK, _("OK"));
    okButton->SetDefault();
    totalpane->Add (okButton, 0, wxALIGN_CENTER | wxALL, 10);

    SetSizerAndFit (totalpane);

    ShowModal();
}

