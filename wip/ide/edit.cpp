//////////////////////////////////////////////////////////////////////////////
// Original authors:  Wyo, John Labenski, Otto Wyss
// Copyright: (c) wxGuide, (c) John Labenski, Otto Wyss
// Modified by: Randy Hollines
// Licence: wxWindows licence
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
#include "wx/numdlg.h"

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
    EVT_AUINOTEBOOK_PAGE_CLOSE(wxID_ANY, Notebook::OnPageClose)
    // find/replace
    EVT_MENU(myID_DLG_FIND_TEXT, Notebook::OnEdit)
    EVT_MENU(myID_FINDNEXT, Notebook::OnEdit)
    EVT_MENU(myID_DLG_REPLACE_TEXT, Notebook::OnEdit)
    EVT_MENU(myID_REPLACENEXT, Notebook::OnEdit)
    // view
    EVT_MENU(myID_DISPLAYEOL, Notebook::OnDisplayEOL)
    EVT_MENU(myID_INDENTGUIDE, Notebook::OnIndentGuide)
    EVT_MENU(myID_LINENUMBER, Notebook::OnLineNumber)
    EVT_MENU(myID_LONGLINEON, Notebook::OnLongLineOn)
    EVT_MENU(myID_WHITESPACE, Notebook::OnWhiteSpace)
    // editor operations
    EVT_MENU(wxID_UNDO, Notebook::OnEdit)
    EVT_MENU(wxID_REDO, Notebook::OnEdit)
    EVT_MENU(wxID_SELECTALL, Notebook::OnEdit)
    EVT_MENU_RANGE(wxID_EDIT, wxID_PROPERTIES, Notebook::OnEdit)
    EVT_MENU_RANGE(myID_EDIT_SECOND, myID_EDIT_LAST, Notebook::OnEdit)
END_EVENT_TABLE()

Notebook::Notebook(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size, long style) : 
  wxAuiNotebook(parent, id, pos, size, style)
{
}

Notebook::~Notebook()
{
}

Edit* Notebook::GetEdit() {
  return static_cast<Edit*>(GetCurrentPage());
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

    Freeze();
    const wxString title = w.GetName() + wxT('.') + w.GetExt();
    AddPage(edit, title);
    Thaw();

    pages[filename] = edit;

    int page_index = GetPageIndex(edit);
    if (page_index != wxNOT_FOUND) {
      SetSelection(page_index);
    }
  }
}

void Notebook::CloseAll()
{
  const size_t page_count = GetPageCount();
  for (size_t i = 0; i < page_count; ++i) {
    ClosePage(static_cast<Edit*>(GetPage(i)));
  }
}

// common event handlers
void Notebook::OnEdit(wxCommandEvent &event)
{
  Edit* edit = GetEdit();
  if (!edit) {
    return;
  }

  edit->GetEventHandler()->ProcessEvent(event);
}

void Notebook::OnPageClose(wxAuiNotebookEvent& event)
{
  Edit* edit = static_cast<Edit*>(GetPage(GetSelection()));
  if (edit) {
    ClosePage(edit);
  }
}

void Notebook::ClosePage(Edit* edit)
{
  if (!edit) return;

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

void Notebook::OnDisplayEOL(wxCommandEvent &WXUNUSED(event))
{
  const size_t page_count = GetPageCount();
  for (size_t i = 0; i < page_count; ++i) {
    static_cast<Edit*>(GetPage(i))->ShowEOL();
  }
}

void Notebook::OnIndentGuide(wxCommandEvent &WXUNUSED(event))
{
  const size_t page_count = GetPageCount();
  for (size_t i = 0; i < page_count; ++i) {
    static_cast<Edit*>(GetPage(i))->ShowIndentGuide();
  }
}

void Notebook::OnLineNumber(wxCommandEvent &WXUNUSED(event))
{
  const size_t page_count = GetPageCount();
  for (size_t i = 0; i < page_count; ++i) {
    static_cast<Edit*>(GetPage(i))->ShowLineNumbers();
  }
}

void Notebook::OnLongLineOn(wxCommandEvent &WXUNUSED(event))
{
  const size_t page_count = GetPageCount();
  for (size_t i = 0; i < page_count; ++i) {
    static_cast<Edit*>(GetPage(i))->ShowLongLines();
  }
}

void Notebook::OnWhiteSpace(wxCommandEvent &WXUNUSED(event))
{
  const size_t page_count = GetPageCount();
  for (size_t i = 0; i < page_count; ++i) {
    static_cast<Edit*>(GetPage(i))->ShowWhiteSpace();
  }
}

//----------------------------------------------------------------------------
// Edit
//----------------------------------------------------------------------------

BEGIN_EVENT_TABLE(Edit, wxStyledTextCtrl)
    // common
    EVT_SIZE(Edit::OnSize)
    // edit
    EVT_MENU(wxID_CLEAR, Edit::OnEditClear)
    EVT_MENU(wxID_CUT, Edit::OnEditCut)
    EVT_MENU(wxID_COPY, Edit::OnEditCopy)
    EVT_MENU(wxID_PASTE, Edit::OnEditPaste)
    EVT_MENU(myID_INDENTINC, Edit::OnEditIndentInc)
    EVT_MENU(myID_INDENTRED, Edit::OnEditIndentRed)
    EVT_MENU(wxID_SELECTALL, Edit::OnEditSelectAll)
    EVT_MENU(myID_SELECTLINE, Edit::OnEditSelectLine)
    EVT_MENU(wxID_REDO, Edit::OnEditRedo)
    EVT_MENU(wxID_UNDO, Edit::OnEditUndo)
    // mode
    EVT_MENU(myID_FOLDTOGGLE, Edit::OnFoldToggle)
    EVT_MENU(myID_CHARSETANSI, Edit::OnUseCharset)
    EVT_MENU(myID_CHARSETMAC, Edit::OnUseCharset)
    // find
    EVT_MENU(myID_DLG_FIND_TEXT, Edit::OnFindReplace)
    EVT_MENU(myID_FINDNEXT, Edit::OnFindReplace)
    EVT_MENU(myID_DLG_REPLACE_TEXT, Edit::OnFindReplace)
    EVT_MENU(myID_REPLACENEXT, Edit::OnFindReplace)
    EVT_FIND(wxID_ANY, Edit::OnFindReplaceDialog)
    EVT_FIND_NEXT(wxID_ANY, Edit::OnFindReplaceDialog)
    EVT_FIND_REPLACE(wxID_ANY, Edit::OnFindReplaceDialog)
    EVT_FIND_REPLACE_ALL(wxID_ANY, Edit::OnFindReplaceDialog)
    EVT_FIND_CLOSE(wxID_ANY, Edit::OnFindReplaceDialog)
    EVT_MENU(myID_BRACEMATCH, Edit::OnBraceMatch)
    EVT_MENU(myID_GOTO, Edit::OnGoto)
    // annotations
    EVT_MENU(myID_ANNOTATION_ADD, Edit::OnAnnotationAdd)
    EVT_MENU(myID_ANNOTATION_REMOVE, Edit::OnAnnotationRemove)
    EVT_MENU(myID_ANNOTATION_CLEAR, Edit::OnAnnotationClear)
    EVT_MENU(myID_ANNOTATION_STYLE_HIDDEN, Edit::OnAnnotationStyle)
    EVT_MENU(myID_ANNOTATION_STYLE_STANDARD, Edit::OnAnnotationStyle)
    EVT_MENU(myID_ANNOTATION_STYLE_BOXED, Edit::OnAnnotationStyle)
    // extra
    EVT_MENU(myID_CHANGELOWER, Edit::OnChangeCase)
    EVT_MENU(myID_CHANGEUPPER, Edit::OnChangeCase)
    EVT_MENU(myID_CONVERTCR, Edit::OnConvertEOL)
    EVT_MENU(myID_CONVERTCRLF, Edit::OnConvertEOL)
    EVT_MENU(myID_CONVERTLF, Edit::OnConvertEOL)
    // stc
    EVT_STC_MARGINCLICK(wxID_ANY, Edit::OnMarginClick)
    EVT_STC_CHARADDED(wxID_ANY, Edit::OnCharAdded)
    EVT_STC_MODIFIED(wxID_ANY, Edit::OnModified)
END_EVENT_TABLE()

Edit::Edit (wxWindow *parent, wxWindowID id,
            const wxPoint &pos,
            const wxSize &size,
            long style)
    : wxStyledTextCtrl (parent, id, pos, size, style) {

    m_findReplace = NULL;
    m_filename = wxEmptyString;

    m_LineNrID = 0;
    m_DividerID = 1;
    m_FoldingID = 2;

    // initialize language
    m_language = NULL;

    // Use all the bits in the style byte as styles, not indicators.
    SetStyleBits(8);
    
    m_modified = false;
    SetModEventMask(wxSTC_MOD_INSERTTEXT | wxSTC_MOD_DELETETEXT);
    
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
    SetMarginSensitive(MARGIN_FOLD, true);

    // Properties found from http://www.scintilla.org/SciTEDoc.html
    SetProperty(wxT("fold"), wxT("1"));
    SetProperty(wxT("fold.comment"), wxT("1"));
    SetProperty(wxT("fold.compact"), wxT("1"));

    SetProperty(wxT("fold"), wxT("1"));
    SetProperty(wxT("fold.comment"), wxT("1"));
    SetProperty(wxT("fold.compact"), wxT("1"));
    SetProperty(wxT("fold.preprocessor"), wxT("1"));
    
    MarkerDefine(wxSTC_MARKNUM_FOLDER,        wxSTC_MARK_BOXPLUS, wxT("WHITE"), wxT("GREY"));
    MarkerDefine(wxSTC_MARKNUM_FOLDEROPEN,    wxSTC_MARK_BOXMINUS,  wxT("WHITE"), wxT("GREY"));
    MarkerDefine(wxSTC_MARKNUM_FOLDERSUB,     wxSTC_MARK_VLINE,     wxT("WHITE"), wxT("GREY"));
    MarkerDefine(wxSTC_MARKNUM_FOLDEREND,     wxSTC_MARK_BOXPLUSCONNECTED, wxT("WHITE"), wxT("GREY"));
    MarkerDefine(wxSTC_MARKNUM_FOLDEROPENMID, wxSTC_MARK_BOXMINUSCONNECTED, wxT("WHITE"), wxT("GREY"));
    MarkerDefine(wxSTC_MARKNUM_FOLDERMIDTAIL, wxSTC_MARK_TCORNER,     wxT("WHITE"), wxT("GREY"));
    MarkerDefine(wxSTC_MARKNUM_FOLDERTAIL,    wxSTC_MARK_LCORNER,     wxT("WHITE"), wxT("GREY"));
    SetFoldFlags(wxSTC_FOLDFLAG_LINEBEFORE_CONTRACTED | wxSTC_FOLDFLAG_LINEAFTER_CONTRACTED);

    // miscellaneous
    m_LineNrMargin = TextWidth (wxSTC_STYLE_LINENUMBER, wxT("_999999"));
    m_FoldingMargin = 16;
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

// edit event handlers
void Edit::OnEditRedo(wxCommandEvent &WXUNUSED(event)) {
  if (!CanRedo()) return;
  Redo();
}

void Edit::OnEditUndo(wxCommandEvent &WXUNUSED(event)) {
  if (!CanUndo()) return;
  Undo();
}

void Edit::OnEditClear(wxCommandEvent &WXUNUSED(event)) {
  if (GetReadOnly()) return;
  Clear();
}

void Edit::OnEditCut(wxCommandEvent &WXUNUSED(event)) {
  if (GetReadOnly() || (GetSelectionEnd() - GetSelectionStart() <= 0)) return;
  Cut();
}

void Edit::OnEditCopy(wxCommandEvent &WXUNUSED(event)) {
  if (GetSelectionEnd() - GetSelectionStart() <= 0) return;
  Copy();
}

void Edit::OnEditPaste(wxCommandEvent &WXUNUSED(event)) {
  if (!CanPaste()) return;
  Paste();
}

void Edit::OnFindReplace(wxCommandEvent &event) {
  const wxEventType type = event.GetId();
  const wxString find_string = m_FindData.GetFindString();
  int found_start, found_end;

  if (event.GetId() == myID_FINDNEXT) {
    // search
    if (FindText(found_start, found_end, true)) {
      SetSelection(found_start, found_end);
    }
    else {
      wxMessageDialog dialog(this, wxT("Cannot find the text \"" + find_string + "\" from current position"), wxT("Find"));
      dialog.ShowModal();
    }
  }
  if (event.GetId() == myID_REPLACENEXT) {
    ReplaceText(find_string);
  }
  else {
    if (m_findReplace) {
      delete m_findReplace;
      m_findReplace = NULL;
    }

    if (event.GetId() == myID_DLG_FIND_TEXT) {
      m_findReplace = new wxFindReplaceDialog(this, &m_FindData, wxT("Find"));
    }
    else {
      m_findReplace = new wxFindReplaceDialog(this, &m_FindData, wxT("Find & Replace"), wxFR_REPLACEDIALOG);
    }
    m_findReplace->Show();
  }
}

void Edit::OnFindReplaceDialog(wxFindDialogEvent& event)
{
  const wxEventType type = event.GetEventType();
  const wxString find_string = m_FindData.GetFindString();
  int found_start, found_end;

  if (type == wxEVT_FIND || type == wxEVT_FIND_NEXT) {
    // search
    if (FindText(found_start, found_end, type == wxEVT_FIND_NEXT)) {
      SetSelection(found_start, found_end);
    }
    else {
      wxMessageDialog dialog(this, wxT("Cannot find the text \"" + find_string + "\" from current position"), wxT("Find"));
      dialog.ShowModal();
    }
  }
  else if (type == wxEVT_FIND_REPLACE) {
    ReplaceText(find_string);
  }
  else if (type == wxEVT_FIND_REPLACE_ALL) {
    const wxString replace_string = m_FindData.GetReplaceString();
    int start_index = 0;
    bool found = true;
    do {
      // set search area
      SetTargetStart(start_index); SetTargetEnd(GetLastPosition());

      // search and replace
      found_start = SearchInTarget(find_string);
      if (found_start > -1) {
        found_end = found_start + find_string.size();
        SetTargetStart(found_start);
        SetTargetEnd(found_end);
        ReplaceTarget(replace_string);
        start_index = found_start + replace_string.size();
      }
      else {
        found = false;
      }
    }
    while (found);
  }
}

bool Edit::FindText(int &found_start, int &found_end, bool find_next)
{
  const int flags = m_FindData.GetFlags();
  const bool find_down = (flags & wxFR_DOWN) ? true : false;
  const wxString find_string = m_FindData.GetFindString();
  found_start = found_end = -1;

  if (find_next) {
    GotoPos(find_down ? GetCurrentPos() : GetCurrentPos() - find_string.size());
  }
  SearchAnchor();

  // search up/down
  found_start = find_down ? SearchNext(flags, find_string) : SearchPrev(flags, find_string);

  // found
  if (found_start > -1) {
    found_end = found_start + find_string.size();
    GotoPos(found_start);
    return true;
  }
  
  return false;
}

void Edit::ReplaceText(const wxString &find_string)
{
  int found_start, found_end;
  const int select_start = GetSelectionStart();
  const int select_end = GetSelectionEnd();

  // replace & search
  if (select_start > -1 && select_end > select_start) {
    const wxString select_string = GetSelectedText();
    if (select_string == find_string) {
      ReplaceSelection(m_FindData.GetReplaceString());
      // search
      if (FindText(found_start, found_end, false)) {
        SetSelection(found_start, found_end);
      }
      else {
        wxMessageDialog dialog(this, wxT("Cannot find the text \"" + find_string + "\" from current position"), wxT("Find"));
        dialog.ShowModal();
      }
    }
  }
  // search
  else if (FindText(found_start, found_end, false)) {
    SetSelection(found_start, found_end);
  }
}

void Edit::OnGoto(wxCommandEvent &WXUNUSED(event)) {
  const wxString message = wxString::Format(_("Line number : 1 - %d"), GetLineCount());
  const long line_number = wxGetNumberFromUser(wxEmptyString, message, wxT("Go To Line"), 1, 1, 100, this);
  if (line_number > 0) {
    GotoLine(line_number - 1);
  }
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

void Edit::OnEditIndentInc(wxCommandEvent &WXUNUSED(event)) {
  CmdKeyExecute(wxSTC_CMD_TAB);
}

void Edit::OnEditIndentRed(wxCommandEvent &WXUNUSED(event)) {
  CmdKeyExecute(wxSTC_CMD_DELETEBACK);
}

void Edit::OnFoldToggle(wxCommandEvent &WXUNUSED(event)) {
  ToggleFold(GetFoldParent(GetCurrentLine()));
}

void Edit::OnEditSelectAll(wxCommandEvent &WXUNUSED(event)) {
  SetSelection(0, GetTextLength());
}

void Edit::OnEditSelectLine(wxCommandEvent &WXUNUSED(event)) {
  int lineStart = PositionFromLine(GetCurrentLine());
  int lineEnd = PositionFromLine(GetCurrentLine() + 1);
  SetSelection(lineStart, lineEnd);
}

void Edit::OnUseCharset(wxCommandEvent &event) {
  int Nr;
  int charset = GetCodePage();
  switch (event.GetId()) {
  case myID_CHARSETANSI: {charset = wxSTC_CHARSET_ANSI; break; }
  case myID_CHARSETMAC: {charset = wxSTC_CHARSET_ANSI; break; }
  }
  for (Nr = 0; Nr < wxSTC_STYLE_LASTPREDEFINED; Nr++) {
    StyleSetCharacterSet(Nr, charset);
  }
  SetCodePage(charset);
}

void Edit::OnAnnotationAdd(wxCommandEvent& WXUNUSED(event))
{
  const int line = GetCurrentLine();

  wxString ann = AnnotationGetText(line);
  ann = wxGetTextFromUser
    (
    wxString::Format("Enter annotation for the line %d", line),
    "Edit annotation",
    ann,
    this
    );
  if (ann.empty())
    return;

  AnnotationSetText(line, ann);
  AnnotationSetStyle(line, ANNOTATION_STYLE);

  // Scintilla doesn't update the scroll width for annotations, even with
  // scroll width tracking on, so do it manually.
  const int width = GetScrollWidth();

  // NB: The following adjustments are only needed when using
  //     wxSTC_ANNOTATION_BOXED annotations style, but we apply them always
  //     in order to make things simpler and not have to redo the width
  //     calculations when the annotations visibility changes. In a real
  //     program you'd either just stick to a fixed annotations visibility or
  //     update the width when it changes.

  // Take into account the fact that the annotation is shown indented, with
  // the same indent as the line it's attached to.
  int indent = GetLineIndentation(line);

  // This is just a hack to account for the width of the box, there doesn't
  // seem to be any way to get it directly from Scintilla.
  indent += 3;

  const int widthAnn = TextWidth(ANNOTATION_STYLE, ann + wxString(indent, ' '));

  if (widthAnn > width)
    SetScrollWidth(widthAnn);
}

void Edit::OnAnnotationRemove(wxCommandEvent& WXUNUSED(event))
{
  AnnotationSetText(GetCurrentLine(), wxString());
}

void Edit::OnAnnotationClear(wxCommandEvent& WXUNUSED(event))
{
  AnnotationClearAll();
}

void Edit::OnAnnotationStyle(wxCommandEvent& event)
{
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

  AnnotationSetVisible(style);
}

void Edit::OnChangeCase(wxCommandEvent &event) {
  switch (event.GetId()) {
  case myID_CHANGELOWER: {
    CmdKeyExecute(wxSTC_CMD_LOWERCASE);
    break;
  }
  case myID_CHANGEUPPER: {
    CmdKeyExecute(wxSTC_CMD_UPPERCASE);
    break;
  }
  }
}

void Edit::OnConvertEOL(wxCommandEvent &event) {
  int eolMode = GetEOLMode();
  switch (event.GetId()) {
  case myID_CONVERTCR: { eolMode = wxSTC_EOL_CR; break; }
  case myID_CONVERTCRLF: { eolMode = wxSTC_EOL_CRLF; break; }
  case myID_CONVERTLF: { eolMode = wxSTC_EOL_LF; break; }
  }
  ConvertEOLs(eolMode);
  SetEOLMode(eolMode);
}

//! misc
void Edit::OnMarginClick(wxStyledTextEvent &event) {
  if (event.GetMargin() == 2) {
    int lineClick = LineFromPosition(event.GetPosition());
    int levelClick = GetFoldLevel(lineClick);
    if ((levelClick & wxSTC_FOLDLEVELHEADERFLAG) > 0) {
      ToggleFold(lineClick);
    }
  }
}

void Edit::OnCharAdded(wxStyledTextEvent &event) {
  event.Skip();
  
  const wxChar c = event.GetKey();
  if (c == wxT('\n')) {
    const int line = GetCurrentLine();
    const int indent = line < 1 ? 0 : GetLineIndentation(line - 1);

    if (indent != 0) {
      SetLineIndentation(line, indent);
      GotoPos(GetLineIndentPosition(line));
    }
  }
}

void Edit::OnModified(wxStyledTextEvent& WXUNUSED(event))
{
  if (m_modified) {
    return;
  }

  Notebook* notebook = static_cast<Notebook*>(GetParent());
  const int page_index = notebook->GetPageIndex(this);
  if (page_index > -1) {
    wxString page_text = notebook->GetPageText(page_index);
    page_text += wxT('*');
    notebook->SetPageText(page_index, page_text);
    m_modified = true;
  }
}

//----------------------------------------------------------------------------
// private functions
wxString Edit::DeterminePrefs(const wxString &filename) {

  LanguageInfo const* curInfo;

  // determine language from filepatterns
  int languageNr;
  for (languageNr = 0; languageNr < g_LanguagePrefsSize; languageNr++) {
    curInfo = &g_LanguagePrefs[languageNr];
    wxString filepattern = curInfo->filepattern;
    filepattern.Lower();
    while (!filepattern.empty()) {
      wxString cur = filepattern.BeforeFirst(';');
      if ((cur == filename) ||
        (cur == (filename.BeforeLast('.') + wxT(".*"))) ||
        (cur == (wxT("*.") + filename.AfterLast('.')))) {
        return curInfo->name;
      }
      filepattern = filepattern.AfterFirst(';');
    }
  }
  return wxEmptyString;

}

bool Edit::InitializePrefs(const wxString &name) {

  // initialize styles
  StyleClearAll();
  LanguageInfo const* curInfo = NULL;

  // determine language
  bool found = false;
  int languageNr;
  for (languageNr = 0; languageNr < g_LanguagePrefsSize; languageNr++) {
    curInfo = &g_LanguagePrefs[languageNr];
    if (curInfo->name == name) {
      found = true;
      break;
    }
  }
  if (!found) return false;

  // set lexer and language
  SetLexer(curInfo->lexer);
  m_language = curInfo;

  // set margin for line numbers
  SetMarginType(m_LineNrID, wxSTC_MARGIN_NUMBER);
  StyleSetForeground(wxSTC_STYLE_LINENUMBER, wxColour(wxT("DARK GREY")));
  StyleSetBackground(wxSTC_STYLE_LINENUMBER, *wxWHITE);
  SetMarginWidth(m_LineNrID, 0); // start out not visible

  // annotations style
  StyleSetBackground(ANNOTATION_STYLE, wxColour(244, 220, 220));
  StyleSetForeground(ANNOTATION_STYLE, *wxBLACK);
  StyleSetSizeFractional(ANNOTATION_STYLE,
    (StyleGetSizeFractional(wxSTC_STYLE_DEFAULT) * 4) / 5);

  // default fonts for all styles!
  int Nr;
  for (Nr = 0; Nr < wxSTC_STYLE_LASTPREDEFINED; Nr++) {
    wxFont font(10, wxMODERN, wxNORMAL, wxNORMAL);
    StyleSetFont(Nr, font);
  }

  // set common styles
  StyleSetForeground(wxSTC_STYLE_DEFAULT, wxColour(wxT("DARK GREY")));
  StyleSetForeground(wxSTC_STYLE_INDENTGUIDE, wxColour(wxT("DARK GREY")));

  // initialize settings
  if (g_CommonPrefs.syntaxEnable) {
    int keywordnr = 0;
    for (Nr = 0; Nr < STYLE_TYPES_COUNT; Nr++) {
      if (curInfo->styles[Nr].type == -1) continue;
      const StyleInfo &curType = g_StylePrefs[curInfo->styles[Nr].type];
      wxFont font(curType.fontsize, wxMODERN, wxNORMAL, wxNORMAL, false,
        curType.fontname);
      StyleSetFont(Nr, font);
      if (curType.foreground) {
        StyleSetForeground(Nr, wxColour(curType.foreground));
      }
      if (curType.background) {
        StyleSetBackground(Nr, wxColour(curType.background));
      }
      StyleSetBold(Nr, (curType.fontstyle & mySTC_STYLE_BOLD) > 0);
      StyleSetItalic(Nr, (curType.fontstyle & mySTC_STYLE_ITALIC) > 0);
      StyleSetUnderline(Nr, (curType.fontstyle & mySTC_STYLE_UNDERL) > 0);
      StyleSetVisible(Nr, (curType.fontstyle & mySTC_STYLE_HIDDEN) == 0);
      StyleSetCase(Nr, curType.lettercase);
      const char *pwords = curInfo->styles[Nr].words;
      if (pwords) {
        SetKeyWords(keywordnr, pwords);
        keywordnr += 1;
      }
    }
  }

  // set margin as unused
  SetMarginType(m_DividerID, wxSTC_MARGIN_SYMBOL);
  SetMarginWidth(m_DividerID, 0);
  SetMarginSensitive(m_DividerID, false);

  // folding
  SetMarginType(m_FoldingID, wxSTC_MARGIN_SYMBOL);
  SetMarginMask(m_FoldingID, wxSTC_MASK_FOLDERS);
  StyleSetBackground(m_FoldingID, *wxWHITE);
  SetMarginWidth(m_FoldingID, 0);
  SetMarginSensitive(m_FoldingID, false);
  if (g_CommonPrefs.foldEnable) {
    SetMarginWidth(m_FoldingID, curInfo->folds != 0 ? m_FoldingMargin : 0);
    SetMarginSensitive(m_FoldingID, curInfo->folds != 0);
    SetProperty(wxT("fold"), curInfo->folds != 0 ? wxT("1") : wxT("0"));
    SetProperty(wxT("fold.comment"),
      (curInfo->folds & mySTC_FOLD_COMMENT) > 0 ? wxT("1") : wxT("0"));
    SetProperty(wxT("fold.compact"),
      (curInfo->folds & mySTC_FOLD_COMPACT) > 0 ? wxT("1") : wxT("0"));
    SetProperty(wxT("fold.preprocessor"),
      (curInfo->folds & mySTC_FOLD_PREPROC) > 0 ? wxT("1") : wxT("0"));
  }
  SetFoldFlags(wxSTC_FOLDFLAG_LINEBEFORE_CONTRACTED |
    wxSTC_FOLDFLAG_LINEAFTER_CONTRACTED);

  // set spaces and indention
  SetTabWidth(3);
  SetUseTabs(true);
  SetTabIndents(true);
  SetBackSpaceUnIndents(true);
  SetIndent(g_CommonPrefs.indentEnable ? 3 : 0);

  // others
  SetViewEOL(g_CommonPrefs.displayEOLEnable);
  SetIndentationGuides(g_CommonPrefs.indentGuideEnable);
  SetEdgeColumn(80);
  SetEdgeMode(g_CommonPrefs.longLineOnEnable ? wxSTC_EDGE_LINE : wxSTC_EDGE_NONE);
  SetViewWhiteSpace(g_CommonPrefs.whiteSpaceEnable ?
  wxSTC_WS_VISIBLEALWAYS : wxSTC_WS_INVISIBLE);
  SetOvertype(g_CommonPrefs.overTypeInitial);
  SetReadOnly(g_CommonPrefs.readOnlyInitial);
  SetWrapMode(g_CommonPrefs.wrapModeInitial ?
  wxSTC_WRAP_WORD : wxSTC_WRAP_NONE);

  return true;
}

bool Edit::LoadFile()
{
#if wxUSE_FILEDLG
  // get filname
  if (!m_filename) {
    wxFileDialog dlg(this, wxT("Open file"), wxEmptyString, wxEmptyString,
      wxT("Any file (*)|*"), wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_CHANGE_DIR);
    if (dlg.ShowModal() != wxID_OK) return false;
    m_filename = dlg.GetPath();
  }

  // load file
  return LoadFile(m_filename);
#else
  return false;
#endif // wxUSE_FILEDLG
}

bool Edit::LoadFile(const wxString &filename) {

  // load file in edit and clear undo
  if (!filename.empty()) m_filename = filename;

  wxStyledTextCtrl::LoadFile(m_filename);

  EmptyUndoBuffer();

  // determine lexer language
  wxFileName fname(m_filename);
  InitializePrefs(DeterminePrefs(fname.GetFullName()));

  return true;
}

bool Edit::SaveFile()
{
#if wxUSE_FILEDLG
  // return if no change
  if (!Modified()) return true;

  // get filname
  if (!m_filename) {
    wxFileDialog dlg(this, wxT("Save file"), wxEmptyString, wxEmptyString, wxT("Objeck files (*.obs)|*.obs;*.obw|All types (*.*)|*.*"),
      wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() != wxID_OK) return false;
    m_filename = dlg.GetPath();
  }

  // save file
  return SaveFile(m_filename);
#else
  return false;
#endif // wxUSE_FILEDLG
}

bool Edit::SaveFile(const wxString &filename) {
  // return if no change
  if (!Modified()) return true;

  Notebook* notebook = static_cast<Notebook*>(GetParent());
  if (notebook->GetCurrentPage()) {
    const int page_index = notebook->GetSelection();
    wxString page_text = notebook->GetPageText(page_index);
    if (page_text.EndsWith(wxT('*'))) {
      page_text.RemoveLast();
      notebook->SetPageText(page_index, page_text);
    }
  }

  if (m_modified) {
    Notebook* notebook = static_cast<Notebook*>(GetParent());
    const int page_index = notebook->GetPageIndex(this);
    if (page_index > -1) {
      wxString page_text = notebook->GetPageText(page_index);
      if (page_text.EndsWith(wxT('*'))) {
        page_text.RemoveLast();
        notebook->SetPageText(page_index, page_text);
      }
      m_modified = false;
    }
  }
  
  return wxStyledTextCtrl::SaveFile(filename);
}

bool Edit::Modified() {
  return (IsModified() && !GetReadOnly());
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

