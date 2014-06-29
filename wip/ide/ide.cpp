//////////////////////////////////////////////////////////////////////////////
// Purpose:     Objeck IDE Demo
// Maintainer:  Modified by Randy Hollines
// Maintainer:  Otto Wyss
// Created:     2003-09-01
// Modified By: Randy Hollines (c) 2014
// Copyright:   (c) wxGuide
// Licence:     wxWindows licence
//////////////////////////////////////////////////////////////////////////////

#include "ide.h"

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
  EVT_MENU(wxID_OPEN, MyFrame::OnFileOpen)
  EVT_MENU(wxID_SAVE, MyFrame::OnFileSave)
  EVT_MENU(wxID_SAVEAS, MyFrame::OnFileSaveAs)
  EVT_MENU(wxID_CLOSE, MyFrame::OnFileClose)
  // And all our edit-related menu commands.  
  EVT_MENU(myID_DLG_FIND_TEXT, MyFrame::OnEdit)
  EVT_MENU(myID_FINDNEXT, MyFrame::OnEdit)
  EVT_MENU_RANGE(myID_EDIT_FIRST, myID_EDIT_LAST, MyFrame::OnEdit)
END_EVENT_TABLE()

MyFrame::MyFrame(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) : 
    wxFrame(parent, id, title, pos, size, style) 
{
  // setup window manager
  aui_manager.SetManagedWindow(this);
  aui_manager.AddPane(CreateTreeCtrl(), wxAuiPaneInfo().Left());
  m_notebook = CreateNotebook(), 
    aui_manager.AddPane(m_notebook, wxAuiPaneInfo().Centre());
  aui_manager.AddPane(CreateTextCtrl(wxT("blah")), wxAuiPaneInfo().Bottom());
  
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

void MyFrame::OnClose(wxCloseEvent &event) 
{
  /*
  wxCommandEvent evt;
  OnFileClose(evt);
  if (m_edit && m_edit->Modified()) {
    if (event.CanVeto()) event.Veto(true);
    return;
  }
  Destroy();
  */
}

// file event handlers
void MyFrame::OnFileOpen(wxCommandEvent &WXUNUSED(event)) 
{
  if (!m_notebook) {
    return;
  }

  wxFileDialog dlg(this, wxT("Open file"), wxEmptyString, wxEmptyString, wxT("Any file (*)|*"),
    wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_CHANGE_DIR);
  if (dlg.ShowModal() != wxID_OK) {
    return;
  }
  m_notebook->OpenFile(dlg.GetPath());
}

void MyFrame::OnFileSave(wxCommandEvent &WXUNUSED(event)) 
{
  if (!m_notebook->GetEdit()) return;

  if (!m_notebook->GetEdit()->Modified()) {
    wxMessageBox(_("There is nothing to save!"), _("Save file"),
      wxOK | wxICON_EXCLAMATION);
    return;
  }
  m_notebook->GetEdit()->SaveFile();
}

void MyFrame::OnFileSaveAs(wxCommandEvent &WXUNUSED(event)) 
{
  if (!m_notebook->GetEdit()) return;
#if wxUSE_FILEDLG
  wxString filename = wxEmptyString;
  wxFileDialog dlg(this, wxT("Save file"), wxEmptyString, wxEmptyString, wxT("Any file (*)|*"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
  if (dlg.ShowModal() != wxID_OK) return;
  filename = dlg.GetPath();
  m_notebook->GetEdit()->SaveFile(filename);
#endif // wxUSE_FILEDLG
}

void MyFrame::OnFileClose(wxCommandEvent &WXUNUSED(event)) 
{
  if (!m_notebook->GetEdit()) return;
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
  m_notebook->GetEdit()->SetFilename(wxEmptyString);
  m_notebook->GetEdit()->ClearAll();
  m_notebook->GetEdit()->SetSavePoint();
}

wxMenuBar* MyFrame::CreateMenuBar()
{
  // File menu
  wxMenu *menuFile = new wxMenu;
  menuFile->Append(wxID_OPEN, _("&Open ..\tCtrl+O"));
  menuFile->Append(wxID_SAVE, _("&Save\tCtrl+S"));
  menuFile->Append(wxID_SAVEAS, _("Save &as ..\tCtrl+Shift+S"));
  menuFile->Append(wxID_CLOSE, _("&Close\tCtrl+W"));

  // Edit menu
  wxMenu *menuEdit = new wxMenu;
  menuEdit->Append(wxID_UNDO, _("&Undo\tCtrl+Z"));
  menuEdit->Append(wxID_REDO, _("&Redo\tCtrl+Shift+Z"));
  menuEdit->AppendSeparator();
  menuEdit->Append(wxID_CUT, _("Cu&t\tCtrl+X"));
  menuEdit->Append(wxID_COPY, _("&Copy\tCtrl+C"));
  menuEdit->Append(wxID_PASTE, _("&Paste\tCtrl+V"));
  menuEdit->Append(wxID_CLEAR, _("&Delete\tDel"));
  menuEdit->AppendSeparator();
  menuEdit->Append(myID_DLG_FIND_TEXT, _("&Find\tCtrl+F"));
  menuEdit->Append(myID_FINDNEXT, _("Find &next\tF3"));
  menuEdit->Append(myID_REPLACE, _("&Replace\tCtrl+H"));
  menuEdit->Append(myID_REPLACENEXT, _("Replace &again\tShift+F4"));
  
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

// Demo tree
wxTreeCtrl* MyFrame::CreateTreeCtrl() 
{
  wxTreeCtrl* tree = new wxTreeCtrl(this, wxID_ANY,
    wxPoint(0, 0), wxSize(160, 250),
    wxTR_DEFAULT_STYLE | wxNO_BORDER);

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

  Notebook* ctrl = new Notebook(this, wxID_ANY,
    wxPoint(client_size.x, client_size.y),
    wxSize(430, 200),
    wxAUI_NB_DEFAULT_STYLE | wxAUI_NB_TAB_EXTERNAL_MOVE | wxNO_BORDER);

  ctrl->Freeze();
  wxBitmap page_bmp = wxArtProvider::GetBitmap(wxART_NORMAL_FILE, wxART_OTHER, wxSize(16, 16));
  ctrl->AddPage(new Edit(ctrl), wxT("new"));

  ctrl->Thaw();

  return ctrl;
}

wxHtmlWindow* MyFrame::CreateHTMLCtrl(wxWindow* parent)
{
  if (!parent)
    parent = this;

  wxHtmlWindow* ctrl = new wxHtmlWindow(parent, wxID_ANY,
    wxDefaultPosition,
    wxSize(400, 300));
  ctrl->SetPage(GetIntroText());
  return ctrl;
}

// Demo text
wxTextCtrl* MyFrame::CreateTextCtrl(const wxString& ctrl_text)
{
  static int n = 0;

  wxString text;
  if (!ctrl_text.empty())
    text = ctrl_text;
  else
    text.Printf(wxT("This is text box %d"), ++n);

  return new wxTextCtrl(this, wxID_ANY, text,
    wxPoint(0, 0), wxSize(150, 90),
    wxNO_BORDER | wxTE_MULTILINE);
}
