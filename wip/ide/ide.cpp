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
  m_iniManager = new InIManager(wxT("ide.ini"));
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
  SetMinSize(wxSize(400, 300));
  
  // set tool bar
  aui_manager.AddPane(DoCreateToolBar(), wxAuiPaneInfo().
                      Name(wxT("toolbar")).Caption(wxT("Toolbar 3")).
                      ToolbarPane().Top().Row(1).Position(1));
  
  // set global options dialog
  m_globalOptions = new GlobalOptions(this, m_iniManager, 0);
  m_globalOptions->SetName("My Global Options");
  
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
void MyFrame::OnFileNew(wxCommandEvent &WXUNUSED(event))
{
  m_notebook->Freeze();
  const wxString title = wxString::Format(wxT("new %d"), m_newPageCount++);
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
  m_globalOptions->DoShow();
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

wxAuiToolBar* MyFrame::DoCreateToolBar()
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
  const wxString title = wxT("new");
  notebook_ctrl->AddPage(new Edit(notebook_ctrl), title);
  notebook_ctrl->Thaw();

  return notebook_ctrl;
}

wxAuiNotebook* MyFrame::CreateInfoCtrl()
{
  wxString text;

  // const wxString base_path = wxT("C:\\Users\\Randy\\Documents\\Code\\objeck-lang\\src\\objeck\\deploy");
  const wxString base_path = wxT("/home/objeck/Documents/Code/objeck-lang/src/objeck/deploy");
  
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
  const char* cc = text.mb_str(); 		
  
  wxFont font(10, wxMODERN, wxNORMAL, wxNORMAL);
  wxTextCtrl* output_ctrl = new wxTextCtrl(this, wxID_ANY, cc, wxPoint(0, 0), wxSize(150, 100), wxNO_BORDER | wxTE_MULTILINE);
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
// InIManager
//----------------------------------------------------------------------------
/******************************
 * Load file into memory
 ******************************/
wstring InIManager::LoadFile(wstring filename) {
  char* buffer;

  string fn(filename.begin(), filename.end());
  ifstream in(fn.c_str(), ios_base::in | ios_base::binary | ios_base::ate);
  if(in.good()) {
    // get file size
    in.seekg(0, ios::end);
    size_t buffer_size = (size_t)in.tellg();
    in.seekg(0, ios::beg);
    buffer = (char*)calloc(buffer_size + 1, sizeof(char));
    in.read(buffer, buffer_size);
    // close file
    in.close();
  }
  else {
    wcerr << L"Unable to read file: " << filename << endl;
    exit(1);
  }  
  wstring out = BytesToUnicode(buffer);
    
  free(buffer);
  return out;
}
 
/******************************
 * Write file
 ******************************/
bool InIManager::WriteFile(const wstring &filename, const wstring &output) {
  string fn(filename.begin(), filename.end());
  ofstream out(fn.c_str(), ios_base::out | ios_base::binary);
  if(out.good()) {
    const string bytes = UnicodeToBytes(output);
    out.write(bytes.c_str(), bytes.size());      
    // close file
    out.close();
    return true;
  }
  else {
    wcerr << L"Unable to write file: " << filename << endl;
    exit(1);
  }  
    
  return false;
}
 
/******************************
 * Next parse token
 ******************************/
void InIManager::NextChar() {
  if(cur_pos < input.size()) {
    cur_char = input[cur_pos++];
    if(cur_pos < input.size()) {
      next_char = input[cur_pos];
    }
    else {
      next_char = L'\0';
    }
  }
  else {
    cur_char = next_char = L'\0';
  }
}
  
/******************************
 * Clear sections and names/values
 ******************************/
void InIManager::Clear() {
  map<const wstring, map<const wstring, wstring>*>::iterator iter;
  for(iter = section_map.begin(); iter != section_map.end(); ++iter) {
    map<const wstring, wstring>* value_map = iter->second;
    value_map->clear();
    // free map
    delete value_map;
    value_map = NULL;
  }

  if(!section_map.empty()) {
    section_map.clear();
  }
}
  
/******************************
 * Serializes internal structures 
 ******************************/
wstring InIManager::Serialize() {
  wstring out;
  // sections
  map<const wstring, map<const wstring, wstring>*>::iterator section_iter;
  for(section_iter = section_map.begin(); section_iter != section_map.end(); ++section_iter) {
    out += L"[";
    out += section_iter->first;
    out += L"]\r\n";
    // name/value pairs
    map<const wstring, wstring>::iterator value_iter;
    for(value_iter = section_iter->second->begin(); value_iter != section_iter->second->end(); ++value_iter) {
      out += value_iter->first;
      out += L"=";
      out += value_iter->second;
      out += L"\r\n";
    }
  }

  return out;
}

/******************************
 * Parses setions and name/value
 * pairs and loads internal 
 * structures 
 ******************************/
void InIManager::Deserialize() {
  map<const wstring, wstring>* value_map = NULL;
    
  NextChar();    
  while(cur_char != L'\0') {
    // ignore white space
    while(cur_char == L' ' || cur_char == L'\t' || cur_char == L'\r' || cur_char == L'\n') {
      NextChar();
    }
      
    // parse section
    size_t start;
    if(cur_char == L'[') {
      start = cur_pos;
      while(cur_pos < input.size() && iswprint(cur_char) && cur_char != L']') {
        NextChar();
      }
      const wstring section = input.substr(start, cur_pos - start - 1);
      if(cur_char == L']') {
        NextChar();
      }        
      value_map = new map<const wstring, wstring>;
      section_map.insert(pair<const wstring, map<const wstring, wstring>*>(section, value_map));
    }
    // comment
    else if(cur_char == L'#') {
      while(cur_pos < input.size() && cur_char != L'\r' && cur_char != L'\n') {
        NextChar();
      }
    }
    // key/value
    else if(iswalpha(cur_char)) {
      start = cur_pos - 1;
      while(cur_pos < input.size() && iswprint(cur_char) && cur_char != L'=') {
        NextChar();
      }
      const wstring key = input.substr(start, cur_pos - start - 1);
      NextChar();
        
      wstring value;
      start = cur_pos - 1;
      while(cur_pos < input.size() && iswprint(cur_char) && cur_char != L'\r' && cur_char != L'\n') {
        if(cur_char == L'\\') {
          switch(next_char) {
          case L'n':
            value += L'\n';
            NextChar();
            break;
          case L'r':
            value += L'\r';
            NextChar();
            break;
          default:
            value += L'\\';
            break;
          }
        }
        else {
          value += cur_char;
        }
          
        NextChar();
      }
      
      // add key/value pair
      if(value_map) {
        value_map->insert(pair<const wstring, wstring>(key, value));
      }
    } 
  }
}

/******************************
 * Constructor/deconstructor
 ******************************/
InIManager::InIManager(const wstring &f) {
  filename = f;
  cur_char = next_char = L'\0';
  cur_pos = 0;

  Read();
}
  
InIManager::~InIManager() {
  Write();
  Clear();
}
  
/******************************
 * Fetch value per section and key
 ******************************/
wstring InIManager::GetValue(const wstring &sec, const wstring &key) {
  map<const wstring, map<const wstring, wstring>*>::iterator section = section_map.find(sec);
  if(section != section_map.end()) {
    map<const wstring, wstring>::iterator value = section->second->find(key);
    if(value != section->second->end()) {
      return value->second;
    }
  }
    
  return L"";
}
  
/******************************
 * Fetch value per section and key
 ******************************/
void InIManager::SetValue(const wstring &sec, const wstring &key, wstring &value) {
  map<const wstring, map<const wstring, wstring>*>::iterator section = section_map.find(sec);
  if(section != section_map.end()) {
    (*section->second)[key] = value;
  }
}

/******************************
 * Write contentes of memory
 * to file
 ******************************/
void InIManager::Read() {
  Clear();
    
  input = LoadFile(filename);
  if(input.size() > 0) {
    Deserialize();
  }
}

/******************************
 * Write contentes of memory
 * to file
 ******************************/
void InIManager::Write() {
  const wstring output = Serialize();
  if(output.size() > 0) {
    WriteFile(filename, output);
  }
}

//----------------------------------------------------------------------------
// GlobalOptions
//----------------------------------------------------------------------------

GlobalOptions::GlobalOptions(wxWindow* parent, InIManager* ini, long style) :
  wxDialog(parent, wxID_ANY, wxT("Settings"), wxDefaultPosition, wxDefaultSize, style | wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
  m_IniManager = ini;
  SetSizeHints(wxDefaultSize, wxDefaultSize);

  wxBoxSizer* bSizer1 = new wxBoxSizer(wxVERTICAL);

  wxBoxSizer* bSizer3 = new wxBoxSizer(wxHORIZONTAL);

  wxStaticText* staticText4 = new wxStaticText(this, wxID_ANY, wxT("Objeck Path"), wxDefaultPosition, wxDefaultSize, 0);
  staticText4->Wrap(-1);
  bSizer3->Add(staticText4, 0, wxALL, 5);
  
  wxString path_string = m_IniManager->GetValue(wxT("Options"), wxT("path"));
  m_textCtrl4 = new wxTextCtrl(this, wxID_ANY, path_string, wxDefaultPosition, wxDefaultSize, 0);
  bSizer3->Add(m_textCtrl4, 1, wxALL, 5);

  m_pathButton = new wxButton(this, wxID_ANY, wxT("..."), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
  bSizer3->Add(m_pathButton, 0, wxALL, 5);
  
  bSizer1->Add(bSizer3, 0, wxEXPAND, 5);
  wxStaticBoxSizer* sbSizer1 = new wxStaticBoxSizer(new wxStaticBox(this, wxID_ANY, wxT("Editor")), wxVERTICAL);

  wxFlexGridSizer* fgSizer1;
  fgSizer1 = new wxFlexGridSizer(3, 2, 0, 0);
  fgSizer1->SetFlexibleDirection(wxBOTH);
  fgSizer1->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);

  wxStaticText* staticText6 = new wxStaticText(this, wxID_ANY, wxT("Line Endings"), wxDefaultPosition, wxDefaultSize, 0);
  staticText6->Wrap(-1);
  fgSizer1->Add(staticText6, 0, wxALL, 5);

  wxBoxSizer* bSizer6 = new wxBoxSizer(wxHORIZONTAL);

  m_winEnding = new wxRadioButton(this, wxID_ANY, wxT("Windows"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
  bSizer6->Add(m_winEnding, 0, wxALL, 5);

  m_unixEnding = new wxRadioButton(this, wxID_ANY, wxT("Unix"), wxDefaultPosition, wxDefaultSize, 0);
  bSizer6->Add(m_unixEnding, 0, wxALL, 5);

  m_macEndig = new wxRadioButton(this, wxID_ANY, wxT("Mac"), wxDefaultPosition, wxDefaultSize, 0);
  bSizer6->Add(m_macEndig, 0, wxALL, 5);


  fgSizer1->Add(bSizer6, 1, wxEXPAND | wxLEFT, 5);

  wxStaticText* staticText8 = new wxStaticText(this, wxID_ANY, wxT("Indent"), wxDefaultPosition, wxDefaultSize, 0);
  staticText8->Wrap(-1);
  fgSizer1->Add(staticText8, 0, wxALL, 5);

  wxBoxSizer* bSizer7 = new wxBoxSizer(wxHORIZONTAL);

  m_tabIdent = new wxRadioButton(this, wxID_ANY, wxT("Tab"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
  bSizer7->Add(m_tabIdent, 0, wxALL, 5);

  m_spaceIdent = new wxRadioButton(this, wxID_ANY, wxT("Spaces"), wxDefaultPosition, wxDefaultSize, 0);
  bSizer7->Add(m_spaceIdent, 0, wxALL, 5);

  m_identSize = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(50, -1), wxSP_ARROW_KEYS, 0, 10, 0);
  bSizer7->Add(m_identSize, 0, wxALL, 5);


  fgSizer1->Add(bSizer7, 1, wxEXPAND | wxLEFT, 5);

  m_fontSelect = new wxStaticText(this, wxID_ANY, wxT("Font"), wxDefaultPosition, wxDefaultSize, 0);
  m_fontSelect->Wrap(-1);
  fgSizer1->Add(m_fontSelect, 0, wxALL, 5);

  wxBoxSizer* bSizer8 = new wxBoxSizer(wxHORIZONTAL);

  m_comboBox1 = new wxComboBox(this, wxID_ANY, wxT("Combo!"), wxDefaultPosition, wxDefaultSize, 0, NULL, 0);
  bSizer8->Add(m_comboBox1, 0, wxALL, 5);

  wxStaticText* staticText10 = new wxStaticText(this, wxID_ANY, wxT("Size"), wxDefaultPosition, wxDefaultSize, 0);
  staticText10->Wrap(-1);
  bSizer8->Add(staticText10, 0, wxALL, 5);

  font_size = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 10, 0);
  font_size->SetMinSize(wxSize(50, -1));

  bSizer8->Add(font_size, 0, wxALL, 5);
  fgSizer1->Add(bSizer8, 1, wxEXPAND, 5);
  sbSizer1->Add(fgSizer1, 1, wxEXPAND, 5);
  bSizer1->Add(sbSizer1, 1, wxEXPAND, 5);

  m_sdbSizer1 = new wxStdDialogButtonSizer();
  m_sdbSizer1OK = new wxButton(this, wxID_OK);
  m_sdbSizer1->AddButton(m_sdbSizer1OK);
  m_sdbSizer1Cancel = new wxButton(this, wxID_CANCEL);
  m_sdbSizer1->AddButton(m_sdbSizer1Cancel);
  m_sdbSizer1->Realize();

  bSizer1->Add(m_sdbSizer1, 1, wxEXPAND, 5);
  
  SetSizer(bSizer1);
  Layout();

  Centre(wxBOTH);
}

void GlobalOptions::DoShow() {
  ShowModal();
  
  // write out values
  wstring path_string = m_textCtrl4->GetValue().ToStdWstring();
  m_IniManager->SetValue(wxT("Options"), wxT("path"), path_string);
  m_IniManager->Write();
}


