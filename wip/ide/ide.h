#ifndef __IDE_H__
#define __IDE_H__

// For compilers that support precompilation, includes "wx/wx.h".
#include "wx/wxprec.h"

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#include "wx/app.h"
#include "wx/aui/aui.h"
#include "wx/treectrl.h"
#include "wx/artprov.h"

class MyApp : public wxApp {

public:
  bool OnInit();
};

class MyFrame : public wxFrame {
  enum {
    ID_SampleItem
  };

  wxAuiManager aui_manager;

  void DoUpdate();
  
  wxMenuBar* CreateMenuBar();
  wxAuiToolBar* CreateToolBar();
  wxTreeCtrl* CreateTreeCtrl();
  wxTextCtrl* CreateTextCtrl(const wxString& ctrl_text);
  
public:
  MyFrame(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize, long style = wxDEFAULT_FRAME_STYLE | wxSUNKEN_BORDER);

  ~MyFrame();
};

#endif