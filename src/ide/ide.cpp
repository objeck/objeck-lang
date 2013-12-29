#include "ide.h"

IMPLEMENT_APP(MyApp)

//
// application
//
bool MyApp::OnInit()
{
  if (!wxApp::OnInit()) {
    return false;
  }

  MyFrame* frame = new MyFrame;
  frame->Show(true);

  return true;
}

//
// frame
//
MyFrame::MyFrame() : wxFrame(NULL, wxID_ANY, wxT("wxSplitterWindow sample"), wxDefaultPosition, 
                             wxSize(800, 600), wxDEFAULT_FRAME_STYLE | wxNO_FULL_REPAINT_ON_RESIZE)
{
  aui_manager = new wxAuiManager(this);
  SetIcon(wxICON(sample));

#if wxUSE_STATUSBAR
  CreateStatusBar(2);
#endif // wxUSE_STATUSBAR

  // create menu
  wxMenu* split_menu = new wxMenu;
  toggle_right = split_menu->AppendCheckItem(TOGGLE_LEFT, wxT("Show &right\tCtrl-L"), wxT("Show right"));
  toggle_right->Toggle();

  toggle_bottom = split_menu->AppendCheckItem(TOGGLE_BOTTOM, wxT("Show &bottom\tCtrl-B"), wxT("Show bottom"));
  toggle_bottom->Toggle();

  split_menu->Append(SPLIT_QUIT, wxT("E&xit\tAlt-X"), wxT("Exit"));

  // set menu bar
  wxMenuBar* menu_bar = new wxMenuBar;
  menu_bar->Append(split_menu, wxT("&Splitter"));
  SetMenuBar(menu_bar);

  wxNotebook *notebook = new wxNotebook(this, wxID_ANY);
  notebook->AddPage(new wxTextCtrl(notebook, wxID_ANY, wxT("Some code"), wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxNO_BORDER), wxT("hello.obs"));
  notebook->AddPage(new wxTextCtrl(notebook, wxID_ANY, wxT("Some code"), wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxNO_BORDER), wxT("hello_again.obs"));
  center = notebook;

  bottom = new wxTextCtrl(this, wxID_ANY, wxT("Output"), wxDefaultPosition, wxDefaultSize);
  right = new wxTreeCtrl(this, wxID_ANY);
  
  aui_manager->AddPane(center, wxCENTER);
  aui_manager->AddPane(bottom, wxBOTTOM);
  aui_manager->AddPane(right, wxRIGHT);
  aui_manager->Update();
    
#if wxUSE_STATUSBAR
  SetStatusText(wxT("Min pane size = 0"), 1);
#endif // wxUSE_STATUSBAR
}

MyFrame::~MyFrame() 
{
  aui_manager->UnInit();
}

void MyFrame::OnQuit(wxCommandEvent& WXUNUSED(event))
{
  Close(true);
}

void MyFrame::OnToggleLeft(wxCommandEvent& WXUNUSED(event))
{
  wxAuiPaneInfo& right_panel_info = aui_manager->GetPane(right);
  if(right_panel_info.IsShown()) {
    right_panel_info.Hide();
  }
  else {
    right_panel_info.Show();
  }
  aui_manager->Update();
}

void MyFrame::OnToggleBottom(wxCommandEvent& WXUNUSED(event))
{
  wxAuiPaneInfo& bottom_panel_info = aui_manager->GetPane(bottom);
  if(bottom_panel_info.IsShown()) {
    bottom_panel_info.Hide();
  }
  else {
    bottom_panel_info.Show();
  }
  aui_manager->Update();
}

void MyFrame::OnPaneClose(wxAuiManagerEvent& event)
{
  const wxWindow* panel = event.pane->window;
  if(panel == right) {
    toggle_right->Toggle();
  }
  else if(panel == bottom) {
    toggle_bottom->Toggle();
  }
}