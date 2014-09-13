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
#include <wx/persist.h>
#include <wx/persist/toplevel.h>
#include <wx/file.h>
#include <wx/filename.h>

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
EVT_MENU(myID_NEW_FILE, MyFrame::OnFileNew)
EVT_MENU(myID_NEW_PROJECT, MyFrame::OnProjectNew)
EVT_MENU(myID_OPEN_FILE, MyFrame::OnFileOpen)
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
  m_iniManager = new IniManager(wxT("ide.ini"));
  m_projectManager = NULL;
  m_newPageCount = 1;
  
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
  SetMinSize(wxSize(800, 600));
  
  // set tool bar
  aui_manager.AddPane(DoCreateToolBar(), wxAuiPaneInfo().
                      Name(wxT("toolbar")).Caption(wxT("Toolbar 3")).
                      ToolbarPane().Top().Row(1).Position(1));
  
  // update
  m_notebook->SetFocus();
  aui_manager.Update();
}

MyFrame::~MyFrame() 
{
  if(m_iniManager) {
    delete m_iniManager;
    m_iniManager = NULL;
  }
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
void MyFrame::OnProjectNew(wxCommandEvent &WXUNUSED(event))
{
  NewProject project_dialog(this);
  if(project_dialog.ShowModal() == wxID_OK) {
    const wxString name = project_dialog.GetName();
    const wxString path = project_dialog.GetPath();
    
    wxFileName full_name(path + wxFileName::GetPathSeparator() + name + wxT(".obp"));
    if(full_name.FileExists()) {
      wxMessageDialog fileOverWrite(this, wxT("File ") + full_name.GetFullName() + wxT(" already exists.\nWould you like to overwrite it?"),
                                    "Overwrite File", wxCENTER | wxNO_DEFAULT | wxYES_NO | wxICON_INFORMATION);
      if(fileOverWrite.ShowModal() == wxID_YES) {
        m_projectManager = new ProjectManager(this, name, full_name.GetFullPath());
      }
    }
    else {
      // m_projectManager = new ProjectManager(this, name, full_name.GetFullPath());
      m_projectManager = new ProjectManager(this, L"/home/randy/Documents/Code/objeck-lang/wip/ide/proj.ini");
      m_projectManager->GetFiles();
    }
  }
}

void MyFrame::OnFileNew(wxCommandEvent &WXUNUSED(event))
{
  m_notebook->Freeze();
  const wxString title = wxString::Format(wxT("new %zu"), m_newPageCount++);
  m_notebook->AddPage(new Edit(m_notebook), title);
  m_notebook->SetSelection(m_notebook->GetPageCount() - 1);
  m_notebook->Thaw();
}

void MyFrame::OnFileOpen(wxCommandEvent &WXUNUSED(event)) 
{
  wxFileDialog dlg(this, wxT("Open file"), wxEmptyString, wxEmptyString, 
    wxT("Objeck files (*.obs)|*.obs;*.obw|All types (*.*)|*.*"),
    wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_CHANGE_DIR);
  if(dlg.ShowModal() != wxID_OK) {
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
  m_iniManager->ShowOptionsDialog(this);
}

wxMenuBar* MyFrame::CreateMenuBar()
{
  // File menu
  wxMenu* menuFile = new wxMenu;
  // new
  wxMenu* menuFileNew = new wxMenu;
  menuFileNew->Append(myID_NEW_FILE, _("&File\tCtrl+N"));
  menuFileNew->Append(myID_NEW_PROJECT, _("&Project\tCtrl+Shift+N"));
  menuFile->Append(wxID_ANY, _("New..."), menuFileNew);
  // open
  wxMenu* menuFileOpen = new wxMenu;
  menuFileOpen->Append(myID_OPEN_FILE, _("&File\tCtrl+O"));
  menuFileOpen->Append(myID_OPEN_PROJECT, _("&Project\tCtrl+Shift+O"));
  menuFile->Append(wxID_ANY, _("Open..."), menuFileOpen);
  
  menuFile->Append(wxID_SAVE, _("&Save\tCtrl+S"));
  menuFile->Append(wxID_SAVEAS, _("Save &as...\tCtrl+Shift+S"));

  // close
  wxMenu* menuFileClose = new wxMenu;
  menuFileClose->Append(myID_CLOSE_FILE, _("&File\tCtrl+W"));
  menuFileClose->Append(myID_CLOSE_PROJECT, _("&Project\tCtrl+Shift+W"));  
  menuFile->Append(wxID_ANY, _("Close..."), menuFileClose);
  
  menuFile->AppendSeparator();
  menuFile->Append(wxID_PROPERTIES, _("Proper&ties\tCtrl+T"));
  
  // edit menu
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
  menuEdit->Append(myID_DLG_OPTIONS, _("Genearl Options...\tALT+O"));
  
  // view menu
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

  // project menu
  wxMenu *projectView = new wxMenu;
  projectView->AppendCheckItem(wxID_ANY, _("Build\tCtrl+Shift+B"));
  projectView->AppendSeparator();
  projectView->AppendCheckItem(wxID_ANY, _("&Add file\tCtrl+Shift+A"));
  projectView->AppendCheckItem(wxID_ANY, _("&Remove file\tCtrl+Shift+R"));
  projectView->AppendSeparator();
  projectView->Append(wxID_ANY, _("Project options...\tALT+Shift+O"));
  
  // menu bar
  wxMenuBar* menu_bar = new wxMenuBar;
  menu_bar->Append(menuFile, wxT("&File"));
  menu_bar->Append(menuEdit, wxT("&Edit"));
  menu_bar->Append(projectView, wxT("&Project"));
  menu_bar->Append(menuView, wxT("&View"));
  menu_bar->Append(new wxMenu, wxT("&Help"));

  return menu_bar;
}

wxAuiToolBar* MyFrame::DoCreateToolBar()
{
  wxAuiToolBarItemArray prepend_items;
  wxAuiToolBarItemArray append_items;
  wxAuiToolBar* toolbar = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
    wxAUI_TB_DEFAULT_STYLE | wxAUI_TB_OVERFLOW);
  toolbar->SetToolBitmapSize(wxSize(16, 16));
  
  wxBitmap tb3_bmp1 = wxArtProvider::GetBitmap(wxART_FOLDER, wxART_OTHER, wxSize(16, 16));

  toolbar->AddTool(ID_SampleItem + 16, wxT("New File"), wxArtProvider::GetBitmap(wxART_NEW, wxART_OTHER, wxSize(16, 16)), wxT("Check 1"), wxITEM_CHECK);
  toolbar->AddTool(ID_SampleItem + 17, wxT("Open File"), wxArtProvider::GetBitmap(wxART_FILE_OPEN, wxART_OTHER, wxSize(16, 16)), wxT("Check 2"), wxITEM_CHECK);
  toolbar->AddTool(ID_SampleItem + 18, wxT("Save File"), wxArtProvider::GetBitmap(wxART_FILE_SAVE, wxART_OTHER, wxSize(16, 16)), wxT("Check 3"), wxITEM_CHECK);
  toolbar->AddTool(ID_SampleItem + 19, wxT("Save File As..."), wxArtProvider::GetBitmap(wxART_FILE_SAVE_AS, wxART_OTHER, wxSize(16, 16)), wxT("Check 4"), wxITEM_CHECK);
  toolbar->AddSeparator();
  toolbar->AddTool(ID_SampleItem + 20, wxT("Copy"), wxArtProvider::GetBitmap(wxART_COPY, wxART_OTHER, wxSize(16, 16)), wxT("Copy"), wxITEM_RADIO);
  toolbar->AddTool(ID_SampleItem + 21, wxT("Cut"), wxArtProvider::GetBitmap(wxART_CUT, wxART_OTHER, wxSize(16, 16)), wxT("Cut"), wxITEM_RADIO);
  toolbar->AddTool(ID_SampleItem + 22, wxT("Paste"), wxArtProvider::GetBitmap(wxART_PASTE, wxART_OTHER, wxSize(16, 16)), wxT("Paste"), wxITEM_RADIO);
  toolbar->AddTool(ID_SampleItem + 23, wxT("Undo"), wxArtProvider::GetBitmap(wxART_UNDO, wxART_OTHER, wxSize(16, 16)), wxT("Undo"), wxITEM_RADIO);
  toolbar->AddTool(ID_SampleItem + 24, wxT("Redo"), wxArtProvider::GetBitmap(wxART_REDO, wxART_OTHER, wxSize(16, 16)), wxT("Redo"), wxITEM_RADIO);
  toolbar->AddSeparator();
  toolbar->AddTool(ID_SampleItem + 25, wxT("Build Project"), wxArtProvider::GetBitmap(wxART_GO_HOME, wxART_OTHER, wxSize(16, 16)), wxT("Build Project"), wxITEM_RADIO);toolbar->AddSeparator();
  toolbar->AddTool(ID_SampleItem + 26, wxT("Project Options"), wxArtProvider::GetBitmap(wxART_HELP_SETTINGS, wxART_OTHER, wxSize(16, 16)), wxT("Project Options"), wxITEM_RADIO);
  // toolbar->SetCustomOverflowItems(prepend_items, append_items);
  toolbar->Realize();

  return toolbar;
}

wxTreeCtrl* MyFrame::CreateTreeCtrl() 
{
  wxTreeCtrl* tree = new wxTreeCtrl(this, wxID_ANY, wxPoint(0, 0), wxSize(160, 250), wxTR_DEFAULT_STYLE | wxNO_BORDER);

  wxImageList* imglist = new wxImageList(16, 16, true, 2);
  imglist->Add(wxArtProvider::GetBitmap(wxART_GO_HOME, wxART_OTHER, wxSize(16, 16)));
  imglist->Add(wxArtProvider::GetBitmap(wxART_LIST_VIEW, wxART_OTHER, wxSize(16, 16)));
  imglist->Add(wxArtProvider::GetBitmap(wxART_NORMAL_FILE, wxART_OTHER, wxSize(16, 16)));
  imglist->Add(wxArtProvider::GetBitmap(wxART_EXECUTABLE_FILE, wxART_OTHER, wxSize(16, 16)));
  tree->AssignImageList(imglist);

  // root
  wxTreeItemId root = tree->AddRoot(wxT("XML Parser"), 0);

  // source
  wxArrayTreeItemIds source_items;
  wxTreeItemId source = tree->AppendItem(root, wxT("Source"), 1);
  source_items.Add(tree->AppendItem(source, wxT("scanner.obs"), 2));
  source_items.Add(tree->AppendItem(source, wxT("tree.obs"), 2));
  source_items.Add(tree->AppendItem(source, wxT("print.obs"), 2));
  
  // libraries
  wxArrayTreeItemIds lib_items;
  wxTreeItemId libs = tree->AppendItem(root, wxT("Libaries"), 1);
  lib_items.Add(tree->AppendItem(libs, wxT("lang.obl"), 3));
  lib_items.Add(tree->AppendItem(libs, wxT("collect.obl"), 3));

  tree->Expand(root);
  tree->Expand(source);
  tree->Expand(libs);
 
  return tree;
}

Notebook* MyFrame::CreateNotebook()
{
  // create the notebook off-window to avoid flicker
  wxSize client_size = GetClientSize();
  Notebook* notebook_ctrl = new Notebook(this, wxID_ANY, wxPoint(client_size.x, client_size.y), wxSize(430, 200), 
    wxAUI_NB_DEFAULT_STYLE | wxAUI_NB_TAB_EXTERNAL_MOVE | wxNO_BORDER);

  notebook_ctrl->Freeze();
  const wxString title = wxT("new");
  notebook_ctrl->AddPage(new Edit(notebook_ctrl), title);
  notebook_ctrl->Thaw();

  return notebook_ctrl;
}

wxAuiNotebook* MyFrame::CreateInfoCtrl()
{
  wxString text;

  // TODO: move to OnBuild
  /*
  if(m_globalOptions) {
    // const wxString base_path = wxT("C:\\Users\\Randy\\Documents\\Code\\objeck-lang\\src\\objeck\\deploy");
    // const wxString base_path = wxT("/home/objeck/Documents/Code/objeck-lang/src/objeck/deploy");
    const wxString base_path = m_globalOptions->GetPath();
  
    // TODO: move this into a class
    MyProcess process; wxExecuteEnv env;
    // env.env[wxT("OBJECK_LIB_PATH")] = base_path + wxT("\\bin");
    env.env[wxT("OBJECK_LIB_PATH")] = base_path + wxT("/bin");
    env.env[wxT("LANG")] = wxT("en_US.UTF-8");
  
    wxString cmd = wxT("\"");
    cmd += base_path;
    // cmd += wxT("\\bin\\obc.exe\" -src '");
    cmd += wxT("/bin/obc\" -src '");
    cmd += base_path;
    // cmd += wxT("\\examples\\hello.obs' -dest a.obe");
    cmd += wxT("/examples/hello.obs' -dest a.obe");

    const int code = wxExecute(cmd.mb_str(), wxEXEC_SYNC | wxEXEC_HIDE_CONSOLE, &process, &env);
  
    const wxString error_text = ReadInputStream(process.GetErrorStream());
    const wxString out_text = ReadInputStream(process.GetInputStream());
    text = error_text + out_text;
  } 		
  */

  wxFont font(10, wxMODERN, wxNORMAL, wxNORMAL);
  wxTextCtrl* output_ctrl = new wxTextCtrl(this, wxID_ANY, text, wxPoint(0, 0), wxSize(150, 100), wxNO_BORDER | wxTE_MULTILINE);
  output_ctrl->SetFont(font);

  wxTextCtrl* debug_ctrl = new wxTextCtrl(this, wxID_ANY, text, wxPoint(0, 0), wxSize(150, 100), wxNO_BORDER | wxTE_MULTILINE);
  debug_ctrl->SetFont(font);
  
  wxAuiNotebook* info_ctrl = new wxAuiNotebook(this, wxID_ANY, wxDefaultPosition, wxSize(150, 100),
    wxAUI_NB_BOTTOM | wxAUI_NB_TAB_SPLIT | wxAUI_NB_TAB_MOVE | wxAUI_NB_SCROLL_BUTTONS | wxAUI_NB_MIDDLE_CLICK_CLOSE);    
  info_ctrl->AddPage(output_ctrl, wxT("Build"));
  info_ctrl->AddPage(debug_ctrl, wxT("Output"));

  return info_ctrl;
}



