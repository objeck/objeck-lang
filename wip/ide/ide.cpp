#include "ide.h"

bool MyApp::OnInit() {
  if (!wxApp::OnInit()) {
    return false;
  }

  wxFrame* frame = new wxFrame(NULL, wxID_ANY, wxT("wxAUI Sample Application"), wxDefaultPosition, wxSize(800, 600));
  frame->Show();

  return true;
}

DECLARE_APP(MyApp)
IMPLEMENT_APP(MyApp)