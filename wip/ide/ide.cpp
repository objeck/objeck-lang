//////////////////////////////////////////////////////////////////////////////
// Original authors:  Wyo, John Labenski, Otto Wyss
// Copyright: (c) wxGuide, (c) John Labenski, Otto Wyss
// Modified by: Randy Hollines
// Licence: wxWindows licence
//////////////////////////////////////////////////////////////////////////////

#include "ide.h"
#include <wx/sstream.h>
#include <wx/mstream.h>


#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/string.h>
#include <wx/stattext.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/radiobut.h>
#include <wx/spinctrl.h>
#include <wx/combobox.h>
#include <wx/statbox.h>
#include <wx/dialog.h>

/////////////////////////
// MyApp
/////////////////////////

bool MyApp::OnInit() 
{
  if (!wxApp::OnInit()) {
    return false;
  }

  wxFrame* frame = new MyFrame(NULL, wxID_ANY, wxT("wxAUI Sample Application"), wxDefaultPosition, wxSize(800, 600));
  frame->Show();

  return true;
}

DECLARE_APP(MyApp)
IMPLEMENT_APP(MyApp)

/////////////////////////
// MyFrame
/////////////////////////

BEGIN_EVENT_TABLE(MyFrame, wxFrame)
// common
EVT_CLOSE(MyFrame::OnClose)
// file
EVT_MENU(wxID_NEW, MyFrame::OnFileNew)
EVT_MENU(wxID_OPEN, MyFrame::OnFileOpen)
EVT_MENU(wxID_SAVE, MyFrame::OnFileSave)
EVT_MENU(wxID_SAVEAS, MyFrame::OnFileSaveAs)
EVT_MENU(wxID_CLOSE, MyFrame::OnFileClose)
// find/replace
EVT_MENU(myID_DLG_FIND_TEXT, MyFrame::OnEdit)
EVT_MENU(myID_FINDNEXT, MyFrame::OnEdit)
EVT_MENU(myID_DLG_REPLACE_TEXT, MyFrame::OnEdit)
EVT_MENU(myID_REPLACENEXT, MyFrame::OnEdit)
EVT_MENU(myID_DLG_OPTIONS, MyFrame::OnOptions)
// editor operations
EVT_MENU(wxID_UNDO, MyFrame::OnEdit)
EVT_MENU(wxID_REDO, MyFrame::OnEdit)
EVT_MENU(wxID_SELECTALL, MyFrame::OnEdit)
EVT_MENU_RANGE(wxID_EDIT, wxID_PROPERTIES, MyFrame::OnEdit)
EVT_MENU_RANGE(myID_EDIT_FIRST, myID_EDIT_LAST, MyFrame::OnEdit)
END_EVENT_TABLE()

MyFrame::MyFrame(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) : 
    wxFrame(parent, id, title, pos, size, style) 
{
  new_page_count = 1;

  // setup window manager
  aui_manager.SetManagedWindow(this);
  aui_manager.AddPane(CreateTreeCtrl(), wxAuiPaneInfo().Left().PaneBorder(false));
  m_notebook = CreateNotebook(), 
  aui_manager.AddPane(m_notebook, wxAuiPaneInfo().CenterPane().PaneBorder(false));
  aui_manager.AddPane(CreateInfoCtrl(), wxAuiPaneInfo().Bottom().PaneBorder(false));
  
  // set menu and status bars
  SetMenuBar(CreateMenuBar());
  CreateStatusBar();
  GetStatusBar()->SetStatusText(wxT("Ready"));

  // set windows sizes
  SetMinSize(wxSize(400, 300));
  
  // set tool bar
  aui_manager.AddPane(CreateToolBar(), wxAuiPaneInfo().
    Name(wxT("toolbar")).Caption(wxT("Toolbar 3")).
    ToolbarPane().Top().Row(1).Position(1));

  // update
  m_notebook->SetFocus();
  aui_manager.Update();
}

MyFrame::~MyFrame() 
{
  aui_manager.UnInit();
}

void MyFrame::DoUpdate() 
{
  aui_manager.Update();
}

// common event handlers
void MyFrame::OnEdit(wxCommandEvent &event) 
{
  if (m_notebook) {
    m_notebook->GetEventHandler()->ProcessEvent(event);
  }
}

void MyFrame::OnClose(wxCloseEvent &WXUNUSED(event))
{
  // close all pages and prompt to save files
  m_notebook->CloseAll();
  Destroy();
}

// file event handlers
void MyFrame::OnFileNew(wxCommandEvent &WXUNUSED(event))
{
  m_notebook->Freeze();
  const wxString title = wxString::Format(wxT("new %d"), new_page_count++);
  m_notebook->AddPage(new Edit(m_notebook), title);
  m_notebook->SetSelection(m_notebook->GetPageCount() - 1);
  m_notebook->Thaw();
}

void MyFrame::OnFileOpen(wxCommandEvent &WXUNUSED(event)) 
{
  wxFileDialog dlg(this, wxT("Open file"), wxEmptyString, wxEmptyString, 
    wxT("Objeck files (*.obs)|*.obs;*.obw|All types (*.*)|*.*"),
    wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_CHANGE_DIR);
  if (dlg.ShowModal() != wxID_OK) {
    return;
  }
	wxString path = dlg.GetPath();
  m_notebook->OpenFile(path);
}

void MyFrame::OnFileSave(wxCommandEvent &WXUNUSED(event)) 
{
  if (!m_notebook->GetEdit()) return;

  if (m_notebook->GetEdit()->Modified()) {
    m_notebook->GetEdit()->SaveFile();
  }
}

void MyFrame::OnFileSaveAs(wxCommandEvent &WXUNUSED(event)) 
{
  if (!m_notebook->GetEdit()) return;

  wxString filename = wxEmptyString;
  wxFileDialog dlg(this, wxT("Save file"), wxEmptyString, wxEmptyString, 
    wxT("Objeck files (*.obs)|*.obs;*.obw|All types (*.*)|*.*"), 
    wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
  if (dlg.ShowModal() != wxID_OK) return;
  filename = dlg.GetPath();
  m_notebook->GetEdit()->SaveFile(filename);
}

void MyFrame::OnFileClose(wxCommandEvent &WXUNUSED(event)) 
{
  if (!m_notebook->GetEdit()) return;

  // check to see if file has been modified
  if (m_notebook->GetEdit()->Modified()) {
    if (wxMessageBox(_("Text is not saved, save before closing?"), _("Close"),
      wxYES_NO | wxICON_QUESTION) == wxYES) {
      m_notebook->GetEdit()->SaveFile();
      if (m_notebook->GetEdit()->Modified()) {
        wxMessageBox(_("Text could not be saved!"), _("Close abort"),
          wxOK | wxICON_EXCLAMATION);
        return;
      }
    }
  }

  // close file
  m_notebook->GetEdit()->SetFilename(wxEmptyString);
  m_notebook->GetEdit()->ClearAll();
  m_notebook->GetEdit()->SetSavePoint();

  // destroy page
  const int page_index = m_notebook->GetSelection();
  if (page_index > -1) {
    m_notebook->DeletePage(page_index);
  }
}

void MyFrame::OnOptions(wxCommandEvent &WXUNUSED(event))
{
  GlobalOptions dlg(this, 0);
}

wxMenuBar* MyFrame::CreateMenuBar()
{
  // File menu
  wxMenu *menuFile = new wxMenu;
  menuFile->Append(wxID_NEW, _("&New...\tCtrl+N"));
  menuFile->Append(wxID_OPEN, _("&Open...\tCtrl+O"));
  menuFile->Append(wxID_SAVE, _("&Save\tCtrl+S"));
  menuFile->Append(wxID_SAVEAS, _("Save &as...\tCtrl+Shift+S"));
  menuFile->Append(wxID_CLOSE, _("&Close\tCtrl+W"));
  menuFile->AppendSeparator();
  menuFile->Append(wxID_PROPERTIES, _("Proper&ties\tCtrl+T"));

  // Edit menu
  wxMenu *menuEdit = new wxMenu;
  menuEdit->Append(wxID_UNDO, _("&Undo\tCtrl+Z"));
  menuEdit->Append(wxID_REDO, _("&Redo\tCtrl+Y"));
  menuEdit->AppendSeparator();
  menuEdit->Append(wxID_CUT, _("Cu&t\tCtrl+X"));
  menuEdit->Append(wxID_COPY, _("&Copy\tCtrl+C"));
  menuEdit->Append(wxID_PASTE, _("&Paste\tCtrl+V"));
  menuEdit->Append(wxID_SELECTALL, _("&Select All\tCtrl+A"));
  menuEdit->AppendSeparator();
  menuEdit->AppendCheckItem(myID_OVERTYPE, _("Over &type\tCtrl+Shift+T"));
  menuEdit->AppendCheckItem(myID_READONLY, _("Read-&only\tCtrl+Shift+R"));
  menuEdit->AppendCheckItem(myID_WRAPMODEON, _("&Word wrap\tCtrl+Shift+W"));
  menuEdit->AppendSeparator();
  menuEdit->Append(myID_DLG_FIND_TEXT, _("&Find...\tCtrl+F"));
  menuEdit->Append(myID_FINDNEXT, _("Find &next\tF3"));
  menuEdit->Append(myID_DLG_REPLACE_TEXT, _("&Replace...\tCtrl+H"));
  menuEdit->Append(myID_REPLACENEXT, _("&Replace &again\tShift+F3"));
  menuEdit->Append(myID_GOTO, _("&Go To...\tCtrl+G"));
  menuEdit->AppendSeparator();
  menuEdit->Append(myID_DLG_OPTIONS, _("Options...\tCtrl+ALT+O"));
  
  // View menu
  wxMenu *menuView = new wxMenu;
  menuView->AppendCheckItem(myID_FOLDTOGGLE, _("&Toggle current fold\tCtrl+T"));
  menuView->AppendCheckItem(myID_OVERTYPE, _("&Overwrite mode\tIns"));
  menuView->AppendCheckItem(myID_WRAPMODEON, _("&Wrap mode\tCtrl+U"));
  menuView->AppendSeparator();
  menuView->AppendCheckItem(myID_DISPLAYEOL, _("Show line &endings"));
  menuView->AppendCheckItem(myID_INDENTGUIDE, _("Show &indent guides"));
  menuView->AppendCheckItem(myID_LINENUMBER, _("Show line &numbers"));
  menuView->AppendCheckItem(myID_LONGLINEON, _("Show &long line marker"));
  menuView->AppendCheckItem(myID_WHITESPACE, _("Show white&space"));

  wxMenuBar* menu_bar = new wxMenuBar;
  menu_bar->Append(menuFile, wxT("&File"));
  menu_bar->Append(menuEdit, wxT("&Edit"));
  menu_bar->Append(menuView, wxT("&View"));
  menu_bar->Append(new wxMenu, wxT("&Help"));

  return menu_bar;
}

wxAuiToolBar* MyFrame::CreateToolBar()
{
  wxAuiToolBarItemArray prepend_items;
  wxAuiToolBarItemArray append_items;
  wxAuiToolBar* toolbar = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
    wxAUI_TB_DEFAULT_STYLE | wxAUI_TB_OVERFLOW);
  toolbar->SetToolBitmapSize(wxSize(16, 16));
  wxBitmap tb3_bmp1 = wxArtProvider::GetBitmap(wxART_FOLDER, wxART_OTHER, wxSize(16, 16));
  toolbar->AddTool(ID_SampleItem + 16, wxT("Check 1"), tb3_bmp1, wxT("Check 1"), wxITEM_CHECK);
  toolbar->AddTool(ID_SampleItem + 17, wxT("Check 2"), tb3_bmp1, wxT("Check 2"), wxITEM_CHECK);
  toolbar->AddTool(ID_SampleItem + 18, wxT("Check 3"), tb3_bmp1, wxT("Check 3"), wxITEM_CHECK);
  toolbar->AddTool(ID_SampleItem + 19, wxT("Check 4"), tb3_bmp1, wxT("Check 4"), wxITEM_CHECK);
  toolbar->AddSeparator();
  toolbar->AddTool(ID_SampleItem + 20, wxT("Radio 1"), tb3_bmp1, wxT("Radio 1"), wxITEM_RADIO);
  toolbar->AddTool(ID_SampleItem + 21, wxT("Radio 2"), tb3_bmp1, wxT("Radio 2"), wxITEM_RADIO);
  toolbar->AddTool(ID_SampleItem + 22, wxT("Radio 3"), tb3_bmp1, wxT("Radio 3"), wxITEM_RADIO);
  toolbar->AddSeparator();
  toolbar->AddTool(ID_SampleItem + 23, wxT("Radio 1 (Group 2)"), tb3_bmp1, wxT("Radio 1 (Group 2)"), wxITEM_RADIO);
  toolbar->AddTool(ID_SampleItem + 24, wxT("Radio 2 (Group 2)"), tb3_bmp1, wxT("Radio 2 (Group 2)"), wxITEM_RADIO);
  toolbar->AddTool(ID_SampleItem + 25, wxT("Radio 3 (Group 2)"), tb3_bmp1, wxT("Radio 3 (Group 2)"), wxITEM_RADIO);
  toolbar->SetCustomOverflowItems(prepend_items, append_items);
  toolbar->Realize();

  return toolbar;
}

wxTreeCtrl* MyFrame::CreateTreeCtrl() 
{
  wxTreeCtrl* tree = new wxTreeCtrl(this, wxID_ANY, wxPoint(0, 0), wxSize(160, 250), wxTR_DEFAULT_STYLE | wxNO_BORDER);

  wxImageList* imglist = new wxImageList(16, 16, true, 2);
  imglist->Add(wxArtProvider::GetBitmap(wxART_FOLDER, wxART_OTHER, wxSize(16, 16)));
  imglist->Add(wxArtProvider::GetBitmap(wxART_NORMAL_FILE, wxART_OTHER, wxSize(16, 16)));
  tree->AssignImageList(imglist);

  wxTreeItemId root = tree->AddRoot(wxT("wxAUI Project"), 0);
  wxArrayTreeItemIds items;

  items.Add(tree->AppendItem(root, wxT("Item 1"), 0));
  items.Add(tree->AppendItem(root, wxT("Item 2"), 0));
  items.Add(tree->AppendItem(root, wxT("Item 3"), 0));
  items.Add(tree->AppendItem(root, wxT("Item 4"), 0));
  items.Add(tree->AppendItem(root, wxT("Item 5"), 0));
  
  int i, count;
  for (i = 0, count = items.Count(); i < count; ++i)
  {
    wxTreeItemId id = items.Item(i);
    tree->AppendItem(id, wxT("Subitem 1"), 1);
    tree->AppendItem(id, wxT("Subitem 2"), 1);
    tree->AppendItem(id, wxT("Subitem 3"), 1);
    tree->AppendItem(id, wxT("Subitem 4"), 1);
    tree->AppendItem(id, wxT("Subitem 5"), 1);
  }
  tree->Expand(root);

  return tree;
}

Notebook* MyFrame::CreateNotebook()
{
  // create the notebook off-window to avoid flicker
  wxSize client_size = GetClientSize();
  Notebook* notebook_ctrl = new Notebook(this, wxID_ANY, wxPoint(client_size.x, client_size.y), wxSize(430, 200), 
    wxAUI_NB_DEFAULT_STYLE | wxAUI_NB_TAB_EXTERNAL_MOVE | wxNO_BORDER);

  notebook_ctrl->Freeze();
  const wxString title = wxString::Format(wxT("new %d"), new_page_count++);
  notebook_ctrl->AddPage(new Edit(notebook_ctrl), title);
  notebook_ctrl->Thaw();

  return notebook_ctrl;
}

wxAuiNotebook* MyFrame::CreateInfoCtrl()
{
  const wxString base_path = wxT("C:\\Users\\Randy\\Documents\\Code\\objeck-lang\\src\\objeck\\deploy");
  // TODO: move this into a class
  MyProcess process; wxExecuteEnv env;
  env.env[wxT("OBJECK_LIB_PATH")] = base_path + wxT("\\bin");
  wxString cmd = "\"";
  cmd += base_path;
  cmd += wxT("\\bin\\obc.exe\" -src '");
  cmd += base_path;
  cmd += wxT("\\examples\\hello.obs' -dest a.obe");

  const int code = wxExecute(cmd, wxEXEC_SYNC | wxEXEC_HIDE_CONSOLE, &process, &env);
  
  const wxString error_text = ReadInputStream(process.GetErrorStream());
  const wxString out_text = ReadInputStream(process.GetInputStream());
  wxString text = error_text + out_text;

  wxFont font(10, wxMODERN, wxNORMAL, wxNORMAL);

  wxTextCtrl* output_ctrl = new wxTextCtrl(this, wxID_ANY, text, wxPoint(0, 0), wxSize(150, 100), wxNO_BORDER | wxTE_MULTILINE);
  output_ctrl->SetFont(font);

  wxTextCtrl* debug_ctrl = new wxTextCtrl(this, wxID_ANY, text, wxPoint(0, 0), wxSize(150, 100), wxNO_BORDER | wxTE_MULTILINE);
  debug_ctrl->SetFont(font);

  wxAuiNotebook* info_ctrl = new wxAuiNotebook(this, wxID_ANY, wxDefaultPosition, wxSize(150, 100),
    wxAUI_NB_BOTTOM | wxAUI_NB_TAB_SPLIT | wxAUI_NB_TAB_MOVE | wxAUI_NB_SCROLL_BUTTONS | wxAUI_NB_MIDDLE_CLICK_CLOSE);    
  info_ctrl->AddPage(output_ctrl, wxT("Output"));
  info_ctrl->AddPage(debug_ctrl, wxT("Debug"));

  return info_ctrl;
}

//----------------------------------------------------------------------------
// GlobalOptions
//----------------------------------------------------------------------------

GlobalOptions::GlobalOptions(wxWindow* parent, long style) :
  wxDialog(parent, wxID_ANY, wxT("Settings"), wxDefaultPosition, wxDefaultSize, style | wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {

  wxStaticText* m_staticText4;
  wxTextCtrl* m_textCtrl4;
  wxButton* m_button6;
  wxStaticText* m_staticText6;
  wxRadioButton* win_ending;
  wxRadioButton* unix_ending;
  wxRadioButton* mac_endig;
  wxStaticText* m_staticText8;
  wxRadioButton* tab_ident;
  wxRadioButton* space_ident;
  wxSpinCtrl* ident_size;
  wxStaticText* font_select;
  wxComboBox* m_comboBox1;
  wxStaticText* m_staticText10;
  wxSpinCtrl* font_size;


  this->SetSizeHints(wxDefaultSize, wxDefaultSize);

  wxBoxSizer* bSizer1;
  bSizer1 = new wxBoxSizer(wxVERTICAL);

  wxBoxSizer* bSizer3;
  bSizer3 = new wxBoxSizer(wxHORIZONTAL);

  m_staticText4 = new wxStaticText(this, wxID_ANY, wxT("Objeck Path"), wxDefaultPosition, wxDefaultSize, 0);
  m_staticText4->Wrap(-1);
  bSizer3->Add(m_staticText4, 0, wxALL, 5);

  m_textCtrl4 = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
  bSizer3->Add(m_textCtrl4, 1, wxALL, 5);

  m_button6 = new wxButton(this, wxID_ANY, wxT("..."), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
  bSizer3->Add(m_button6, 0, wxALL, 5);


  bSizer1->Add(bSizer3, 0, wxEXPAND, 5);

  wxStaticBoxSizer* sbSizer1;
  sbSizer1 = new wxStaticBoxSizer(new wxStaticBox(this, wxID_ANY, wxT("Editor")), wxVERTICAL);

  wxFlexGridSizer* fgSizer1;
  fgSizer1 = new wxFlexGridSizer(3, 2, 0, 0);
  fgSizer1->SetFlexibleDirection(wxBOTH);
  fgSizer1->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);

  m_staticText6 = new wxStaticText(this, wxID_ANY, wxT("Line Endings"), wxDefaultPosition, wxDefaultSize, 0);
  m_staticText6->Wrap(-1);
  fgSizer1->Add(m_staticText6, 0, wxALL, 5);

  wxBoxSizer* bSizer6;
  bSizer6 = new wxBoxSizer(wxHORIZONTAL);

  win_ending = new wxRadioButton(this, wxID_ANY, wxT("Windows"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
  bSizer6->Add(win_ending, 0, wxALL, 5);

  unix_ending = new wxRadioButton(this, wxID_ANY, wxT("Unix"), wxDefaultPosition, wxDefaultSize, 0);
  bSizer6->Add(unix_ending, 0, wxALL, 5);

  mac_endig = new wxRadioButton(this, wxID_ANY, wxT("Mac"), wxDefaultPosition, wxDefaultSize, 0);
  bSizer6->Add(mac_endig, 0, wxALL, 5);


  fgSizer1->Add(bSizer6, 1, wxEXPAND | wxLEFT, 5);

  m_staticText8 = new wxStaticText(this, wxID_ANY, wxT("Indent"), wxDefaultPosition, wxDefaultSize, 0);
  m_staticText8->Wrap(-1);
  fgSizer1->Add(m_staticText8, 0, wxALL, 5);

  wxBoxSizer* bSizer7;
  bSizer7 = new wxBoxSizer(wxHORIZONTAL);

  tab_ident = new wxRadioButton(this, wxID_ANY, wxT("Tab"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
  bSizer7->Add(tab_ident, 0, wxALL, 5);

  space_ident = new wxRadioButton(this, wxID_ANY, wxT("Spaces"), wxDefaultPosition, wxDefaultSize, 0);
  bSizer7->Add(space_ident, 0, wxALL, 5);

  ident_size = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(50, -1), wxSP_ARROW_KEYS, 0, 10, 0);
  bSizer7->Add(ident_size, 0, wxALL, 5);


  fgSizer1->Add(bSizer7, 1, wxEXPAND | wxLEFT, 5);

  font_select = new wxStaticText(this, wxID_ANY, wxT("Font"), wxDefaultPosition, wxDefaultSize, 0);
  font_select->Wrap(-1);
  fgSizer1->Add(font_select, 0, wxALL, 5);

  wxBoxSizer* bSizer8;
  bSizer8 = new wxBoxSizer(wxHORIZONTAL);

  m_comboBox1 = new wxComboBox(this, wxID_ANY, wxT("Combo!"), wxDefaultPosition, wxDefaultSize, 0, NULL, 0);
  bSizer8->Add(m_comboBox1, 0, wxALL, 5);

  m_staticText10 = new wxStaticText(this, wxID_ANY, wxT("Size"), wxDefaultPosition, wxDefaultSize, 0);
  m_staticText10->Wrap(-1);
  bSizer8->Add(m_staticText10, 0, wxALL, 5);

  font_size = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 10, 0);
  font_size->SetMinSize(wxSize(50, -1));

  bSizer8->Add(font_size, 0, wxALL, 5);


  fgSizer1->Add(bSizer8, 1, wxEXPAND, 5);


  sbSizer1->Add(fgSizer1, 1, wxEXPAND, 5);


  bSizer1->Add(sbSizer1, 1, wxEXPAND, 5);


  this->SetSizer(bSizer1);
  this->Layout();

  this->Centre(wxBOTH);

  // accordingly and prevent it from being resized
  // to smaller size

  /*
  // fullname
  wxBoxSizer *fullname = new wxBoxSizer(wxHORIZONTAL);
  fullname->Add(10, 0);
  fullname->Add(new wxStaticText(this, wxID_ANY, _("Full filename"),
    wxDefaultPosition, wxSize(80, wxDefaultCoord)),
    0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
  fullname->Add(new wxStaticText(this, wxID_ANY, edit->GetFilename()),
    0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);

  // text info
  wxGridSizer *textinfo = new wxGridSizer(4, 0, 2);
  textinfo->Add(new wxStaticText(this, wxID_ANY, _("Language"),
    wxDefaultPosition, wxSize(80, wxDefaultCoord)),
    0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
  textinfo->Add(new wxStaticText(this, wxID_ANY, edit->m_language->name),
    0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
  textinfo->Add(new wxStaticText(this, wxID_ANY, _("Lexer-ID: "),
    wxDefaultPosition, wxSize(80, wxDefaultCoord)),
    0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
  text = wxString::Format(wxT("%d"), edit->GetLexer());
  textinfo->Add(new wxStaticText(this, wxID_ANY, text),
    0, wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
  wxString EOLtype = wxEmptyString;
  switch (edit->GetEOLMode()) {
  case wxSTC_EOL_CR: {EOLtype = wxT("CR (Unix)"); break; }
  case wxSTC_EOL_CRLF: {EOLtype = wxT("CRLF (Windows)"); break; }
  case wxSTC_EOL_LF: {EOLtype = wxT("CR (Macintosh)"); break; }
  }
  textinfo->Add(new wxStaticText(this, wxID_ANY, _("Line endings"),
    wxDefaultPosition, wxSize(80, wxDefaultCoord)),
    0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
  textinfo->Add(new wxStaticText(this, wxID_ANY, EOLtype),
    0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

  // text info box
  wxStaticBoxSizer *textinfos = new wxStaticBoxSizer(
    new wxStaticBox(this, wxID_ANY, _("Informations")),
    wxVERTICAL);
  textinfos->Add(textinfo, 0, wxEXPAND);
  textinfos->Add(0, 6);

  // statistic
  wxGridSizer *statistic = new wxGridSizer(4, 0, 2);
  statistic->Add(new wxStaticText(this, wxID_ANY, _("Total lines"),
    wxDefaultPosition, wxSize(80, wxDefaultCoord)),
    0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
  text = wxString::Format(wxT("%d"), edit->GetLineCount());
  statistic->Add(new wxStaticText(this, wxID_ANY, text),
    0, wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
  statistic->Add(new wxStaticText(this, wxID_ANY, _("Total chars"),
    wxDefaultPosition, wxSize(80, wxDefaultCoord)),
    0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
  text = wxString::Format(wxT("%d"), edit->GetTextLength());
  statistic->Add(new wxStaticText(this, wxID_ANY, text),
    0, wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
  statistic->Add(new wxStaticText(this, wxID_ANY, _("Current line"),
    wxDefaultPosition, wxSize(80, wxDefaultCoord)),
    0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
  text = wxString::Format(wxT("%d"), edit->GetCurrentLine());
  statistic->Add(new wxStaticText(this, wxID_ANY, text),
    0, wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
  statistic->Add(new wxStaticText(this, wxID_ANY, _("Current pos"),
    wxDefaultPosition, wxSize(80, wxDefaultCoord)),
    0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
  text = wxString::Format(wxT("%d"), edit->GetCurrentPos());
  statistic->Add(new wxStaticText(this, wxID_ANY, text),
    0, wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

  // char/line statistics
  wxStaticBoxSizer *statistics = new wxStaticBoxSizer(
    new wxStaticBox(this, wxID_ANY, _("Statistics")),
    wxVERTICAL);
  statistics->Add(statistic, 0, wxEXPAND);
  statistics->Add(0, 6);

  // total pane
  wxBoxSizer *totalpane = new wxBoxSizer(wxVERTICAL);
  totalpane->Add(fullname, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);
  totalpane->Add(0, 6);
  totalpane->Add(textinfos, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
  totalpane->Add(0, 10);
  totalpane->Add(statistics, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
  totalpane->Add(0, 6);
  wxButton *okButton = new wxButton(this, wxID_OK, _("OK"));
  okButton->SetDefault();
  totalpane->Add(okButton, 0, wxALIGN_CENTER | wxALL, 10);

  SetSizerAndFit(totalpane);
  */

  ShowModal();
}


