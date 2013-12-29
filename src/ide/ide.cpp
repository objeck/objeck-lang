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
  SetIcon(wxICON(sample));

#if wxUSE_STATUSBAR
  CreateStatusBar(2);
#endif // wxUSE_STATUSBAR

  // create menu
  wxMenu* split_menu = new wxMenu;
  split_menu->Append(SPLIT_VERTICAL, wxT("Split &Vertically\tCtrl-V"), wxT("Split vertically"));
  split_menu->Append(SPLIT_QUIT, wxT("E&xit\tAlt-X"), wxT("Exit"));

  // set menu bar
  wxMenuBar* menu_bar = new wxMenuBar;
  menu_bar->Append(split_menu, wxT("&Splitter"));
  SetMenuBar(menu_bar);

  // set main_splitter
  main_splitter = new MySplitterWindow(this);
  main_splitter->SetSize(GetClientSize());
  main_splitter->SetSashGravity(1.0);

  right = new wxTreeCtrl(main_splitter);
  left = new wxTextCtrl(main_splitter, wxID_ANY, wxT("Multi line without vertical scrollbar."), wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE);
  
  // you can also try -100
  main_splitter->SplitVertically(left, right, 600);
  
#if wxUSE_STATUSBAR
  SetStatusText(wxT("Min pane size = 0"), 1);
#endif // wxUSE_STATUSBAR
}

MyFrame::~MyFrame() 
{
  
}

void MyFrame::OnSplitVertical(wxCommandEvent& WXUNUSED(event))
{
  if(main_splitter->IsSplit()) {
    main_splitter->Unsplit();
  }

  left->Show(true);
  right->Show(true);
  main_splitter->SplitVertically(left, right, 600);

#if wxUSE_STATUSBAR
  SetStatusText(wxT("Splitter split vertically"), 1);
#endif // wxUSE_STATUSBAR
}

void MyFrame::OnQuit(wxCommandEvent& WXUNUSED(event))
{
  Close(true);
}

//
// main_splitter
//
MySplitterWindow::MySplitterWindow(wxFrame* parent) : wxSplitterWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, 
                                                                       wxSP_3D | wxSP_LIVE_UPDATE | wxCLIP_CHILDREN /* | wxSP_NO_XP_THEME */)
{
  this->parent = parent;
}

void MySplitterWindow::OnDClick(wxSplitterEvent& event)
{
#if wxUSE_STATUSBAR
  parent->SetStatusText(wxT("Splitter double clicked"), 1);
#endif // wxUSE_STATUSBAR

  event.Skip();
}

void MySplitterWindow::OnUnsplitEvent(wxSplitterEvent& event)
{
#if wxUSE_STATUSBAR
  parent->SetStatusText(wxT("Splitter unsplit"), 1);
#endif // wxUSE_STATUSBAR

  event.Skip();
}