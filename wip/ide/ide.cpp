//////////////////////////////////////////////////////////////////////////////
// Original authors:  Wyo, John Labenski, Otto Wyss
// Copyright: (c) wxGuide, (c) John Labenski, Otto Wyss
// Modified by: Randy Hollines
// Licence: wxWindows licence
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

wxMenuBar* MyFrame::CreateMenuBar()
{
  // File menu
  wxMenu *menuFile = new wxMenu;
  menuFile->Append(wxID_NEW, _("&New ..\tCtrl+N"));
  menuFile->Append(wxID_OPEN, _("&Open ..\tCtrl+O"));
  menuFile->Append(wxID_SAVE, _("&Save\tCtrl+S"));
  menuFile->Append(wxID_SAVEAS, _("Save &as ..\tCtrl+Shift+S"));
  menuFile->Append(wxID_CLOSE, _("&Close\tCtrl+W"));

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
  wxArrayString output, errors;
  int code = wxExecute("\"C:\\Users\\Randy\\Documents\\Code\\objeck-lang\\src\\objeck\\deploy\\bin\\obc.exe\"", output, errors);
  
  wxString text;
  if (errors.size() > 0) {
    for (size_t i = 0; i < errors.size(); ++i) {
      text += errors[i] + wxT('\n');
    }
  }
  else {
    for (size_t i = 0; i < output.size(); ++i) {
      text += output[i] + wxT('\n');
    }
  }

  wxAuiNotebook* info_ctrl = new wxAuiNotebook(this, wxID_ANY, wxDefaultPosition, wxSize(150, 100),
    wxAUI_NB_BOTTOM | wxAUI_NB_TAB_SPLIT | wxAUI_NB_TAB_MOVE | wxAUI_NB_SCROLL_BUTTONS | wxAUI_NB_MIDDLE_CLICK_CLOSE);    
  info_ctrl->AddPage(new wxTextCtrl(this, wxID_ANY, text, wxPoint(0, 0), wxSize(150, 100), wxNO_BORDER | wxTE_MULTILINE), wxT("Output"));
  info_ctrl->AddPage(new wxTextCtrl(this, wxID_ANY, wxEmptyString), wxT("Debug"));

  return info_ctrl;
}
